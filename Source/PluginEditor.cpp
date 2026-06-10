/*  This file is part of the Feedback Resonance Killer audio plugin.
    Copyright (C) 2026 Bytemixer
    SPDX-License-Identifier: AGPL-3.0-or-later

    This program is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at
    your option) any later version. It is distributed WITHOUT ANY WARRANTY;
    see the LICENSE file for details.
*/

#include "PluginEditor.h"

namespace
{
    enum Kind { K_FLOAT, K_BOOL, K_CHOICE };
    struct PInfo { const char* id; const char* name; Kind kind; };

    // Display order (mirrors the parameter layout).
    const PInfo kParams[] = {
        { "mscThreshold",   "MSC Threshold",        K_FLOAT },
        { "floorMargin",    "Floor Margin (dB)",    K_FLOAT },
        { "mscIntegration", "MSC Integration (ms)", K_FLOAT },
        { "band1Enable",    "Band 1 Enable",        K_BOOL  },
        { "band1Min",       "Band 1 Min (Hz)",      K_FLOAT },
        { "band1Max",       "Band 1 Max (Hz)",      K_FLOAT },
        { "band2Enable",    "Band 2 Enable",        K_BOOL  },
        { "band2Min",       "Band 2 Min (Hz)",      K_FLOAT },
        { "band2Max",       "Band 2 Max (Hz)",      K_FLOAT },
        { "band3Enable",    "Band 3 Enable",        K_BOOL  },
        { "band3Min",       "Band 3 Min (Hz)",      K_FLOAT },
        { "band3Max",       "Band 3 Max (Hz)",      K_FLOAT },
        { "mode",           "Mode",                 K_CHOICE },
        { "maxAttenuation", "Max Attenuation (dB)", K_FLOAT },
        { "notchWidth",     "Notch Width (bins)",   K_FLOAT },
        { "notchTaper",     "Notch Edge Taper",     K_FLOAT },
        { "overcut",        "Overcut",              K_FLOAT },
        { "minCut",         "Min Cut (dB)",         K_FLOAT },
        { "attack",         "Attack (ms)",          K_FLOAT },
        { "release",        "Release (ms)",         K_FLOAT },
        { "hold",           "Hold (ms)",            K_FLOAT },
        { "replaceAnchorClean", "Replace Anchor Clean", K_FLOAT },
        { "tonalGate",      "Tonal Gate (Phase)",   K_FLOAT },
        { "channelMode",    "Channel Mode",         K_CHOICE },
        { "fftSize",        "FFT Size",             K_CHOICE },
    };
}

FeedbackKillerEditor::FeedbackKillerEditor (FeedbackKillerProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), analyzer (p)
{
    auto& apvts = proc.apvts;

    addAndMakeVisible (analyzer);

    for (auto& pi : kParams)
    {
        auto* lab = labels.add (new juce::Label ({}, pi.name));
        lab->setJustificationType (juce::Justification::centredRight);
        content.addAndMakeVisible (lab);

        juce::Component* ctrl = nullptr;

        if (pi.kind == K_FLOAT)
        {
            auto* s = sliders.add (new juce::Slider (juce::Slider::LinearHorizontal,
                                                     juce::Slider::TextBoxRight));
            s->setTextBoxStyle (juce::Slider::TextBoxRight, /*readOnly*/ false, 84, 22);
            content.addAndMakeVisible (s);
            sliderAtts.add (new APVTS::SliderAttachment (apvts, pi.id, *s));
            ctrl = s;
        }
        else if (pi.kind == K_BOOL)
        {
            auto* t = toggles.add (new juce::ToggleButton());
            content.addAndMakeVisible (t);
            toggleAtts.add (new APVTS::ButtonAttachment (apvts, pi.id, *t));
            ctrl = t;
        }
        else // K_CHOICE
        {
            auto* c = combos.add (new juce::ComboBox());
            if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (pi.id)))
                c->addItemList (cp->choices, 1);
            content.addAndMakeVisible (c);
            comboAtts.add (new APVTS::ComboBoxAttachment (apvts, pi.id, *c));
            ctrl = c;
        }

        rows.push_back ({ lab, ctrl });
    }

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);

    // footer: title + version on the left, About on the right
    titleLabel.setText (juce::String ("Feedback Resonance Killer  v") + JucePlugin_VersionString,
                        juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (12.0f));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
    addAndMakeVisible (titleLabel);

    aboutButton.onClick = [this] { aboutOverlay.setVisible (true); aboutOverlay.toFront (false); };
    addAndMakeVisible (aboutButton);

    addChildComponent (aboutOverlay);   // hidden until About is clicked

    setResizable (true, true);
    setSize (620, 740);
}

// ============================================================================
//  About overlay - the AGPL "Appropriate Legal Notices"
// ============================================================================
FeedbackKillerEditor::AboutOverlay::AboutOverlay()
    : repoLink ("github.com/Bytemixer/feedback-resonance-killer",
                juce::URL ("https://github.com/Bytemixer/feedback-resonance-killer"))
{
    setInterceptsMouseClicks (true, true);
    repoLink.setFont (juce::FontOptions (14.0f), false);
    addAndMakeVisible (repoLink);
}

void FeedbackKillerEditor::AboutOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xee14161a));

    const juce::String lines =
        juce::String ("Feedback Resonance Killer  v") + JucePlugin_VersionString + "\n"
        "Copyright (C) 2026 Bytemixer\n\n"
        "This program is free software, licensed under the GNU Affero\n"
        "General Public License v3 (or later). It comes with\n"
        "ABSOLUTELY NO WARRANTY. See the LICENSE file for details.\n\n"
        "Built with the JUCE framework (AGPLv3).\n"
        "VST is a trademark of Steinberg Media Technologies GmbH.\n\n"
        "Source code:";

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::FontOptions (14.0f));
    auto r = getLocalBounds().toFloat().reduced (24.0f);
    g.drawFittedText (lines, r.toNearestInt().withTrimmedBottom (90),
                      juce::Justification::centred, 14);

    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("(click anywhere to close)", getLocalBounds().removeFromBottom (28),
                juce::Justification::centred);
}

void FeedbackKillerEditor::AboutOverlay::resized()
{
    // just under the notice text block (which is centred)
    repoLink.setBounds (getLocalBounds().withSizeKeepingCentre (380, 24).translated (0, 96));
}

void FeedbackKillerEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void FeedbackKillerEditor::resized()
{
    auto area = getLocalBounds();

    aboutOverlay.setBounds (area);   // full-window overlay

    // footer strip: title/version + About button
    auto footer = area.removeFromBottom (24);
    aboutButton.setBounds (footer.removeFromRight (64).reduced (4, 2));
    titleLabel.setBounds (footer.reduced (6, 2));

    // analyzer on top (fixed-ish height), parameter list fills the rest
    const int analyzerH = juce::jlimit (180, 320, area.getHeight() / 3);
    analyzer.setBounds (area.removeFromTop (analyzerH));

    viewport.setBounds (area);

    const int rowH = 26, gap = 2, pad = 8, labelW = 170, scrollbar = 12;
    const int w = area.getWidth() - scrollbar;

    content.setSize (w, (int) rows.size() * (rowH + gap) + pad * 2);

    int y = pad;
    for (auto& r : rows)
    {
        r.label->setBounds (0, y, labelW, rowH);
        r.control->setBounds (labelW + pad, y, w - labelW - pad * 2, rowH);
        y += rowH + gap;
    }
}
