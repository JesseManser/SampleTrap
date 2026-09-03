#pragma once

#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"

class SquareButton final : public juce::TextButton
{
public:
    SquareButton() = default;
    explicit SquareButton(const juce::String& name) : juce::TextButton(name) {}

    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent&) override;
    std::function<void()> onRightClick;
};

class SampleTrapAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit SampleTrapAudioProcessorEditor(SampleTrapAudioProcessor&);
    ~SampleTrapAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyStateChanged(bool isKeyDown) override;
    void focusLost(FocusChangeType) override;

private:
    void timerCallback() override;
    void releaseKeyboardPads();

    SampleTrapAudioProcessor& audioProcessor;
    SquareButton recordArmButton { "RECORD ARM" };
    SquareButton monitorInputButton { "MONITOR INPUT" };
    SquareButton holdModeButton { "HOLD MODE" };
    SquareButton midiLearnButton { "MIDI LEARN" };
    juce::Slider padCountSlider;
    juce::Label padCountLabel;
    std::array<SquareButton, SampleTrapAudioProcessor::maxPads> padButtons;
    std::array<bool, SampleTrapAudioProcessor::maxPads> mousePadDown {};
    std::array<bool, SampleTrapAudioProcessor::maxPads> mousePadLearnClick {};
    std::array<bool, SampleTrapAudioProcessor::keyboardPads> keyboardPadDown {};
    std::array<uint32_t, SampleTrapAudioProcessor::maxPads> lastTriggerCounts {};
    std::array<int, SampleTrapAudioProcessor::maxPads> flashFrames {};
    juce::Image logo;
    int lastActivePadCount = -1;
    int pendingLearnPad = -1;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> recordArmAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monitorInputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> holdModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> padCountAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleTrapAudioProcessorEditor)
};
