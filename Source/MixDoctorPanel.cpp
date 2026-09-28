#include "MixDoctorPanel.h"
#include "LookAndFeel.h"

namespace
{
    // The UI's face where it is installed (macOS). Elsewhere TextLayout would
    // fall back to whatever face comes first, often a monospaced one, so
    // pick a common sans instead.
    const juce::String& reportFace()
    {
        static const juce::String face = []
        {
            const auto installed = juce::Font::findAllTypefaceNames();
            for (auto* name : { "Helvetica Neue", "Helvetica", "Segoe UI", "Arial", "Liberation Sans", "DejaVu Sans" })
                if (installed.contains (name)) return juce::String (name);
            return juce::String ("Helvetica Neue");
        }();
        return face;
    }

    juce::Font uiFont (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (reportFace(), size, bold ? juce::Font::bold : juce::Font::plain));
    }

    void styleButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId,  JP::surfaceRaised);
        b.setColour (juce::TextButton::textColourOffId, JP::textMuted);
        b.setColour (juce::TextButton::textColourOnId,  JP::accent);
    }
}

namespace MixDoctorUi
{
    juce::Colour colourFor (MixDoctor::Severity s)
    {
        switch (s)
        {
            case MixDoctor::Severity::high:    return JP::error;
            case MixDoctor::Severity::medium:  return JP::warning;
            case MixDoctor::Severity::low:     return JP::pastel[2];
            case MixDoctor::Severity::healthy: break;
        }
        return JP::pastel[3];
    }

    juce::String summarise (const MixDoctor::Report& r)
    {
        int critical = 0, moderate = 0, healthy = 0;
        for (const auto& f : r.findings)
        {
            if (f.severity == MixDoctor::Severity::high)         ++critical;
            else if (f.severity == MixDoctor::Severity::healthy) ++healthy;
            else                                                 ++moderate;
        }
        juce::StringArray parts;
        if (critical > 0) parts.add (juce::String (critical) + " critical");
        if (moderate > 0) parts.add (juce::String (moderate) + " moderate");
        if (healthy > 0)  parts.add (juce::String (healthy) + " healthy");
        return parts.isEmpty() ? juce::String ("nothing flagged") : parts.joinIntoString (", ");
    }
}

// ── Bar ─────────────────────────────────────────────────────────────────────

MixDoctorBar::MixDoctorBar()
{
    setComponentID ("mixDoctorBar");

    title.setText ("MIX DOCTOR", juce::dontSendNotification);
    title.setFont (juce::FontOptions ("Helvetica Neue", 12.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, JP::text);
    addAndMakeVisible (title);

    for (auto* b : { &runButton, &reportButton, &copyButton })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }
    runButton.setComponentID ("mixDoctorRun");
    runButton.setColour (juce::TextButton::textColourOffId, JP::text);
    runButton.onClick = [this] { if (onRunClicked) onRunClicked(); };

    reportButton.setComponentID ("mixDoctorReportToggle");
    reportButton.setTooltip ("Show or hide the report.");
    reportButton.onClick = [this] { if (onReportClicked) onReportClicked(); };

    copyButton.setComponentID ("mixDoctorCopy");
    copyButton.setTooltip ("Copy the report as text (Markdown) to paste into notes or a message.");
    copyButton.onClick = [this] { if (onCopyClicked) onCopyClicked(); };

    status.setComponentID ("mixDoctorStatus");
    status.setFont (juce::FontOptions ("Helvetica Neue", 12.0f, juce::Font::plain));
    status.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (status);

    setRunning (false);
    setCanCopy (false);
}

void MixDoctorBar::setRunning (bool running)
{
    runButton.setButtonText (running ? "STOP" : "RUN MIX DOCTOR");
    runButton.setToggleState (running, juce::dontSendNotification);
    runButton.setTooltip (running ? "Stop listening and keep this report."
                                  : "Start listening, then play the mix (a whole song or a section).\n"
                                    "Mix Doctor observes the output from now until you stop it.");
}

void MixDoctorBar::setReportOpen (bool open) { reportButton.setToggleState (open, juce::dontSendNotification); }
void MixDoctorBar::setCanCopy (bool can)     { copyButton.setEnabled (can); }

void MixDoctorBar::setStatus (const juce::String& text, juce::Colour colour)
{
    status.setText (text, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, colour);
    status.setTooltip (text);
}

void MixDoctorBar::paint (juce::Graphics& g)
{
    g.fillAll (JP::surface);
    g.setColour (JP::border);
    g.drawLine (0.0f, 0.5f, (float) getWidth(), 0.5f, 1.0f);
}

void MixDoctorBar::resized()
{
    auto r = getLocalBounds().reduced (12, juce::jmax (0, (getHeight() - 24) / 2));
    title.setBounds (r.removeFromLeft (84));
    r.removeFromLeft (6);
    runButton.setBounds (r.removeFromLeft (128));
    r.removeFromLeft (12);
    copyButton.setBounds (r.removeFromRight (58));
    r.removeFromRight (6);
    reportButton.setBounds (r.removeFromRight (76));
    r.removeFromRight (12);
    status.setBounds (r);
}

// ── Report ──────────────────────────────────────────────────────────────────

MixDoctorReportView::MixDoctorReportView()
{
    setComponentID ("mixDoctorReport");
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

void MixDoctorReportView::setReport (const MixDoctor::Report& r)
{
    auto text = r.toMarkdown();
    report = r;
    if (showing && text == shownText) return;
    showing = true;
    shownText = std::move (text);
    rebuild();
}

void MixDoctorReportView::clearReport (const juce::String& newIntro)
{
    if (! showing && newIntro == intro && ! blocks.empty()) return;
    showing = false;
    report = {};
    shownText.clear();
    intro = newIntro;
    rebuild();
}

void MixDoctorReportView::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedLeft (1));
    rebuild();
}

void MixDoctorReportView::paint (juce::Graphics& g)
{
    g.fillAll (JP::surface);
    g.setColour (JP::border);
    g.drawLine (0.5f, 0.0f, 0.5f, (float) getHeight(), 1.0f);
}

void MixDoctorReportView::rebuild()
{
    using MixDoctor::Severity;
    blocks.clear();

    const int   width = juce::jmax (160, getWidth() - 1 - viewport.getScrollBarThickness());
    const float pad = 12.0f, inner = (float) width - 2.0f * pad;
    float y = pad;

    // A card has a coloured stripe, 14 px of text inset on the left and 8 on the right.
    const auto add = [&] (juce::AttributedString s, juce::Colour stripe, float gapAfter)
    {
        const bool card = ! stripe.isTransparent();
        s.setWordWrap (juce::AttributedString::byWord);
        Block b;
        b.stripe = stripe;
        b.text.createLayout (s, card ? inner - 22.0f : inner);
        const float h = std::ceil (b.text.getHeight()) + (card ? 18.0f : 0.0f);
        b.area = { pad, y, inner, h };
        y += h + gapAfter;
        blocks.push_back (std::move (b));
    };
    const auto heading = [&] (const juce::String& text)
    {
        juce::AttributedString s;
        s.append (text, uiFont (10.5f, true), JP::textMuted);
        add (s, juce::Colours::transparentBlack, 6.0f);
    };

    {
        juce::AttributedString s;
        s.append ("Mix Doctor report\n", uiFont (15.0f, true), JP::text);
        juce::String line;
        if (! showing)
            line = intro;
        else if (! report.ready)
            line = report.notReadyReason;
        else
            line = "Heard " + juce::String (report.observedSeconds, 1) + " s of signal"
                 + (report.referenceName.isNotEmpty()
                        ? ", compared with \"" + report.referenceName + "\"."
                        : juce::String (", no reference loaded (tone, density and loudness need one)."));
        s.append (line, uiFont (12.0f), JP::text.withAlpha (0.7f));
        add (s, juce::Colours::transparentBlack, 14.0f);
    }

    if (showing && report.ready)
    {
        const auto group = [&] (const char* name, auto pick)
        {
            bool first = true;
            for (const auto& f : report.findings)
            {
                if (! pick (f.severity)) continue;
                if (first) { heading (name); first = false; }

                juce::AttributedString s;
                const auto colour = MixDoctorUi::colourFor (f.severity);
                if (f.severity == Severity::healthy)
                {
                    s.append (f.title, uiFont (12.0f), JP::text.withAlpha (0.85f));
                    s.append ("  (" + MixDoctor::toString (f.confidence) + " confidence)", uiFont (11.0f), JP::textMuted);
                    add (s, colour.withAlpha (0.6f), 6.0f);
                    continue;
                }
                const auto body = uiFont (12.0f), label = uiFont (12.0f, true);
                const auto soft = JP::text.withAlpha (0.7f);
                s.append (f.title + "\n", uiFont (13.5f, true), JP::text);
                s.append (MixDoctor::toString (f.severity) + " severity  |  "
                              + MixDoctor::toString (f.confidence) + " confidence\n", uiFont (11.0f, true), colour);
                s.append (f.observation + "\n", body, JP::text.withAlpha (0.9f));
                s.append ("Why it matters: ", label, soft);
                s.append (f.impact + "\n", body, soft);
                if (! f.potentialCauses.isEmpty())
                {
                    s.append ("Possible causes: ", label, soft);
                    s.append (f.potentialCauses.joinIntoString ("; ") + "\n", body, soft);
                }
                s.append ("Try first: ", label, JP::accent);
                s.append (f.action, body, JP::text);
                add (s, colour, 8.0f);
            }
            if (! first) y += 8.0f;
        };
        group ("CRITICAL", [] (Severity s) { return s == Severity::high; });
        group ("MODERATE", [] (Severity s) { return s == Severity::medium || s == Severity::low; });
        group ("HEALTHY",  [] (Severity s) { return s == Severity::healthy; });

        juce::AttributedString s;
        s.append ("Findings come from the mix bus, so they point at ranges and possible causes, not tracks. "
                  "Confidence grows with listening.", uiFont (11.0f), JP::textMuted);
        add (s, juce::Colours::transparentBlack, 0.0f);
    }

    content.setSize (width, (int) std::ceil (y + pad));
    content.repaint();
}

void MixDoctorReportView::Content::paint (juce::Graphics& g)
{
    const auto clip = g.getClipBounds().toFloat();
    for (const auto& b : blocks)
    {
        if (! b.area.intersects (clip)) continue;
        auto textArea = b.area;
        if (! b.stripe.isTransparent())
        {
            g.setColour (JP::surfaceRaised);
            g.fillRoundedRectangle (b.area, 6.0f);
            g.setColour (b.stripe);
            g.fillRoundedRectangle (b.area.withWidth (3.0f), 1.5f);
            textArea = b.area.reduced (0.0f, 9.0f).withTrimmedLeft (14.0f).withTrimmedRight (8.0f);
        }
        b.text.draw (g, textArea);
    }
}
