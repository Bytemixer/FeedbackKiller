/*  This file is part of the Feedback Resonance Killer audio plugin.
    Copyright (C) 2026 Bytemixer
    SPDX-License-Identifier: AGPL-3.0-or-later

    This program is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at
    your option) any later version. It is distributed WITHOUT ANY WARRANTY;
    see the LICENSE file for details.
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Frequency parameters: 100 Hz .. 24 kHz with a skew so the low end gets
    // more knob travel (matches where the action is for feedback work).
    juce::NormalisableRange<float> freqRange()
    {
        // Linear, 1 Hz step. The value fields are editable, so any exact Hz can
        // be typed to match the JSFX bands precisely for a clean null test.
        return { 100.0f, 24000.0f, 1.0f };
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout
FeedbackKillerProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pf = [&] (const char* id, const char* name, NormalisableRange<float> r, float def,
                   const char* suffix = "")
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, r, def,
            AudioParameterFloatAttributes().withLabel (suffix)));
    };
    auto pb = [&] (const char* id, const char* name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };
    auto pc = [&] (const char* id, const char* name, StringArray choices, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name,
                                                            std::move (choices), def));
    };

    // --- Detection ---
    pf ("mscThreshold",   "MSC Threshold",      { 0.0f, 1.0f, 0.01f }, 0.7f);
    pf ("floorMargin",    "Floor Margin",       { 0.0f, 40.0f, 0.5f }, 16.0f, "dB");
    pf ("mscIntegration", "MSC Integration",    { 10.0f, 500.0f, 1.0f }, 150.0f, "ms");

    // --- Bands ---
    pb ("band1Enable", "Band 1 Enable", true);
    pf ("band1Min",    "Band 1 Min Freq", freqRange(), 3500.0f, "Hz");
    pf ("band1Max",    "Band 1 Max Freq", freqRange(), 6200.0f, "Hz");
    pb ("band2Enable", "Band 2 Enable", true);
    pf ("band2Min",    "Band 2 Min Freq", freqRange(), 8500.0f, "Hz");
    pf ("band2Max",    "Band 2 Max Freq", freqRange(), 12500.0f, "Hz");
    pb ("band3Enable", "Band 3 Enable", false);
    pf ("band3Min",    "Band 3 Min Freq", freqRange(), 12500.0f, "Hz");
    pf ("band3Max",    "Band 3 Max Freq", freqRange(), 16000.0f, "Hz");

    // --- Reduction shape ---
    pc ("mode", "Mode", { "Process", "Bypass", "Solo (Notched Energy)", "Spectral Replace" }, 0);
    pf ("maxAttenuation", "Max Attenuation", { 0.0f, 120.0f, 0.5f }, 60.0f, "dB");
    pf ("notchWidth",     "Notch Width",     { 0.0f, 15.0f, 1.0f }, 1.0f, "bins");
    pf ("notchTaper",     "Notch Edge Taper",{ 0.0f, 20.0f, 1.0f }, 4.0f, "bins");
    pf ("overcut",        "Overcut",         { 1.0f, 6.0f, 0.1f }, 1.6f);
    pf ("minCut",         "Min Cut",         { 0.0f, 40.0f, 0.5f }, 4.0f, "dB");

    // --- Dynamics ---
    pf ("attack",  "Attack",  { 1.0f, 500.0f, 1.0f }, 10.0f, "ms");
    pf ("release", "Release", { 10.0f, 2000.0f, 5.0f }, 300.0f, "ms");
    pf ("hold",    "Hold",    { 50.0f, 3000.0f, 5.0f }, 200.0f, "ms");

    // --- Spectral Replace ---
    pf ("replaceAnchorClean", "Replace Anchor Clean", { 0.50f, 0.99f, 0.01f }, 0.85f);

    // --- Enhancement: phase-stability tonal gate (0 = off => faithful JSFX) ---
    pf ("tonalGate", "Tonal Gate (Phase)", { 0.0f, 1.0f, 0.01f }, 0.0f);

    // --- Engine ---
    pc ("channelMode", "Channel Mode",
        { "Auto-Detect", "Forced Stereo", "Forced Mono (Bus/Panned)",
          "Unlinked Dual-Mono" }, 0);
    pc ("fftSize", "FFT Size", { "4096", "8192", "16384", "32768" }, 1);

    return layout;
}

FeedbackKillerProcessor::FeedbackKillerProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

FeedbackKillerDSP::Params FeedbackKillerProcessor::readParams() const
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    FeedbackKillerDSP::Params p;
    p.mscThreshold      = get ("mscThreshold");
    p.floorMarginDb     = get ("floorMargin");
    p.mscIntegrationMs  = get ("mscIntegration");
    p.band1En = get ("band1Enable") > 0.5f; p.band1Min = get ("band1Min"); p.band1Max = get ("band1Max");
    p.band2En = get ("band2Enable") > 0.5f; p.band2Min = get ("band2Min"); p.band2Max = get ("band2Max");
    p.band3En = get ("band3Enable") > 0.5f; p.band3Min = get ("band3Min"); p.band3Max = get ("band3Max");
    p.mode            = (int) get ("mode");
    p.maxAttenDb      = get ("maxAttenuation");
    p.notchWidthBins  = get ("notchWidth");
    p.notchTaperBins  = get ("notchTaper");
    p.overcut         = get ("overcut");
    p.minCutDb        = get ("minCut");
    p.attackMs        = get ("attack");
    p.releaseMs       = get ("release");
    p.holdMs          = get ("hold");
    p.replaceAnchorClean = get ("replaceAnchorClean");
    p.tonalGate       = get ("tonalGate");
    p.channelMode     = (int) get ("channelMode");
    p.fftOrder        = 12 + (int) get ("fftSize");   // choice index 0..3 -> order 12..15
    return p;
}

void FeedbackKillerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.setParams (readParams());     // so prepare() configures the correct FFT size
    dsp.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    reportedLatencySamples = dsp.getLatencySamples();
    setLatencySamples (reportedLatencySamples);
}

bool FeedbackKillerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void FeedbackKillerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    SafetyGuard::makeFinite (buffer);   // keep bad input from poisoning recursive state

    dsp.setParams (readParams());
    dsp.setRealtime (! isNonRealtime());   // offline render runs the hop engine inline
    dsp.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());

    safety.sanitize (buffer);           // protect-your-ears: no NaN/Inf or blow-up ever leaves

    if (dsp.getLatencySamples() != reportedLatencySamples)
    {
        reportedLatencySamples = dsp.getLatencySamples();
        setLatencySamples (reportedLatencySamples);
    }
}

juce::AudioProcessorEditor* FeedbackKillerProcessor::createEditor()
{
    // Custom panel with EDITABLE value fields so bands can be typed to exact Hz
    // for null-testing against the JSFX. Visualizer comes later.
    return new FeedbackKillerEditor (*this);
}

void FeedbackKillerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        std::unique_ptr<juce::XmlElement> xml (state.createXml());
        copyXmlToBinary (*xml, destData);
    }
}

void FeedbackKillerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FeedbackKillerProcessor();
}
