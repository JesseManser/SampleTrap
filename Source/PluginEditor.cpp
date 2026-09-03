#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
const auto background = juce::Colour::fromRGB(4, 4, 5);
const auto panel = juce::Colour::fromRGB(27, 28, 31);
const auto border = juce::Colour::fromRGB(78, 80, 86);
const auto blue = juce::Colour::fromRGB(55, 117, 176);
const auto red = juce::Colour::fromRGB(190, 49, 54);
const auto green = juce::Colour::fromRGB(47, 139, 92);
}

void SquareButton::paintButton(juce::Graphics& graphics, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat();
    auto colour = findColour(juce::TextButton::buttonColourId);

    if (highlighted)
        colour = colour.brighter(0.08f);
    if (down)
        colour = colour.brighter(0.18f);

    graphics.setColour(colour);
    graphics.fillRect(bounds);
    graphics.setColour(findColour(juce::TextButton::buttonOnColourId));
    graphics.drawRect(bounds, 1.0f);
    graphics.setColour(findColour(juce::TextButton::textColourOffId));
    graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    graphics.drawFittedText(getButtonText(), getLocalBounds().reduced(8), juce::Justification::centred, 4);
}

void SquareButton::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isRightButtonDown())
    {
        if (onRightClick)
            onRightClick();
        return;
    }
    juce::TextButton::mouseDown(event);
}

SampleTrapAudioProcessorEditor::SampleTrapAudioProcessorEditor(SampleTrapAudioProcessor& owner)
    : AudioProcessorEditor(owner), audioProcessor(owner)
{
    logo = juce::ImageCache::getFromMemory(BinaryData::sampletraplogo_png, BinaryData::sampletraplogo_pngSize);

    setWantsKeyboardFocus(true);
    setFocusContainerType(juce::Component::FocusContainerType::focusContainer);

    for (auto* button : { &recordArmButton, &monitorInputButton, &holdModeButton, &midiLearnButton })
    {
        button->setClickingTogglesState(true);
        button->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(220, 222, 226));
        addAndMakeVisible(*button);
    }

    auto& state = audioProcessor.getParameterState();
    recordArmAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, "recordArm", recordArmButton);
    monitorInputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, "monitorInput", monitorInputButton);
    holdModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, "holdMode", holdModeButton);
    midiLearnButton.onClick = [this]
    {
        if (! midiLearnButton.getToggleState())
        {
            pendingLearnPad = -1;
            audioProcessor.cancelMidiLearn();
        }
    };

    padCountLabel.setText("ACTIVE PADS", juce::dontSendNotification);
    padCountLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(190, 192, 198));
    padCountLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(padCountLabel);

    padCountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    padCountSlider.setRange(1.0, SampleTrapAudioProcessor::maxPads, 1.0);
    padCountSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 24);
    padCountSlider.setColour(juce::Slider::trackColourId, blue);
    padCountSlider.setColour(juce::Slider::backgroundColourId, panel);
    padCountSlider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    padCountSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    padCountSlider.setColour(juce::Slider::textBoxBackgroundColourId, panel);
    padCountSlider.setColour(juce::Slider::textBoxOutlineColourId, border);
    addAndMakeVisible(padCountSlider);
    padCountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "activePadCount", padCountSlider);

    for (int pad = 0; pad < SampleTrapAudioProcessor::maxPads; ++pad)
    {
        auto& button = padButtons[static_cast<size_t>(pad)];
        button.setButtonText(juce::String(pad + 1) + "\nEMPTY");
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        button.setColour(juce::TextButton::buttonOnColourId, border);
        button.setMouseClickGrabsKeyboardFocus(true);
        button.onStateChange = [this, pad]
        {
            const auto index = static_cast<size_t>(pad);
            const bool down = padButtons[static_cast<size_t>(pad)].isDown();
            if (down == mousePadDown[index])
                return;
            mousePadDown[index] = down;

            if (down && midiLearnButton.getToggleState())
            {
                mousePadLearnClick[index] = true;
                pendingLearnPad = pad;
                audioProcessor.beginMidiLearn(pad);
                return;
            }
            if (! down && mousePadLearnClick[index])
            {
                mousePadLearnClick[index] = false;
                return;
            }
            audioProcessor.setPadGateFromUi(pad, down);
        };
        button.onRightClick = [this, pad]
        {
            audioProcessor.clearMidiAssignment(pad);
            if (pendingLearnPad == pad)
            {
                pendingLearnPad = -1;
                audioProcessor.cancelMidiLearn();
                midiLearnButton.setToggleState(false, juce::dontSendNotification);
            }
        };
        lastTriggerCounts[static_cast<size_t>(pad)] = audioProcessor.getTriggerCount(pad);
        addAndMakeVisible(button);
    }

    setResizable(true, true);
    setResizeLimits(500, 620, 1100, 1200);
    setSize(720, 860);
    startTimerHz(20);
}

void SampleTrapAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(background);

    if (audioProcessor.isRecordArmed())
    {
        graphics.setColour(red);
        graphics.drawRect(getLocalBounds().toFloat().reduced(2.0f), 2.0f);
    }

    if (logo.isValid())
    {
        const int logoWidth = juce::jmin(500, static_cast<int>(getWidth() * 0.66f));
        const int logoHeight = juce::jmax(1, logoWidth * logo.getHeight() / logo.getWidth());
        graphics.drawImageWithin(logo,
                                 (getWidth() - logoWidth) / 2, 8, logoWidth, logoHeight,
                                 juce::RectanglePlacement::centred
                                     | juce::RectanglePlacement::onlyReduceInSize);
    }
}

void SampleTrapAudioProcessorEditor::resized()
{
    const int activePads = audioProcessor.getActivePadCount();
    auto area = getLocalBounds().reduced(20);
    area.removeFromTop(165);

    auto controls = area.removeFromTop(42);
    const int controlsWidth = 124 + 10 + 140 + 10 + 110 + 10 + 110;
    controls = controls.withSizeKeepingCentre(controlsWidth, controls.getHeight());
    recordArmButton.setBounds(controls.removeFromLeft(124));
    controls.removeFromLeft(10);
    monitorInputButton.setBounds(controls.removeFromLeft(140));
    controls.removeFromLeft(10);
    holdModeButton.setBounds(controls.removeFromLeft(110));
    controls.removeFromLeft(10);
    midiLearnButton.setBounds(controls.removeFromLeft(110));

    area.removeFromTop(8);
    auto padCountArea = area.removeFromTop(36).withSizeKeepingCentre(330, 36);
    padCountLabel.setBounds(padCountArea.removeFromLeft(96));
    padCountSlider.setBounds(padCountArea);
    area.removeFromTop(15);

    const int columns = activePads == 1 ? 1 : (activePads <= 4 ? 2 : 4);
    const int rows = (activePads + columns - 1) / columns;
    const int gap = 10;
    const int side = juce::jmax(28, juce::jmin(145,
        juce::jmin((area.getWidth() - gap * (columns - 1)) / columns,
                   (area.getHeight() - gap * (rows - 1)) / rows)));
    const int gridHeight = rows * side + (rows - 1) * gap;
    const int firstY = area.getY() + juce::jmax(0, (area.getHeight() - gridHeight) / 2);

    for (int pad = 0; pad < SampleTrapAudioProcessor::maxPads; ++pad)
    {
        auto& button = padButtons[static_cast<size_t>(pad)];
        const bool visible = pad < activePads;
        button.setVisible(visible);
        if (! visible)
            continue;

        const int row = pad / columns;
        const int column = pad % columns;
        const int padsInRow = juce::jmin(columns, activePads - row * columns);
        const int rowWidth = padsInRow * side + (padsInRow - 1) * gap;
        const int firstX = area.getX() + (area.getWidth() - rowWidth) / 2;
        button.setBounds(firstX + column * (side + gap), firstY + row * (side + gap), side, side);
    }

    lastActivePadCount = activePads;
}

bool SampleTrapAudioProcessorEditor::keyStateChanged(bool)
{
    if (audioProcessor.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
        return false;

    bool handled = false;
    const int activePads = audioProcessor.getActivePadCount();
    for (int pad = 0; pad < SampleTrapAudioProcessor::keyboardPads; ++pad)
    {
        const bool down = pad < activePads && juce::KeyPress::isKeyCurrentlyDown('1' + pad);
        if (down != keyboardPadDown[static_cast<size_t>(pad)])
        {
            keyboardPadDown[static_cast<size_t>(pad)] = down;
            audioProcessor.setPadGateFromUi(pad, down);
        }
        handled = handled || down;
    }
    return handled;
}

void SampleTrapAudioProcessorEditor::focusLost(FocusChangeType)
{
    releaseKeyboardPads();
}

void SampleTrapAudioProcessorEditor::releaseKeyboardPads()
{
    for (int pad = 0; pad < SampleTrapAudioProcessor::keyboardPads; ++pad)
    {
        if (keyboardPadDown[static_cast<size_t>(pad)])
        {
            keyboardPadDown[static_cast<size_t>(pad)] = false;
            audioProcessor.setPadGateFromUi(pad, false);
        }
    }
}

void SampleTrapAudioProcessorEditor::timerCallback()
{
    const int activePads = audioProcessor.getActivePadCount();
    if (activePads != lastActivePadCount)
        resized();

    recordArmButton.setColour(juce::TextButton::buttonColourId,
                              audioProcessor.isRecordArmed() ? red : panel);
    monitorInputButton.setColour(juce::TextButton::buttonColourId,
                                audioProcessor.isMonitorInputEnabled() ? green : panel);
    holdModeButton.setColour(juce::TextButton::buttonColourId,
                            audioProcessor.isHoldModeEnabled() ? blue : panel);

    const int learningPad = audioProcessor.getMidiLearnPad();
    if (pendingLearnPad >= 0 && learningPad != pendingLearnPad)
    {
        pendingLearnPad = -1;
        midiLearnButton.setToggleState(false, juce::dontSendNotification);
    }
    midiLearnButton.setButtonText(learningPad >= 0 ? "PLAY NOTE" : "MIDI LEARN");
    midiLearnButton.setColour(
    juce::TextButton::buttonColourId,
    learningPad >= 0
        ? green
        : (midiLearnButton.getToggleState() ? blue : panel));

    for (int pad = 0; pad < SampleTrapAudioProcessor::maxPads; ++pad)
    {
        auto& button = padButtons[static_cast<size_t>(pad)];
        const auto index = static_cast<size_t>(pad);
        const bool recording = audioProcessor.isPadRecording(pad);
        const bool hasSample = audioProcessor.padHasSample(pad);
        const bool gateDown = audioProcessor.isPadGateDown(pad);
        const uint32_t triggers = audioProcessor.getTriggerCount(pad);

        if (triggers != lastTriggerCounts[index])
        {
            lastTriggerCounts[index] = triggers;
            flashFrames[index] = 3;
        }

        juce::Colour colour = hasSample ? blue : panel;
        if (recording)
            colour = red;
        else if (gateDown || flashFrames[index] > 0)
            colour = colour.brighter(0.24f);
        button.setColour(juce::TextButton::buttonColourId, colour);

        juce::String status = "EMPTY";
        if (recording)
            status = "RECORDING";
        else if (hasSample)
            status = juce::String(audioProcessor.getRecordedSeconds(pad), 2) + " s";
        const int midiNote = audioProcessor.getMidiAssignment(pad);
        juce::String midiText = "MIDI --";
        if (midiNote >= 0)
            midiText = juce::MidiMessage::getMidiNoteName(midiNote, true, true, 4)
                + "  (" + juce::String(midiNote) + ")";
        if (learningPad == pad)
            midiText = "PLAY MIDI NOTE";
        button.setButtonText(juce::String(pad + 1) + "\n" + status + "\n" + midiText);

        if (flashFrames[index] > 0)
            --flashFrames[index];
    }

    repaint();
}
