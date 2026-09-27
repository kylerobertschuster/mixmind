#include "AiFirewall.h"
#include <algorithm>
#include <cmath>

namespace
{
    // Numbers only: a JSON bool or string is never silently read as a number.
    bool readNumber (const juce::var& v, double& out)
    {
        if (! (v.isInt() || v.isInt64() || v.isDouble())) return false;
        out = (double) v;
        return true;
    }

    juce::String hzText (double hz) { return juce::String (hz, hz < 100.0 ? 1 : 0) + " Hz"; }

    bool isDigit (char c)     { return c >= '0' && c <= '9'; }
    bool isSpace (char c)     { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
    bool endsNumber (char c)  { return c == 0 || c == ',' || c == '}' || c == ']' || isSpace (c); }

    // juce::JSON is lenient where a firewall cannot be: "-x" and "-.5" parse as
    // numbers (-72 and 15), integers past int64 wrap to the wrong sign, "- 5"
    // and single-quoted strings pass, text after the object is ignored, and
    // nesting recurses without limit. So the reply is checked against strict
    // JSON (RFC 8259 numbers and literals) first; JUCE only parses what passed.
    juce::Result checkStrictJson (const char* s)
    {
        constexpr int kMaxDepth  = 8;    // the schema needs 3
        constexpr int kMaxDigits = 15;   // integer part; keeps JUCE's int64 accumulator exact

        int depth = 0;
        bool started = false, closed = false;
        while (*s != 0)
        {
            const char c = *s;
            if (isSpace (c)) { ++s; continue; }
            if (closed)             return juce::Result::fail ("text after the JSON object");
            if (! started && c != '{') return juce::Result::fail ("expected a JSON object");
            started = true;

            if (c == '{' || c == '[')
            {
                if (++depth > kMaxDepth) return juce::Result::fail ("nested too deeply");
                ++s;
            }
            else if (c == '}' || c == ']')
            {
                if (--depth < 0) return juce::Result::fail ("unbalanced brackets");
                closed = depth == 0;
                ++s;
            }
            else if (c == ':' || c == ',')
            {
                ++s;
            }
            else if (c == '"')
            {
                for (++s;; ++s)
                {
                    if (*s == 0)                   return juce::Result::fail ("unterminated string");
                    if ((unsigned char) *s < 0x20) return juce::Result::fail ("control character in a string");
                    if (*s == '\\')
                    {
                        if (*++s == 0) return juce::Result::fail ("unterminated string");
                        continue;   // the escaped character belongs to the string, even a quote
                    }
                    if (*s == '"') { ++s; break; }
                }
            }
            else if (c == '-' || isDigit (c))
            {
                const char* p = s + (c == '-' ? 1 : 0);
                const char* integer = p;
                if (*p == '0')         ++p;
                else if (isDigit (*p)) while (isDigit (*p)) ++p;
                else                   return juce::Result::fail ("malformed number");
                if (p - integer > kMaxDigits) return juce::Result::fail ("number has too many digits");
                if (*p == '.')
                {
                    if (! isDigit (*++p)) return juce::Result::fail ("malformed number");
                    while (isDigit (*p)) ++p;
                }
                if (*p == 'e' || *p == 'E')
                {
                    ++p;
                    if (*p == '+' || *p == '-') ++p;
                    if (! isDigit (*p)) return juce::Result::fail ("malformed number");
                    while (isDigit (*p)) ++p;
                }
                if (! endsNumber (*p)) return juce::Result::fail ("malformed number");
                s = p;
            }
            else if (c >= 'a' && c <= 'z')
            {
                const char* p = s;
                while (*p >= 'a' && *p <= 'z') ++p;
                const juce::String word (s, (size_t) (p - s));
                if (word != "true" && word != "false" && word != "null")
                    return juce::Result::fail ("unexpected \"" + word + "\"");
                s = p;
            }
            else
            {
                return juce::Result::fail ("unexpected character");
            }
        }
        return closed ? juce::Result::ok() : juce::Result::fail ("incomplete JSON");
    }
}

AiFirewall::AiFirewall (const juce::Array<juce::AudioProcessorParameter*>& list)
{
    for (auto* p : list)
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (ranged == nullptr) continue;   // no ID, so not addressable

        ParamInfo info { ranged->getParameterID(), p->getParameterIndex(), Kind::continuous,
                         ranged->getNormalisableRange(), {} };
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
        {
            info.kind = Kind::choice;
            for (const auto& name : choice->choices)
                info.choiceKeys.add (choiceKey (name));
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
        {
            info.kind = Kind::toggle;
        }
        params.push_back (std::move (info));
    }
    jassert ((int) params.size() <= AiPayload::kMaxChanges);   // grow kMaxChanges with the parameter list
}

juce::String AiFirewall::choiceKey (const juce::String& s)
{
    return s.toLowerCase().removeCharacters (" _-");
}

const AiFirewall::ParamInfo* AiFirewall::find (const juce::String& id) const
{
    for (const auto& p : params)
        if (p.id == id) return &p;
    return nullptr;
}

juce::Result AiFirewall::validate (const juce::String& json, AiPayload& out) const
{
    out = {};
    if (json.getNumBytesAsUTF8() > (size_t) kMaxTextBytes)
        return juce::Result::fail ("reply is larger than " + juce::String (kMaxTextBytes / 1024) + " KB");

    const auto strict = checkStrictJson (json.toRawUTF8());
    if (strict.failed())
        return juce::Result::fail ("reply is not valid JSON (" + strict.getErrorMessage() + ")");

    juce::var parsed;
    const auto parse = juce::JSON::parse (json, parsed);
    if (parse.failed())
        return juce::Result::fail ("reply is not valid JSON (" + parse.getErrorMessage() + ")");
    return validate (parsed, out);
}

juce::Result AiFirewall::validate (const juce::var& payload, AiPayload& out) const
{
    out = {};
    auto* object = payload.getDynamicObject();
    if (object == nullptr)
        return juce::Result::fail ("expected a JSON object with \"parameters\" and/or \"trace\"");

    AiPayload p;
    for (const auto& field : object->getProperties())
    {
        if (field.name == juce::Identifier ("parameters"))
        {
            auto* changes = field.value.getDynamicObject();
            if (changes == nullptr)
                return juce::Result::fail ("\"parameters\" must be an object of { \"<parameter ID>\": value }");

            for (const auto& entry : changes->getProperties())
            {
                const auto* info = find (entry.name.toString());
                if (info == nullptr)
                    return juce::Result::fail (entry.name.toString() + ": not a parameter");
                if (p.numChanges >= AiPayload::kMaxChanges)
                    return juce::Result::fail ("too many parameter changes");

                bool clamped = false;
                const auto r = readParameter (*info, entry.value, p.changes[(size_t) p.numChanges], clamped);
                if (r.failed()) return r;
                ++p.numChanges;
                if (clamped) ++p.numClamped;
            }
        }
        else if (field.name == juce::Identifier ("trace"))
        {
            const auto r = readTrace (field.value, p);
            if (r.failed()) return r;
        }
        else
        {
            return juce::Result::fail ("unknown field \"" + field.name.toString()
                                       + "\" (allowed: \"parameters\", \"trace\")");
        }
    }

    if (p.numChanges == 0 && ! p.setsTrace)
        return juce::Result::fail ("nothing to apply");

    out = p;
    return juce::Result::ok();
}

juce::Result AiFirewall::readParameter (const ParamInfo& info, const juce::var& value,
                                        AiPayload::Change& change, bool& clamped) const
{
    const auto fail = [&info] (const juce::String& why) { return juce::Result::fail (info.id + ": " + why); };

    double v = 0.0;
    if (info.kind == Kind::choice && value.isString())
    {
        const int i = info.choiceKeys.indexOf (choiceKey (value.toString()));
        if (i < 0) return fail ("unknown choice \"" + value.toString() + "\"");
        v = i;
    }
    else if (value.isBool())
    {
        if (info.kind != Kind::toggle) return fail ("expected a number");
        v = (bool) value ? 1.0 : 0.0;
    }
    else if (! readNumber (value, v))
    {
        return fail (info.kind == Kind::choice ? "expected a choice name or index" : "expected a number");
    }

    if (! std::isfinite (v))
        return fail ("not a finite number");

    const double lo = info.range.start, hi = info.range.end;
    float legal = 0.0f;
    if (info.kind == Kind::continuous)
    {
        clamped = v < lo || v > hi;
        legal = info.range.snapToLegalValue ((float) juce::jlimit (lo, hi, v));
    }
    else
    {
        // An index or a switch is not a quantity: clamping "24" (meaning 24 dB/oct)
        // to the last slope would silently pick the wrong setting, so anything
        // but an exact legal index is refused.
        if (! juce::exactlyEqual (std::round (v), v) || v < lo || v > hi)
            return fail ("not a valid " + juce::String (info.kind == Kind::toggle ? "switch value (use true / false)"
                                                                                   : "choice index"));
        legal = (float) v;
    }

    const float normalised = info.range.convertTo0to1 (legal);
    if (! std::isfinite (normalised))
        return fail ("not a finite number");

    change = { info.index, juce::jlimit (0.0f, 1.0f, normalised) };
    return juce::Result::ok();
}

juce::Result AiFirewall::readTrace (const juce::var& value, AiPayload& p)
{
    auto* points = value.getArray();
    if (points == nullptr)
        return juce::Result::fail ("\"trace\" must be an array of [hz, dB] points");
    if (points->size() > AiPayload::kMaxTracePoints)
        return juce::Result::fail ("trace has more than " + juce::String (AiPayload::kMaxTracePoints) + " points");
    if (points->size() == 1)
        return juce::Result::fail ("a trace needs at least two points ([] clears it)");

    std::array<ShaperProcessor::TracePoint, AiPayload::kMaxTracePoints> pts {};
    int n = 0, clamped = 0;
    for (const auto& item : *points)
    {
        const auto* pair = item.getArray();
        double hz = 0.0, db = 0.0;
        if (pair == nullptr || pair->size() != 2 || ! readNumber ((*pair)[0], hz) || ! readNumber ((*pair)[1], db))
            return juce::Result::fail ("trace point " + juce::String (n + 1) + ": expected [hz, dB]");
        if (! std::isfinite (hz) || ! std::isfinite (db))
            return juce::Result::fail ("trace point " + juce::String (n + 1) + ": not a finite number");

        if (hz < kTraceMinHz || hz > kTraceMaxHz || db < kTraceMinDb || db > kTraceMaxDb) ++clamped;
        pts[(size_t) n++] = { (float) juce::jlimit ((double) kTraceMinHz, (double) kTraceMaxHz, hz),
                              (float) juce::jlimit ((double) kTraceMinDb, (double) kTraceMaxDb, db) };
    }

    std::sort (pts.begin(), pts.begin() + n, [] (const auto& a, const auto& b) { return a.hz < b.hz; });

    // Points on (nearly) the same frequency make the curve a step with no
    // defined level — including points that clamping moved onto the band edge.
    for (int i = 1; i < n; ++i)
        if (pts[(size_t) i].hz < pts[(size_t) i - 1].hz * kTraceMinSpacing)
            return juce::Result::fail ("trace points at " + hzText (pts[(size_t) i - 1].hz) + " and "
                                       + hzText (pts[(size_t) i].hz) + " are less than 1 % apart");

    p.trace = pts;
    p.numTracePoints = n;
    p.setsTrace = true;
    p.numClamped += clamped;
    return juce::Result::ok();
}
