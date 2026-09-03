#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

class SampleTrapAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int maxPads = 16;
    static constexpr int keyboardPads = 8;
    static constexpr double maxRecordSeconds = 5.0;

    SampleTrapAudioProcessor();
    ~SampleTrapAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& getParameterState() noexcept { return parameters; }
    void setPadGateFromUi(int padIndex, bool isDown);
    void beginMidiLearn(int padIndex) noexcept;
    void cancelMidiLearn() noexcept;
    void clearMidiAssignment(int padIndex) noexcept;

    bool isRecordArmed() const noexcept;
    bool isMonitorInputEnabled() const noexcept;
    bool isHoldModeEnabled() const noexcept;
    bool isPadGateDown(int padIndex) const noexcept;
    int getActivePadCount() const noexcept;
    int getMidiAssignment(int padIndex) const noexcept;
    int getMidiLearnPad() const noexcept;
    bool padHasSample(int padIndex) const noexcept;
    bool isPadRecording(int padIndex) const noexcept;
    double getRecordedSeconds(int padIndex) const noexcept;
    uint32_t getTriggerCount(int padIndex) const noexcept;

    static juce::String padParameterId(int padIndex);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void handlePadDown(int padIndex) noexcept;
    void handlePadUp(int padIndex) noexcept;
    void updateCombinedGate(int padIndex) noexcept;
    void handleMidiMessage(const juce::MidiMessage&) noexcept;
    void assignLearnedNote(int padIndex, int noteNumber) noexcept;
    void beginPlaybackRelease(int padIndex) noexcept;
    void startRecording(int padIndex) noexcept;
    void finishRecording() noexcept;
    void renderAudio(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* recordArmParameter = nullptr;
    std::atomic<float>* monitorInputParameter = nullptr;
    std::atomic<float>* holdModeParameter = nullptr;
    std::atomic<float>* padCountParameter = nullptr;
    std::array<std::atomic<float>*, maxPads> padGateParameters {};

    std::array<juce::AudioBuffer<float>, maxPads> sampleBuffers;
    std::array<std::atomic<int>, maxPads> recordedSamples {};
    std::array<std::atomic<uint32_t>, maxPads> triggerCounts {};
    std::array<int, maxPads> playbackPositions {};
    std::array<bool, maxPads> playing {};
    std::array<bool, maxPads> parameterGates {};
    std::array<bool, maxPads> midiGates {};
    std::array<bool, maxPads> combinedGates {};
    std::array<bool, maxPads> releasing {};
    std::array<int, maxPads> releaseSamplesRemaining {};
    std::array<std::atomic<bool>, maxPads> midiGatesForUi {};
    std::array<std::atomic<bool>, maxPads> resetMidiGateRequests {};
    std::array<std::atomic<int>, maxPads> midiAssignments {};

    bool audioThreadRecordArmed = false;
    bool audioThreadMonitorInput = false;
    bool audioThreadHoldMode = false;
    int audioThreadActivePads = 8;
    std::atomic<int> recordingPadForUi { -1 };
    std::atomic<int> midiLearnPad { -1 };
    int recordingPad = -1;
    int writePosition = 0;
    int allocatedChannels = 0;
    int allocatedSamples = 0;
    std::atomic<double> currentSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleTrapAudioProcessor)
};
