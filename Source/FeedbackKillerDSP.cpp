#include "FeedbackKillerDSP.h"
#include <algorithm>

using std::min;
using std::max;

// ============================================================================
//  Lifecycle
// ============================================================================
void FeedbackKillerDSP::prepare (double sampleRate, int /*maxBlockSize*/, int numChannels)
{
    fs = sampleRate;
    (void) numChannels;

    // Pre-build an FFT for every supported order (no allocation in process()).
    for (int o = 12; o <= kMaxOrder; ++o)
        ffts[o - 12] = std::make_unique<juce::dsp::FFT> (o);

    // Allocate all working storage for the maximum FFT size, once.
    inBufL.assign (kMaxFFT, 0.0f);   inBufR.assign (kMaxFFT, 0.0f);
    outBufL.assign (kMaxFFT, 0.0f);  outBufR.assign (kMaxFFT, 0.0f);
    winBuf.assign (kMaxFFT, 0.0f);
    fftL.assign (2 * kMaxFFT, 0.0f); fftR.assign (2 * kMaxFFT, 0.0f);
    sxyRe.assign (kMaxBins, 0.0f);   sxyIm.assign (kMaxBins, 0.0f);
    sxx.assign (kMaxBins, 0.0f);     syy.assign (kMaxBins, 0.0f);
    magSmooth.assign (kMaxBins, 0.0f);
    vizMag.assign (kMaxBins, kEps);  vizMsc.assign (kMaxBins, 0.0f);
    notchTarget.assign (kMaxBins, 1.0f);
    notchCurr.assign (kMaxBins, 1.0f);
    effTarget.assign (kMaxBins, 1.0f);
    peakAttenDb.assign (kMaxBins, 0.0f);
    binHold.assign (kMaxBins, 0);
    leftAnchor.assign (kMaxBins, 0);
    rightAnchor.assign (kMaxBins, 0);
    scratchL.assign (2 * kMaxBins, 0.0f);
    scratchR.assign (2 * kMaxBins, 0.0f);

    vizMagR.assign (kMaxBins, kEps);
    magSmoothR.assign (kMaxBins, 0.0f);
    notchTargetR.assign (kMaxBins, 1.0f);
    notchCurrR.assign (kMaxBins, 1.0f);
    effTargetR.assign (kMaxBins, 1.0f);
    peakAttenDbR.assign (kMaxBins, 0.0f);
    binHoldR.assign (kMaxBins, 0);
    leftAnchorR.assign (kMaxBins, 0);
    rightAnchorR.assign (kMaxBins, 0);

    // Channel views: chA = shared/left detection state, chB = right (unlinked).
    // The vectors never reallocate after prepare(), so these pointers are stable.
    chA = { vizMag.data(),  magSmooth.data(),  notchTarget.data(),  notchCurr.data(),
            effTarget.data(),  peakAttenDb.data(),  binHold.data(),
            leftAnchor.data(),  rightAnchor.data() };
    chB = { vizMagR.data(), magSmoothR.data(), notchTargetR.data(), notchCurrR.data(),
            effTargetR.data(), peakAttenDbR.data(), binHoldR.data(),
            leftAnchorR.data(), rightAnchorR.data() };

    prevPhase.assign (kMaxBins, 0.0f);
    phaseCohC.assign (kMaxBins, 0.0f);
    phaseCohS.assign (kMaxBins, 0.0f);
    phaseCoh.assign  (kMaxBins, 0.0f);

    spectrum.prepare (kMaxBins);     // size all telemetry slots once (no alloc in process)

    configureFFT (pending.fftOrder);
}

void FeedbackKillerDSP::reset()
{
    configureFFT (fftOrder);
}

void FeedbackKillerDSP::configureFFT (int order)
{
    fftOrder = juce::jlimit (12, kMaxOrder, order);
    fftSize  = 1 << fftOrder;
    hopSize  = fftSize / 4;
    nbins    = fftSize / 2;
    // JSFX uses an un-normalised inverse FFT, so its ola_norm carries a 1/fftSize.
    // juce::dsp::FFT already normalises the inverse by 1/N, so we only need the
    // window-sum (Hann^2 @ 75% overlap = ola_gain) compensation here.
    olaNorm  = (float) (1.0 / kOlaGain);

    // Symmetric Hann, matching the JSFX exactly.
    for (int i = 0; i < fftSize; ++i)
        winBuf[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                     * (float) i / (float) (fftSize - 1));

    std::fill (inBufL.begin(),  inBufL.end(),  0.0f);
    std::fill (inBufR.begin(),  inBufR.end(),  0.0f);
    std::fill (outBufL.begin(), outBufL.end(), 0.0f);
    std::fill (outBufR.begin(), outBufR.end(), 0.0f);
    std::fill (sxyRe.begin(), sxyRe.end(), 0.0f);
    std::fill (sxyIm.begin(), sxyIm.end(), 0.0f);
    std::fill (sxx.begin(),   sxx.end(),   0.0f);
    std::fill (syy.begin(),   syy.end(),   0.0f);
    std::fill (peakAttenDb.begin(), peakAttenDb.end(), 0.0f);
    std::fill (binHold.begin(), binHold.end(), 0);
    std::fill (notchTarget.begin(), notchTarget.end(), 1.0f);
    std::fill (notchCurr.begin(),   notchCurr.end(),   1.0f);
    std::fill (effTarget.begin(),   effTarget.end(),   1.0f);
    std::fill (vizMag.begin(), vizMag.end(), kEps);
    std::fill (peakAttenDbR.begin(), peakAttenDbR.end(), 0.0f);
    std::fill (binHoldR.begin(), binHoldR.end(), 0);
    std::fill (notchTargetR.begin(), notchTargetR.end(), 1.0f);
    std::fill (notchCurrR.begin(),   notchCurrR.end(),   1.0f);
    std::fill (effTargetR.begin(),   effTargetR.end(),   1.0f);
    std::fill (vizMagR.begin(), vizMagR.end(), kEps);
    std::fill (prevPhase.begin(), prevPhase.end(), 0.0f);
    std::fill (phaseCohC.begin(), phaseCohC.end(), 0.0f);
    std::fill (phaseCohS.begin(), phaseCohS.end(), 0.0f);
    std::fill (phaseCoh.begin(),  phaseCoh.end(),  0.0f);

    inPos = 0; samplesToFFT = 0; firstFFTDone = false;
}

void FeedbackKillerDSP::updateDerived (const Params& p)
{
    mscThresh   = p.mscThreshold;
    floorMargin = p.floorMarginDb;
    maxAttenDb  = p.maxAttenDb;
    overcut     = max (1.0f, p.overcut);
    minCutDb    = max (0.0f, p.minCutDb);
    replaceAnchorClean = juce::jlimit (0.5f, 0.99f, p.replaceAnchorClean);
    mode        = p.mode;
    chanMode    = p.channelMode;
    tonalGate   = juce::jlimit (0.0f, 1.0f, p.tonalGate);
    notchWidthBins = (int) juce::jlimit (0.0f, 15.0f, p.notchWidthBins);
    notchTaperBins = (int) juce::jlimit (0.0f, 20.0f, p.notchTaperBins);

    b1en = p.band1En; b2en = p.band2En; b3en = p.band3En;
    auto toBinLo = [&] (float hz) { return max (1, (int) std::floor (hz * fftSize / fs)); };
    auto toBinHi = [&] (float hz) { return min (nbins - 1, (int) std::ceil (hz * fftSize / fs)); };
    b1min = toBinLo (p.band1Min); b1max = toBinHi (p.band1Max);
    b2min = toBinLo (p.band2Min); b2max = toBinHi (p.band2Max);
    b3min = toBinLo (p.band3Min); b3max = toBinHi (p.band3Max);

    floorWinRadius = max (8, (int) std::floor (floorWinHz * fftSize / fs));
    floorStride = (fftSize >= 32768) ? 4 : 1;
    winStride   = (fftSize >= 32768) ? 4 : 1;

    const float fps = (float) (fs / hopSize);
    attackCoef  = std::exp (-1.0f / max (0.01f, p.attackMs  / 1000.0f * fps));
    releaseCoef = std::exp (-1.0f / max (0.01f, p.releaseMs / 1000.0f * fps));
    mscAlpha    = std::exp (-1.0f / max (1.0f,  p.mscIntegrationMs / 1000.0f * fps));
    // Phase coherence needs many hops to be reliable (small-sample R is biased
    // high), so it gets its own ~600 ms constant rather than reusing mscAlpha.
    phaseAlpha  = std::exp (-1.0f / max (1.0f,  0.600f * fps));
    holdFramesTarget = max (1, (int) std::ceil (p.holdMs / 1000.0f * fps));
}

// ============================================================================
//  Band mask + robust floor (faithful)
// ============================================================================
bool FeedbackKillerDSP::binInActiveBand (int k) const
{
    return (b1en && k >= b1min && k <= b1max)
        || (b2en && k >= b2min && k <= b2max)
        || (b3en && k >= b3min && k <= b3max);
}

float FeedbackKillerDSP::robustFloorBin (const float* mag, int k, int r, int st) const
{
    const int lo = max (1, k - r);
    const int hi = min (nbins - 1, k + r);
    const int np = (hi - lo) / st + 1;

    double sum = 0.0; int n = 0; int j = lo;
    for (int c = 0; c < np; ++c) { sum += mag[j]; ++n; j += st; }
    float m1 = (n > 0) ? (float) (sum / n) : mag[k];

    sum = 0.0; int cnt = 0; j = lo;
    for (int c = 0; c < np; ++c) { float v = mag[j]; if (v < m1) { sum += v; ++cnt; } j += st; }
    float m2 = (cnt > 0) ? (float) (sum / cnt) : m1;

    sum = 0.0; cnt = 0; j = lo;
    for (int c = 0; c < np; ++c) { float v = mag[j]; if (v < m2) { sum += v; ++cnt; } j += st; }
    if (cnt > 0) m2 = (float) (sum / cnt);

    return max (m2, kEps);
}

// ============================================================================
//  DSP methods (faithful translation of the JSFX functions)
// ============================================================================
void FeedbackKillerDSP::dspLoadAndFFT()
{
    for (int i = 0; i < fftSize; ++i)
    {
        int src = inPos + i; if (src >= fftSize) src -= fftSize;
        const float w = winBuf[(size_t) i];
        fftL[(size_t) i] = inBufL[(size_t) src] * w;   // real input, contiguous
        fftR[(size_t) i] = inBufR[(size_t) src] * w;
    }
    auto* fft = ffts[fftOrder - 12].get();
    fft->performRealOnlyForwardTransform (fftL.data(), false);
    fft->performRealOnlyForwardTransform (fftR.data(), false);
}

void FeedbackKillerDSP::dspAnalyzeSpectrum()
{
    lEnergy = 0.0; rEnergy = 0.0;
    double blockXYre = 0.0, blockXYim = 0.0;

    for (int k = 1; k < nbins; ++k)
    {
        const float lr = fftL[(size_t)(2*k)],   li = fftL[(size_t)(2*k+1)];
        const float rr = fftR[(size_t)(2*k)],   ri = fftR[(size_t)(2*k+1)];
        const float magL = std::sqrt (lr*lr + li*li);
        const float magR = std::sqrt (rr*rr + ri*ri);
        if (chanMode == 3)   // Unlinked: each channel keeps its own magnitude
        {
            vizMag[(size_t) k]  = magL + kEps;
            vizMagR[(size_t) k] = magR + kEps;
        }
        else
        {
            const float magAvg = (chanMode == 2) ? max (magL, magR) : ((magL + magR) * 0.5f);
            vizMag[(size_t) k] = magAvg + kEps;
        }
        lEnergy += lr*lr + li*li;
        rEnergy += rr*rr + ri*ri;
        blockXYre += lr*rr + li*ri;
        blockXYim += li*rr - lr*ri;

        if (chanMode != 3)   // MSC is meaningless with single-channel detection
        {
            sxyRe[(size_t) k] = mscAlpha * sxyRe[(size_t) k] + (1 - mscAlpha) * (lr*rr + li*ri);
            sxyIm[(size_t) k] = mscAlpha * sxyIm[(size_t) k] + (1 - mscAlpha) * (li*rr - lr*ri);
            sxx[(size_t) k]   = mscAlpha * sxx[(size_t) k]   + (1 - mscAlpha) * (lr*lr + li*li);
            syy[(size_t) k]   = mscAlpha * syy[(size_t) k]   + (1 - mscAlpha) * (rr*rr + ri*ri);
        }

        // Phase-stability tonality (only when gated & in-band -> cheap). A steady
        // sinusoid advances phase by a constant amount each hop, so the EMA of the
        // unit phase-increment vector keeps |R| -> 1; phase-chaotic content -> 0.
        if (tonalGate > 0.0f && binInActiveBand (k))
        {
            const bool  useR = (magR > magL);
            const float ph = std::atan2 (useR ? ri : li, useR ? rr : lr);
            const float d  = ph - prevPhase[(size_t) k];
            prevPhase[(size_t) k] = ph;
            phaseCohC[(size_t) k] = phaseAlpha * phaseCohC[(size_t) k] + (1 - phaseAlpha) * std::cos (d);
            phaseCohS[(size_t) k] = phaseAlpha * phaseCohS[(size_t) k] + (1 - phaseAlpha) * std::sin (d);
            const float cc = phaseCohC[(size_t) k], ss = phaseCohS[(size_t) k];
            phaseCoh[(size_t) k] = std::sqrt (cc*cc + ss*ss);
        }
    }

    const bool isLeftOnly  = (lEnergy > 0 && rEnergy < lEnergy * 0.001);
    const bool isRightOnly = (rEnergy > 0 && lEnergy < rEnergy * 0.001);
    const bool isMono = isLeftOnly || isRightOnly;
    const double denom = lEnergy * rEnergy;
    const double blockCorr = (lEnergy > kEps && rEnergy > kEps)
                           ? ((blockXYre*blockXYre + blockXYim*blockXYim) / denom) : 0.0;
    const bool isDualMono = blockCorr > 0.999;
    autoMscBypass = (isMono || isDualMono) ? 1 : 0;
}

void FeedbackKillerDSP::dspCalculateFloor (ChanState& c)
{
    float lastFloor = kEps; int cnt = 0;
    for (int k = 1; k < nbins; ++k)
    {
        if (binInActiveBand (k))
        {
            if (cnt <= 0) { lastFloor = robustFloorBin (c.mag, k, floorWinRadius, winStride); cnt = floorStride; }
            --cnt;
            c.floorM[k] = lastFloor;
        }
        else { c.floorM[k] = c.mag[k]; cnt = 0; }
    }
}

void FeedbackKillerDSP::dspCalculateTargets (ChanState& c, bool writeViz)
{
    // Forced Mono and Unlinked Dual-Mono both run single-channel detection,
    // so cross-channel coherence cannot gate the cut.
    const int mscBypass = (chanMode == 2 || chanMode == 3) ? 1
                        : (chanMode == 0 ? autoMscBypass : 0);
    const float log10inv = 0.43429448190325176f;

    for (int k = 1; k < nbins; ++k)
    {
        const bool inRange = binInActiveBand (k);
        if (inRange)
        {
            float msc = 0.0f;
            if (!mscBypass || writeViz)
            {
                const float dn = sxx[(size_t) k] * syy[(size_t) k];
                msc = dn > 0 ? (sxyRe[(size_t) k]*sxyRe[(size_t) k]
                              + sxyIm[(size_t) k]*sxyIm[(size_t) k]) / dn : 0.0f;
            }
            if (writeViz) vizMsc[(size_t) k] = msc;

            const float floorVal = c.floorM[k];
            const float promDb = 20.0f * std::log (c.mag[k] / floorVal) * log10inv;

            const float mscExcess = msc - mscThresh;
            const float mscWeight = mscBypass ? 1.0f
                                  : (mscExcess > 0 ? min (1.0f, mscExcess / 0.15f) : 0.0f);

            // Phase-stability gate (enhancement): require coherence R near tonalGate
            // before notching. Off (tonalGate==0) -> weight 1.0 -> faithful.
            float tonalWeight = 1.0f;
            if (tonalGate > 0.0f)
            {
                const float R  = phaseCoh[(size_t) k];
                const float lo = tonalGate - 0.20f;
                const float hi = tonalGate + 0.05f;
                float t = juce::jlimit (0.0f, 1.0f, (R - lo) / max (1.0e-4f, hi - lo));
                tonalWeight = t * t * (3.0f - 2.0f * t);     // smoothstep
            }

            const float overFloor = promDb - floorMargin;
            const float confidence = (overFloor > 0) ? mscWeight * tonalWeight : 0.0f;

            const float promCut = max (0.0f, overFloor) * overcut * confidence;
            float desAtten = confidence > 0
                ? min (maxAttenDb, max (minCutDb * confidence * confidence, promCut)) : 0.0f;

            const bool doUpdate   = desAtten > c.peakAtten[k];
            const bool wasHolding = c.hold[k] > 0;
            if (doUpdate) { c.peakAtten[k] = desAtten; c.hold[k] = holdFramesTarget; }
            if (!doUpdate && wasHolding) { c.hold[k] -= 1; desAtten = c.peakAtten[k]; }
            if (!doUpdate && !wasHolding) { c.peakAtten[k] = 0.0f; }

            c.notchTarget[k] = desAtten > 0.01f
                ? std::exp (-desAtten / 20.0f / log10inv) : 1.0f;
        }
        else
        {
            if (writeViz) vizMsc[(size_t) k] = 0.0f;
            c.peakAtten[k] = 0.0f;
            c.hold[k] = 0;
            c.notchTarget[k] = 1.0f;
        }
    }
}

void FeedbackKillerDSP::dspApplyDilationAndSmoothing (ChanState& c)
{
    const float log10inv = 0.43429448190325176f;
    for (int k = 1; k < nbins; ++k) c.effTarget[k] = 1.0f;

    for (int k = 1; k < nbins; ++k)
    {
        if (c.notchTarget[k] < 0.99f)
        {
            const float tgt = c.notchTarget[k];
            const float attenLoc = -20.0f * std::log (max (tgt, kEps)) * log10inv;

            const int lo = max (1, k - notchWidthBins);
            const int hi = min (nbins - 1, k + notchWidthBins);
            for (int j = lo; j <= hi; ++j)
                if (binInActiveBand (j))
                    c.effTarget[j] = min (c.effTarget[j], tgt);

            if (notchTaperBins > 0)
            {
                for (int e = 1; e <= notchTaperBins; ++e)
                {
                    const float taperF = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi
                                                  * (float) e / (float) (notchTaperBins + 1)));
                    const float gainTap = std::exp (-(attenLoc * taperF) / 20.0f / log10inv);
                    int ti = k - notchWidthBins - e;
                    if (ti >= 1 && binInActiveBand (ti))
                        c.effTarget[ti] = min (c.effTarget[ti], gainTap);
                    ti = k + notchWidthBins + e;
                    if (ti <= nbins - 1 && binInActiveBand (ti))
                        c.effTarget[ti] = min (c.effTarget[ti], gainTap);
                }
            }
        }
    }

    for (int k = 1; k < nbins; ++k)
    {
        const float tgt = c.effTarget[k];
        const float cur = c.notchCurr[k];
        const float tgtDb = -20.0f * std::log (max (tgt, kEps)) * log10inv;
        const float curDb = -20.0f * std::log (max (cur, kEps)) * log10inv;
        const float coef  = (tgtDb > curDb) ? attackCoef : releaseCoef;
        const float newDb = tgtDb + (curDb - tgtDb) * coef;
        c.notchCurr[k] = std::exp (-newDb / 20.0f / log10inv);
    }
}

// ---- strategies (single-channel; called once per channel) ----
void FeedbackKillerDSP::strategyProcessCh (float* fft, int k, float g)
{
    fft[2*k] *= g; fft[2*k+1] *= g;
}

void FeedbackKillerDSP::strategySoloCh (float* fft, int k, float g)
{
    float sg = 1.0f - g; if (sg < 0.15f) sg = 0.0f;
    fft[2*k] *= sg; fft[2*k+1] *= sg;
}

void FeedbackKillerDSP::strategyReplaceCh (float* fft, const float* scratch,
                                           const ChanState& c, int k)
{
    const float replaceW = 1.0f - c.notchCurr[k];

    auto mag = [scratch] (int idx)
    { return std::sqrt (scratch[2*idx]*scratch[2*idx] + scratch[2*idx+1]*scratch[2*idx+1]); };

    if (replaceW > 0.05f)
    {
        if (binInActiveBand (k))
        {
            const int la = c.lAnchor[k];
            const int ra = c.rAnchor[k];
            bool handled = false;

            if (la >= 1 && ra >= 1 && ra > la)
            {
                const float t = (float)(k - la) / (float)(ra - la), omt = 1.0f - t;
                const float tgt = mag (la) * omt + mag (ra) * t;
                const float cur = mag (k) + kEps;
                const float g = (1 - replaceW) + min (1.0f, tgt / cur) * replaceW;
                fft[2*k] = scratch[2*k] * g; fft[2*k+1] = scratch[2*k+1] * g;
                handled = true;
            }
            if (!handled && (la >= 1 || ra >= 1))
            {
                const int ref = la >= 1 ? la : ra;
                const float tgt = mag (ref);
                const float cur = mag (k) + kEps;
                const float g = (1 - replaceW) + min (1.0f, tgt / cur) * replaceW;
                fft[2*k] = scratch[2*k] * g; fft[2*k+1] = scratch[2*k+1] * g;
                handled = true;
            }
            if (!handled) strategyProcessCh (fft, k, c.notchCurr[k]);
        }
        else
        {
            strategyProcessCh (fft, k, c.notchCurr[k]);
        }
    }
    else if (c.notchCurr[k] < 0.999f)
        strategyProcessCh (fft, k, c.notchCurr[k]);
}

void FeedbackKillerDSP::dspApplyStrategy()
{
    // Linked modes: both channels follow chA's notch (identical to the original
    // shared behavior). Unlinked: the right channel follows its own state.
    const bool unlinked = (chanMode == 3);
    ChanState& cR = unlinked ? chB : chA;

    if (mode == 3)
    {
        std::copy (fftL.begin(), fftL.begin() + 2 * nbins, scratchL.begin());
        std::copy (fftR.begin(), fftR.begin() + 2 * nbins, scratchR.begin());

        auto buildAnchors = [this] (ChanState& c)
        {
            int lastCln = -1;
            for (int k = 1; k < nbins; ++k)
            { if (c.notchCurr[k] > replaceAnchorClean) lastCln = k; c.lAnchor[k] = lastCln; }
            lastCln = -1;
            for (int k = nbins - 1; k >= 1; --k)
            { if (c.notchCurr[k] > replaceAnchorClean) lastCln = k; c.rAnchor[k] = lastCln; }
        };
        buildAnchors (chA);
        if (unlinked) buildAnchors (chB);
    }

    for (int k = 1; k < nbins; ++k)
    {
        if (mode == 0)
        {
            strategyProcessCh (fftL.data(), k, chA.notchCurr[k]);
            strategyProcessCh (fftR.data(), k, cR.notchCurr[k]);
        }
        else if (mode == 2)
        {
            strategySoloCh (fftL.data(), k, chA.notchCurr[k]);
            strategySoloCh (fftR.data(), k, cR.notchCurr[k]);
        }
        else if (mode == 3)
        {
            strategyReplaceCh (fftL.data(), scratchL.data(), chA, k);
            strategyReplaceCh (fftR.data(), scratchR.data(), cR, k);
        }

        const int mk = fftSize - k;
        fftL[(size_t)(2*mk)] = fftL[(size_t)(2*k)]; fftL[(size_t)(2*mk+1)] = -fftL[(size_t)(2*k+1)];
        fftR[(size_t)(2*mk)] = fftR[(size_t)(2*k)]; fftR[(size_t)(2*mk+1)] = -fftR[(size_t)(2*k+1)];
    }
}

void FeedbackKillerDSP::dspIfftAndOla()
{
    auto* fft = ffts[fftOrder - 12].get();
    fft->performRealOnlyInverseTransform (fftL.data());
    fft->performRealOnlyInverseTransform (fftR.data());

    for (int i = 0; i < fftSize; ++i)
    {
        int dst = inPos + i; if (dst >= fftSize) dst -= fftSize;
        const float w = winBuf[(size_t) i] * olaNorm;
        outBufL[(size_t) dst] += fftL[(size_t) i] * w;   // reals contiguous after inverse
        outBufR[(size_t) dst] += fftR[(size_t) i] * w;
    }
}

// Snapshot the per-hop spectral state for the analyzer. notchCurr is final
// after dilation/smoothing and is mode-independent (the strategy only *applies*
// it), so the displayed notch curve is identical across Process/Solo/Replace.
void FeedbackKillerDSP::publishTelemetry()
{
    const float log10inv = 0.43429448190325176f;
    SpectrumFrame& f = spectrum.writeSlot();

    f.numBins    = nbins;
    f.fftSize    = fftSize;
    f.sampleRate = fs;
    f.mode       = mode;
    f.b1en = b1en; f.b2en = b2en; f.b3en = b3en;
    f.b1min = b1min; f.b1max = b1max;
    f.b2min = b2min; f.b2max = b2max;
    f.b3min = b3min; f.b3max = b3max;

    const bool unlinked = (chanMode == 3);
    for (int k = 0; k < nbins; ++k)
    {
        // Unlinked: show the louder channel and the deeper cut so the analyzer
        // reflects everything the plugin is doing across both channels.
        const float m = unlinked ? max (vizMag[(size_t) k], vizMagR[(size_t) k])
                                 : vizMag[(size_t) k];
        const float fl = unlinked ? max (magSmooth[(size_t) k], magSmoothR[(size_t) k])
                                  : magSmooth[(size_t) k];
        const float g = unlinked ? min (notchCurr[(size_t) k], notchCurrR[(size_t) k])
                                 : notchCurr[(size_t) k];
        f.mag[(size_t) k]      = m;
        f.floorMag[(size_t) k] = fl;
        f.gainDb[(size_t) k]   = (g >= 0.999f) ? 0.0f
                               : 20.0f * std::log (max (g, kEps)) * log10inv;
        f.msc[(size_t) k]      = vizMsc[(size_t) k];
    }
    spectrum.publish();
}

void FeedbackKillerDSP::runHop()
{
    dspLoadAndFFT();
    dspAnalyzeSpectrum();

    dspCalculateFloor (chA);
    dspCalculateTargets (chA, /*writeViz*/ true);
    dspApplyDilationAndSmoothing (chA);

    if (chanMode == 3)   // Unlinked: run the full detection pipeline for R too
    {
        dspCalculateFloor (chB);
        dspCalculateTargets (chB, /*writeViz*/ false);
        dspApplyDilationAndSmoothing (chB);
    }

    dspApplyStrategy();   // mode 1 (Bypass) applies no cut but still mirrors -> passthrough
    dspIfftAndOla();
    firstFFTDone = true;

    if (telemetryOn.load (std::memory_order_relaxed))
        publishTelemetry();
}

// ============================================================================
//  Real-time block entry
// ============================================================================
void FeedbackKillerDSP::process (float* const* channels, int numChannels, int numSamples)
{
    const Params p = pending;
    if (p.fftOrder != fftOrder)
        configureFFT (p.fftOrder);
    updateDerived (p);

    float* chL = channels[0];
    float* chR = (numChannels > 1) ? channels[1] : channels[0];

    for (int n = 0; n < numSamples; ++n)
    {
        const float inL = chL[n];
        const float inR = (numChannels > 1) ? chR[n] : inL;

        inBufL[(size_t) inPos] = inL;
        inBufR[(size_t) inPos] = inR;

        const float oL = firstFFTDone ? outBufL[(size_t) inPos] : 0.0f;
        const float oR = firstFFTDone ? outBufR[(size_t) inPos] : 0.0f;
        outBufL[(size_t) inPos] = 0.0f;
        outBufR[(size_t) inPos] = 0.0f;

        chL[n] = oL;
        if (numChannels > 1) chR[n] = oR;

        if (++inPos >= fftSize) inPos = 0;

        if (++samplesToFFT >= hopSize)
        {
            samplesToFFT = 0;
            runHop();
        }
    }
}
