#include "ThreeFXEditor.h"

ThreeFXEditor::ThreeFXEditor(ThreeFXProcessor& p) : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&laf); JP::setAccent(juce::Colour(0xffffff8d));
    setSize(650,380); setResizable(true,true); setResizeLimits(450,280,1100,600);

    titleLabel.setText("JuicePipe - 3FX",juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions("Helvetica Neue",12,juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId,JP::text.withAlpha(0.5f));
    addAndMakeVisible(titleLabel);
    licenseButton.setColour(juce::TextButton::buttonColourId,JP::surfaceRaised);
    licenseButton.setColour(juce::TextButton::textColourOffId,JP::text);
    licenseButton.onClick=[this]{showLicenseDialog();}; updateLicenseDisplay(); addAndMakeVisible(licenseButton);

    auto mkLabel=[&](juce::Label& l){l.setFont(juce::FontOptions("Helvetica Neue",9,juce::Font::bold));l.setColour(juce::Label::textColourId,JP::text);addAndMakeVisible(l);};
    mkLabel(phaseLabel); mkLabel(flangeLabel); mkLabel(delayLabel);

    auto mk=[&](juce::Slider& s,juce::Label& l,const juce::String& n,float mn,float mx,float d,float st){
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag); s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,44,14);
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
    mk(pRate,pRateL,"PHASE RATE",0.02f,3,0.5f,0.01f);
    mk(pDepth,pDepthL,"PHASE DEPTH",0,1,0.5f,0.01f);
    mk(fRate,fRateL,"FLANGE RATE",0.02f,3,0.3f,0.01f);
    mk(fDepth,fDepthL,"FLANGE DEPTH",0,1,0.4f,0.01f);
    mk(dTime,dTimeL,"DELAY TIME",0.01f,1,0.3f,0.01f);
    mk(dFb,dFbL,"DELAY FDBK",0,0.9f,0.4f,0.01f);
    mk(dMix,dMixL,"MIX",0,1,0.5f,0.01f);

    pRate.onValueChange=[this]{proc.phaseRate=(float)pRate.getValue();};
    pDepth.onValueChange=[this]{proc.phaseDepth=(float)pDepth.getValue();};
    fRate.onValueChange=[this]{proc.flangeRate=(float)fRate.getValue();};
    fDepth.onValueChange=[this]{proc.flangeDepth=(float)fDepth.getValue();};
    dTime.onValueChange=[this]{proc.delayTime=(float)dTime.getValue();};
    dFb.onValueChange=[this]{proc.delayFb=(float)dFb.getValue();};
    dMix.onValueChange=[this]{proc.mix=(float)dMix.getValue();};

    if(!proc.licenseManager.isLicensed()) juce::Timer::callAfterDelay(500,[this]{showLicenseDialog();});
}

void ThreeFXEditor::paint(juce::Graphics& g){
    g.fillAll(JP::bg); g.setColour(JP::surface);
    g.fillRect(juce::Rectangle<float>(0,0,(float)getWidth(),(float)JP::headerH));
    g.setColour(JP::border); g.drawLine(0,(float)JP::headerH,(float)getWidth(),(float)JP::headerH,1);
}

void ThreeFXEditor::resized(){
    auto h=getLocalBounds().removeFromTop(JP::headerH);
    titleLabel.setBounds(h.withLeft(14).withWidth(260));
    licenseButton.setBounds(h.withLeft(getWidth()-130).withWidth(110).withHeight(26).withY(7));
    auto a=getLocalBounds().withTrimmedTop(JP::headerH+8).reduced(12);
    int w=(a.getWidth()-48)/5;
    int rowH=(a.getHeight()-16)/3;
    int y0=a.getY()+8;
    auto pl=[&](int row,int c,juce::Slider& s,juce::Label& l){
        int x=12+c*(w+12);
        s.setBounds(x,y0+row*(rowH-4),w,w);
        l.setBounds(x,y0+row*(rowH-4)+w+4,w,14);
    };
    pl(0,0,pRate,pRateL); pl(0,1,pDepth,pDepthL); phaseLabel.setBounds(12,y0,w,14);
    pl(1,0,fRate,fRateL); pl(1,1,fDepth,fDepthL); flangeLabel.setBounds(12,y0+rowH-4,w,14);
    pl(2,0,dTime,dTimeL); pl(2,1,dFb,dFbL); pl(2,2,dMix,dMixL); delayLabel.setBounds(12,y0+2*(rowH-4),w,14);
}

void ThreeFXEditor::showLicenseDialog(){
    auto& lm=proc.licenseManager;
    juce::String msg=lm.isLicensed()?"Licensed. Enter new key:":juce::String(lm.getFreePromptsRemaining())+" free prompts. Paste key:";
    auto*w=new juce::AlertWindow("License",msg,juce::AlertWindow::QuestionIcon,this);
    w->addTextEditor("key",lm.getLicenseKey(),"MM-XXXXXXXXXXXXXXXX"); w->addButton("Activate",1); w->addButton("Cancel",0);
    w->enterModalState(true,juce::ModalCallbackFunction::create([this,w](int r){
        if(r==1){auto k=w->getTextEditorContents("key").trim(); if(k.isNotEmpty()){proc.licenseManager.setLicenseKey(k);updateLicenseDisplay();}}
        delete w;
    })); lm.markWelcomeShown();
}

void ThreeFXEditor::updateLicenseDisplay(){
    if(proc.licenseManager.isLicensed()) licenseButton.setButtonText("LICENSED");
    else licenseButton.setButtonText(juce::String(proc.licenseManager.getFreePromptsRemaining())+" FREE");
}
