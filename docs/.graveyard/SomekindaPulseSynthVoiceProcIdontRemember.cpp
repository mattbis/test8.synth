// SimplePulseSynthProcessor.h/.cpp combined example
#pragma once

#include <JuceHeader.h>
#include "PulseSynthVoice.h" // include the voice header you created earlier

class SimplePulseSynthProcessor  : public juce::AudioProcessor {
public:
    SimplePulseSynthProcessor()
    : parameters(*this, nullptr, "PARAMS", createParameterLayout())
    {
        // create synth and voices
        synth.clearVoices();
        const int numVoices = 8;
        for (int i = 0; i < numVoices; ++i) {
            auto* v = new PulseSynthVoice();
            voices.emplace_back(v);
            synth.addVoice(v);
        }
        synth.clearSounds();
        synth.addSound(new juce::SynthesiserSound()); // simple passthrough sound
    }

    ~SimplePulseSynthProcessor() override {}

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        // Prepare synth voices
        VoiceParams vp;
        vp.duty = (float)*parameters.getRawParameterValue("duty");
        vp.pwm  = (float)*parameters.getRawParameterValue("pwm");
        vp.gain = (float)*parameters.getRawParameterValue("gain");
        // quality and bandMode defaults
        vp.quality = JUCEPulseOscillator_AVX2::Quality::High;
        vp.bandMode = JUCEPulseOscillator_AVX2::BandlimitMode::PolyBLEP;

        for (auto& vptr : voices) {
            if (vptr) vptr->prepare(sampleRate, samplesPerBlock, vp);
        }

        synth.setCurrentPlaybackSampleRate(sampleRate);
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override {
        // mono/stereo only
        if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
            && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
            return false;
        return true;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();

        // Read parameters once per block (real-time safe)
        float dutyVal = parameters.getRawParameterValue("duty")->load();
        float pwmVal  = parameters.getRawParameterValue("pwm")->load();
        float gainVal = parameters.getRawParameterValue("gain")->load();
        int qualityIdx = (int)parameters.getRawParameterValue("quality")->load();
        int bandIdx = (int)parameters.getRawParameterValue("bandmode")->load();

        // Map indices to enums
        JUCEPulseOscillator_AVX2::Quality q = JUCEPulseOscillator_AVX2::Quality::High;
        if (qualityIdx == 0) q = JUCEPulseOscillator_AVX2::Quality::Fast;
        else if (qualityIdx == 1) q = JUCEPulseOscillator_AVX2::Quality::Medium;
        else q = JUCEPulseOscillator_AVX2::Quality::High;

        JUCEPulseOscillator_AVX2::BandlimitMode bm = (bandIdx == 0)
            ? JUCEPulseOscillator_AVX2::BandlimitMode::PolyBLEP
            : JUCEPulseOscillator_AVX2::BandlimitMode::MinBLEP;

        // Propagate parameter changes to voices (cheap per-block)
        for (auto& vptr : voices) {
            if (!vptr) continue;
            vptr->setDuty(dutyVal);
            vptr->setPWM(pwmVal);
            vptr->setGain(gainVal);
            // If you want to change quality/bandmode at runtime:
            // reconfigure oscillator quality/bandmode by calling prepare on voice (costly)
            // For now we set only the fast-changing params; quality changes can be applied on UI thread.
        }

        // Render synth into buffer
        synth.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());
    }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return "SimplePulseSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override {
        auto state = parameters.copyState();
        std::unique_ptr<juce::XmlElement> xml (state.createXml());
        copyXmlToBinary (*xml, destData);
    }

    void setStateInformation (const void* data, int sizeInBytes) override {
        std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
        if (xmlState.get() != nullptr) {
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
        }
    }

    // Expose parameters
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
        using Param = juce::AudioProcessorValueTreeState;
        std::vector<std::unique_ptr<Param::Parameter>> params;

        // Duty: 0.01 .. 0.99
        params.push_back(std::make_unique<juce::AudioParameterFloat>("duty", "Duty",
            juce::NormalisableRange<float>(0.01f, 0.99f, 0.0001f, 0.5f), 0.5f));

        // PWM: -1 .. +1 (modulation amount)
        params.push_back(std::make_unique<juce::AudioParameterFloat>("pwm", "PWM",
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.0001f), 0.0f));

        // Gain
        params.push_back(std::make_unique<juce::AudioParameterFloat>("gain", "Gain",
            juce::NormalisableRange<float>(0.0f, 2.0f, 0.0001f, 0.5f), 1.0f));

        // Quality: 0=Fast,1=Medium,2=High
        params.push_back(std::make_unique<juce::AudioParameterInt>("quality", "Quality", 0, 2, 2));

        // Bandlimit mode: 0=PolyBLEP,1=MinBLEP
        params.push_back(std::make_unique<juce::AudioParameterInt>("bandmode", "Bandlimit Mode", 0, 1, 0));

        return { params.begin(), params.end() };
    }

private:
    //==============================================================================
    juce::Synthesiser synth;
    std::vector<PulseSynthVoice*> voices; // owned by synth, but keep pointers for prepare
    juce::AudioProcessorValueTreeState parameters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimplePulseSynthProcessor)
};
