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

    setResizable (true, true);
    setSize (620, 720);
}

void FeedbackKillerEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void FeedbackKillerEditor::resized()
{
    auto area = getLocalBounds();

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
