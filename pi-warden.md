This file holds mixmind's enforceable rules. It is plain Markdown: readable by a human,
parseable by a guardrail, and useful with or without one installed.

How it is parsed, so edits keep working:
  - Headings delimit rules. There is deliberately no document title: the shallowest heading
    in this file is the rule level, so adding a `# Title` above the rules would swallow all
    of them into one rule.
  - One rule per heading. Each rule is judged in isolation, so no rule may depend on another.
  - A body is clipped to 400 characters when sent for judging. Keep it under that.
  - An optional first line `paths: <glob>, <glob>` scopes a rule to files; unanchored, so
    `Source/*.cpp` matches at any depth.
  - At most 31 rules whose globs match a path are judged per edit.
  - Text before the first heading is not a rule. Notes go here, not in a heading.

Which file owns what, so drift has one home:
  - This file: the enforceable rules — what must and must not be written.
  - AGENTS.md: how to operate the repo — purpose, commands, layout, workflow.
  - .pi/skills/mixmind-dsp/SKILL.md: the reasoning behind the DSP rules and the traps.

Pre-existing deviations. The code does not satisfy all of these today; do not read a
violation as precedent. LoudnessMeter grows gating containers on the audio thread.
ShaperProcessor::process takes the coefficients lock and copies the taps per block.
TelemetryCanvas::setUserBins reads the analyzer's bin array unsynchronized.
ReferenceAnalyzer::loadFile analyses on the message thread.

# No blocking work on the audio thread
paths: Source/PluginProcessor.cpp, Source/ShaperProcessor.cpp, Source/LoudnessMeter.cpp, Source/AudioAnalyzer.cpp
Never do blocking work on the audio thread: anything reachable from processBlock has no locks, no heap allocation, no file or network I/O, no logging. Allocate in prepareToPlay. A lock that an audio-thread reader takes is forbidden even when the writer is on the message thread — the writer owns synchronization.

# The metering path does not allocate
paths: Source/LoudnessMeter.cpp, Source/AudioAnalyzer.cpp
Gating windows and gating blocks are pre-allocated and recycled; process() must not push to a container that can grow or free. Metering state is sized from the sample rate in prepare(), not from however long the session runs.

# Cross-thread readouts are atomic or a snapshot
paths: Source/AudioAnalyzer.cpp, Source/AudioAnalyzer.h, Source/PluginEditor.cpp, Source/TelemetryCanvas.cpp
Every value the message thread reads that the audio thread writes must be atomic or a snapshot published behind an atomic index. Scalars are Atomic. A bin array is never read live: the audio thread fills one buffer while the GUI reads the other.

# The shaper swaps coefficients on the writer side
paths: Source/ShaperProcessor.cpp, Source/PluginProcessor.cpp
setFilter runs on the message thread and owns all synchronization. The audio thread reads the live taps without locking and without copying them. A double buffer alone is not enough: if the writer can publish twice inside one block it must not write a buffer the reader holds — rotate three buffers or claim a buffer and skip the design.

# Trace v-space is linear in dB
paths: Source/MatchState.h, Source/TelemetryCanvas.cpp, Source/ShaperProcessor.cpp, Source/PluginEditor.cpp
The canvas trace and the control points map [0..1] to [-100..0] dBFS, so v is linear in dB, never linear magnitude. Subtract live from target in native units. Convert between v and gain exactly once, inside buildTargetFromCurve, and never with v * 100 - 100.

# Match-filter constants are frozen
paths: Source/ShaperProcessor.h, Source/ShaperProcessor.cpp
kTapCount 1024, kDesignOrder 11, kDesignSize 2048, kNumBins 1024 and kLatency 512 are compile-time constants that other code and saved sessions depend on. Propose a change with its latency and resolution consequences; do not change one silently.

# Keep the 1/3-octave smoothing
paths: Source/ShaperProcessor.cpp, Source/PluginProcessor.cpp
Smoothing the designed response to 1/3 octave is the guard against pre-ringing in a linear-phase filter. It is not a cosmetic choice and is not removed to sharpen the match.

# No manual 1/N after the inverse FFT
paths: Source/ShaperProcessor.cpp
JUCE's perform(..., inverse=true) and performRealOnlyInverseTransform both already apply the 1/N scale. Adding another one is a silent level error.

# One shaper engine serves manual and auto
paths: Source/ShaperProcessor.cpp, Source/PluginProcessor.cpp
Manual mode targets traceTarget minus live; auto mode targets reference minus live. Both feed the same design path. Do not fork the engine per mode, and do not add a second design path.

# Bin counts must stay equal
paths: Source/AudioAnalyzer.h, Source/ReferenceAnalyzer.h, Source/ShaperProcessor.cpp, Source/PluginProcessor.cpp
ReferenceAnalyzer::numBins, AudioAnalyzer::numBins and the shaper's kNumBins must all stay 1024. If any changes, live-versus-target subtraction needs interpolation first. Stop and report rather than guessing a resampling scheme.

# Never let a silent live signal become a boost
paths: Source/PluginProcessor.cpp, Source/ShaperProcessor.cpp
When a live bin is below -80 dB the requested change is clamped so it cannot be positive. That clamp is what stops a stopped transport, or a decaying analyzer average, from designing a large boost. It stays.

# Nothing slow runs on the message thread
paths: Source/ReferenceAnalyzer.cpp, Source/PluginEditor.cpp
Loading and analysing a reference file takes as long as the file is long. In a DAW that blocks the host's UI and reads as a hang. Analyse on a background thread and publish the finished result; the message thread only starts the job and displays the outcome.

# Loudness follows BS.1770-4
paths: Source/LoudnessMeter.cpp, Source/LoudnessMeter.h, Source/AudioAnalyzer.cpp, Source/ReferenceAnalyzer.cpp
K-weighting is the shelf at 1681.97 Hz, Q 0.707, +3.9998 dB, plus the high-pass at 38.135 Hz, Q 0.5003. Integrated loudness is toLufs = -0.691 + 10*log10(energy/count). No other weighting or constant is substituted.

# Integrated loudness is gated
paths: Source/LoudnessMeter.cpp, Source/LoudnessMeter.h
Integrated loudness uses 400 ms blocks with an absolute gate at -70 LUFS and a relative gate at -10 LU below the ungated mean. Momentary and short-term windows do not feed the integrated value.

# True peak is 4x oversampled
paths: Source/LoudnessMeter.cpp, Source/LoudnessMeter.h
True peak is measured through a 4x windowed-sinc polyphase interpolator: cutoff 0.125 cycles per sample, 12 taps per phase. Sample peak is never reported as true peak.

# Never substitute RMS - 3 dB for LUFS
paths: Source/LoudnessMeter.cpp, Source/AudioAnalyzer.cpp, Source/ReferenceAnalyzer.cpp, Source/PluginEditor.cpp
Any figure shown or compared as loudness is BS.1770 loudness. RMS with a fixed offset is not loudness; if a source still does this, replace it with the meter rather than porting the approximation.

# Every behaviour change ships a test
Always ship the test for a behaviour change in the same PR, and run the full suite before proposing a diff. A change that cannot be tested is reported as untested rather than presented as done.

# Never weaken an assertion to make a test pass
A failing assertion is a finding. Fix the code, or report the assertion as wrong with evidence. Loosening a tolerance, deleting a case, or skipping a test to get green is not a fix.

# Check every consuming target when editing a shared file
AudioAnalyzer.cpp is compiled by MixMind, Scope, Meter and EQT, and it depends on LoudnessMeter.cpp. LicenseManager.cpp is used by six other plugins. When a shared file changes, every target that compiles it is rebuilt and reported, or the change is not done.

# Do not touch LicenseManager
paths: Source/LicenseManager.cpp, Source/LicenseManager.h
Do not edit or remove LicenseManager.cpp or LicenseManager.h: they are shared with Scope, Meter, EQT, Reflex, ThreeFX and Neat, and removing them breaks their builds. MixMind does not use LicenseManager, and that is deliberate.

# No new dependencies
JUCE 8.0.6 and the C++ standard library are the baseline, and both are already chosen. A new dependency is proposed and agreed before it is added, never introduced as part of another change.

# No AAX, and keep proxy scaffolding out
AAX is deferred to V2: it is not added to the CMake target lists. ApiClient, ChatComponent, ContextPanel and AIAnalysis stay out of the MixMind target — they were removed deliberately and are not reintroduced.

# Ask before changing build, CI or signing
CMakeLists.txt, .github/ and any signing or notarization material are changed only after asking. This includes adding targets, changing formats, and touching the packaging script.

# Branch and commit conventions
Work branches from main, one logical change per branch, Conventional Commits for messages. Nothing is committed directly to main, a shared branch is never force-pushed, and published history is not rewritten.

# Never commit node_modules or package files
node_modules/, package.json and package-lock.json were removed from this repository and stay removed. They are not committed, restored, or added back as a side effect of a tool.
