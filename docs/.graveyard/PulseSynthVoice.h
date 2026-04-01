#pragma once

#include <JuceHeader.h>
#include "JUCEPulseOscillator_AVX2.h" // include the oscillator header you already added

// Simple MIDI -> frequency helper
static inline double midiNoteToFreq(int midiNote) noexcept {
    return 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
}

// Per-voice parameter block (can be expanded or replaced by references to shared parameters)
struct VoiceParams {
    float duty = 0.5f;
    float pwm = 0.0f;
    JUCEPulseOscillator_AVX2::Quality quality = JUCEPulseOscillator_AVX2::Quality::High;
    JUCEPulseOscillator_AVX2::BandlimitMode bandMode = JUCEPulseOscillator_AVX2::BandlimitMode::PolyBLEP;
    // ADSR times in seconds
    float attack  = 0.005f;
    float decay   = 0.08f;
    float sustain = 0.8f;
    float release = 0.12f;
    float gain    = 1.0f; // master voice gain
};

// PulseSynthVoice: a JUCE SynthesiserVoice that uses JUCEPulseOscillator_AVX2
class PulseSynthVoice : public juce::SynthesiserVoice {
public:
    PulseSynthVoice()
    : adsr(), osc()
    {
        // default ADSR parameters
        adsrParams.attack  = 0.005f;
        adsrParams.decay   = 0.08f;
        adsrParams.sustain = 0.8f;
        adsrParams.release = 0.12f;
        adsr.setParameters(adsrParams);
    }

    // Call from AudioProcessor::prepareToPlay for each voice
    void prepare(double sampleRate, int maxBlockSize, const VoiceParams& p) {
        sr = sampleRate;
        params = p;
        // prepare oscillator
        osc.prepare(sr, maxBlockSize, params.quality, params.bandMode);
        // ADSR
        adsrParams.attack  = params.attack;
        adsrParams.decay   = params.decay;
        adsrParams.sustain = params.sustain;
        adsrParams.release = params.release;
        adsr.setSampleRate(sr);
        adsr.setParameters(adsrParams);
        // smoothing for gain and PWM/duty if you want
        gainSmooth.reset(sr, 0.01);
        gainSmooth.setCurrentAndTarget(params.gain);
        dutySmooth.reset(sr, 0.005);
        dutySmooth.setCurrentAndTarget(params.duty);
        pwmSmooth.reset(sr, 0.005);
        pwmSmooth.setCurrentAndTarget(params.pwm);

        // allocate temp buffer once
        tempBuffer.setSize(1, maxBlockSize);
    }

    // SynthesiserVoice overrides
    bool canPlaySound(juce::SynthesiserSound* sound) override {
        return dynamic_cast<juce::SynthesiserSound*>(sound) != nullptr;
    }

    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* /*sound*/,
                   int /*currentPitchWheelPosition*/) override
    {
        currentMidiNote = midiNoteNumber;
        currentVelocity = velocity;
        // set oscillator frequency and reset phase if desired
        double freq = midiNoteToFreq(midiNoteNumber);
        osc.setFrequency(freq);
        // set duty and pwm from smoothed params
        osc.setDuty(dutySmooth.getNextValue());
        osc.setPWM(pwmSmooth.getNextValue());
        // ADSR
        adsr.noteOn();
        // set gain scaled by velocity
        gainSmooth.setTarget(params.gain * velocity);
    }

    void stopNote(float /*velocity*/, bool allowTailOff) override {
        if (allowTailOff) {
            adsr.noteOff();
        } else {
            clearCurrentNote();
            adsr.reset();
        }
    }

    void pitchWheelMoved(int /*newValue*/) override {}
    void controllerMoved(int /*controllerNumber*/, int /*newValue*/) override {}

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override {
        if (!isVoiceActive()) {
            // still need to advance ADSR to keep internal state consistent
            adsr.applyEnvelopeToBuffer(tempBuffer, 0, 0);
            return;
        }

        // update smoothed params and push into oscillator
        // snapshot duty/pwm/gain per block start
        float dutyVal = dutySmooth.getNextValue();
        float pwmVal  = pwmSmooth.getNextValue();
        float gainVal = (float)gainSmooth.getNextValue();

        osc.setDuty(dutyVal);
        osc.setPWM(pwmVal);

        // render oscillator into tempBuffer (mono)
        tempBuffer.clear();
        osc.renderBlock(tempBuffer, 0, 0, numSamples);

        // apply ADSR envelope and gain, then mix into outputBuffer
        auto* temp = tempBuffer.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i) {
            float env = adsr.getNextSample();
            float sample = temp[i] * env * gainVal;
            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch) {
                outputBuffer.addSample(ch, startSample + i, sample);
            }
        }

        // if ADSR finished, clear note
        if (!adsr.isActive()) {
            clearCurrentNote();
        }
    }

    // Optional: update voice parameters in real time (thread-safe setters)
    void setDuty(float d) noexcept { dutySmooth.setTarget(std::clamp(d, 0.0001f, 0.9999f)); }
    void setPWM(float p) noexcept { pwmSmooth.setTarget(std::clamp(p, -1.0f, 1.0f)); }
    void setGain(float g) noexcept { gainSmooth.setTarget(g); }
    void setADSR(float a, float d, float s, float r) noexcept {
        adsrParams.attack  = a;
        adsrParams.decay   = d;
        adsrParams.sustain = s;
        adsrParams.release = r;
        adsr.setParameters(adsrParams);
    }

private:
    // internal state
    double sr = 48000.0;
    int currentMidiNote = -1;
    float currentVelocity = 0.0f;

    VoiceParams params;

    // oscillator instance (from previous header)
    JUCEPulseOscillator_AVX2 osc;

    // ADSR
    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;

    // smoothing helpers (simple one-pole)
    struct OnePoleSimple {
        double sr = 48000.0;
        double tau = 0.01;
        double a = 0.0;
        double cur = 0.0;
        void reset(double sampleRate, double timeConstant) { sr = sampleRate; tau = timeConstant; a = exp(-1.0/(sr * tau)); }
        void setCurrentAndTarget(double v) { cur = v; target = v; }
        void setTarget(double v) { target = v; }
        void setTargetImmediate(double v) { target = v; cur = v; }
        double getNextValue() { cur = a * cur + (1.0 - a) * target; return cur; }
        double target = 0.0;
    } gainSmooth, dutySmooth, pwmSmooth;

    // temp mono buffer to render oscillator into
    juce::AudioBuffer<float> tempBuffer;
};
