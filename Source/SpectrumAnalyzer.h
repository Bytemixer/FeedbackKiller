#pragma once

#include "PluginProcessor.h"

// ============================================================================
//  SpectrumAnalyzer
//  Real-time, mode-independent view of what the tool is doing to the spectrum:
//    - live input magnitude spectrum (dBFS, log-frequency)
//    - robust detection floor (so you can see resonances poke above it)
//    - shaded active band regions (where the tool is allowed to act)
//    - notches carved straight into the spectrum; removed energy shown as the
//      orange delta between the original (ghost) and the processed curve
//
//  Reads frames from the processor's lock-free telemetry on a timer; never
//  touches DSP internals. Enables telemetry while alive, disables on destroy.
//
//  The active band regions are interactive: drag an edge to set min/max, or drag
//  the body to slide the whole band. Edits go through the APVTS (the single
//  source of truth) so the sliders below stay in sync automatically.
// ============================================================================

class SpectrumAnalyzer : public juce::Component,
                         private juce::Timer
{
public:
    explicit SpectrumAnalyzer (FeedbackKillerProcessor&);
    ~SpectrumAnalyzer() override;

    void paint (juce::Graphics&) override;

    void mouseMove  (const juce::MouseEvent&) override;
    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    juce::Rectangle<float> plotArea() const;
    float freqToX (float hz, juce::Rectangle<float> r) const;
    float xToFreq (float x,  juce::Rectangle<float> r) const;
    float dbToY   (float db, juce::Rectangle<float> r) const;

    // ---- band interaction ----
    struct BandRef { const char* en; const char* lo; const char* hi; const char* name; };
    static const BandRef kBands[3];

    enum class Grab { None, Min, Max, Body };
    bool  paramOn    (const char* id) const;
    float paramHz    (const char* id) const;
    void  setParamHz (const char* id, float hz);
    void  hitTest    (float x, juce::Rectangle<float> r, int& band, Grab& grab) const;

    // Bin -> pixel-column map, cached so paint never calls log() per bin. Rebuilt
    // only when FFT size / sample rate / width change.
    void rebuildColumnMap (int width);

    // Resample a per-bin array to one value per horizontal pixel (peak within the
    // column; sparse low-frequency columns interpolated). Turns the linear-spaced
    // FFT into a clean, resolution-independent log display.
    void buildColumns (const std::vector<float>& perBin,
                       juce::Rectangle<float> r, std::vector<float>& out) const;

    void drawGrid       (juce::Graphics&, juce::Rectangle<float> r);
    void drawBands      (juce::Graphics&, juce::Rectangle<float> r);
    void drawSpectrum   (juce::Graphics&, juce::Rectangle<float> r);  // input ghost + carved + delta
    void drawFloor      (juce::Graphics&, juce::Rectangle<float> r);

    FeedbackKillerProcessor& proc;

    // display state — message thread only
    int    numBins = 0, fftSize = 0, mode = 0;
    double sampleRate = 44100.0;
    bool   haveFrame = false;

    // Latest frame targets, plus the eased values actually drawn. Easing runs on
    // the timer (30 Hz) so motion stays smooth even when frames arrive at ~6 Hz
    // (large FFT). magDb/floorDb/redDb are the displayed curves.
    std::vector<float> magDb, floorDb, redDb;        // displayed (eased), dB per bin
    std::vector<float> tgtMagDb, tgtFloorDb, tgtRedDb; // latest frame values

    // cached bin -> column map
    std::vector<int> binCol;
    int    cacheW = 0, cacheFft = 0;
    double cacheSR = 0.0;

    // interaction state
    int  dragBand = -1;
    Grab dragGrab = Grab::None;
    float dragStartLo = 0.0f, dragStartHi = 0.0f, dragStartHz = 0.0f;
    int  hoverBand = -1;
    Grab hoverGrab = Grab::None;

    // axis configuration
    static constexpr float kTopDb = 6.0f;
    static constexpr float kBotDb = -96.0f;
    static constexpr float kMinHz = 20.0f;
    static constexpr float kEdgeTolPx = 5.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzer)
};
