// MinimalPulseEditor.h
#pragma once

#include <JuceHeader.h>

// Forward declare your processor type
class SimplePulseSynthProcessor;

// MinimalPulseEditor class
class MinimalPulseEditor  : public juce::AudioProcessorEditor {
public:
    MinimalPulseEditor (SimplePulseSynthProcessor& p);
    ~MinimalPulseEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SimplePulseSynthProcessor& processorRef;

    // UI controls
    juce::Slider dutySlider;
    juce::Label  dutyLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dutyAttachment;

    juce::Slider pwmSlider;
    juce::Label  pwmLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pwmAttachment;

    juce::Slider gainSlider;
    juce::Label  gainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;

    juce::ComboBox qualityBox;
    juce::Label   qualityLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAttachment;

    juce::ComboBox bandModeBox;
    juce::Label   bandModeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bandModeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MinimalPulseEditor)
};


// MinimalPulseEditor.cpp
#include "MinimalPulseEditor.h"
#include "SimplePulseSynthProcessor.h" // include your processor header

MinimalPulseEditor::MinimalPulseEditor (SimplePulseSynthProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    using APVTS = juce::AudioProcessorValueTreeState;

    // Duty slider
    dutySlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    dutySlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    dutyLabel.setText("Duty", juce::dontSendNotification);
    addAndMakeVisible(dutySlider);
    addAndMakeVisible(dutyLabel);
    dutyAttachment.reset(new APVTS::SliderAttachment(processorRef.parameters, "duty", dutySlider));

    // PWM slider
    pwmSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    pwmSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    pwmLabel.setText("PWM", juce::dontSendNotification);
    addAndMakeVisible(pwmSlider);
    addAndMakeVisible(pwmLabel);
    pwmAttachment.reset(new APVTS::SliderAttachment(processorRef.parameters, "pwm", pwmSlider));

    // Gain slider
    gainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    gainLabel.setText("Gain", juce::dontSendNotification);
    addAndMakeVisible(gainSlider);
    addAndMakeVisible(gainLabel);
    gainAttachment.reset(new APVTS::SliderAttachment(processorRef.parameters, "gain", gainSlider));

    // Quality combo box
    qualityLabel.setText("Quality", juce::dontSendNotification);
    qualityBox.addItem("Fast", 1);
    qualityBox.addItem("Medium", 2);
    qualityBox.addItem("High", 3);
    qualityBox.setSelectedId(3);
    addAndMakeVisible(qualityLabel);
    addAndMakeVisible(qualityBox);
    qualityAttachment.reset(new APVTS::ComboBoxAttachment(processorRef.parameters, "quality", qualityBox));

    // Bandlimit mode combo box
    bandModeLabel.setText("Bandlimit", juce::dontSendNotification);
    bandModeBox.addItem("PolyBLEP", 1);
    bandModeBox.addItem("MinBLEP", 2);
    bandModeBox.setSelectedId(1);
    addAndMakeVisible(bandModeLabel);
    addAndMakeVisible(bandModeBox);
    bandModeAttachment.reset(new APVTS::ComboBoxAttachment(processorRef.parameters, "bandmode", bandModeBox));

    // Basic window size
    setSize (420, 220);
}

MinimalPulseEditor::~MinimalPulseEditor() = default;

void MinimalPulseEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::darkslategrey);
    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    g.drawFittedText ("Pulse Synth", getLocalBounds().removeFromTop(28), juce::Justification::centred, 1);
}

void MinimalPulseEditor::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto top = area.removeFromTop(28); // title area

    // Layout: three rotaries on left, two combos on right
    auto left = area.removeFromLeft(area.getWidth() * 0.66f).reduced(8);
    auto right = area.reduced(8);

    // Rotaries arranged horizontally
    auto rotaryArea = left.removeFromTop(left.getHeight());
    int rotaryW = rotaryArea.getWidth() / 3;
    dutySlider.setBounds(rotaryArea.removeFromLeft(rotaryW).reduced(8));
    dutyLabel.setBounds(dutySlider.getX(), dutySlider.getBottom(), dutySlider.getWidth(), 18);

    pwmSlider.setBounds(rotaryArea.removeFromLeft(rotaryW).reduced(8));
    pwmLabel.setBounds(pwmSlider.getX(), pwmSlider.getBottom(), pwmSlider.getWidth(), 18);

    gainSlider.setBounds(rotaryArea.removeFromLeft(rotaryW).reduced(8));
    gainLabel.setBounds(gainSlider.getX(), gainSlider.getBottom(), gainSlider.getWidth(), 18);

    // Right side: combo boxes stacked
    auto comboArea = right.removeFromTop(right.getHeight());
    qualityLabel.setBounds(comboArea.removeFromTop(28).reduced(4));
    qualityBox.setBounds(comboArea.removeFromTop(36).reduced(4));
    bandModeLabel.setBounds(comboArea.removeFromTop(28).reduced(4));
    bandModeBox.setBounds(comboArea.removeFromTop(36).reduced(4));
}
