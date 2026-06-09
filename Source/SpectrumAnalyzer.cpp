#include "SpectrumAnalyzer.h"

namespace
{
    // FFT magnitude -> dBFS. The forward transform is un-normalised; a full-scale
    // sine through the Hann window lands at ~fftSize/4, so 4*mag/fftSize ~= 1.0.
    inline float magToDb (float mag, int fftSize)
    {
        const float ref = (fftSize > 0) ? (4.0f / (float) fftSize) : 1.0f;
        return 20.0f * std::log10 (ref * mag + 1.0e-9f);
    }

    const juce::Colour kBg         { 0xff121417 };
    const juce::Colour kGrid       { 0x18ffffff };
    const juce::Colour kText       { 0x80ffffff };
    const juce::Colour kSpectrum   { 0xff6fd0ff };
    const juce::Colour kFloor      { 0xffe8c84a };
    const juce::Colour kReduction  { 0xffff7a3c };
    const juce::Colour kBandFill   { 0x1e7fb4ff };   // active band wash
    const juce::Colour kBandFillHi { 0x387fb4ff };   // hovered band
    const juce::Colour kBandEdge   { 0x807fb4ff };   // band edge handle
    const juce::Colour kBandEdgeHi { 0xffa9d4ff };   // hovered/dragged edge
}

const SpectrumAnalyzer::BandRef SpectrumAnalyzer::kBands[3] = {
    { "band1Enable", "band1Min", "band1Max", "B1" },
    { "band2Enable", "band2Min", "band2Max", "B2" },
    { "band3Enable", "band3Min", "band3Max", "B3" },
};

SpectrumAnalyzer::SpectrumAnalyzer (FeedbackKillerProcessor& p) : proc (p)
{
    if (proc.getSampleRate() > 0.0) sampleRate = proc.getSampleRate();  // axis correct before first frame
    proc.setTelemetryEnabled (true);
    startTimerHz (30);
}

SpectrumAnalyzer::~SpectrumAnalyzer()
{
    stopTimer();
    proc.setTelemetryEnabled (false);
}

float SpectrumAnalyzer::freqToX (float hz, juce::Rectangle<float> r) const
{
    const float maxHz = (float) (sampleRate * 0.5);
    hz = juce::jlimit (kMinHz, maxHz, hz);
    const float t = std::log (hz / kMinHz) / std::log (maxHz / kMinHz);
    return r.getX() + t * r.getWidth();
}

float SpectrumAnalyzer::dbToY (float db, juce::Rectangle<float> r) const
{
    const float t = (kTopDb - db) / (kTopDb - kBotDb);
    return r.getY() + juce::jlimit (0.0f, 1.0f, t) * r.getHeight();
}

juce::Rectangle<float> SpectrumAnalyzer::plotArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f);
}

float SpectrumAnalyzer::xToFreq (float x, juce::Rectangle<float> r) const
{
    const float maxHz = (float) (sampleRate * 0.5);
    const float t = juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth());
    return kMinHz * std::pow (maxHz / kMinHz, t);
}

bool SpectrumAnalyzer::paramOn (const char* id) const
{
    if (auto* v = proc.apvts.getRawParameterValue (id)) return v->load() > 0.5f;
    return false;
}

float SpectrumAnalyzer::paramHz (const char* id) const
{
    if (auto* v = proc.apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void SpectrumAnalyzer::setParamHz (const char* id, float hz)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (hz));   // APVTS = single source of truth
}

// Which band/edge (if any) is under x. Edges win over the body.
void SpectrumAnalyzer::hitTest (float x, juce::Rectangle<float> r, int& band, Grab& grab) const
{
    band = -1; grab = Grab::None;
    for (int i = 0; i < 3; ++i)
    {
        if (! paramOn (kBands[i].en)) continue;
        const float x0 = freqToX (paramHz (kBands[i].lo), r);
        const float x1 = freqToX (paramHz (kBands[i].hi), r);
        if (std::abs (x - x0) <= kEdgeTolPx) { band = i; grab = Grab::Min;  return; }
        if (std::abs (x - x1) <= kEdgeTolPx) { band = i; grab = Grab::Max;  return; }
    }
    for (int i = 0; i < 3; ++i)
    {
        if (! paramOn (kBands[i].en)) continue;
        const float x0 = freqToX (paramHz (kBands[i].lo), r);
        const float x1 = freqToX (paramHz (kBands[i].hi), r);
        if (x > x0 && x < x1) { band = i; grab = Grab::Body; return; }
    }
}

void SpectrumAnalyzer::timerCallback()
{
    // 1) ingest a new frame if one is available (only ~6/s at 32K) -> set targets
    if (const SpectrumFrame* f = proc.readSpectrum())
    {
        numBins = f->numBins; fftSize = f->fftSize; sampleRate = f->sampleRate; mode = f->mode;

        if ((int) tgtMagDb.size() != numBins)
        {
            tgtMagDb.assign   ((size_t) numBins, kBotDb);
            tgtFloorDb.assign ((size_t) numBins, kBotDb);
            tgtRedDb.assign   ((size_t) numBins, 0.0f);
            magDb.assign   ((size_t) numBins, kBotDb);
            floorDb.assign ((size_t) numBins, kBotDb);
            redDb.assign   ((size_t) numBins, 0.0f);
        }
        for (int k = 0; k < numBins; ++k)
        {
            tgtMagDb[(size_t) k]   = magToDb (f->mag[(size_t) k],      fftSize);
            tgtFloorDb[(size_t) k] = magToDb (f->floorMag[(size_t) k], fftSize);
            tgtRedDb[(size_t) k]   = -f->gainDb[(size_t) k];   // reduction depth, >= 0
        }
        haveFrame = true;
    }

    // 2) ease the displayed curves toward the targets EVERY tick (30 Hz) so the
    //    motion is smooth regardless of how rarely frames actually arrive.
    if (haveFrame && (int) magDb.size() == numBins)
    {
        for (int k = 0; k < numBins; ++k)
        {
            float& m = magDb[(size_t) k];
            m += (tgtMagDb[(size_t) k] - m) * (tgtMagDb[(size_t) k] > m ? 0.6f : 0.3f);
            floorDb[(size_t) k] += (tgtFloorDb[(size_t) k] - floorDb[(size_t) k]) * 0.4f;
            float& rd = redDb[(size_t) k];
            rd += (tgtRedDb[(size_t) k] - rd) * (tgtRedDb[(size_t) k] > rd ? 0.6f : 0.35f);
        }
    }
    repaint();
}

void SpectrumAnalyzer::drawGrid (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (kGrid);
    const float maxHz = (float) (sampleRate * 0.5);
    const float decades[] = { 20.0f, 30.0f, 50.0f, 100.0f, 200.0f, 300.0f, 500.0f,
                              1000.0f, 2000.0f, 3000.0f, 5000.0f, 10000.0f, 20000.0f };
    for (float hz : decades)
    {
        if (hz < kMinHz || hz > maxHz) continue;
        const float x = freqToX (hz, r);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
    }
    g.setColour (kText);
    g.setFont (11.0f);
    const std::pair<float, const char*> labels[] = {
        { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } };
    for (auto& l : labels)
    {
        if (l.first > maxHz) continue;
        const float x = freqToX (l.first, r);
        g.drawText (l.second, (int) x - 18, (int) r.getBottom() - 14, 36, 12,
                    juce::Justification::centred);
    }

    g.setColour (kGrid);
    for (float db = 0.0f; db >= kBotDb; db -= 24.0f)
    {
        const float y = dbToY (db, r);
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (kText);
        g.drawText (juce::String ((int) db), (int) r.getX() + 2, (int) y - 12, 34, 12,
                    juce::Justification::left);
        g.setColour (kGrid);
    }
}

void SpectrumAnalyzer::drawBands (juce::Graphics& g, juce::Rectangle<float> r)
{
    const int active = (dragBand >= 0) ? dragBand : hoverBand;
    const Grab activeGrab = (dragBand >= 0) ? dragGrab : hoverGrab;

    for (int i = 0; i < 3; ++i)
    {
        if (! paramOn (kBands[i].en)) continue;
        const float lo = paramHz (kBands[i].lo);
        const float hi = paramHz (kBands[i].hi);
        if (hi <= lo) continue;

        const float x0 = freqToX (lo, r);
        const float x1 = freqToX (hi, r);
        const bool  hot = (i == active);

        g.setColour (hot ? kBandFillHi : kBandFill);
        g.fillRect (juce::Rectangle<float> (x0, r.getY(), x1 - x0, r.getHeight()));

        // edge handles
        const bool minHot = hot && (activeGrab == Grab::Min);
        const bool maxHot = hot && (activeGrab == Grab::Max);
        g.setColour (minHot ? kBandEdgeHi : kBandEdge);
        g.fillRect (juce::Rectangle<float> (x0 - 1.0f, r.getY(), 2.0f, r.getHeight()));
        g.setColour (maxHot ? kBandEdgeHi : kBandEdge);
        g.fillRect (juce::Rectangle<float> (x1 - 1.0f, r.getY(), 2.0f, r.getHeight()));

        g.setColour (kText);
        g.setFont (10.0f);
        g.drawText (kBands[i].name, (int) x0 + 4, (int) r.getY() + 2, 28, 12,
                    juce::Justification::left);

        // live frequency readout on the grabbed edge
        if (dragBand == i && dragGrab != Grab::None)
        {
            auto label = [&] (float hz, float x, juce::Justification j)
            {
                g.setColour (kBandEdgeHi);
                g.setFont (11.0f);
                g.drawText (juce::String (juce::roundToInt (hz)) + " Hz",
                            (int) x - 54, (int) r.getY() + 14, 50, 14, j);
            };
            if (dragGrab == Grab::Min || dragGrab == Grab::Body) label (lo, x0 + 56, juce::Justification::left);
            if (dragGrab == Grab::Max || dragGrab == Grab::Body) label (hi, x1,       juce::Justification::right);
        }
    }
}

void SpectrumAnalyzer::rebuildColumnMap (int width)
{
    if (width == cacheW && fftSize == cacheFft && sampleRate == cacheSR
        && (int) binCol.size() == numBins)
        return;

    cacheW = width; cacheFft = fftSize; cacheSR = sampleRate;
    binCol.assign ((size_t) juce::jmax (0, numBins), -1);
    if (numBins < 2 || width < 2) return;

    const float maxHz  = (float) (sampleRate * 0.5);
    const float invLog = 1.0f / std::log (maxHz / kMinHz);
    for (int k = 1; k < numBins; ++k)
    {
        const float hz = (float) (k * sampleRate / fftSize);
        int c;
        if      (hz <= kMinHz) c = 0;
        else if (hz >= maxHz)  c = width - 1;
        else                   c = (int) std::floor (std::log (hz / kMinHz) * invLog * (float) width);
        binCol[(size_t) k] = juce::jlimit (0, width - 1, c);
    }
}

void SpectrumAnalyzer::buildColumns (const std::vector<float>& perBin,
                                     juce::Rectangle<float> r, std::vector<float>& out) const
{
    const int W = juce::jmax (1, (int) r.getWidth());
    constexpr float kEmpty = -1.0e9f;
    out.assign ((size_t) W, kEmpty);

    // peak of every bin that lands in each pixel column (cached map -> no log here)
    const int n = juce::jmin (numBins, (int) binCol.size());
    for (int k = 1; k < n; ++k)
    {
        const int cx = binCol[(size_t) k];
        if (cx < 0 || cx >= W) continue;
        if (perBin[(size_t) k] > out[(size_t) cx]) out[(size_t) cx] = perBin[(size_t) k];
    }

    // find the first/last filled column
    int first = -1, last = -1;
    for (int x = 0; x < W; ++x) if (out[(size_t) x] > kEmpty) { if (first < 0) first = x; last = x; }
    if (first < 0) { std::fill (out.begin(), out.end(), 0.0f); return; }

    for (int x = 0; x < first; ++x)     out[(size_t) x] = out[(size_t) first];   // leading hold
    for (int x = last + 1; x < W; ++x)  out[(size_t) x] = out[(size_t) last];    // trailing hold

    // interpolate interior gaps (sparse low-frequency columns)
    int prev = first;
    for (int x = first + 1; x <= last; ++x)
    {
        if (out[(size_t) x] <= kEmpty) continue;
        if (x > prev + 1)
        {
            const float a = out[(size_t) prev], b = out[(size_t) x];
            for (int xi = prev + 1; xi < x; ++xi)
                out[(size_t) xi] = a + (b - a) * (float) (xi - prev) / (float) (x - prev);
        }
        prev = x;
    }
}

void SpectrumAnalyzer::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (! haveFrame || numBins < 2) return;

    std::vector<float> colIn, colRed;
    buildColumns (magDb, r, colIn);     // input magnitude (dB) per column
    buildColumns (redDb, r, colRed);    // reduction depth (dB, >= 0) per column

    const size_t W = colIn.size();
    if (W < 2) return;

    auto px    = [&] (size_t x) { return r.getX() + (float) x; };
    auto inY   = [&] (size_t x) { return dbToY (colIn[x], r); };
    auto procY = [&] (size_t x) { return dbToY (colIn[x] - colRed[x], r); };  // carved

    // carved (processed) outline
    juce::Path carved;
    carved.startNewSubPath (px (0), procY (0));
    for (size_t x = 1; x < W; ++x) carved.lineTo (px (x), procY (x));

    // 1) translucent body under the carved spectrum
    juce::Path body = carved;
    body.lineTo (r.getRight(), r.getBottom());
    body.lineTo (r.getX(),     r.getBottom());
    body.closeSubPath();
    g.setColour (kSpectrum.withAlpha (0.14f));
    g.fillPath (body);

    // 2) removed energy: the region between the original (top) and carved (bottom)
    juce::Path delta;
    delta.startNewSubPath (px (0), inY (0));
    for (size_t x = 1; x < W; ++x) delta.lineTo (px (x), inY (x));
    for (size_t x = W; x-- > 0; )   delta.lineTo (px (x), procY (x));
    delta.closeSubPath();
    g.setColour (kReduction.withAlpha (0.32f));
    g.fillPath (delta);

    // 3) ghost of the original signal (where it would sit without cuts)
    juce::Path ghost;
    ghost.startNewSubPath (px (0), inY (0));
    for (size_t x = 1; x < W; ++x) ghost.lineTo (px (x), inY (x));
    g.setColour (kSpectrum.withAlpha (0.28f));
    g.strokePath (ghost, juce::PathStrokeType (1.0f));

    // 4) carved spectrum on top
    g.setColour (kSpectrum);
    g.strokePath (carved, juce::PathStrokeType (1.4f));
}

void SpectrumAnalyzer::drawFloor (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (! haveFrame || numBins < 2) return;
    std::vector<float> col;
    buildColumns (floorDb, r, col);

    juce::Path line;
    for (size_t x = 0; x < col.size(); ++x)
    {
        const float px = r.getX() + (float) x;
        const float py = dbToY (col[x], r);
        if (x == 0) line.startNewSubPath (px, py);
        else        line.lineTo (px, py);
    }
    const float dashes[] = { 3.0f, 3.0f };
    juce::Path dashed;
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
    g.setColour (kFloor.withAlpha (0.55f));
    g.strokePath (dashed, juce::PathStrokeType (1.0f));
}

void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    auto r = plotArea();
    g.setColour (kBg);
    g.fillRect (getLocalBounds());

    rebuildColumnMap ((int) r.getWidth());   // cheap no-op unless geometry changed

    drawBands     (g, r);
    drawGrid      (g, r);
    drawFloor     (g, r);
    drawSpectrum  (g, r);

    // mode tag, top-right
    if (haveFrame)
    {
        const char* modeName[] = { "PROCESS", "BYPASS", "SOLO", "SPECTRAL REPLACE" };
        g.setColour (kText);
        g.setFont (11.0f);
        g.drawText (modeName[juce::jlimit (0, 3, mode)],
                    (int) r.getRight() - 160, (int) r.getY() + 2, 156, 14,
                    juce::Justification::right);
    }

    g.setColour (kGrid);
    g.drawRect (getLocalBounds(), 1);
}

// ============================================================================
//  Band interaction (edits flow through the APVTS -> sliders stay in sync)
// ============================================================================
void SpectrumAnalyzer::mouseMove (const juce::MouseEvent& e)
{
    int b; Grab gr;
    hitTest ((float) e.x, plotArea(), b, gr);
    if (b != hoverBand || gr != hoverGrab)
    {
        hoverBand = b; hoverGrab = gr;
        setMouseCursor (gr == Grab::Min || gr == Grab::Max ? juce::MouseCursor::LeftRightResizeCursor
                       : gr == Grab::Body                  ? juce::MouseCursor::DraggingHandCursor
                                                           : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SpectrumAnalyzer::mouseDown (const juce::MouseEvent& e)
{
    int b; Grab gr;
    hitTest ((float) e.x, plotArea(), b, gr);
    dragBand = b; dragGrab = gr;
    if (b >= 0)
    {
        dragStartLo = paramHz (kBands[b].lo);
        dragStartHi = paramHz (kBands[b].hi);
        dragStartHz = xToFreq ((float) e.x, plotArea());
        repaint();
    }
}

void SpectrumAnalyzer::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0 || dragGrab == Grab::None) return;
    auto r = plotArea();
    const float hz = xToFreq ((float) e.x, r);
    const auto& bnd = kBands[dragBand];
    constexpr float kGapHz = 5.0f;

    if (dragGrab == Grab::Min)
    {
        setParamHz (bnd.lo, juce::jlimit (100.0f, paramHz (bnd.hi) - kGapHz, hz));
    }
    else if (dragGrab == Grab::Max)
    {
        setParamHz (bnd.hi, juce::jlimit (paramHz (bnd.lo) + kGapHz, 24000.0f, hz));
    }
    else // Body: slide both edges, preserving width on the log axis
    {
        const float ratio = hz / juce::jmax (1.0f, dragStartHz);
        float lo = dragStartLo * ratio;
        float hi = dragStartHi * ratio;
        if (lo < 100.0f)   { hi *= 100.0f / lo;   lo = 100.0f; }
        if (hi > 24000.0f) { lo *= 24000.0f / hi; hi = 24000.0f; }
        setParamHz (bnd.lo, lo);
        setParamHz (bnd.hi, hi);
    }
    repaint();
}

void SpectrumAnalyzer::mouseUp (const juce::MouseEvent&)
{
    dragBand = -1; dragGrab = Grab::None;
    repaint();
}

void SpectrumAnalyzer::mouseExit (const juce::MouseEvent&)
{
    if (dragBand < 0) { hoverBand = -1; hoverGrab = Grab::None; repaint(); }
}
