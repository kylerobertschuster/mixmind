#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>
#include "PluginProcessor.h"
#include "TelemetryCanvas.h"
#include "FocusModel.h"
#include "LookAndFeel.h"
#include "MixDoctorPanel.h"

// A TextButton whose secondary (right / ctrl) click runs onContextClick
// instead of onClick.
class ContextTextButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onContextClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onContextClick) onContextClick(); return; }
        juce::TextButton::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        juce::TextButton::mouseUp (e);
    }
};

class MixMindEditor : public juce::AudioProcessorEditor,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    explicit MixMindEditor (MixMindProcessor&);
    ~MixMindEditor() override;

    void paint  (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // The GPU context is attached from construction; it only starts once the
    // editor is on screen.
    bool rendersWithOpenGL() const noexcept { return openGLContext.getTargetComponent() == this; }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true;  repaint(); }
    void fileDragExit (const juce::StringArray&) override            { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    MixMindProcessor& audioProcessor;
    JuicePipeLAF      laf;

    // Renders the editor (and every child) on the GPU; repaints stay
    // event-driven (the canvas timer), not continuous.
    juce::OpenGLContext openGLContext;
    juce::TooltipWindow tooltips { this, 600 };

    TelemetryCanvas   telemetry;

    // Mix Doctor: the run lives in the processor; these show it.
    MixDoctorBar        doctorBar;
    MixDoctorReportView doctorReport;
    bool doctorReportOpen { false };
    int  doctorTick { 0 };

    // Header
    juce::Label       titleLabel;
    ContextTextButton loadRefButton { "LOAD REF" };
    ContextTextButton layerButton { "LAYER" };
    ContextTextButton traceButton { "TRACE" };
    juce::Slider      opacitySlider;
    juce::ComboBox    focusBox;
    ColorSwatch       colorSwatch;

    // Shaper controls (attached to the automatable parameters)
    juce::TextButton  shapeButton { "SHAPE" };
    juce::TextButton  modeButton  { "AUTO" };
    juce::TextButton  stereoButton { "LINK" };
    juce::Slider      amountSlider;
    std::unique_ptr<APVTS::ButtonAttachment> shapeAttachment, modeAttachment, stereoAttachment;
    std::unique_ptr<APVTS::SliderAttachment> amountAttachment;

    void chooseReference();
    void showReferenceMenu();
    void chooseImageLayer();
    void loadImageLayer (const juce::File&);
    void showLayerMenu();
    void setLayerVisible (bool);
    void applyFocusSelection();
    void writeBand (int band, const ParametricEq::Band&);
    void bandGesture (int band, bool starting);
    void refreshReference();
    void refreshMixDoctor();
    void toggleMixDoctorRun();
    void setMixDoctorReportOpen (bool);
    void timerCallback() override;

    std::unique_ptr<juce::FileChooser> chooser;
    int  seenRefVersion { -1 };
    int  seenTraceVersion { -1 };
    bool dragHover { false };
    int  dotPhase { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixMindEditor)
};
