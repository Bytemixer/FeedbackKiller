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
//  All buffers are pre-allocated in prepare() for the MAX FFT size (32768);
//  changing FFT size at runtime only recomputes derived values + clears state,
//  never allocates. Nothing here allocates, locks, or blocks in process().
// ============================================================================

class FeedbackKillerDSP
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

    FeedbackKillerDSP() = default;

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setParams (const Params& p) { pending = p; }

    // Latency in samples the host must compensate (== current FFT size).
    int  getLatencySamples() const { return fftSize; }

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
    static constexpr double kOlaGain = 1.5;
    static constexpr float  kEps     = 1.0e-9f;

    void configureFFT (int order);                 // recompute derived values + clear
    void updateDerived (const Params& p);          // band bins, coefs, msc alpha (per block)
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

    // ---- runtime state ----
    double fs = 44100.0;
    int    fftSize = 8192, hopSize = 2048, nbins = 4096, fftOrder = 13;
    float  olaNorm = 1.0f;

    int    inPos = 0, samplesToFFT = 0;
    bool   firstFFTDone = false;

    // derived per block
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

    // analyzer telemetry
    SpectrumTripleBuffer spectrum;
    std::atomic<bool>    telemetryOn { false };
    void publishTelemetry();
};
