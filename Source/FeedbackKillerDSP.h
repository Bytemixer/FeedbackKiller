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

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>
#include <cmath>
#include <atomic>

#include "SpectrumTelemetry.h"

// ============================================================================
//  FeedbackKillerDSP
//  Faithful C++ port of the "Feedback Resonance Killer" JSFX.
//
//  Pipeline per analysis hop (hop = fftSize/4, 75% overlap, symmetric Hann):
//    load+FFT -> analyze spectrum (+MSC) -> robust floor -> targets -> hold ->
//    dilation/taper + attack/release smoothing -> strategy (mode) -> IFFT+OLA.
//
//  THREADING (async hop engine):
//  The whole spectral hop is a burst of work (4x 32k FFTs + the robust floor
//  scan) that lands in a single device block. At pro buffer sizes (<=128) that
//  burst can miss the real-time deadline, so the audio thread never runs it:
//
//    audio thread:  stage input -> hop slot ring -> pop processed FIFO output
//    worker thread: slot ring -> engine (the JSFX hop, unchanged) -> FIFO
//
//  This costs two extra hops of latency (reported as fftSize + 2*hopSize, host
//  PDC-compensated) and gives the engine a full hop period (~170 ms @ 32k) of
//  budget instead of one device buffer (~3 ms). Offline render drives the same
//  pipeline inline on the calling thread, so output is deterministic and
//  identical to real time. If the worker ever falls behind, the output FIFO
//  emits silence and re-aligns via a sample-debt counter (no drift).
//
//  All buffers are pre-allocated in prepare() for the MAX FFT size (32768);
//  changing FFT size at runtime only recomputes derived values + clears state,
//  never allocates. Nothing here allocates, locks, or blocks in process()
//  (the engine SpinLock is contended only on an explicit FFT-size change).
// ============================================================================

class FeedbackKillerDSP : private juce::Thread
{
public:
    // Per-block parameter snapshot, pushed by the processor before process().
    struct Params
    {
        float mscThreshold      = 0.7f;
        float floorMarginDb     = 16.0f;
        float mscIntegrationMs  = 150.0f;

        bool  band1En = true,  band2En = true,  band3En = false;
        float band1Min = 3500.f, band1Max = 6200.f;
        float band2Min = 8500.f, band2Max = 12500.f;
        float band3Min = 12500.f, band3Max = 16000.f;

        int   mode = 0;             // 0 Process, 1 Bypass, 2 Solo, 3 Spectral Replace
        float maxAttenDb = 60.f;
        float notchWidthBins = 1.f;
        float notchTaperBins = 4.f;
        float overcut = 1.6f;
        float minCutDb = 4.f;

        float attackMs = 10.f;
        float releaseMs = 300.f;
        float holdMs = 200.f;

        float replaceAnchorClean = 0.85f;

        // 0 Auto, 1 Forced Stereo, 2 Forced Mono, 3 Unlinked Dual-Mono.
        // Unlinked (enhancement, diverges from JSFX): each channel gets its own
        // floor/prominence/hold/notch, so a one-sided resonance is cut only in
        // the channel it lives in. MSC is bypassed (single-channel detection).
        int   channelMode = 0;
        int   fftOrder = 13;        // log2(fftSize): 12..15 -> 4096..32768

        // Enhancement (diverges from JSFX): phase-stability "tonal" gate.
        // 0 = off (faithful). >0 = require per-bin phase coherence R >= ~tonalGate
        // before notching, so steady tonal feedback is cut but phase-chaotic
        // content (tambourine/noise) is spared. Off costs nothing.
        float tonalGate = 0.0f;
    };

    FeedbackKillerDSP() : juce::Thread ("FK Hop Worker") {}
    ~FeedbackKillerDSP() override
    {
        signalThreadShouldExit();
        notify();
        stopThread (2000);
    }

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setParams (const Params& p) { pending = p; }

    // Real time: hops run on the worker thread. Offline render: inline.
    void setRealtime (bool isRealtime) noexcept { realtime = isRealtime; }

    // Latency the host must compensate: fftSize (STFT) + 2*hopSize (async budget).
    int  getLatencySamples() const { return latencyAtomic.load (std::memory_order_relaxed); }

    void process (float* const* channels, int numChannels, int numSamples);

    // ---- analyzer telemetry (audio -> UI, lock-free) ----
    // Enable only while an editor is open, so offline render pays nothing.
    void setTelemetryEnabled (bool on) noexcept { telemetryOn.store (on, std::memory_order_relaxed); }
    const SpectrumFrame* readSpectrum() noexcept { return spectrum.readLatest(); }

private:
    // ---- configuration ----
    static constexpr int kMaxOrder  = 15;          // 32768
    static constexpr int kMaxFFT    = 1 << kMaxOrder;
    static constexpr int kMaxBins   = kMaxFFT / 2;
    static constexpr int kMaxHop    = kMaxFFT / 4;
    static constexpr int kSlots     = 4;           // staged-hop ring depth
    static constexpr double kOlaGain = 1.5;
    static constexpr float  kEps     = 1.0e-9f;

    void configureFFT (int order);                 // recompute derived values + clear
    void updateDerived (const Params& p);          // band bins, coefs, msc alpha (per hop)
    bool binInActiveBand (int k) const;

    // Per-channel detection view. Linked modes run the pipeline once on the
    // shared state (chA) and apply it to both channels; Unlinked Dual-Mono runs
    // it twice (chA = left, chB = right). Pointers into the pre-allocated
    // vectors below — no ownership, no allocation.
    struct ChanState
    {
        float* mag;          // input magnitude per bin
        float* floorM;       // robust floor
        float* notchTarget;  // raw per-bin target gain
        float* notchCurr;    // smoothed applied gain
        float* effTarget;    // dilated/tapered target
        float* peakAtten;    // hold engine: held attenuation (dB)
        int*   hold;         // hold engine: frames remaining
        int*   lAnchor;      // Spectral Replace: nearest clean bin left
        int*   rAnchor;      // Spectral Replace: nearest clean bin right
    };

    float robustFloorBin (const float* mag, int k, int r, int st) const;

    void runHop();                                 // the full JSFX @sample hop body
    void dspLoadAndFFT();
    void dspAnalyzeSpectrum();
    void dspCalculateFloor (ChanState& c);
    void dspCalculateTargets (ChanState& c, bool writeViz);
    void dspApplyDilationAndSmoothing (ChanState& c);
    void dspApplyStrategy();
    void dspIfftAndOla();

    void strategyProcessCh (float* fft, int k, float g);
    void strategySoloCh    (float* fft, int k, float g);
    void strategyReplaceCh (float* fft, const float* scratch, const ChanState& c, int k);

    // ---- async hop pipeline ----
    struct HopSlot
    {
        Params params;
        std::vector<float> inL, inR;   // kMaxHop each
    };

    void run() override;                           // worker loop
    void processSlot (HopSlot& s);                 // one staged hop through the engine
    void drainSlotsInline();                       // offline-render path
    void resetPipeline (int order);                // audio thread; takes engineLock
    void resetPipelineLocked (int order);          // core; caller holds engineLock
    void pushSlot (const Params& p);
    void popOutput (float* L, float* R, int n, int numChannels);

    // ---- runtime state (engine: owned by the worker / lock holder) ----
    double fs = 44100.0;
    int    fftSize = 8192, hopSize = 2048, nbins = 4096, fftOrder = 13;
    float  olaNorm = 1.0f;

    int    inPos = 0, samplesToFFT = 0;
    bool   firstFFTDone = false;

    // derived per hop
    float  mscThresh = 0.7f, floorMargin = 16.f, maxAttenDb = 60.f;
    float  overcut = 1.6f, minCutDb = 4.f, replaceAnchorClean = 0.85f;
    int    mode = 0, chanMode = 0;
    int    notchWidthBins = 1, notchTaperBins = 4;
    float  attackCoef = 0.f, releaseCoef = 0.f, mscAlpha = 0.f;
    float  phaseAlpha = 0.f;    // dedicated, longer EMA for phase coherence
    int    holdFramesTarget = 1;
    int    b1min=1,b1max=1,b2min=1,b2max=1,b3min=1,b3max=1;
    bool   b1en=true,b2en=true,b3en=false;
    int    floorWinRadius = 300, floorStride = 1, winStride = 1;
    float  floorWinHz = 1800.f;
    float  tonalGate = 0.f;     // 0 = phase gate off (faithful)

    // analysis scalars
    double lEnergy = 0.0, rEnergy = 0.0;
    int    autoMscBypass = 0;

    Params pending {};

    // FFT objects, one per supported order (pre-built, no alloc in process)
    std::unique_ptr<juce::dsp::FFT> ffts[kMaxOrder - 12 + 1];

    // ---- buffers (sized for kMaxFFT in prepare) ----
    std::vector<float> inBufL, inBufR, outBufL, outBufR;   // kMaxFFT ring buffers
    std::vector<float> winBuf;                              // kMaxFFT
    std::vector<float> fftL, fftR;                          // 2*kMaxFFT (complex interleaved)
    std::vector<float> sxyRe, sxyIm, sxx, syy;             // kMaxBins
    std::vector<float> magSmooth, vizMag, vizMsc;          // kMaxBins
    std::vector<float> notchTarget, notchCurr, effTarget;  // kMaxBins
    std::vector<float> peakAttenDb;                         // kMaxBins
    std::vector<int>   binHold, leftAnchor, rightAnchor;   // kMaxBins
    std::vector<float> scratchL, scratchR;                 // 2*kMaxBins

    // Unlinked Dual-Mono: independent right-channel detection state (the
    // shared/left set above doubles as the left channel in that mode).
    std::vector<float> vizMagR, magSmoothR, notchTargetR, notchCurrR,
                       effTargetR, peakAttenDbR;           // kMaxBins
    std::vector<int>   binHoldR, leftAnchorR, rightAnchorR; // kMaxBins
    ChanState chA {}, chB {};                              // views set in prepare()

    // phase-stability gate state (per bin): previous-hop phase + EMA of the
    // unit phase-increment vector. |(cohC,cohS)| = coherence R in [0,1].
    std::vector<float> prevPhase, phaseCohC, phaseCohS, phaseCoh;  // kMaxBins

    // ---- async pipeline state ----
    HopSlot           slots[kSlots];
    std::atomic<int>  slotWriteCount { 0 }, slotReadCount { 0 };
    std::vector<float> stageL, stageR;                     // kMaxHop staging
    int               stageFill = 0;

    juce::AbstractFifo outFifo { kMaxHop * 4 };
    std::vector<float> outFifoL, outFifoR;                 // kMaxHop*4 each
    int               outDebt = 0;                          // silence emitted while starved

    int               audioOrder = 13, audioHop = 2048;    // audio-thread view
    std::atomic<int>  latencyAtomic { 8192 + 2048 };
    bool              realtime = true;

    // Guards the engine + FIFO across worker / inline-drain / FFT-size reset.
    // Never contended in steady state (worker holds it per slot; the audio
    // thread takes it only on an explicit FFT-size change).
    juce::SpinLock    engineLock;

    // analyzer telemetry
    SpectrumTripleBuffer spectrum;
    std::atomic<bool>    telemetryOn { false };
    void publishTelemetry();
};
