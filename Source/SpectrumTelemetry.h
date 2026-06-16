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

#include <atomic>
#include <array>
#include <vector>

// ============================================================================
//  SpectrumTelemetry
//  Lock-free, allocation-free hand-off of per-hop spectral data from the audio
//  thread (producer) to the editor (single consumer). "Latest frame wins":
//  intermediate frames are dropped, which is exactly what a ~60 Hz analyzer
//  wants. No JUCE dependency so the DSP core stays portable/testable.
//
//  Mechanism: classic 3-slot triple buffer. The published-index, the writer's
//  slot, and the reader's slot are ALWAYS a permutation of {0,1,2}, so the
//  producer and consumer never touch the same slot at the same time -> no
//  torn reads, and the producer never blocks.
// ============================================================================

struct SpectrumFrame
{
    int    numBins    = 0;
    int    fftSize    = 0;
    double sampleRate = 44100.0;
    int    mode       = 0;            // 0 Process, 1 Bypass, 2 Solo, 3 Spectral Replace

    bool b1en = false, b2en = false, b3en = false;
    int  b1min = 0, b1max = 0, b2min = 0, b2max = 0, b3min = 0, b3max = 0;

    std::vector<float> mag;       // linear input magnitude per bin
    std::vector<float> floorMag;  // robust detection floor (linear) per bin
    std::vector<float> gainDb;    // applied notch gain in dB, <= 0 (0 = untouched)
    std::vector<float> msc;       // magnitude-squared coherence 0..1 per bin

    void resize (int maxBins)
    {
        mag.assign      ((size_t) maxBins, 0.0f);
        floorMag.assign ((size_t) maxBins, 0.0f);
        gainDb.assign   ((size_t) maxBins, 0.0f);
        msc.assign      ((size_t) maxBins, 0.0f);
    }
};

class SpectrumTripleBuffer
{
public:
    // Called once before processing (audio not yet running). Sizes all slots so
    // the producer never allocates.
    void prepare (int maxBins)
    {
        for (auto& f : slots) f.resize (maxBins);
        mailbox.store (0, std::memory_order_relaxed);
        writeIndex = 1;
        readIndex  = 2;
    }

    // ---- producer (audio thread) ----
    SpectrumFrame& writeSlot() noexcept { return slots[(size_t) writeIndex]; }

    void publish() noexcept
    {
        const int prev = mailbox.exchange (writeIndex | kDirty, std::memory_order_acq_rel);
        writeIndex = prev & kMask;     // recycle whatever slot was previously published
    }

    // ---- consumer (message thread) ----
    // Returns the most recent frame if a new one was published since the last
    // call, otherwise nullptr (caller keeps showing the previous frame).
    const SpectrumFrame* readLatest() noexcept
    {
        if ((mailbox.load (std::memory_order_acquire) & kDirty) == 0)
            return nullptr;
        const int prev = mailbox.exchange (readIndex, std::memory_order_acq_rel);
        readIndex = prev & kMask;
        return &slots[(size_t) readIndex];
    }

private:
    static constexpr int kMask  = 0x3;   // index lives in the low 2 bits (0..2)
    static constexpr int kDirty = 0x4;   // "fresh data" flag

    std::array<SpectrumFrame, 3> slots;
    std::atomic<int> mailbox { 0 };      // published index (+ dirty bit)
    int writeIndex = 1;                  // producer-owned
    int readIndex  = 2;                  // consumer-owned
};
