# Feedback Resonance Killer

NOTE/caveat: 
While I designed and architected the ideas and concepts for the signal flow and how the tool needed to behave:  the actual math, calculations, and code were handled by LLM. 
I'm a musician and recording/mixing/audio engineer. And while I retain some knowledge and foundations of code, (My A.A. was in computer/information science) I'm not technically a coder, but more of a designer/conceptor/planner.

The VST3 version of this tool was checked through Tracktion's PluginVal under R10 stress testing to make sure it didn't crash or exhibit weird issues, and to ensure this plugin should work in most DAWs.
Still this is provided as is with no warranty. Though if/when I find problems or decide to add a feature, the source and binaries will be updated.

A surgical FFT-based feedback and resonance suppressor (VST3 / Standalone,
Windows). Born from cleaning up a live concert recording where ordinary
dynamic tools couldn't tame a segment with broadback feedback problems 
(and I didn't own RX at the time).

In a nutshell, it hunts bins that rise above a **robust spectral floor** inside up to three
user-defined frequency bands, then surgically attenuates them with narrow dynamic notches
while a live analyzer shows the spectrum, the detection floor, and every cut
as it happens.

## Highlights

- **Robust floor detection** -- peak-rejecting noise-floor estimate per bin, so
  the music's own level doesn't hide the ring.
- **Stereo coherence (MSC) discrimination** -- feedback fills the room the same
  way in both channels; uncorrelated music is spared.
- **Three independent frequency bands** with draggable on-graph editing.
- **Four reduction modes** -- Process (notch), Bypass, Solo (hear what's being
  removed), Spectral Replace (rebuild the notch from clean neighbours).
- **Four channel modes** -- Auto-Detect, Forced Stereo, Forced Mono, and
  Unlinked Dual-Mono (independent per-channel detection and cutting).
- **Optional phase-stability Tonal Gate** -- spare phase-chaotic content while
  still cutting steady tonal rings.
- **FFT 4096–32768** -- scalpel for dense feedback clusters at 32k, or a smooth
  dynamic-EQ "bus leveler" at smaller sizes with wider notches.
- Real-time carved-spectrum analyzer; editable value fields on every control.
- Reports its latency to the host (full PDC support).

## Building from source

Requirements: Windows 10+, CMake 3.22+, Visual Studio 2022 (Desktop C++
workload). macOS/Linux are untested but nothing in the code is
Windows-specific; JUCE handles the platform layer.

```sh
git clone https://github.com/Bytemixer/FeedbackKiller.git
cd FeedbackKiller

# Option A: let CMake download JUCE automatically
cmake -B build

# Option B: use an existing JUCE 8 checkout
cmake -B build -DFK_JUCE_PATH="C:/path/to/JUCE"

cmake --build build --config Release --target FeedbackKiller_VST3
```

The plugin lands in
`build/FeedbackKiller_artefacts/Release/VST3/Feedback Resonance Killer.vst3/Contents/x86_64-win/`.
Copy the inner `Feedback Resonance Killer.vst3` file (or the whole bundle
folder) into your VST3 folder, e.g. `C:\Program Files\Common Files\VST3`.

## License

Copyright (C) 2026 Bytemixer.

This program is **free software**, licensed under the
[GNU Affero General Public License v3.0 or later](LICENSE). It is distributed
in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

Built with the [JUCE framework](https://juce.com) under AGPLv3. See
[NOTICE.md](NOTICE.md) for third-party notices. VST is a trademark of
Steinberg Media Technologies GmbH.
