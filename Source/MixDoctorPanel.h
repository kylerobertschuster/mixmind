#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MixDoctor.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Mix Doctor on screen (ADR-001): a bar along the bottom of the editor (run,
//  progress, headline) and the report beside the plot. Both are message-
//  thread components; they show copies handed to them on the editor's timer,
//  because paint() runs on the GL thread (with the message manager locked).
//  They present findings and never create or grade them (ADR-002).
// ─────────────────────────────────────────────────────────────────────────────

namespace MixDoctorUi
{
    juce::Colour colourFor (MixDoctor::Severity);
    // "1 critical, 2 moderate, 4 healthy" (or "nothing flagged").
    juce::String summarise (const MixDoctor::Report&);
}

class MixDoctorBar : public juce::Component
{
public:
    MixDoctorBar();

    std::function<void()> onRunClicked, onReportClicked, onCopyClicked;

    void setRunning (bool);
    void setReportOpen (bool);
    void setCanCopy (bool);
    void setStatus (const juce::String&, juce::Colour);
    juce::String getStatus() const { return status.getText(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label      title;
    juce::TextButton runButton { "RUN MIX DOCTOR" };
    juce::Label      status;
    juce::TextButton reportButton { "REPORT" };
    juce::TextButton copyButton { "COPY" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixDoctorBar)
};

class MixDoctorReportView : public juce::Component
{
public:
    MixDoctorReportView();

    // Message thread. Laid out once per change, not per paint.
    void setReport (const MixDoctor::Report&);
    void clearReport (const juce::String& intro);
    bool hasReport() const { return showing; }
    const MixDoctor::Report& getReport() const { return report; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Block
    {
        juce::Rectangle<float> area;
        juce::TextLayout text;
        juce::Colour stripe;   // transparent = plain text, no card
    };

    class Content : public juce::Component
    {
    public:
        explicit Content (const std::vector<Block>& b) : blocks (b) {}
        void paint (juce::Graphics&) override;
    private:
        const std::vector<Block>& blocks;
    };

    void rebuild();

    MixDoctor::Report report;
    bool showing { false };
    juce::String intro, shownText;
    std::vector<Block> blocks;
    Content content { blocks };
    juce::Viewport viewport;   // declared after `content`, so it goes first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixDoctorReportView)
};
