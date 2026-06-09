#pragma once

#include "PluginProcessor.h"
#include "SpectrumAnalyzer.h"

// ============================================================================
//  FeedbackKillerEditor
//  A scrollable parameter panel with EDITABLE value fields (double-click /
//  click the number on the right of each slider to type an exact value).
//  Built data-driven from the APVTS so it stays in sync with the parameters.
// ============================================================================

class FeedbackKillerEditor : public juce::AudioProcessorEditor
{
public:
    explicit FeedbackKillerEditor (FeedbackKillerProcessor&);
    ~FeedbackKillerEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    FeedbackKillerProcessor& proc;

    SpectrumAnalyzer analyzer;

    juce::Viewport  viewport;
    juce::Component content;

    juce::OwnedArray<juce::Label>                 labels;
    juce::OwnedArray<juce::Slider>                sliders;
    juce::OwnedArray<APVTS::SliderAttachment>     sliderAtts;
    juce::OwnedArray<juce::ComboBox>              combos;
    juce::OwnedArray<APVTS::ComboBoxAttachment>   comboAtts;
    juce::OwnedArray<juce::ToggleButton>          toggles;
    juce::OwnedArray<APVTS::ButtonAttachment>     toggleAtts;

    struct RowItem { juce::Label* label; juce::Component* control; };
    std::vector<RowItem> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FeedbackKillerEditor)
};
