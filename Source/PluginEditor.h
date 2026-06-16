/*  This file is part of the Feedback Resonance Killer audio plugin.
    Copyright (C) 2026 Bytemixer
    SPDX-License-Identifier: AGPL-3.0-or-later

    This program is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at
    your option) any later version. It is distributed WITHOUT ANY WARRANTY;
    see the LICENSE file for details.
*/

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

    // ---- footer + About overlay (AGPL "Appropriate Legal Notices") ----
    class AboutOverlay : public juce::Component
    {
    public:
        AboutOverlay();
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
    private:
        juce::HyperlinkButton repoLink;
    };

    juce::Label      titleLabel;
    juce::TextButton aboutButton { "About" };
    AboutOverlay     aboutOverlay;

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
