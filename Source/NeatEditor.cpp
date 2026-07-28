#include "NeatEditor.h"

NeatEditor::NeatEditor(NeatProcessor& p) : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&laf); JP::setTheme(juce::Colour(0xff22d3ee));
    setSize(680,420); setResizable(true,true); setResizeLimits(480,300,1200,700);

    titleLabel.setText("JuicePipe - Neat Neat Neat",juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions("Helvetica Neue",12,juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId,JP::text.withAlpha(0.5f));
    addAndMakeVisible(titleLabel);
    licenseButton.setColour(juce::TextButton::buttonColourId,JP::surfaceRaised);
    licenseButton.setColour(juce::TextButton::textColourOffId,JP::text);
    licenseButton.onClick=[this]{showLicenseDialog();}; updateLicenseDisplay(); addAndMakeVisible(licenseButton);

    auto mk=[&](juce::Slider& s,juce::Label& l,const juce::String& n,float mn,float mx,float d,float st){
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag); s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,40,14);
        s.setRange(mn,mx,st); s.setValue(d);
        s.setColour(juce::Slider::rotarySliderFillColourId,JP::accent);
        s.setColour(juce::Slider::thumbColourId,JP::accent);
        s.setColour(juce::Slider::textBoxTextColourId,JP::text);
        s.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
        addAndMakeVisible(s); l.setText(n,juce::dontSendNotification);
        l.setFont(juce::FontOptions("Helvetica Neue",8,juce::Font::plain));
        l.setColour(juce::Label::textColourId,JP::textMuted);
        l.setJustificationType(juce::Justification::centred); addAndMakeVisible(l);
    };

    const char* names[]={"NEAT 1","NEAT 2","NEAT 3"};
    for(int e=0;e<3;++e){
        juce::String n(names[e]);
        mk(rpt[e],rptL[e],n+" REPEATS",1,16,proc.repeats[e],1);
        mk(decayS[e],decayL[e],n+" DECAY",0,0.95f,proc.decay[e],0.01f);
        mk(pitchS[e],pitchL[e],n+" PITCH",-12,12,proc.pitch[e],1);
        int ee=e;
        rpt[e].onValueChange=[this,ee]{proc.repeats[ee]=(float)rpt[ee].getValue();};
        decayS[e].onValueChange=[this,ee]{proc.decay[ee]=(float)decayS[ee].getValue();};
        pitchS[e].onValueChange=[this,ee]{proc.pitch[ee]=(float)pitchS[ee].getValue();};
    }
    mk(mixS,mixL,"MIX",0,1,0.5f,0.01f);
    mixS.onValueChange=[this]{proc.mix=(float)mixS.getValue();};

    if(!proc.licenseManager.isLicensed()) juce::Timer::callAfterDelay(500,[this]{showLicenseDialog();});
}

void NeatEditor::paint(juce::Graphics& g){
    g.fillAll(JP::bg); g.setColour(JP::surface);
    g.fillRect(juce::Rectangle<float>(0,0,(float)getWidth(),(float)JP::headerH));
    g.setColour(JP::border); g.drawLine(0,(float)JP::headerH,(float)getWidth(),(float)JP::headerH,1);
}

void NeatEditor::resized(){
    auto h=getLocalBounds().removeFromTop(JP::headerH);
    titleLabel.setBounds(h.withLeft(14).withWidth(300));
    licenseButton.setBounds(h.withLeft(getWidth()-130).withWidth(110).withHeight(26).withY(7));
    auto a=getLocalBounds().withTrimmedTop(JP::headerH+8).reduced(8);
    int w=(a.getWidth()-32)/4;
    auto pl=[&](int c,juce::Slider& s,juce::Label& l){int x=8+c*(w+8); s.setBounds(x,a.getY()+12,w,w); l.setBounds(x,a.getY()+w+12,w,14);};
    pl(0,rpt[0],rptL[0]); pl(1,decayS[0],decayL[0]); pl(2,pitchS[0],pitchL[0]);
    int y2=a.getY()+12+w+36;
    auto pl2=[&](int c,juce::Slider& s,juce::Label& l){int x=8+c*(w+8); s.setBounds(x,y2,w,w); l.setBounds(x,y2+w+12,w,14);};
    pl2(0,rpt[1],rptL[1]); pl2(1,decayS[1],decayL[1]); pl2(2,pitchS[1],pitchL[1]);
    int y3=y2+w+36;
    auto pl3=[&](int c,juce::Slider& s,juce::Label& l){int x=8+c*(w+8); s.setBounds(x,y3,w,w); l.setBounds(x,y3+w+12,w,14);};
    pl3(0,rpt[2],rptL[2]); pl3(1,decayS[2],decayL[2]); pl3(2,pitchS[2],pitchL[2]);
    pl3(3,mixS,mixL);
}

void NeatEditor::showLicenseDialog(){
    auto& lm=proc.licenseManager;
    juce::String msg=lm.isLicensed()?"Licensed. Enter new key:":juce::String(lm.getFreePromptsRemaining())+" free prompts. Paste key:";
    auto*w=new juce::AlertWindow("License",msg,juce::AlertWindow::QuestionIcon,this);
    w->addTextEditor("key",lm.getLicenseKey(),"MM-XXXXXXXXXXXXXXXX"); w->addButton("Activate",1); w->addButton("Cancel",0);
    w->enterModalState(true,juce::ModalCallbackFunction::create([this,w](int r){
        if(r==1){auto k=w->getTextEditorContents("key").trim(); if(k.isNotEmpty()){proc.licenseManager.setLicenseKey(k);updateLicenseDisplay();}}
        delete w;
    })); lm.markWelcomeShown();
}

void NeatEditor::updateLicenseDisplay(){
    if(proc.licenseManager.isLicensed()) licenseButton.setButtonText("LICENSED");
    else licenseButton.setButtonText(juce::String(proc.licenseManager.getFreePromptsRemaining())+" FREE");
}
