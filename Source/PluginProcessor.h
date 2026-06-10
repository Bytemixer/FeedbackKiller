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

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "FeedbackKillerDSP.h"
#include "SafetyGuard.h"

// ============================================================================
//  FeedbackKillerProcessor
//  The JUCE AudioProcessor shell. Owns the parameter tree (APVTS) and the
//  standalone DSP core. processBlock caches parameters once per block and
//  hands the audio to the DSP.
// ============================================================================

class FeedbackKillerProcessor : public juce::AudioProcessor
{
public:
    FeedbackKillerProcessor();
    ~FeedbackKillerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Feedback Resonance Killer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- analyzer telemetry passthrough (for the editor) ----
    void setTelemetryEnabled (bool on) noexcept { dsp.setTelemetryEnabled (on); }
    const SpectrumFrame* readSpectrum() noexcept { return dsp.readSpectrum(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    FeedbackKillerDSP::Params readParams() const;

    FeedbackKillerDSP dsp;
    SafetyGuard safety;
    int reportedLatencySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FeedbackKillerProcessor)
};
