#include "PluginProcessor.h"
#include "TelemetryCanvas.h"
#include "TestSignals.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>

using namespace TestSignals;

namespace
{
    constexpr double kFs  = 48000.0;
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    constexpr double kInf = std::numeric_limits<double>::infinity();

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> fields)
    {
        auto* o = new juce::DynamicObject();
        for (const auto& f : fields) o->setProperty (f.first, f.second);
        return juce::var (o);
    }

    juce::var array (std::initializer_list<juce::var> items)
    {
        return juce::var (juce::Array<juce::var> (items));
    }

    juce::var point (double hz, double db) { return array ({ hz, db }); }

    juce::var params (std::initializer_list<std::pair<const char*, juce::var>> fields)
    {
        return object ({ { "parameters", object (fields) } });
    }

    const AiPayload::Change* changeFor (const MixMindProcessor& p, const AiPayload& payload, const char* id)
    {
        const int index = p.parameters.getParameter (id)->getParameterIndex();
        for (int i = 0; i < payload.numChanges; ++i)
            if (payload.changes[(size_t) i].param == index) return &payload.changes[(size_t) i];
        return nullptr;
    }

    float norm (const MixMindProcessor& p, const char* id, float value)
    {
        return p.parameters.getParameter (id)->convertTo0to1 (value);
    }

    bool isEmpty (const AiPayload& p) { return p.numChanges == 0 && ! p.setsTrace && p.numTracePoints == 0; }

    bool containsNonFinite (const juce::var& v)
    {
        if (v.isDouble()) return ! std::isfinite ((double) v);
        if (auto* a = v.getArray())
            return std::any_of (a->begin(), a->end(), [] (const juce::var& x) { return containsNonFinite (x); });
        if (auto* o = v.getDynamicObject())
            for (const auto& f : o->getProperties())
                if (containsNonFinite (f.value)) return true;
        return false;
    }

    // Everything an accepted payload may contain, checked independently of the firewall.
    juce::String invariantViolation (const MixMindProcessor& proc, const AiPayload& p)
    {
        const auto& list = proc.getParameters();
        if (p.numChanges < 0 || p.numChanges > AiPayload::kMaxChanges) return "change count";
        for (int i = 0; i < p.numChanges; ++i)
        {
            const auto& c = p.changes[(size_t) i];
            auto* prm = dynamic_cast<juce::RangedAudioParameter*> (list[c.param]);
            if (prm == nullptr) return "parameter index " + juce::String (c.param);
            if (! std::isfinite (c.value) || c.value < 0.0f || c.value > 1.0f) return prm->getParameterID() + " value";
            const auto& range = prm->getNormalisableRange();
            const float real = range.convertFrom0to1 (c.value);
            if (! std::isfinite (real) || real < range.start || real > range.end) return prm->getParameterID() + " range";
            if (range.interval >= 1.0f && ! juce::exactlyEqual (std::round (real), real)) return prm->getParameterID() + " index";
        }
        if (p.numTracePoints < 0 || p.numTracePoints > AiPayload::kMaxTracePoints) return "trace count";
        if (! p.setsTrace && p.numTracePoints != 0) return "trace without setsTrace";
        if (p.setsTrace && p.numTracePoints == 1) return "single-point trace";
        for (int i = 0; i < p.numTracePoints; ++i)
        {
            const auto& t = p.trace[(size_t) i];
            if (! std::isfinite (t.hz) || ! std::isfinite (t.db)) return "trace non-finite";
            if (t.hz < AiFirewall::kTraceMinHz || t.hz > AiFirewall::kTraceMaxHz) return "trace hz";
            if (t.db < AiFirewall::kTraceMinDb || t.db > AiFirewall::kTraceMaxDb) return "trace dB";
            if (i > 0 && t.hz < p.trace[(size_t) i - 1].hz * AiFirewall::kTraceMinSpacing) return "trace spacing";
        }
        return {};
    }

    // Pops one result from a worker, waiting up to timeoutMs (no message loop needed).
    bool popWithin (AiWorker& w, AiResult& out, int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (! w.pop (out))
        {
            if (juce::Time::getMillisecondCounter() > end) return false;
            juce::Thread::sleep (1);
        }
        return true;
    }

    bool pumpUntil (const std::function<bool()>& done, int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (! done())
        {
            if (juce::Time::getMillisecondCounter() > end) return false;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
        return true;
    }

    AiWorker::Backend replyWith (const juce::String& json)
    {
        return [json] (const juce::String&, juce::String& reply, const std::function<bool()>&)
        {
            reply = json;
            return juce::Result::ok();
        };
    }

    struct GestureCounter : juce::AudioProcessorListener
    {
        std::atomic<int> begins { 0 }, ends { 0 }, changes { 0 };
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++changes; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begins; }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { ++ends; }
    };

    std::vector<float> snapshot (const MixMindProcessor& p)
    {
        std::vector<float> v;
        for (auto* prm : p.getParameters()) v.push_back (prm->getValue());
        return v;
    }
}

class AiTests : public juce::UnitTest
{
public:
    AiTests() : juce::UnitTest ("AI firewall + worker queue", "AI") {}

    void runTest() override
    {
        MixMindProcessor proc;
        const AiFirewall fw (proc.getParameters());

        beginTest ("A valid payload maps every value onto its parameter");
        {
            AiPayload out;
            const auto r = fw.validate (juce::String (R"({
                "parameters": { "shapeAmount": 0.5, "matchStereo": 1,
                                "b1On": true, "b1Type": "High Shelf", "b1Freq": 320, "b1Gain": -3.5,
                                "b1Q": 0.7, "b1Slope": "24 dB/oct", "b1Place": "side", "b2Type": "low_shelf" },
                "trace": [ [1000, -50], [100, -45], [10000, -60] ] })"), out);
            expect (r.wasOk(), r.getErrorMessage());
            expectEquals (out.numChanges, 10);
            expectEquals (out.numClamped, 0);

            const auto check = [&] (const char* id, float real)
            {
                const auto* c = changeFor (proc, out, id);
                expect (c != nullptr, id);
                if (c != nullptr) expectWithinAbsoluteError (c->value, norm (proc, id, real), 1.0e-6f, id);
            };
            check ("shapeAmount", 0.5f);  check ("matchStereo", 1.0f);
            check ("b1On", 1.0f);         check ("b1Type", 2.0f);     check ("b1Freq", 320.0f);
            check ("b1Gain", -3.5f);      check ("b1Q", 0.7f);        check ("b1Slope", 1.0f);
            check ("b1Place", 2.0f);      check ("b2Type", 1.0f);

            expect (out.setsTrace);
            expectEquals (out.numTracePoints, 3);
            expectEquals (out.trace[0].hz, 100.0f);   // sorted by frequency
            expectEquals (out.trace[1].hz, 1000.0f);
            expectEquals (out.trace[2].hz, 10000.0f);
            expectEquals (out.trace[2].db, -60.0f);
            expect (invariantViolation (proc, out).isEmpty());
        }

        beginTest ("NaN and infinity reject the whole payload");
        {
            for (double bad : { kNaN, kInf, -kInf })
            {
                AiPayload out;
                auto r = fw.validate (params ({ { "b1Freq", 1000.0 }, { "b1Gain", bad } }), out);
                expect (r.failed() && r.getErrorMessage().contains ("b1Gain"), r.getErrorMessage());
                expect (isEmpty (out));

                r = fw.validate (params ({ { "b1Type", bad } }), out);
                expect (r.failed() && isEmpty (out));
                r = fw.validate (params ({ { "b1On", bad } }), out);
                expect (r.failed() && isEmpty (out));
                r = fw.validate (object ({ { "trace", array ({ point (100, -40), point (1000, bad) }) } }), out);
                expect (r.failed() && isEmpty (out));
                r = fw.validate (object ({ { "trace", array ({ point (bad, -40), point (1000, -50) }) } }), out);
                expect (r.failed() && isEmpty (out));
            }

            // Straight from text: overflowing literals and non-JSON NaN.
            for (const char* text : { R"({"parameters":{"b1Gain":1e999}})", R"({"parameters":{"b1Gain":-1e999}})",
                                      R"({"parameters":{"b1Gain":NaN}})", R"({"parameters":{"b1Gain":Infinity}})",
                                      R"({"trace":[[100,-40],[1000,1e999]]})" })
            {
                AiPayload out;
                expect (fw.validate (juce::String (text), out).failed(), text);
                expect (isEmpty (out));
            }
        }

        beginTest ("Out-of-range quantities are clamped to the parameter's own range");
        {
            AiPayload out;
            auto r = fw.validate (params ({ { "b1Gain", 60.0 }, { "b2Gain", -1.0e30 }, { "b1Freq", 5.0 }, { "b2Freq", 1.0e6 },
                                            { "b1Q", 1000.0 }, { "shapeAmount", -0.3 }, { "b3Gain", 6.0 } }), out);
            expect (r.wasOk(), r.getErrorMessage());
            expectEquals (out.numClamped, 6);   // b3Gain was in range
            expectEquals (changeFor (proc, out, "b1Gain")->value, 1.0f);
            expectEquals (changeFor (proc, out, "b2Gain")->value, 0.0f);
            expectEquals (changeFor (proc, out, "b1Freq")->value, 0.0f);
            expectEquals (changeFor (proc, out, "b2Freq")->value, 1.0f);
            expectEquals (changeFor (proc, out, "b1Q")->value, 1.0f);
            expectEquals (changeFor (proc, out, "shapeAmount")->value, 0.0f);
            expectWithinAbsoluteError (changeFor (proc, out, "b3Gain")->value, norm (proc, "b3Gain", 6.0f), 1.0e-6f);

            r = fw.validate (object ({ { "trace", array ({ point (5, -40), point (1000, 6), point (5000, -300), point (50000, -20) }) } }), out);
            expect (r.wasOk(), r.getErrorMessage());
            expectEquals (out.numClamped, 4);
            expectEquals (out.trace[0].hz, 20.0f);
            expectEquals (out.trace[1].db, 0.0f);
            expectEquals (out.trace[2].db, -100.0f);
            expectEquals (out.trace[3].hz, 20000.0f);
            expect (invariantViolation (proc, out).isEmpty());

            // The trace limits are the canvas's plot range.
            expectEquals (AiFirewall::kTraceMinDb, TelemetryCanvas::kFloorDb);
            expectEquals (AiFirewall::kTraceMaxDb, TelemetryCanvas::kCeilDb);
        }

        beginTest ("Choices and switches must be exact, never clamped");
        {
            const auto rejected = [&] (const char* id, const juce::var& v)
            {
                AiPayload out;
                const auto r = fw.validate (params ({ { id, v } }), out);
                expect (r.failed() && r.getErrorMessage().startsWith (id) && isEmpty (out),
                        juce::String (id) + " = " + juce::JSON::toString (v, true));
            };
            rejected ("b1Slope", 24);          // meant 24 dB/oct; index 24 does not exist
            rejected ("b1Type", 1.5);
            rejected ("b1Type", -1);
            rejected ("b1Type", "shelf");
            rejected ("b1Type", true);
            rejected ("b1On", 0.7);
            rejected ("b1On", "true");
            rejected ("b1Gain", true);
            rejected ("b1Gain", "3");
            rejected ("b1Gain", array ({ 3.0 }));
            rejected ("b1Gain", juce::var());

            AiPayload out;
            expect (fw.validate (params ({ { "b1On", 1 }, { "b1Type", 3 }, { "b1Place", "MID" } }), out).wasOk());
            expectEquals (out.numClamped, 0);
        }

        beginTest ("Anything malformed rejects the whole payload");
        {
            const auto rejected = [&] (const juce::String& text)
            {
                AiPayload out;
                expect (fw.validate (text, out).failed() && isEmpty (out), text.substring (0, 80));
            };
            rejected (R"({"parameters":{"b1Gain":3,"b9Gain":2}})");      // one unknown ID spoils it all
            rejected (R"({"parameters":{"b1Gain":3},"coefficients":[1,0,0,1,0,0]})");
            rejected (R"({"parameters":{"b1Gain":3},"note":"cut the mud"})");
            rejected (R"([{"parameters":{"b1Gain":3}}])");
            rejected (R"("b1Gain")");
            rejected ("42");
            rejected ("");
            rejected ("Sure! Here is the JSON: {\"parameters\":{\"b1Gain\":3}}");
            rejected ("```json\n{\"parameters\":{\"b1Gain\":3}}\n```");
            rejected ("{}");
            rejected (R"({"parameters":{}})");
            rejected (R"({"parameters":[]})");
            rejected (R"({"parameters":5})");
            rejected (R"({"parameters":{"b1Gain":3})");                   // truncated

            // juce::JSON alone reads these as numbers or ignores the damage.
            rejected (R"({"parameters":{"b1Gain":-x}})");                  // JUCE: -72
            rejected (R"({"parameters":{"b1Gain":-.5}})");                 // JUCE: 15
            rejected (R"({"parameters":{"b1Gain":- 5}})");
            rejected (R"({"parameters":{"b1Gain":99999999999999999999}})"); // wraps int64
            rejected (R"({"parameters":{"b1Gain":1.}})");
            rejected (R"({"parameters":{"b1Gain":1e}})");
            rejected (R"({"parameters":{"b1Gain":0x10}})");
            rejected (R"({'parameters':{'b1Gain':3}})");
            rejected (R"({"parameters":{"b1Gain":3}} {"parameters":{"b1Gain":-24}})");
            rejected (R"({"parameters":{"b1Gain":3}} trailing)");
            rejected (R"({"parameters":{"b1Type":"Bell\"}})");            // escaped quote, never closed
            rejected ("{\"parameters\":{\"b1Gain\":3}}\x01");
            rejected (juce::String::repeatedString ("[", 5000) + juce::String::repeatedString ("]", 5000));
            {
                AiPayload out;   // strict but not fussy: whitespace, escapes, exponents and -0 are fine
                const auto r = fw.validate (juce::String ("\n {\"parameters\" : {\"b1Gain\": -0.35e1, \"b1Type\":\"High\\u0020Shelf\"}}\r\n"), out);
                expect (r.wasOk(), r.getErrorMessage());
                expectWithinAbsoluteError (changeFor (proc, out, "b1Gain")->value, norm (proc, "b1Gain", -3.5f), 1.0e-6f);
                expectEquals (changeFor (proc, out, "b1Type")->value, norm (proc, "b1Type", 2.0f));
            }

            juce::String huge ("{\"parameters\":{\"b1Gain\":3},\"trace\":[");
            while (huge.getNumBytesAsUTF8() <= (size_t) AiFirewall::kMaxTextBytes) huge << "[100,-40],";
            rejected (huge + "[200,-40]]}");
        }

        beginTest ("Trace: shape rules");
        {
            const auto result = [&] (const juce::var& trace, AiPayload& out) { return fw.validate (object ({ { "trace", trace } }), out); };
            AiPayload out;
            expect (result (array ({ point (100, -40) }), out).failed());
            expect (result (array ({ point (100, -40), point (1005, -40), point (1000, -40) }), out).failed());   // < 1 % apart
            expect (result (array ({ point (5, -40), point (10, -40), point (1000, -40) }), out).failed());      // both clamp to 20 Hz
            expect (result (array ({ array ({ 100.0 }), point (1000, -40) }), out).failed());
            expect (result (array ({ array ({ "100", -40.0 }), point (1000, -40) }), out).failed());
            expect (result (array ({ object ({}), point (1000, -40) }), out).failed());
            expect (result (juce::var ("flat"), out).failed());
            expect (isEmpty (out));

            juce::Array<juce::var> many;
            for (int i = 0; i <= AiPayload::kMaxTracePoints; ++i) many.add (point (20.0 * std::pow (1.1, i), -50));
            expect (result (juce::var (many), out).failed());
            many.removeLast();
            expect (result (juce::var (many), out).wasOk());
            expectEquals (out.numTracePoints, AiPayload::kMaxTracePoints);

            expect (result (array ({}), out).wasOk());   // [] clears the trace
            expect (out.setsTrace && out.numTracePoints == 0);
        }

        beginTest ("Fuzzed payloads never let a non-finite, unknown or out-of-range value through");
        {
            juce::Random rng (0x5eed);
            juce::StringArray ids;
            for (auto* prm : proc.getParameters())
                ids.add (dynamic_cast<juce::RangedAudioParameter*> (prm)->getParameterID());
            juce::StringArray words { "Bell", "high shelf", "notch", "Mid", "SIDE", "12 dB/oct", "48dB/oct", "off", "" };
            words.add (juce::String::fromUTF8 ("\xc3\xa9"));

            int accepted = 0, violations = 0, nonFiniteAccepted = 0;
            juce::String firstViolation;

            for (int iter = 0; iter < 6000; ++iter)
            {
                const auto number = [&]() -> juce::var
                {
                    switch (rng.nextInt (9))
                    {
                        case 0:  return kNaN;
                        case 1:  return rng.nextBool() ? kInf : -kInf;
                        case 2:  return (rng.nextDouble() - 0.5) * 2.0e30;
                        case 3:  return rng.nextInt (juce::Range<int> (-3, 30));
                        case 4:  return (juce::int64) rng.nextInt64();
                        default: return (rng.nextDouble() - 0.3) * std::pow (10.0, rng.nextInt (6));
                    }
                };
                const auto value = [&]() -> juce::var
                {
                    switch (rng.nextInt (12))
                    {
                        case 0:  return rng.nextBool();
                        case 1:  return words[rng.nextInt (words.size())];
                        case 2:  return array ({ number() });
                        case 3:  return object ({ { "value", number() } });
                        case 4:  return juce::var();
                        default: return number();
                    }
                };
                // Half the time, something this parameter can legally take (a
                // continuous value may still be far out of range).
                const auto valueFor = [&] (const juce::String& id) -> juce::var
                {
                    auto* prm = proc.parameters.getParameter (id);
                    if (prm == nullptr || rng.nextBool()) return value();
                    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (prm))
                        return rng.nextBool() ? juce::var (rng.nextInt (c->choices.size()))
                                              : juce::var (c->choices[rng.nextInt (c->choices.size())]);
                    if (dynamic_cast<juce::AudioParameterBool*> (prm) != nullptr)
                        return rng.nextBool();
                    return (rng.nextDouble() - 0.3) * std::pow (10.0, rng.nextInt (6));
                };

                auto* top = new juce::DynamicObject();
                juce::var payload (top);
                if (rng.nextInt (10) < 8)
                {
                    auto* ps = new juce::DynamicObject();
                    for (int k = 1 + rng.nextInt (4); --k >= 0;)
                    {
                        const auto id = rng.nextInt (20) != 0 ? ids[rng.nextInt (ids.size())] : juce::String ("b9Gain");
                        ps->setProperty (id, valueFor (id));
                    }
                    top->setProperty ("parameters", juce::var (ps));
                }
                if (rng.nextInt (10) < 4)
                {
                    juce::Array<juce::var> pts;
                    for (int k = rng.nextInt (8); --k >= 0;)
                        pts.add (rng.nextInt (20) == 0 ? value()
                                                       : point ((double) number(), (double) number()));
                    top->setProperty ("trace", pts);
                }
                if (rng.nextInt (20) == 0)
                    top->setProperty ("coefficients", array ({ 1.0, 0.0, 0.0 }));

                const bool hasNonFinite = containsNonFinite (payload);
                AiPayload out;
                const bool ok = rng.nextInt (4) == 0 ? fw.validate (juce::JSON::toString (payload, true), out).wasOk()
                                                     : fw.validate (payload, out).wasOk();
                if (! ok)
                {
                    if (! isEmpty (out)) { ++violations; if (firstViolation.isEmpty()) firstViolation = "rejected but not empty"; }
                    continue;
                }
                ++accepted;
                if (hasNonFinite) ++nonFiniteAccepted;
                const auto why = invariantViolation (proc, out);
                if (why.isNotEmpty()) { ++violations; if (firstViolation.isEmpty()) firstViolation = why; }
            }

            // Random text: must never crash, and whatever passes still obeys the invariants.
            for (int iter = 0; iter < 3000; ++iter)
            {
                static const char alphabet[] = "{}[]\":,.-+eE0123456789 truefalsnNaIiy b1GaQpmtrc";
                juce::String text;
                for (int k = rng.nextInt (60); --k >= 0;) text += alphabet[rng.nextInt ((int) sizeof (alphabet) - 1)];
                if (rng.nextBool()) text = "{\"parameters\":{\"" + ids[rng.nextInt (ids.size())] + "\":" + text + "}}";
                AiPayload out;
                if (fw.validate (text, out).wasOk())
                {
                    ++accepted;
                    const auto why = invariantViolation (proc, out);
                    if (why.isNotEmpty()) { ++violations; if (firstViolation.isEmpty()) firstViolation = why + " in " + text; }
                }
                else if (! isEmpty (out)) ++violations;
            }

            logMessage ("fuzz: " + juce::String (accepted) + " accepted of 9000");
            expect (accepted > 300, "the fuzz should exercise the accepted path too");
            expectEquals (nonFiniteAccepted, 0);
            expectEquals (violations, 0, firstViolation);
        }

        beginTest ("The lock-free FIFO hands every result over, in order, under back-pressure");
        {
            AiWorker w (proc.getParameters());
            w.setBackend ([] (const juce::String& request, juce::String& reply, const std::function<bool()>&)
            {
                reply = "{\"parameters\":{\"shapeAmount\":" + juce::String (request.getIntValue() % 101) + "e-2}}";
                return juce::Result::ok();
            });
            constexpr int kRequests = 400;
            for (int i = 1; i <= kRequests; ++i)
                expectEquals (w.submit (juce::String (i)), i);

            // A slow, jittery consumer keeps the 8-slot ring full most of the time.
            juce::Random rng (7);
            int received = 0, outOfOrder = 0, wrong = 0;
            AiResult r;
            while (received < kRequests && popWithin (w, r, 10000))
            {
                ++received;
                if (r.requestId != received) ++outOfOrder;
                const auto* c = changeFor (proc, r.payload, "shapeAmount");
                if (r.status != AiResult::Status::accepted || c == nullptr
                    || std::abs (c->value - (float) (received % 101) / 100.0f) > 1.0e-6f)
                    ++wrong;
                if (rng.nextInt (8) == 0) juce::Thread::sleep (rng.nextInt (3));
            }
            expectEquals (received, kRequests);
            expectEquals (outOfOrder, 0);
            expectEquals (wrong, 0);
            expect (! w.pop (r));
        }

        beginTest ("Worker failures come back as results, never as exceptions");
        {
            AiWorker w (proc.getParameters());
            AiResult r;

            w.submit ("no model yet");
            expect (popWithin (w, r, 5000));
            expect (r.status == AiResult::Status::failed && juce::String::fromUTF8 (r.message).contains ("no AI model"));

            w.setBackend ([] (const juce::String&, juce::String&, const std::function<bool()>&) { return juce::Result::fail ("connection refused"); });
            w.submit ("x");
            expect (popWithin (w, r, 5000));
            expect (r.status == AiResult::Status::failed && juce::String::fromUTF8 (r.message) == "connection refused");

            w.setBackend ([] (const juce::String&, juce::String&, const std::function<bool()>&) -> juce::Result { throw std::runtime_error ("boom"); });
            w.submit ("x");
            expect (popWithin (w, r, 5000));
            expect (r.status == AiResult::Status::failed && juce::String::fromUTF8 (r.message).contains ("boom"));
            expect (isEmpty (r.payload));

            w.setBackend (replyWith ("I would cut 3 dB at 300 Hz."));
            w.submit ("x");
            expect (popWithin (w, r, 5000));
            expect (r.status == AiResult::Status::rejected && isEmpty (r.payload));

            // A long reason is truncated on a character boundary, still NUL-terminated.
            w.setBackend ([] (const juce::String&, juce::String&, const std::function<bool()>&)
                          { return juce::Result::fail (juce::String::repeatedString (juce::String::fromUTF8 ("\xc3\xa9"), 400)); });
            w.submit ("x");
            expect (popWithin (w, r, 5000));
            const auto msg = juce::String::fromUTF8 (r.message);
            expect (msg.length() > 50 && msg.containsOnly (juce::String::fromUTF8 ("\xc3\xa9")));
        }

        beginTest ("The processor applies an accepted payload as host-visible edits the DSP follows");
        {
            GestureCounter host;
            Harness h;
            h.p.addListener (&host);
            h.p.setAiBackend (replyWith (R"({
                "parameters": { "b2On": true, "b2Type": "Bell", "b2Freq": 250, "b2Gain": -4, "b2Q": 1.2, "shapeAmount": 0.4 },
                "trace": [ [60, -48], [400, -52], [3000, -58] ] })"));
            const int traceBefore = h.p.getTraceVersion();

            const int id = h.p.submitAiRequest ("take some mud out");
            expect (pumpUntil ([&] { return h.p.getLastAiStatus().requestId == id; }, 10000));
            const auto& st = h.p.getLastAiStatus();
            expect (st.status == AiResult::Status::accepted, st.message);
            expectEquals (st.applied, 6);                 // b2Type is already Bell: 5 parameters + the trace
            expectEquals (host.begins.load(), 5);
            expectEquals (host.ends.load(), 5);
            expect (host.changes.load() >= 5);

            const auto band = h.p.readBand (1);
            expect (band.on && band.type == ParametricEq::Type::bell);
            expectWithinAbsoluteError (band.freq, 250.0f, 0.01f);
            expectWithinAbsoluteError (band.gainDb, -4.0f, 0.001f);
            expectWithinAbsoluteError (band.q, 1.2f, 0.001f);
            expectWithinAbsoluteError (h.p.parameters.getRawParameterValue ("shapeAmount")->load(), 0.4f, 1.0e-6f);
            expect (h.p.getTrace().size() == 3 && h.p.getTraceVersion() > traceBefore);

            // The audio path now carries the cut: −4 dB at the band centre.
            const auto tone = sine (kFs, 250.0, 0.25, 1.0);
            const auto out = h.play (tone, tone);
            const size_t skip = (size_t) (0.3 * kFs);
            expectWithinAbsoluteError (rmsDb (out.data() + skip, out.size() - skip) - rmsDb (tone.data() + skip, tone.size() - skip),
                                       -4.0, 0.1);
            h.p.removeListener (&host);
        }

        beginTest ("A rejected payload changes nothing (all or nothing)");
        {
            GestureCounter host;
            Harness h;
            h.p.addListener (&host);
            const auto before = snapshot (h.p);

            for (const char* reply : { R"({"parameters":{"b1On":true,"b1Gain":1e999}})",
                                       R"({"parameters":{"b1On":true,"b12Gain":3}})",
                                       R"({"parameters":{"b1On":true},"trace":[[100,-40]]})" })
            {
                h.p.setAiBackend (replyWith (reply));
                const int id = h.p.submitAiRequest ("x");
                expect (pumpUntil ([&] { return h.p.getLastAiStatus().requestId == id; }, 10000));
                expect (h.p.getLastAiStatus().status == AiResult::Status::rejected, reply);
                expectEquals (h.p.getLastAiStatus().applied, 0);
            }
            expect (h.p.getLastAiStatus().message.contains ("trace"));
            expect (snapshot (h.p) == before);
            expect (h.p.getTrace().empty());
            expectEquals (host.begins.load(), 0);
            h.p.removeListener (&host);
        }

        beginTest ("The audio thread never waits on the model, and runs cleanly while results land");
        {
            juce::WaitableEvent release;
            std::atomic<bool> inBackend { false };
            std::atomic<int> calls { 0 };
            Harness h;
            h.p.setAiBackend ([&] (const juce::String&, juce::String& reply, const std::function<bool()>& cancel)
            {
                if (calls++ == 0)
                {
                    inBackend = true;
                    while (! release.wait (10))
                        if (cancel()) return juce::Result::fail ("cancelled");
                }
                // Alternate sane and hostile answers.
                const int n = calls.load();
                reply = n % 3 == 0 ? R"({"parameters":{"b3Gain":1e999}})"
                                   : "{\"parameters\":{\"b3On\":true,\"b3Freq\":" + juce::String (200 + 37 * n)
                                         + ",\"b3Gain\":" + juce::String ((n % 13) - 6) + "}}";
                return juce::Result::ok();
            });

            h.p.submitAiRequest ("first: blocks until released");
            expect (pumpUntil ([&] { return inBackend.load(); }, 5000));

            // An audio thread runs the whole time; the message thread drains and applies.
            std::atomic<bool> stop { false };
            juce::WaitableEvent audioDone;
            std::atomic<double> worstMs { 0.0 };
            std::atomic<int> blocks { 0 }, nonFinite { 0 };
            juce::Thread::launch ([&]
            {
                const auto noise = whiteNoise (kFs, 1.0, -18.0, 99);
                juce::AudioBuffer<float> buf (2, 512);
                juce::MidiBuffer midi;
                size_t pos = 0;
                while (! stop.load())
                {
                    buf.copyFrom (0, 0, noise.data() + pos, 512);
                    buf.copyFrom (1, 0, noise.data() + pos, 512);
                    pos = (pos + 512) % (noise.size() - 512);
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    h.p.processBlock (buf, midi);
                    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                    if (ms > worstMs.load()) worstMs = ms;
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < 512; ++i)
                            if (! std::isfinite (buf.getSample (ch, i))) ++nonFinite;
                    ++blocks;
                }
                audioDone.signal();
            });

            pumpUntil ([] { return false; }, 400);   // audio keeps running while the model "thinks"
            const int whileBlocked = blocks.load();
            release.signal();

            int last = 0;
            for (int i = 0; i < 30; ++i) last = h.p.submitAiRequest ("again");
            expect (pumpUntil ([&] { return h.p.getLastAiStatus().requestId == last; }, 20000));
            stop = true;
            expect (audioDone.wait (5000));

            logMessage ("audio blocks while the model was busy: " + juce::String (whileBlocked)
                        + ", worst block " + juce::String (worstMs.load(), 2) + " ms");
            expect (whileBlocked > 20, "the audio thread must keep running while the backend blocks");
            expect (worstMs.load() < 50.0, "no audio block may wait on the AI path");
            expectEquals (nonFinite.load(), 0);
            expect (h.p.readBand (2).on);
        }

        beginTest ("Closing the plugin mid-request is prompt");
        {
            std::atomic<bool> entered { false };
            auto p = std::make_unique<MixMindProcessor>();
            p->setAiBackend ([&entered] (const juce::String&, juce::String&, const std::function<bool()>& cancel)
            {
                entered = true;
                for (int i = 0; i < 3000 && ! cancel(); ++i) juce::Thread::sleep (10);
                return juce::Result::fail ("cancelled");
            });
            p->submitAiRequest ("slow");
            expect (pumpUntil ([&] { return entered.load(); }, 5000));
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            p.reset();
            expect (juce::Time::getMillisecondCounterHiRes() - t0 < 1000.0);
        }
    }

private:
    struct Harness
    {
        Harness()
        {
            p.setRateAndBufferSizeDetails (kFs, 512);
            p.prepareToPlay (kFs, 512);
        }

        std::vector<float> play (const std::vector<float>& l, const std::vector<float>& r)
        {
            std::vector<float> out (l.size(), 0.0f);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            for (size_t i = 0; i < l.size(); i += 512)
            {
                const int n = (int) juce::jmin ((size_t) 512, l.size() - i);
                buf.setSize (2, n, false, false, true);
                buf.copyFrom (0, 0, l.data() + i, n);
                buf.copyFrom (1, 0, r.data() + i, n);
                p.processBlock (buf, midi);
                std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + n, out.begin() + (long) i);
            }
            return out;
        }

        MixMindProcessor p;
    };
};

static AiTests aiTests;
