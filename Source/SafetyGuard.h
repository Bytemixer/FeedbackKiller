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

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// ============================================================================
//  SafetyGuard  -  "protect your ears" last line of defence.
//
//  A DSP fault (a stray NaN/Inf, a runaway value, a denormal cascade) must
//  never reach the speakers. This sits at the plugin boundary:
//
//    makeFinite()  cleans the INPUT, so a bad incoming sample can't poison the
//                  plugin's recursive internal state (FFT history, the MSC
//                  running averages, the overlap-add buffers) - where a single
//                  NaN would otherwise persist forever.
//
//    sanitize()    cleans the OUTPUT: NaN/Inf -> silence, then hard-limits to a
//                  safe ceiling so nothing extreme is ever emitted.
// ============================================================================
class SafetyGuard
{
public:
    // Output ceiling (linear). Default ~+12 dBFS: only catches genuine blow-ups,
    // never touches normal program material (which sits at or below ~1.0).
    void setCeiling (float linearCeiling) noexcept { ceiling = std::abs (linearCeiling); }

    // INPUT hygiene: replace any non-finite sample with silence. No clamping,
    // so legitimately hot input passes through untouched.
    static void makeFinite (juce::AudioBuffer<float>& buffer) noexcept
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int n = 0; n < buffer.getNumSamples(); ++n)
                if (! std::isfinite (d[n]))
                    d[n] = 0.0f;
        }
    }

    // OUTPUT guard: non-finite -> silence, and hard-limit to +/- ceiling.
    // Returns true if it had to intervene (handy for a future UI fault indicator).
    bool sanitize (juce::AudioBuffer<float>& buffer) const noexcept
    {
        bool tripped = false;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int n = 0; n < buffer.getNumSamples(); ++n)
            {
                float v = d[n];
                if (! std::isfinite (v)) { v = 0.0f;     tripped = true; }
                else if (v >  ceiling)   { v =  ceiling; tripped = true; }
                else if (v < -ceiling)   { v = -ceiling; tripped = true; }
                d[n] = v;
            }
        }
        return tripped;
    }

private:
    float ceiling = 4.0f;   // ~ +12 dBFS
};
