// JUCEPulseOscillator_AVX2.h
#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <cstdint>
#include <atomic>

#if defined(_MSC_VER)
  #include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
  #include <cpuid.h>
#endif

#if defined(__AVX2__)
  #include <immintrin.h>
#endif

class JUCEPulseOscillator_AVX2 {
public:
    enum class BandlimitMode { PolyBLEP, MinBLEP };
    enum class Quality { Fast, Medium, High };

    JUCEPulseOscillator_AVX2() = default;
    ~JUCEPulseOscillator_AVX2() = default;

    // Call from prepareToPlay or when sample rate changes
    void prepare(double sampleRate, int maxBlockSize, Quality q = Quality::High, BandlimitMode mode = BandlimitMode::PolyBLEP) {
        fs = sampleRate;
        maxBlock = maxBlockSize;
        setQuality(q);
        setBandlimitMode(mode);
        phase = 0.0;
        freqSmooth.reset(sampleRate, 0.002); // short smoothing for parameter changes
        dutySmooth.reset(sampleRate, 0.002);
        freqSmooth.setTargetImmediate(440.0);
        dutySmooth.setTargetImmediate(0.5);
        configureQuality(); // sets oversample/kernelLen
        generateMinBLEPKernelIfNeeded();
        allocateBuffers();
        detectAndSelectRenderer();
    }

    // Parameter setters (real-time safe)
    void setFrequency(float hz) noexcept { freqSmooth.setTarget(hz); }
    void setDuty(float d) noexcept { dutySmooth.setTarget(std::clamp(d, 0.0001f, 0.9999f)); }
    void setBandlimitMode(BandlimitMode m) { bandMode = m; generateMinBLEPKernelIfNeeded(); detectAndSelectRenderer(); }
    void setQuality(Quality q) { quality = q; configureQuality(); generateMinBLEPKernelIfNeeded(); allocateBuffers(); detectAndSelectRenderer(); }

    // Render into a single channel region of buffer (real-time safe)
    void renderBlock(juce::AudioBuffer<float>& buffer, int channel, int startSample, int numSamples) noexcept {
        jassert(channel < buffer.getNumChannels());
        auto* out = buffer.getWritePointer(channel, startSample);

        // chunking parameters
        const int CHUNK = 8; // AVX2 processes 8 float lanes
        // local copies for speed
        double localPhase = phase;
        int n = 0;

        // Ensure buffers are large enough
        if ((int)dtBuf.size() < CHUNK) {
            dtBuf.resize(CHUNK);
            dutyBuf.resize(CHUNK);
            phaseBuf.resize(CHUNK);
        }

        while (n < numSamples) {
            int chunk = std::min(CHUNK, numSamples - n);

            // Snapshot dt, duty, phase for chunk
            for (int i = 0; i < chunk; ++i) {
                float f = (float)freqSmooth.getNextValue();
                float d = (float)dutySmooth.getNextValue();
                float dt = (float)(f / fs);
                dtBuf[i] = dt;
                dutyBuf[i] = d;
                phaseBuf[i] = localPhase;
                // advance localPhase with wrap
                localPhase += dt;
                if (localPhase >= 1.0) localPhase -= 1.0;
            }

            // Write naive base pulse for chunk (vector-friendly)
            for (int i = 0; i < chunk; ++i) {
                out[n + i] = (phaseBuf[i] < dutyBuf[i]) ? 1.0f : -1.0f;
            }

            // Apply corrections: either AVX2 accelerated or scalar fallback
            if (useAVX2) {
                polyBLEP_AVX2_add(out + n, phaseBuf.data(), dtBuf.data(), dutyBuf.data(), chunk);
            } else {
                polyBLEP_scalar_add(out + n, phaseBuf.data(), dtBuf.data(), dutyBuf.data(), chunk);
            }

            n += chunk;
        }

        // advance global phase
        phase = localPhase;
    }

private:
    // --- state ---
    double fs = 48000.0;
    int maxBlock = 512;
    double phase = 0.0;

    // smoothing helper (one-pole)
    struct OnePole {
        double sr = 48000.0;
        double tau = 0.02;
        double a = 0.0;
        double cur = 0.0;
        double target = 440.0;
        void reset(double sampleRate, double timeConstant) { sr = sampleRate; tau = timeConstant; a = exp(-1.0/(sr * tau)); }
        void setCurrentAndTarget(double v) { cur = v; target = v; }
        void setTarget(double v) { target = v; }
        void setTargetImmediate(double v) { target = v; cur = v; }
        double getNextValue() { cur = a * cur + (1.0 - a) * target; return cur; }
    } freqSmooth, dutySmooth;

    // quality and bandlimit
    Quality quality = Quality::High;
    BandlimitMode bandMode = BandlimitMode::PolyBLEP;
    int oversample = 16;
    int kernelLen = 1024;

    // minBLEP kernel and accumulation buffer (if used)
    std::vector<float> kernel;
    std::vector<float> accBuf;
    uint32_t accMask = 0;
    uint32_t writePos = 0;

    // snapshot buffers for chunk processing
    std::vector<float> dtBuf;
    std::vector<float> dutyBuf;
    std::vector<double> phaseBuf;

    // runtime AVX2 flag and renderer pointer
    bool useAVX2 = false;

    // configure quality presets
    void configureQuality() {
        switch (quality) {
            case Quality::Fast:   oversample = 8;  kernelLen = 512;  break;
            case Quality::Medium: oversample = 12; kernelLen = 768;  break;
            case Quality::High:   oversample = 16; kernelLen = 1024; break;
        }
    }

    void allocateBuffers() {
        // acc buffer size power of two >= kernelLen*2
        uint32_t size = 1u;
        while (size < (uint32_t)kernelLen * 2u) size <<= 1;
        accBuf.assign(size, 0.0f);
        accMask = size - 1;
        writePos = 0;

        // snapshot buffers
        dtBuf.resize(8);
        dutyBuf.resize(8);
        phaseBuf.resize(8);
    }

    // --- polyBLEP scalar helper ---
    static inline float polyBLEP_scalar(float t, float dt) noexcept {
        if (t < dt) {
            float x = t / dt;
            return x + x - x * x - 1.0f;
        }
        if (t > 1.0f - dt) {
            float x = (t - 1.0f) / dt;
            return x * x + x + x + 1.0f;
        }
        return 0.0f;
    }

    // scalar correction pass
    static inline void polyBLEP_scalar_add(float* outPtr,
                                           const double* phaseArr,
                                           const float* dtArr,
                                           const float* dutyArr,
                                           int chunk) noexcept
    {
        for (int i = 0; i < chunk; ++i) {
            float dt = dtArr[i];
            float d = dutyArr[i];
            float ph = (float)phaseArr[i];
            float t1 = polyBLEP_scalar(ph, dt);
            float t2 = polyBLEP_scalar(fmodf(ph - d + 1.0f, 1.0f), dt);
            outPtr[i] += (t1 - t2);
        }
    }

#if defined(__AVX2__)
    // AVX2 accelerated correction pass for 8 lanes
    static inline void polyBLEP_AVX2_add(float* outPtr,
                                         const double* phaseArr,
                                         const float* dtArr,
                                         const float* dutyArr,
                                         int chunk) noexcept
    {
        const int L = 8;
        int i = 0;
        for (; i + L <= chunk; i += L) {
            // load dt and duty
            __m256 dt = _mm256_loadu_ps(dtArr + i);
            __m256 duty = _mm256_loadu_ps(dutyArr + i);

            // load phaseArr (double -> float)
            alignas(32) float phaseF[L];
            for (int k = 0; k < L; ++k) phaseF[k] = (float)phaseArr[i + k];
            __m256 phase = _mm256_loadu_ps(phaseF);

            __m256 one = _mm256_set1_ps(1.0f);
            __m256 zero = _mm256_setzero_ps();

            // t = phase
            __m256 t = phase;

            // t2 = (phase - duty + 1.0) mod 1.0
            __m256 t2 = _mm256_sub_ps(phase, duty);
            __m256 maskNeg = _mm256_cmp_ps(t2, zero, _CMP_LT_OS);
            __m256 t2add = _mm256_and_ps(maskNeg, one);
            t2 = _mm256_add_ps(t2, t2add);

            // x = t/dt, x2 = t2/dt
            __m256 x = _mm256_div_ps(t, dt);
            __m256 x2 = _mm256_div_ps(t2, dt);

            // masks for t < dt and t > 1-dt
            __m256 maskRiseLow = _mm256_cmp_ps(t, dt, _CMP_LT_OS);
            __m256 maskRiseHigh = _mm256_cmp_ps(t, _mm256_sub_ps(one, dt), _CMP_GT_OS);

            __m256 maskFallLow = _mm256_cmp_ps(t2, dt, _CMP_LT_OS);
            __m256 maskFallHigh = _mm256_cmp_ps(t2, _mm256_sub_ps(one, dt), _CMP_GT_OS);

            // polyRiseLow = x + x - x*x - 1
            __m256 polyRiseLow = _mm256_sub_ps(_mm256_add_ps(x, x), _mm256_mul_ps(x, x));
            polyRiseLow = _mm256_sub_ps(polyRiseLow, one);

            // polyRiseHigh: xr = (t-1)/dt; xr*xr + xr + xr + 1
            __m256 xr = _mm256_div_ps(_mm256_sub_ps(t, one), dt);
            __m256 polyRiseHigh = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(xr, xr), xr), _mm256_add_ps(xr, one));

            // select rise poly
            __m256 rise = _mm256_blendv_ps(zero, polyRiseLow, maskRiseLow);
            rise = _mm256_blendv_ps(rise, polyRiseHigh, maskRiseHigh);

            // falling edge
            __m256 polyFallLow = _mm256_sub_ps(_mm256_add_ps(x2, x2), _mm256_mul_ps(x2, x2));
            polyFallLow = _mm256_sub_ps(polyFallLow, one);
            __m256 xf = _mm256_div_ps(_mm256_sub_ps(t2, one), dt);
            __m256 polyFallHigh = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(xf, xf), xf), _mm256_add_ps(xf, one));
            __m256 fall = _mm256_blendv_ps(zero, polyFallLow, maskFallLow);
            fall = _mm256_blendv_ps(fall, polyFallHigh, maskFallHigh);

            // corr = rise - fall
            __m256 corr = _mm256_sub_ps(rise, fall);

            // add to out
            __m256 outv = _mm256_loadu_ps(outPtr + i);
            outv = _mm256_add_ps(outv, corr);
            _mm256_storeu_ps(outPtr + i, outv);
        }

        // tail scalar
        for (; i < chunk; ++i) {
            float dt = dtArr[i];
            float d = dutyArr[i];
            float ph = (float)phaseArr[i];
            float t1 = polyBLEP_scalar(ph, dt);
            float t2 = polyBLEP_scalar(fmodf(ph - d + 1.0f, 1.0f), dt);
            outPtr[i] += (t1 - t2);
        }
    }
#else
    // If compiled without AVX2, provide a stub that calls scalar
    static inline void polyBLEP_AVX2_add(float* outPtr,
                                         const double* phaseArr,
                                         const float* dtArr,
                                         const float* dutyArr,
                                         int chunk) noexcept
    {
        polyBLEP_scalar_add(outPtr, phaseArr, dtArr, dutyArr, chunk);
    }
#endif

    // --- minBLEP generation (kept for completeness; not used in AVX2 path) ---
    void generateMinBLEPKernelIfNeeded() {
        if (bandMode != BandlimitMode::MinBLEP) return;
        kernel.assign(kernelLen + 1, 0.0f);
        double fc = 0.5 / (double)oversample;
        double center = (kernelLen - 1) * 0.5;
        for (int n = 0; n < kernelLen; ++n) {
            double t = (double)n - center;
            double x = M_PI * t * fc;
            double sinc = (fabs(x) < 1e-12) ? 1.0 : sin(x) / x;
            double w = 0.42 - 0.5 * cos(2.0 * M_PI * n / (kernelLen - 1))
                           + 0.08 * cos(4.0 * M_PI * n / (kernelLen - 1));
            kernel[n] = (float)(sinc * w);
        }
        float acc = 0.0f;
        for (int i = 0; i < kernelLen; ++i) { acc += kernel[i]; kernel[i] = acc; }
        float last = kernel[kernelLen - 1];
        if (last == 0.0f) last = 1.0f;
        for (int i = 0; i < kernelLen; ++i) kernel[i] /= last;
        kernel[kernelLen] = kernel[kernelLen - 1];
    }

    // --- runtime detection of AVX2 and renderer selection ---
    void detectAndSelectRenderer() {
#if defined(__GNUC__) || defined(__clang__)
        // GCC/Clang: use builtin if available
  #if defined(__x86_64__) || defined(__i386__)
        useAVX2 = __builtin_cpu_supports("avx2");
  #else
        useAVX2 = false;
  #endif
#elif defined(_MSC_VER)
        // MSVC: use __cpuid
  #if defined(_M_X64) || defined(_M_IX86)
        int cpuInfo[4] = {0,0,0,0};
        __cpuid(cpuInfo, 0);
        int nIds = cpuInfo[0];
        useAVX2 = false;
        if (nIds >= 7) {
            __cpuidex(cpuInfo, 7, 0);
            useAVX2 = (cpuInfo[1] & (1 << 5)) != 0; // EBX bit 5 = AVX2
        }
  #else
        useAVX2 = false;
  #endif
#else
        useAVX2 = false;
#endif
        // If bandMode is MinBLEP we still can use AVX2 for polyBLEP path; keep flag as-is.
    }
};
