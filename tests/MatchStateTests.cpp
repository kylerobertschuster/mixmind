#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "MatchState.h"
#include "ReferenceAnalyzer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstring>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Session recall — the match data that is not an APVTS parameter.
//
//  A reference spectrum and the hand-drawn target curve have to survive a
//  save/load or the plugin's whole premise falls over: the user draws a match,
//  saves the session, and reopens it to nothing. These tests pin the round trip
//  (state tree AND the XML leg the host actually stores) plus the failure
//  modes, because a half-restored reference would silently skew the match EQ.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    using MixMindState::TraceCurve;

    // juce::Array cannot deduce a nested brace-init list, so curves are built
    // through this rather than written inline.
    TraceCurve makeCurve (std::initializer_list<std::pair<float, float>> points)
    {
        TraceCurve curve;

        for (const auto& p : points)
            curve.add (p);

        return curve;
    }

    // The exact leg a host puts state through: ValueTree -> XML -> ValueTree.
    juce::ValueTree throughXml (const juce::ValueTree& in)
    {
        std::unique_ptr<juce::XmlElement> xml (in.createXml());
        REQUIRE (xml != nullptr);
        return juce::ValueTree::fromXml (*xml);
    }

    // Builds the tree the way ReferenceAnalyzer::writeToTree does, so the read
    // path can be exercised without decoding a file first.
    juce::ValueTree makeReferenceTree (const std::vector<float>& bins)
    {
        juce::ValueTree tree (MixMindState::matchTree);
        tree.setProperty ("refLoaded", true, nullptr);
        tree.setProperty ("refName", "Reference Track", nullptr);
        tree.setProperty ("refLufs", -14.25, nullptr);
        tree.setProperty ("refWidth", 0.75, nullptr);
        tree.setProperty ("refPhase", 0.5, nullptr);
        tree.setProperty ("refPeakDb", -1.5, nullptr);
        tree.setProperty ("refRmsDb", -18.0, nullptr);

        const juce::MemoryBlock mb (bins.data(), bins.size() * sizeof (float));
        tree.setProperty ("refBins", juce::var (mb), nullptr);
        return tree;
    }

    // A short stereo WAV so the loader is exercised for real rather than stubbed.
    juce::File writeTempWav (double sr, float freq, int numSamples)
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("mixmind_recall_test.wav");
        file.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
            return {};

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), sr, 2, 16, {}, 0));

        if (writer == nullptr)
            return {};

        stream.release();   // the writer owns it from here

        juce::AudioBuffer<float> buffer (2, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const float v = 0.25f * std::sin (juce::MathConstants<float>::twoPi
                                              * freq * (float) i / (float) sr);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }

        writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
        writer.reset();     // flush + close

        return file;
    }
}

// ── Trace curve ───────────────────────────────────────────────────────────

TEST_CASE ("a drawn trace survives a round trip", "[state][trace]")
{
    const auto curve = makeCurve ({ { 20.0f, 0.0f }, { 200.0f, 0.4f }, { 2000.0f, 0.75f }, { 20000.0f, 1.0f } });

    const auto restored = MixMindState::treeToTrace (throughXml (MixMindState::traceToTree (curve)));

    REQUIRE (restored.size() == curve.size());

    for (int i = 0; i < curve.size(); ++i)
    {
        REQUIRE (restored[i].first  == Catch::Approx (curve[i].first).margin (1.0e-4));
        REQUIRE (restored[i].second == Catch::Approx (curve[i].second).margin (1.0e-6));
    }
}

TEST_CASE ("an empty trace round-trips to nothing", "[state][trace]")
{
    REQUIRE (MixMindState::treeToTrace (throughXml (MixMindState::traceToTree ({}))).isEmpty());
}

TEST_CASE ("a trace with no tree at all reads as empty", "[state][trace]")
{
    REQUIRE (MixMindState::treeToTrace (juce::ValueTree()).isEmpty());
}

TEST_CASE ("a trace is stored in frequency order", "[state][trace]")
{
    // Points can be clicked in any order; the FIR designer walks the curve as a
    // sorted segment list, and so does the preview. Both must see the same order.
    const auto clicked = makeCurve ({ { 5000.0f, 0.9f }, { 50.0f, 0.1f }, { 500.0f, 0.5f } });

    const auto restored = MixMindState::treeToTrace (MixMindState::traceToTree (clicked));

    REQUIRE (restored.size() == 3);
    REQUIRE (restored[0].first == Catch::Approx (50.0f));
    REQUIRE (restored[1].first == Catch::Approx (500.0f));
    REQUIRE (restored[2].first == Catch::Approx (5000.0f));
}

TEST_CASE ("a corrupted trace point is clamped rather than trusted", "[state][trace][robustness]")
{
    juce::ValueTree tree (MixMindState::traceTree);

    const auto addPoint = [&tree] (double hz, double value)
    {
        juce::ValueTree node (MixMindState::pointElem);
        node.setProperty ("hz", hz, nullptr);
        node.setProperty ("value", value, nullptr);
        tree.appendChild (node, nullptr);
    };

    addPoint (-100.0, 5.0);      // both out of range
    addPoint (200.0, -0.5);
    addPoint (std::nan (""), 0.5);   // unrecoverable, so dropped

    const auto curve = MixMindState::treeToTrace (tree);

    // A NaN point is dropped rather than clamped: jlimit passes NaN through, and
    // one NaN in the curve makes buildMatchFilter produce a NaN impulse response,
    // which silences the track.
    REQUIRE (curve.size() == 2);

    for (const auto& pt : curve)
    {
        REQUIRE (std::isfinite (pt.first));
        REQUIRE (pt.first >= 1.0f);
        REQUIRE (pt.first <= 96000.0f);
        REQUIRE (pt.second >= 0.0f);
        REQUIRE (pt.second <= 1.0f);
    }

    REQUIRE (curve[0].first == Catch::Approx (1.0f));     // clamped up
    REQUIRE (curve[0].second == Catch::Approx (1.0f));    // clamped down
    REQUIRE (curve[1].first == Catch::Approx (200.0f));   // in range, kept
    REQUIRE (curve[1].second == Catch::Approx (0.0f));
}

// ── Reference spectrum ────────────────────────────────────────────────────

TEST_CASE ("a loaded reference survives a round trip", "[state][reference]")
{
    const auto file = writeTempWav (48000.0, 1000.0f, 48000);

    if (file == juce::File())
        return;   // no writable temp dir — nothing to assert

    ReferenceAnalyzer loaded;
    juce::String error;

    REQUIRE (loaded.loadFile (file, 48000.0, error));
    REQUIRE (loaded.hasReference());
    REQUIRE (error.isEmpty());

    juce::ValueTree tree (MixMindState::matchTree);
    loaded.writeToTree (tree);

    ReferenceAnalyzer restored;
    REQUIRE (restored.readFromTree (throughXml (tree)));
    REQUIRE (restored.hasReference());

    // The bins are what the match EQ is built from, so they must come back
    // bit-exact — an approximate restore would shift the whole shape.
    REQUIRE (std::memcmp (restored.getBins(), loaded.getBins(),
                          sizeof (float) * (size_t) ReferenceAnalyzer::numBins) == 0);

    REQUIRE (restored.getFileName()     == loaded.getFileName());
    REQUIRE (restored.getLufs()         == Catch::Approx (loaded.getLufs()).margin (1.0e-5));
    REQUIRE (restored.getStereoWidth()  == Catch::Approx (loaded.getStereoWidth()).margin (1.0e-6));
    REQUIRE (restored.getPhaseCorr()    == Catch::Approx (loaded.getPhaseCorr()).margin (1.0e-6));
    REQUIRE (restored.getPeakDb()       == Catch::Approx (loaded.getPeakDb()).margin (1.0e-5));

    file.deleteFile();
}

TEST_CASE ("no reference round-trips as no reference", "[state][reference]")
{
    ReferenceAnalyzer none;
    REQUIRE_FALSE (none.hasReference());

    juce::ValueTree tree (MixMindState::matchTree);
    none.writeToTree (tree);

    ReferenceAnalyzer restored;
    REQUIRE_FALSE (restored.readFromTree (throughXml (tree)));
    REQUIRE_FALSE (restored.hasReference());
}

TEST_CASE ("a truncated reference payload fails closed", "[state][reference][robustness]")
{
    const std::vector<float> shortBins (256, 0.5f);   // not numBins long

    ReferenceAnalyzer restored;
    REQUIRE_FALSE (restored.readFromTree (throughXml (makeReferenceTree (shortBins))));

    // Half a spectrum would quietly skew the match EQ, so it must read as none.
    REQUIRE_FALSE (restored.hasReference());
}

TEST_CASE ("a reference with no payload at all fails closed", "[state][reference][robustness]")
{
    juce::ValueTree tree (MixMindState::matchTree);
    tree.setProperty ("refLoaded", true, nullptr);   // flagged loaded, but empty

    ReferenceAnalyzer restored;
    REQUIRE_FALSE (restored.readFromTree (tree));
    REQUIRE_FALSE (restored.hasReference());
}

TEST_CASE ("reading a reference clears whatever was loaded before", "[state][reference]")
{
    const std::vector<float> bins ((size_t) ReferenceAnalyzer::numBins, 0.25f);

    ReferenceAnalyzer restored;
    REQUIRE (restored.readFromTree (makeReferenceTree (bins)));
    REQUIRE (restored.hasReference());

    // Loading a second session must not leave the first reference's bins behind.
    REQUIRE_FALSE (restored.readFromTree (juce::ValueTree (MixMindState::matchTree)));
    REQUIRE_FALSE (restored.hasReference());
    REQUIRE (restored.getBins()[0] == Catch::Approx (0.0f));
}

// ── Both halves in one session tree ───────────────────────────────────────

TEST_CASE ("a trace and a reference coexist in one session", "[state][match]")
{
    const std::vector<float> bins ((size_t) ReferenceAnalyzer::numBins, 0.375f);
    const auto curve = makeCurve ({ { 40.0f, 0.2f }, { 400.0f, 0.6f }, { 4000.0f, 1.0f } });

    juce::ValueTree tree (MixMindState::matchTree);

    tree.appendChild (MixMindState::traceToTree (curve), nullptr);

    ReferenceAnalyzer source;
    REQUIRE (source.readFromTree (makeReferenceTree (bins)));
    source.writeToTree (tree);

    const auto restoredTree = throughXml (tree);

    const auto restoredCurve = MixMindState::treeToTrace (restoredTree.getChildWithName (MixMindState::traceTree));
    ReferenceAnalyzer restoredRef;
    REQUIRE (restoredRef.readFromTree (restoredTree));

    REQUIRE (restoredCurve.size() == 3);
    REQUIRE (restoredCurve[0].first == Catch::Approx (40.0f));
    REQUIRE (restoredRef.hasReference());
    REQUIRE (restoredRef.getBins()[0] == Catch::Approx (0.375f));
}
