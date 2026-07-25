# JuicePipe MixMind — Beta Program

**What:** An AI mixing assistant that actually listens to your mix — real-time
FFT analysis, LUFS tracking, spectral energy bands, and DeepSeek-powered advice.
All inside a VST3/AU plugin. No tab-switching. No typing out what you hear.

**Requirements:** macOS 12+, Ableton Live or Logic Pro, and an internet
connection on first launch.

---

## Reddit Post (r/AudioProductionWithAI)

```
Title: Built an AI mixing plugin that analyzes your track in real time.
       Free beta keys inside.

Body:

I got tired of describing my mix to ChatGPT and getting generic advice.
So I built a VST3/AU plugin that actually listens.

MixMind sits on your master bus, runs real-time FFT analysis on your audio,
and lets you ask questions like:

  • "Is my low end muddy?"
  • "How's the stereo width?"
  • "What frequency is eating my vocal?"

It sees your LUFS, spectral balance, BPM — and answers based on actual data,
not guesses.

I've got 10 free beta licenses to give away to beta
testers. DM me or comment below and I'll send one.

macOS only for now (VST3/AU). Windows version coming if there's interest.
```

## DM Template (When Sending a License Key)

```
Hey — here's your MixMind beta license:

  License: MM-XXXXXXXXXXXXXXX
  Download: https://github.com/kylerobertschuster/mixmind/releases/tag/beta-1
  Proxy: https://juicepipe.audio

To use it:
  1. Download the .pkg and install
  2. Open the plugin in Ableton or Logic
  3. Paste the license key into the settings
  4. Ask it anything about your mix

Two things I'd love to know after you try it:
  1. Did it actually help you on a mix?
  2. Would you pay $9/month for this?

No pressure — just curious. Thanks for trying it out.
```

## What to Track

| User | License | Tried it? | Would pay? | Notes |
|------|---------|-----------|------------|-------|
| 1 | MM-... | | | |
| 2 | MM-... | | | |
| ... | ... | ... | ... | ... |

Generate licenses:

```bash
curl -X POST https://juicepipe.audio/admin/generate-license \
  -H "x-admin-secret: YOUR_SECRET" \
  -d '{"count":10}'
```
