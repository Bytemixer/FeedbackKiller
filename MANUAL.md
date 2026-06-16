# Feedback Resonance Killer — User Manual

**Version 0.4.1 (VST3 / Standalone, Windows) — June 2026**

A plugin that surgically removes **sustained tonal feedback** and similar narrow
resonances from a recording, while preserving the music around them.

> **In one line:** it finds narrow frequency spikes that stick up above the local
> spectrum, confirms they're resonance (not music), and notches or rebuilds them —
> only inside the frequency zones you tell it to police — while a live analyzer
> shows you every cut as it happens.

- **Formats:** VST3 (stereo/mono) and Standalone, 64-bit Windows.
- **Best at:** stable, narrow, *sustained* tones — PA feedback rings, whistles,
  comb clusters, squealing resonances that ring for hundreds of ms to seconds.
- **Also useful as:** a smooth "dynamic EQ"-style spectral leveler at small FFT
  sizes with wide notches (see Recipes).
- **Not designed for:** broadband harshness or short clicks/coughs.

---

## 1. How it works (the short version)

Every short slice of audio is converted to a frequency spectrum (FFT). For each
frequency **inside an enabled band**, the plugin:

1. Estimates a **robust noise floor** — the level of the *non-peak* energy nearby.
   It deliberately ignores spikes, so a thick cluster of resonances can't hide
   itself by raising its own floor.
2. Measures **prominence** — how many dB the frequency sticks up above that floor.
3. Decides it's a resonance if the prominence clears the **Floor Margin** (and, on
   a true stereo signal, if the left/right channels are **coherent** enough —
   feedback fills a room the same way in both channels; music is more spread out).
4. **Attenuates or rebuilds** that frequency according to the selected **Mode**,
   holds the reduction briefly so it doesn't flicker, then converts back to audio.

Frequencies **outside** your enabled bands are never touched.

---

## 2. Installation

1. Copy `Feedback Resonance Killer.vst3` into your VST3 folder — typically
   `C:\Program Files\Common Files\VST3` (or wherever your DAW scans for VST3s).
2. Rescan plugins in your DAW and insert **Feedback Resonance Killer**.
3. The Standalone build runs without a DAW (useful for quick auditions).

The plugin reports its full latency to the host (PDC), so processed audio stays
perfectly in time with the rest of the session.

---

## 3. The analyzer display

The top of the window is a live, log-frequency spectrum view of exactly what the
plugin is doing:

| Element | Meaning |
|---|---|
| **Solid cyan curve** | The **processed** spectrum — what's leaving the plugin. Notches appear as bites carved directly into it. |
| **Faint cyan ghost** | The original input spectrum — where the signal *would* be without processing. |
| **Orange fill** | The **removed energy**: the gap between ghost and processed. Its shape *is* the live notch shape (core width + tapered edges). |
| **Dashed yellow line** | The robust detection floor. Watch resonances poke above it right before they're cut. |
| **Blue shaded zones (B1/B2/B3)** | Your active bands — the only places the plugin may act. |
| **Top-right tag** | The current Mode. |

**The bands are interactive:**

- **Drag a band edge** (the bright vertical handle) to set its Min/Max frequency —
  a live Hz readout follows the drag. The sliders below stay in sync.
- **Drag a band's body** to slide the whole band, keeping its width.
- Every numeric field next to a slider is **editable** — click it and type an
  exact value.

The footer shows the plugin name/version; **About** displays license info and a
link to the source code.

---

## 4. Quick start

**Goal: kill a ringing/whistling resonance in a section.**

1. Insert the plugin on the track (or on a sliced item covering the problem).
2. Set **Channel Mode**: **Forced Mono** for a mono stem, otherwise **Auto-Detect**.
3. Set **Mode** to **Solo (Notched Energy)** temporarily — you now *hear only what
   is being removed.* Drag the band edges on the analyzer until the resonance is
   isolated in Solo with as little music as possible.
4. Switch **Mode** to **Spectral Replace** (most natural) or **Process** (deepest cut).
5. If the resonance still leaks, lower **Floor Margin** a little; if it's eating
   music, raise it.
6. Save a preset in your DAW for reuse.

---

## 5. The three core ideas

### A. Bands = *where* to hunt
Up to **three frequency zones**. Detection and cutting happen only **inside enabled
bands**; the gaps are left completely alone. Bands are set in **Hz**, so they stay
correct at any FFT size or sample rate. Overlap band edges slightly if you want two
bands to behave as one continuous zone.

### B. The robust floor = *what counts as a resonance*
A **peak-rejecting** estimate of the local background level. Because it ignores
spikes, it gives an honest "what should be here" reference even inside a dense comb
of resonances. A frequency is flagged when it rises **Floor Margin** dB above this
floor. You never set the floor directly — only how far above it a spike must be.

### C. Modes = *what to do once found*
- **Spectral Replace** — rebuilds the flagged frequency from clean neighbors
  (most natural; refills the hole).
- **Process** — turns the flagged frequency down (deepest removal).

---

## 6. Parameter reference

### Detection

| Parameter | Range (default) | What it does |
|---|---|---|
| **MSC Threshold** | 0–1 (0.7) | *Stereo only.* How coherent L/R must be at a frequency before it's treated as feedback. Higher = stricter. Ignored in Forced Mono and Unlinked modes. |
| **Floor Margin** | 0–40 dB (16) | The main selectivity knob. How far above the local floor a spike must rise to be flagged. **Lower = more sensitive; higher = safer.** On mono-style detection this is your main discriminator — keep it fairly high (18–26). |
| **MSC Integration** | 10–500 ms (150) | *Stereo only.* How long coherence is averaged. Longer = steadier, slower to react. |

### Bands (targeting)

| Parameter | Range (default) | What it does |
|---|---|---|
| **Band 1 Enable / Min / Max** | On; 100–24000 Hz (3500 / 6200) | Band 1's zone. |
| **Band 2 Enable / Min / Max** | On; 100–24000 Hz (8500 / 12500) | Band 2's zone. |
| **Band 3 Enable / Min / Max** | **Off**; 100–24000 Hz (12500 / 16000) | Band 3. Default off — enable only after *confirming* a problem above ~12 kHz in Solo, since real "air"/cymbal detail can be mistaken for a tone. |

### Reduction shape

| Parameter | Range (default) | What it does |
|---|---|---|
| **Mode** | Process / Bypass / Solo / Spectral Replace (Process) | See the Mode table. |
| **Max Attenuation** | 0–120 dB (60) | A **ceiling, not a depth.** The actual cut depth comes from prominence × Overcut (and Min Cut); this only caps it. Raising it does nothing unless the computed cut is hitting the cap. |
| **Notch Width** | 0–15 bins (1) | Bins on each side of a detected spike that get the full cut. **Keep small (0–1) for narrow tones.** |
| **Notch Edge Taper** | 0–20 bins (4) | Extra bins beyond the core with a *fading* cut (a soft skirt). Large tapers on busy material cause a "phaser/swirl" — if you hear swirling, lower this first. |
| **Overcut** | 1–6 (1.6) | Multiplies cut depth proportionally to how far over the margin a spike is. **This is the main "how hard" control.** At 1.0 a spike is pulled back roughly to the floor+margin line; higher values cut *below* it. |
| **Min Cut** | 0–40 dB (4) | A guaranteed minimum cut for any confidently-flagged frequency. Raise to bury stubborn tones. |

### Dynamics

| Parameter | Range (default) | What it does |
|---|---|---|
| **Attack** | 1–500 ms (10) | How fast a cut engages on detection. |
| **Release** | 10–2000 ms (300) | How fast a cut lets go afterward. |
| **Hold** | 50–3000 ms (200) | Keeps the cut engaged after detection drops so a steady tone doesn't chatter. |

### Spectral Replace

| Parameter | Range (default) | What it does |
|---|---|---|
| **Replace Anchor Clean** | 0.50–0.99 (0.85) | How "clean" a neighboring bin must be to serve as a rebuild reference. Higher = only trust untouched bins. Try 0.85–0.93. |

### Enhancements

| Parameter | Range (default) | What it does |
|---|---|---|
| **Tonal Gate (Phase)** | 0–1 (**0 = off**) | Requires a bin's **phase trajectory** to be stable (tonal) before it may be cut: steady rings pass the gate, phase-chaotic content (noise, some percussion) is spared. Works best at smaller FFT sizes (more analysis frames). 0 disables it entirely — the faithful default. Note: struck-metal partials (e.g. tambourine) are themselves tonal and will still pass the gate. |

### Engine

| Parameter | Options (default) | What it does |
|---|---|---|
| **Channel Mode** | Auto-Detect / Forced Stereo / Forced Mono / **Unlinked Dual-Mono** (Auto) | How detection treats the two channels. See section 8. |
| **FFT Size** | 4096 / 8192 / 16384 / 32768 (8192) | Time-vs-frequency resolution trade. See section 9. |

---

## 7. The four Modes

| Mode | What it does | Use it for |
|---|---|---|
| **Process** | Turns flagged frequencies **down** by the computed amount. | Deepest, most complete kill. |
| **Bypass** | Passes audio through the FFT engine **unprocessed** (same latency). | A/B at matched latency. Not a true dry bypass — use the host's bypass for that. |
| **Solo (Notched Energy)** | Plays **only the energy being removed**. | Setup/diagnosis: tune bands and margin until you hear *just* the resonance. |
| **Spectral Replace** | Rebuilds flagged frequencies by **interpolating from clean neighbors** (keeps phase). | Most natural removal; usually the best default. |

---

## 8. Channel Mode

- **Auto-Detect (default):** detects effectively-mono signals (one side silent, or
  both identical) and automatically bypasses the coherence check for them. Safe
  for most uses.
- **Forced Stereo:** always apply the L/R coherence check. For genuine stereo
  material where coherence helps reject music.
- **Forced Mono (Bus/Panned):** always skip coherence; detection uses the louder
  channel and the **same cut is applied to both channels**. For mono stems and
  hard-panned tracks. Keep Floor Margin high (18–26).
- **Unlinked Dual-Mono:** each channel gets its **own independent detection and
  cuts** — a resonance living only in the left channel is cut only on the left.
  Made for buses fed by hard-panned mono stems (e.g. FOH multitrack recordings).
  Coherence is bypassed. Trade-off: unequal L/R cuts can momentarily shift the
  stereo image at the notched frequencies.

> In Forced Mono and Unlinked modes, **MSC Threshold and MSC Integration do nothing.**

---

## 9. FFT Size, latency and performance

Bigger FFT = finer **frequency** resolution but blurrier **time** and more latency.

| Size | Freq. resolution @ 48 kHz | Reported latency @ 48 kHz | Best for |
|---|---|---|---|
| **4096** | ~11.7 Hz/bin | 6,144 smp (~128 ms) | Transient-heavy material; "dynamic EQ" duty. |
| **8192** | ~5.9 Hz/bin | 12,288 smp (~256 ms) | Balanced general default. |
| **16384** | ~2.9 Hz/bin | 24,576 smp (~512 ms) | Closely-spaced tones; cleaner kill, less swirl. |
| **32768** | ~1.5 Hz/bin | 49,152 smp (~1.02 s) | Maximum surgical precision for dense clusters. |

Latency is always **1.5× the FFT size** (the analysis window plus the background
engine's processing budget) and is fully PDC-compensated by the host.

**Performance.** The spectral work runs on a **background worker thread**, not the
audio thread — so the plugin stays glitch-free even at 32768 on a master chain at
pro buffer sizes (128 samples or below). Multiple instances each get their own
worker and spread across CPU cores. Offline renders are deterministic and
identical to real-time playback.

**Guidance**
- For sustained narrow tones, **bigger is usually better**: tones stand out more,
  notches are tighter in Hz, detection is steadier. 16384/32768 give the cleanest kill.
- For live monitoring, prefer 4096/8192 (latency).
- **Change FFT Size while stopped** — switching rebuilds the engine (brief
  re-prime, and the host re-syncs to the new latency).
- **Notch Width / Taper are measured in bins**, so their Hz footprint changes with
  FFT size — re-check them after a size change (Width 1 = ±5.9 Hz at 8192 but
  ±1.5 Hz at 32768).

---

## 10. Workflow recipes

### A. Single ring in a sliced region (the common case)
```
Channel Mode:  Forced Mono (mono stem) or Auto-Detect
Mode:          Spectral Replace
Band 1:        On — bracket the ring (use Solo + drag the band on the analyzer)
Bands 2/3:     Off
Floor Margin:  20   (raise if it grabs music, lower if it leaks)
Notch Width:   1    Taper: 2
FFT Size:      16384
```

### B. A dense cluster of feedback tones
```
Mode:          Spectral Replace (or Process for the deepest kill)
Bands:         bracket each problem region; leave clean music in the gaps
Floor Margin:  16–24
Overcut:       2.5–4      Min Cut: 10–30 (these set the aggression —
                          Max Attenuation only caps it)
Hold:          300–650 ms
FFT Size:      32768
```

### C. Bus smoother / dynamic-EQ duty
The same engine doubles as a gentle spectral leveler — e.g. taming a resonant
electric-bass region on a mix bus dynamically instead of a static EQ dip:
```
Mode:          Process
Band 1:        bracket the resonant region (e.g. 120–220 Hz)
Floor Margin:  24–30      Overcut: 1.0–1.5    Min Cut: 0–4
Notch Width:   4–8        Taper: 4–10  (wide + soft = smooth shelving bites)
Attack/Rel:    20 / 80–300 ms
FFT Size:      4096 or 8192  (fast time response)
```

### D. Multiple mic stems before a bus
Apply per-stem in **Forced Mono**, tuned per stem. If two zones need very
different aggression, use **separate instances**. For a bus carrying hard-panned
mono stems, try **Unlinked Dual-Mono** on the bus instead.

---

## 11. Tuning guide (fix-it-by-ear)

**It misses the resonance / lets it through**
- Lower **Floor Margin** (24 → 20 → 16).
- Raise **Overcut** and/or **Min Cut** (these set the depth — raising
  Max Attenuation alone does nothing unless the cut is hitting the cap).
- Go to a **bigger FFT Size**, or widen the band slightly.

**It eats / dulls the music**
- Raise **Floor Margin**; narrow the bands.
- Lower **Notch Width** (→ 0–1) and **Taper** (→ 1–2).
- Lower **Overcut** / **Min Cut**.
- Prefer **Spectral Replace** over Process.
- If the false targets are noisy/percussive, try raising **Tonal Gate** (~0.5).

**It "swirls"/phases (watery, flanger-like)**
- **Lower Notch Edge Taper first**, then Notch Width.
- Raise **Floor Margin**.

**It chatters / pumps**
- Raise **Hold** (300–500 ms) and/or **Release** (500–800 ms).

**A cut lingers too long**
- Lower **Hold** and **Release**.

---

## 12. Gotchas & limits

- **Latency is significant by design** (1.5× FFT size). Fully PDC-compensated,
  but too long for live/monitoring use at the larger sizes.
- **Shared controls within one instance:** all three bands share Floor Margin,
  depth and dynamics settings. Different zones needing different treatment =
  separate instances.
- **Not magic on overlapping content:** if a resonance sits at the exact frequency
  of music you want to keep, no frequency-domain tool can fully separate them.
  Spectral Replace + narrow notches gets the best balance.
- **Unlinked Dual-Mono** can subtly shift the stereo image at notched frequencies
  while a one-sided cut is active (inherent to unlinked processing).
- The output is protected by a safety stage: non-finite samples are silenced and
  the output is hard-limited at ~+12 dBFS, so a fault can never reach your ears
  at damaging levels.

---

## 13. How detection actually works (for the curious)

Per analysis hop (every FFT-size/4 samples), for each bin **inside an enabled band**:

1. **Robust floor:** sample magnitudes across a wide neighborhood (~±1.8 kHz) and
   take a low-percentile-style estimate (a mean pass plus two one-sided rejection
   passes). Narrow peaks are rejected no matter how wide the cluster.
2. **Prominence:** `20·log10(magnitude / floor)`.
3. **Confidence:** prominence over **Floor Margin**, coherence over **MSC
   Threshold** (stereo), and phase-stability over the **Tonal Gate** (if enabled).
4. **Desired attenuation:**
   `max( MinCut·confidence², (prominence − FloorMargin)·Overcut·confidence )`,
   capped at **Max Attenuation**.
5. **Hold engine:** captures the peak attenuation and sustains it for **Hold**.
6. **Dilation + taper:** spreads the notch ±**Notch Width** bins at full depth with
   a cosine-tapered skirt of **Notch Edge Taper** bins — clamped inside the band.
7. **Attack/Release smoothing** of every per-bin gain.
8. **Apply (per Mode):** multiply down (Process), play the removed part (Solo), or
   rebuild from the nearest clean anchors while keeping phase (Spectral Replace).
   In Unlinked Dual-Mono, steps 1–7 run independently per channel.
9. **Inverse FFT + overlap-add** back to audio (75% overlap, Hann window) — all on
   a background worker thread so the audio thread never carries the FFT burst.

---

## 14. License

Copyright © 2026 Bytemixer.

This program is free software under the **GNU Affero General Public License v3
(or later)** — "free" refers to the license freedoms, not the price. It is
distributed WITHOUT ANY WARRANTY. Source code:
**github.com/Bytemixer/FeedbackKiller**

Built with the JUCE framework (AGPLv3). VST is a trademark of Steinberg Media
Technologies GmbH. See `NOTICE.md` in the source distribution for third-party
notices.

---

*Built and tuned iteratively for orchestral concert feedback repair.*
