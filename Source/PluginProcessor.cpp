#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
constexpr auto recordArmId = "recordArm";
constexpr auto monitorInputId = "monitorInput";
constexpr auto holdModeId = "holdMode";
constexpr auto activePadCountId = "activePadCount";
constexpr int releaseRampSamples = 32;
}

SampleTrapAudioProcessor::SampleTrapAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    recordArmParameter = parameters.getRawParameterValue(recordArmId);
    monitorInputParameter = parameters.getRawParameterValue(monitorInputId);
    holdModeParameter = parameters.getRawParameterValue(holdModeId);
    padCountParameter = parameters.getRawParameterValue(activePadCountId);

    for (int pad = 0; pad < maxPads; ++pad)
    {
        padGateParameters[static_cast<size_t>(pad)] = parameters.getRawParameterValue(padParameterId(pad));
        midiAssignments[static_cast<size_t>(pad)].store(-1, std::memory_order_relaxed);
    }
    setLatencySamples(0);
}

juce::AudioProcessorValueTreeState::ParameterLayout SampleTrapAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { recordArmId, 1 }, "Record Arm", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { monitorInputId, 1 }, "Monitor Input", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { holdModeId, 1 }, "Hold Mode", false));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { activePadCountId, 1 }, "Active Pad Count", 1, maxPads, 8));
    for (int pad = 0; pad < maxPads; ++pad)
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { padParameterId(pad), 1 },
                    "Pad " + juce::String(pad + 1) + " Gate",
                    juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
    return layout;
}

juce::String SampleTrapAudioProcessor::padParameterId(int padIndex)
{
    return "padGate" + juce::String(padIndex + 1);
}

void SampleTrapAudioProcessor::prepareToPlay(double sampleRate, int)
{
    const double preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentSampleRate.store(preparedSampleRate, std::memory_order_release);
    const int requiredChannels = juce::jmax(1, juce::jmax(getTotalNumInputChannels(), getTotalNumOutputChannels()));
    const int requiredSamples = juce::jmax(1, static_cast<int>(std::ceil(preparedSampleRate * maxRecordSeconds)));

    if (requiredChannels != allocatedChannels || requiredSamples != allocatedSamples)
    {
        allocatedChannels = requiredChannels;
        allocatedSamples = requiredSamples;
        for (int pad = 0; pad < maxPads; ++pad)
        {
            sampleBuffers[static_cast<size_t>(pad)].setSize(allocatedChannels, allocatedSamples, false, true, false);
            sampleBuffers[static_cast<size_t>(pad)].clear();
            recordedSamples[static_cast<size_t>(pad)].store(0, std::memory_order_release);
        }
    }

    playing.fill(false);
    playbackPositions.fill(0);
    parameterGates.fill(false);
    midiGates.fill(false);
    combinedGates.fill(false);
    releasing.fill(false);
    releaseSamplesRemaining.fill(0);
    for (auto& gate : midiGatesForUi)
        gate.store(false, std::memory_order_release);

    recordingPad = -1;
    writePosition = 0;
    recordingPadForUi.store(-1, std::memory_order_release);
    midiLearnPad.store(-1, std::memory_order_release);
    audioThreadRecordArmed = recordArmParameter->load(std::memory_order_relaxed) >= 0.5f;
    audioThreadMonitorInput = monitorInputParameter->load(std::memory_order_relaxed) >= 0.5f;
    audioThreadHoldMode = holdModeParameter->load(std::memory_order_relaxed) >= 0.5f;
    audioThreadActivePads = juce::jlimit(1, maxPads, static_cast<int>(std::lround(padCountParameter->load(std::memory_order_relaxed))));
    setLatencySamples(0);
}

void SampleTrapAudioProcessor::releaseResources() {}

bool SampleTrapAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
        && layouts.getMainInputChannelSet() == output;
}

void SampleTrapAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const bool armedNow = recordArmParameter->load(std::memory_order_relaxed) >= 0.5f;
    const bool holdNow = holdModeParameter->load(std::memory_order_relaxed) >= 0.5f;
    audioThreadMonitorInput = monitorInputParameter->load(std::memory_order_relaxed) >= 0.5f;
    audioThreadActivePads = juce::jlimit(1, maxPads, static_cast<int>(std::lround(padCountParameter->load(std::memory_order_relaxed))));

    if (audioThreadRecordArmed && ! armedNow)
        finishRecording();
    audioThreadRecordArmed = armedNow;

    if (! audioThreadHoldMode && holdNow)
        for (int pad = 0; pad < audioThreadActivePads; ++pad)
            if (playing[static_cast<size_t>(pad)] && ! combinedGates[static_cast<size_t>(pad)])
                beginPlaybackRelease(pad);
    if (audioThreadHoldMode && ! holdNow)
    {
        releasing.fill(false);
        releaseSamplesRemaining.fill(0);
    }
    audioThreadHoldMode = holdNow;

    if (recordingPad >= audioThreadActivePads)
        finishRecording();

    for (int pad = 0; pad < maxPads; ++pad)
    {
        const auto index = static_cast<size_t>(pad);
        if (resetMidiGateRequests[index].exchange(false, std::memory_order_acq_rel))
        {
            midiGates[index] = false;
            midiGatesForUi[index].store(false, std::memory_order_release);
        }
        if (pad >= audioThreadActivePads)
        {
            playing[index] = false;
            midiGates[index] = false;
            midiGatesForUi[index].store(false, std::memory_order_release);
        }
        parameterGates[index] = pad < audioThreadActivePads
            && padGateParameters[index]->load(std::memory_order_relaxed) >= 0.5f;
        updateCombinedGate(pad);
    }

    int cursor = 0;
    const int blockSamples = buffer.getNumSamples();
    for (const auto metadata : midi)
    {
        const int eventSample = juce::jlimit(cursor, blockSamples, metadata.samplePosition);
        renderAudio(buffer, cursor, eventSample - cursor);
        handleMidiMessage(metadata.getMessage());
        cursor = eventSample;
    }
    renderAudio(buffer, cursor, blockSamples - cursor);
}

void SampleTrapAudioProcessor::updateCombinedGate(int padIndex) noexcept
{
    const auto index = static_cast<size_t>(padIndex);
    const bool gate = padIndex < audioThreadActivePads && (parameterGates[index] || midiGates[index]);
    if (gate == combinedGates[index])
        return;
    combinedGates[index] = gate;
    gate ? handlePadDown(padIndex) : handlePadUp(padIndex);
}

void SampleTrapAudioProcessor::handleMidiMessage(const juce::MidiMessage& message) noexcept
{
    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        for (int pad = 0; pad < audioThreadActivePads; ++pad)
        {
            const auto index = static_cast<size_t>(pad);
            midiGates[index] = false;
            midiGatesForUi[index].store(false, std::memory_order_release);
            updateCombinedGate(pad);
        }
        return;
    }

    if (! message.isNoteOnOrOff())
        return;

    const int note = message.getNoteNumber();
    if (message.isNoteOn())
    {
        const int learning = midiLearnPad.exchange(-1, std::memory_order_acq_rel);
        if (juce::isPositiveAndBelow(learning, audioThreadActivePads))
        {
            assignLearnedNote(learning, note);
            return;
        }
    }

    for (int pad = 0; pad < audioThreadActivePads; ++pad)
    {
        const auto index = static_cast<size_t>(pad);
        if (midiAssignments[index].load(std::memory_order_acquire) == note)
        {
            midiGates[index] = message.isNoteOn();
            midiGatesForUi[index].store(midiGates[index], std::memory_order_release);
            updateCombinedGate(pad);
        }
    }
}

void SampleTrapAudioProcessor::assignLearnedNote(int padIndex, int noteNumber) noexcept
{
    for (int pad = 0; pad < maxPads; ++pad)
    {
        const auto index = static_cast<size_t>(pad);
        const bool isTarget = pad == padIndex;
        const bool ownsLearnedNote = midiAssignments[index].load(std::memory_order_relaxed) == noteNumber;
        if (isTarget || ownsLearnedNote)
        {
            midiGates[index] = false;
            midiGatesForUi[index].store(false, std::memory_order_release);
            updateCombinedGate(pad);
        }
        if (! isTarget && ownsLearnedNote)
            midiAssignments[index].store(-1, std::memory_order_release);
    }
    midiAssignments[static_cast<size_t>(padIndex)].store(noteNumber, std::memory_order_release);
}

void SampleTrapAudioProcessor::handlePadDown(int padIndex) noexcept
{
    if (audioThreadRecordArmed)
    {
        startRecording(padIndex);
        return;
    }
    const auto index = static_cast<size_t>(padIndex);
    if (recordedSamples[index].load(std::memory_order_acquire) > 0)
    {
        playbackPositions[index] = 0;
        playing[index] = true;
        releasing[index] = false;
        releaseSamplesRemaining[index] = 0;
        triggerCounts[index].fetch_add(1, std::memory_order_relaxed);
    }
}

void SampleTrapAudioProcessor::handlePadUp(int padIndex) noexcept
{
    if (audioThreadRecordArmed && recordingPad == padIndex)
    {
        finishRecording();
        return;
    }
    if (! audioThreadRecordArmed && audioThreadHoldMode)
        beginPlaybackRelease(padIndex);
}

void SampleTrapAudioProcessor::beginPlaybackRelease(int padIndex) noexcept
{
    const auto index = static_cast<size_t>(padIndex);
    if (playing[index])
    {
        releasing[index] = true;
        releaseSamplesRemaining[index] = releaseRampSamples;
    }
}

void SampleTrapAudioProcessor::startRecording(int padIndex) noexcept
{
    if (recordingPad == padIndex)
        return;
    finishRecording();
    const auto index = static_cast<size_t>(padIndex);
    recordedSamples[index].store(0, std::memory_order_release);
    playbackPositions[index] = 0;
    playing[index] = false;
    releasing[index] = false;
    recordingPad = padIndex;
    writePosition = 0;
    recordingPadForUi.store(padIndex, std::memory_order_release);
}

void SampleTrapAudioProcessor::finishRecording() noexcept
{
    if (recordingPad < 0)
        return;
    recordedSamples[static_cast<size_t>(recordingPad)].store(writePosition, std::memory_order_release);
    recordingPad = -1;
    writePosition = 0;
    recordingPadForUi.store(-1, std::memory_order_release);
}

void SampleTrapAudioProcessor::renderAudio(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;
    const int channelsInBuffer = buffer.getNumChannels();
    if (recordingPad >= 0)
    {
        const int writable = juce::jmin(numSamples, allocatedSamples - writePosition);
        auto& destination = sampleBuffers[static_cast<size_t>(recordingPad)];
        const int channels = juce::jmin(channelsInBuffer, destination.getNumChannels());
        for (int channel = 0; channel < channels; ++channel)
            destination.copyFrom(channel, writePosition, buffer, channel, startSample, writable);
        writePosition += writable;
        if (writePosition >= allocatedSamples)
            finishRecording();
    }

    if (! audioThreadMonitorInput)
        buffer.clear(startSample, numSamples);

    for (int pad = 0; pad < audioThreadActivePads; ++pad)
    {
        const auto index = static_cast<size_t>(pad);
        if (! playing[index])
            continue;
        const int length = recordedSamples[index].load(std::memory_order_acquire);
        const int readable = juce::jmin(numSamples, length - playbackPositions[index]);
        if (readable <= 0)
        {
            playing[index] = false;
            continue;
        }

        const auto& source = sampleBuffers[index];
        const int channels = juce::jmin(channelsInBuffer, source.getNumChannels());
        if (! releasing[index])
        {
            for (int channel = 0; channel < channels; ++channel)
                buffer.addFrom(channel, startSample, source, channel, playbackPositions[index], readable);
            playbackPositions[index] += readable;
        }
        else
        {
            const int ramped = juce::jmin(readable, releaseSamplesRemaining[index]);
            for (int sample = 0; sample < ramped; ++sample)
            {
                const float gain = static_cast<float>(releaseSamplesRemaining[index] - sample)
                    / static_cast<float>(releaseRampSamples);
                for (int channel = 0; channel < channels; ++channel)
                    buffer.addSample(channel, startSample + sample,
                                     source.getSample(channel, playbackPositions[index] + sample) * gain);
            }
            playbackPositions[index] += ramped;
            releaseSamplesRemaining[index] -= ramped;
            if (releaseSamplesRemaining[index] <= 0)
            {
                playing[index] = false;
                releasing[index] = false;
            }
        }
        if (playbackPositions[index] >= length)
            playing[index] = false;
    }
}

void SampleTrapAudioProcessor::setPadGateFromUi(int padIndex, bool isDown)
{
    if (! juce::isPositiveAndBelow(padIndex, maxPads))
        return;
    if (auto* parameter = parameters.getParameter(padParameterId(padIndex)))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(isDown ? 1.0f : 0.0f);
        parameter->endChangeGesture();
    }
}

void SampleTrapAudioProcessor::beginMidiLearn(int padIndex) noexcept
{
    midiLearnPad.store(juce::isPositiveAndBelow(padIndex, maxPads) ? padIndex : -1, std::memory_order_release);
}

void SampleTrapAudioProcessor::cancelMidiLearn() noexcept
{
    midiLearnPad.store(-1, std::memory_order_release);
}

void SampleTrapAudioProcessor::clearMidiAssignment(int padIndex) noexcept
{
    if (juce::isPositiveAndBelow(padIndex, maxPads))
    {
        const auto index = static_cast<size_t>(padIndex);
        midiAssignments[index].store(-1, std::memory_order_release);
        resetMidiGateRequests[index].store(true, std::memory_order_release);
    }
}

bool SampleTrapAudioProcessor::isRecordArmed() const noexcept { return recordArmParameter->load(std::memory_order_relaxed) >= 0.5f; }
bool SampleTrapAudioProcessor::isMonitorInputEnabled() const noexcept { return monitorInputParameter->load(std::memory_order_relaxed) >= 0.5f; }
bool SampleTrapAudioProcessor::isHoldModeEnabled() const noexcept { return holdModeParameter->load(std::memory_order_relaxed) >= 0.5f; }

bool SampleTrapAudioProcessor::isPadGateDown(int padIndex) const noexcept
{
    if (! juce::isPositiveAndBelow(padIndex, maxPads))
        return false;
    const auto index = static_cast<size_t>(padIndex);
    return padGateParameters[index]->load(std::memory_order_relaxed) >= 0.5f
        || midiGatesForUi[index].load(std::memory_order_acquire);
}

int SampleTrapAudioProcessor::getActivePadCount() const noexcept
{
    return juce::jlimit(1, maxPads, static_cast<int>(std::lround(padCountParameter->load(std::memory_order_relaxed))));
}

int SampleTrapAudioProcessor::getMidiAssignment(int padIndex) const noexcept
{
    return juce::isPositiveAndBelow(padIndex, maxPads)
        ? midiAssignments[static_cast<size_t>(padIndex)].load(std::memory_order_acquire) : -1;
}

int SampleTrapAudioProcessor::getMidiLearnPad() const noexcept { return midiLearnPad.load(std::memory_order_acquire); }

bool SampleTrapAudioProcessor::padHasSample(int padIndex) const noexcept
{
    return juce::isPositiveAndBelow(padIndex, maxPads)
        && recordedSamples[static_cast<size_t>(padIndex)].load(std::memory_order_acquire) > 0;
}

bool SampleTrapAudioProcessor::isPadRecording(int padIndex) const noexcept
{
    return recordingPadForUi.load(std::memory_order_acquire) == padIndex;
}

double SampleTrapAudioProcessor::getRecordedSeconds(int padIndex) const noexcept
{
    const double sampleRate = currentSampleRate.load(std::memory_order_acquire);
    if (! juce::isPositiveAndBelow(padIndex, maxPads) || sampleRate <= 0.0)
        return 0.0;
    return recordedSamples[static_cast<size_t>(padIndex)].load(std::memory_order_acquire) / sampleRate;
}

uint32_t SampleTrapAudioProcessor::getTriggerCount(int padIndex) const noexcept
{
    return juce::isPositiveAndBelow(padIndex, maxPads)
        ? triggerCounts[static_cast<size_t>(padIndex)].load(std::memory_order_relaxed) : 0;
}

void SampleTrapAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    for (int pad = 0; pad < maxPads; ++pad)
        state.setProperty("midiNote" + juce::String(pad + 1), getMidiAssignment(pad), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void SampleTrapAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        if (xml->hasTagName(parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml(*xml);
            for (int pad = 0; pad < maxPads; ++pad)
            {
                const int note = static_cast<int>(state.getProperty("midiNote" + juce::String(pad + 1), -1));
                midiAssignments[static_cast<size_t>(pad)].store(juce::jlimit(-1, 127, note), std::memory_order_release);
            }
            parameters.replaceState(state);
        }
    }
    midiLearnPad.store(-1, std::memory_order_release);
    for (int pad = 0; pad < maxPads; ++pad)
        if (auto* parameter = parameters.getParameter(padParameterId(pad)))
            parameter->setValueNotifyingHost(0.0f);
}

juce::AudioProcessorEditor* SampleTrapAudioProcessor::createEditor() { return new SampleTrapAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SampleTrapAudioProcessor(); }
