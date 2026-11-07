# Implementation Plan — Audio Engine Optimization (Phase 1)

> Status: Awaiting explicit human approval for source-code modifications.
> Scope: DiscNativePro, branch `perf/audio-engine-audit`.
> Request feedback: true. User facing: true.

## Objective
Reduce real-time audio glitches and unnecessary processing while preserving current deck, mixer, cue and recording behavior. No VirtualDJ proprietary code or binary analysis.

## Findings to verify
- `DeckPlayer::getNextAudioBlock` acquires a `juce::ScopedLock` shared with load, seek and resource operations; potential callback blocking.
- `ChannelStrip::process` creates IIR coefficient objects each block, potentially allocating on the real-time thread.
- `DeckPlayer::loadFile` synchronously analyzes BPM, key and waveform; determine caller thread before changing it.
- `StretchAdapter::render` relies on bounded temporary buffers; validate sizes, rate extremes and device callbacks.
- Inspect device restart and output routing for safe lifecycle behavior.

## Proposed changes (subject to approval)
- [MODIFY] `src/audio/DjMixer.cpp`, `src/audio/DjMixer.h`: cache coefficient calculations and avoid allocations in audio callbacks; use safe coefficient transfer and smoothing; retain EQ response.
- [MODIFY] `src/audio/DeckPlayer.cpp`, `src/audio/DeckPlayer.h`: remove callback lock contention by staging playback state/source changes outside the audio thread and using a safe handoff; never destroy active sources in callbacks.
- [MODIFY] `src/audio/AudioEngine.cpp`, `src/audio/AudioEngine.h`: validate buffer bounds, callback lifecycle, and independent master/cue routing.
- [MODIFY] `src/audio/StretchAdapter.cpp`, `src/audio/StretchAdapter.h`: add explicit buffer-size safety and quality/CPU measurements without replacing the algorithm prematurely.
- [NEW] `tests/audio/`: deterministic tests for mix, silence, EQ response, rapid load/seek, device restart and callback timing.
- [NEW] `docs/performance-baseline.md`: methodology and baseline results; distinguish measured from unmeasured.
- [NEW] `walkthrough.md`: completed work and verification results after implementation.

## Execution sequence
1. Read all relevant headers, call sites and concurrency contracts.
2. Capture baseline on supported Windows/macOS build.
3. Apply small, reviewable changes, one subsystem at a time.
4. Run automated tests and compare audio outputs for regressions.
5. Compile with `cmake --build build` (and Release configuration as applicable), target zero errors/warnings; report environment limits if build cannot run.
6. Run application and verify decks, headphones, MIDI and prolonged playback on an available supported machine; do not claim verification without execution.

## Acceptance criteria
- No dynamic allocation or contended mutex acquisition in audio callback path.
- No audible glitches in repeatable stress tests.
- No loss of cue/master routing, pitch, scratch or recording features.
- Measured callback worst-case time remains below audio block deadline with safety margin.
- Changes only on the feature branch until separately reviewed.

## Legal and credits
Preserve existing author notices. Public repository visibility does not confer redistribution rights; verify original project and dependency licenses before relicensing or accepting external contributions.

## Approval gate
Do not modify source code until the user explicitly approves this plan in a subsequent message, consistent with `AGENTS.md`.
