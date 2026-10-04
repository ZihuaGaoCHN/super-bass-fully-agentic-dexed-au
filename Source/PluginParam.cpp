/**
 *
 * Copyright (c) 2013-2024 Pascal Gauthier.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 *
 */

#include <time.h>
#include <stdlib.h>
#include <thread>

#include "PluginParam.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Dexed.h"
#include "state/SynthStateService.h"

// Async updater
class CtrlUpdate : public CallbackMessage {
    Ctrl *ctrl;
    float value;
public:
    CtrlUpdate(Ctrl *ctrl, float value) {
        this->ctrl = ctrl;
        this->value = value;
    }
    void messageCallback() {
        ctrl->publishValue(value);
    }
};

// ************************************************************************
//
Ctrl::Ctrl(String name) {
    label << name;
    slider = NULL;
    button = NULL;
    comboBox = NULL;
}

void Ctrl::bind(Slider *s) {
    slider = s;
    updateComponent();
    s->addListener(this);
    s->addMouseListener(this, true);
    s->setVelocityModeParameters (0.1, 1, 0.05, 1, ModifierKeys::shiftModifier);
    s->setTitle(label);
    s->textFromValueFunction = [this](double value) { return this->getValueDisplay(); };
    s->setWantsKeyboardFocus(true);
}

void Ctrl::bind(Button *b) {
    button = b;
    updateComponent();
    b->setTitle(label);
    b->addListener(this);
    b->addMouseListener(this, true);
}

void Ctrl::bind(ComboBox *c) {
    comboBox = c;
    updateComponent();
    c->setTitle(label);
    c->addListener(this);
    c->addMouseListener(this, true);
}

void Ctrl::unbind() {
    if (slider != NULL) {
        slider->removeListener(this);
        slider->removeMouseListener(this);
        slider = NULL;
    }

    if (button != NULL) {
        button->removeListener(this);
        button->removeMouseListener(this);
        button = NULL;
    }

    if (comboBox != NULL) {
        comboBox->removeListener(this);
        comboBox->removeMouseListener(this);
        comboBox = NULL;
    }
}

void Ctrl::publishValueAsync(float value) {
    CtrlUpdate *update = new CtrlUpdate(this, value);
    update->post();
}

void Ctrl::publishValue(float value) {
    parent->beginParameterChangeGesture(idx);
    parent->setParameterNotifyingHost(idx, value);
    parent->endParameterChangeGesture(idx);
}

void Ctrl::sliderValueChanged(Slider* moved) {
    publishValue(moved->getValue());
}

void Ctrl::buttonClicked(Button* clicked) {
    publishValue(clicked->getToggleState());
}

void Ctrl::comboBoxChanged(ComboBox* combo) {
    publishValue((combo->getSelectedId() - 1) / combo->getNumItems());
}

void Ctrl::mouseEnter(const juce::MouseEvent &event) {
    updateDisplayName();
}

void Ctrl::mouseDown(const juce::MouseEvent &event) {
    if ( event.mods.isPopupMenu()) {
        auto *editor = dynamic_cast<DexedAudioProcessorEditor*>(
            parent->getActiveEditor());
        if ( editor != nullptr )
            editor->discoverMidiCC(this);
    }
}

void Ctrl::updateDisplayName() {
}

float Ctrl::getPublishedHostValue() const {
    if (parent != nullptr && parent->hasAtomicParameterStore())
        return parent->getParameter(idx);
    return -1.0f;
}

// ************************************************************************
// Custom displays

class CtrlDXLabel : public CtrlDX {
   StringArray labels;
public:
    CtrlDXLabel(String name, int steps, int offset, StringArray &labels) : CtrlDX(name, steps, offset, 0) {
        this->labels = labels;
    };
    
    String getValueDisplay() {
        return labels[getPublishedValue()];
    }
};

class CtrlDXTranspose : public CtrlDX {
public:
    CtrlDXTranspose(String name, int steps, int offset) : CtrlDX(name, steps, offset, 0) {
    };
    
    String getValueDisplay() {
        String ret ;
        int value = getPublishedValue();
        return ret << (value - 24);
#if 0        
        switch(value % 12) {
            case 0: ret << "C"; break;
            case 1: ret << "C#"; break;
            case 2: ret << "D"; break;
            case 3: ret << "D#"; break;
            case 4: ret << "E"; break;
            case 5: ret << "F"; break;
            case 6: ret << "F#"; break;
            case 7: ret << "G"; break;
            case 8: ret << "G#"; break;
            case 9: ret << "A"; break;
            case 10: ret << "A#"; break;
            case 11: ret << "B"; break;
        }
        return ret << (value/12+1);
#endif        
    }
};

class CtrlDXSwitch : public CtrlDX {
public:
    CtrlDXSwitch(String name, int steps, int offset) : CtrlDX(name, steps, offset, 0) {
    };
    
    String getValueDisplay() {
        return getPublishedValue() ? String("ON") : String("OFF");
    }
};

class CtrlDXOpMode : public CtrlDX {
public:
    CtrlDXOpMode(String name, int steps, int offset) : CtrlDX(name, steps, offset, 0) {
    };
    
    String getValueDisplay() {
        return getPublishedValue() ? String("FIXED") : String("RATIO");
    }
};

class CtrlDXBreakpoint : public CtrlDX {
public:
    CtrlDXBreakpoint(String name, int steps, int offset) : CtrlDX(name, steps, offset, 0) {
    };
    
    String getValueDisplay() {
        const char *breakNames[] = {"A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#"};
        const auto value = getPublishedValue();
        String ret;
        ret << breakNames[value % 12] << (value + 9) / 12 - 1;
        return ret;
    }
};

class CtrlTune : public Ctrl {
public:
    DexedAudioProcessor *processor;
    
    CtrlTune(String name, DexedAudioProcessor *owner) : Ctrl(name) {
        processor = owner;
    }
    
    float getValueHost() {
        // meh. good enough for now
        int32_t tune = processor->controllers.masterTune / (1.0/12);
        tune = (tune >> 11) + 0x2000;
        return (float)tune / 0x4000;
    }
    
    void setValueHost(float v) {
        int32_t tune = (v * 0x4000) - 0x2000;
        processor->controllers.masterTune = ((float) (tune << 11)) * (1.0/12);
    }
    
    String getValueDisplay() {
        String display;
        const auto published = getPublishedHostValue();
        display << ((published >= 0.0f ? published : getValueHost()) * 2) - 1;
        return display;
    }
    
    void updateComponent() {
        if (slider != NULL) {
            const auto published = getPublishedHostValue();
            slider->setValue(
                published >= 0.0f ? published : getValueHost(), dontSendNotification);
        }
    }
};

class CtrlOpSwitch : public Ctrl {
    DexedAudioProcessor *processor;
    char *value;
public :
    CtrlOpSwitch(String name, char *switchValue, DexedAudioProcessor *owner) : Ctrl(name) {
        processor = owner;
        value = switchValue;
    }
    
    void setValueHost(float f) {
        if ( f == 0 )
            *value = '0';
        else
            *value = '1';
        updateDisplayName();
        
        // the value is based on the controller
        parent->setDxValue(155, -1);
    }
    
    float getValueHost() {
        if ( *value == '0' )
            return 0;
        else
            return 1;
    }
    
    String getValueDisplay() {
        const auto published = getPublishedHostValue();
        const auto enabled = published >= 0.0f ? published >= 0.5f : *value != '0';
        String ret;
        ret << label << " " << (enabled ? "ON" : "OFF");
        return ret;
    }
    
    void updateComponent() {
        if (button != NULL) {
            const auto published = getPublishedHostValue();
            const auto enabled = published >= 0.0f ? published >= 0.5f : *value != '0';
            button->setToggleState(enabled, dontSendNotification);
        }
    }
    
    void updateDisplayName() {
        DexedAudioProcessorEditor *editor = (DexedAudioProcessorEditor *) parent->getActiveEditor();
        if ( editor == NULL ) {
            return;
        }
        editor->setParameterMessage(getValueDisplay());
    }
};

class CtrlMonoPoly : public Ctrl {
    DexedAudioProcessor *processor;
public:
    CtrlMonoPoly(String name, DexedAudioProcessor *owner) : Ctrl(name) {
        processor = owner;
    }

    String getValueDisplay() {
        const auto published = getPublishedHostValue();
        const auto mono = published >= 0.0f ? published >= 0.5f : processor->isMonoMode();
        return mono ? String("MONO") : String("POLY");
    }

    float getValueHost() {
        return processor->isMonoMode() ? 1 : 0;
    }

    void setValueHost(float v) {
        processor->setMonoMode(v == 1);
    }

    void updateComponent() {
        if (button != NULL) {
            const auto published = getPublishedHostValue();
            button->setToggleState(
                published >= 0.0f ? published >= 0.5f : processor->isMonoMode(),
                dontSendNotification);
        }
    }
};

// ************************************************************************
// CtrlFloat - control float values
CtrlFloat::CtrlFloat(String name, float *storageValue) : Ctrl(name) {
    vPointer = storageValue;
}

float CtrlFloat::getValueHost() {
    return *vPointer;
}

void CtrlFloat::setValueHost(float v) {
    TRACE("float set idx=%d v=%f", idx, v);
    *vPointer = v;
}

String CtrlFloat::getValueDisplay() {
    String display;
    const auto published = getPublishedHostValue();
    display << (published >= 0.0f ? published : *vPointer);
    return display;
}

void CtrlFloat::updateComponent() {
    if (slider != NULL) {
        const auto published = getPublishedHostValue();
        slider->setValue(
            published >= 0.0f ? published : *vPointer, dontSendNotification);
    }
}

// ************************************************************************
// CtrlDX - control DX mapping
CtrlDX::CtrlDX(String name, int steps, int offset, int displayValue) : Ctrl(name) {
    this->displayValue = displayValue;
    this->steps = steps;
    dxValue = 0;
    dxOffset = offset;
}

float CtrlDX::getValueHost() {
    return getValue() / (float) steps;
}

void CtrlDX::setValueHost(float f) {
    setValue(roundToInt(f * steps));
}

void CtrlDX::setValue(int v) {
    TRACE("setting value idx=%d dxOffset=%d v=%d", idx, dxOffset, v);
    dxValue = v;
    if (dxOffset >= 0) {
        if (parent != NULL)
            parent->setDxValue(dxOffset, dxValue);
    }
}

int CtrlDX::getValue() {
    if (dxOffset >= 0)
        dxValue = parent->data[dxOffset];
    return dxValue;
}

int CtrlDX::getPublishedValue() {
    const auto published = getPublishedHostValue();
    return published >= 0.0f ? roundToInt(published * steps) : getValue();
}

int CtrlDX::getOffset() {
    return dxOffset;
}

String CtrlDX::getValueDisplay() {
    String ret;
    ret << (getPublishedValue() + displayValue);
    return ret;
}

void CtrlDX::updateDisplayName() {
    DexedAudioProcessorEditor *editor = (DexedAudioProcessorEditor *) parent->getActiveEditor();
    if ( editor == NULL ) {
        return;
    }
    String msg;
    msg << label << " = " << getValueDisplay();
    editor->setParameterMessage(msg);
}


void CtrlDX::publishValue(float value) {
    Ctrl::publishValue(value / steps);
    updateDisplayName();
}

void CtrlDX::sliderValueChanged(Slider* moved) {
    publishValue(((int) moved->getValue() - displayValue));
}

void CtrlDX::comboBoxChanged(ComboBox* combo) {
    publishValue(combo->getSelectedId() - 1);
}

void CtrlDX::buttonClicked(Button *button) {
    publishValue((int) button->getToggleState());
}

void CtrlDX::updateComponent() {
    const auto publishedValue = getPublishedValue();
    if (slider != NULL) {
        slider->setValue(publishedValue + displayValue,
                dontSendNotification);
    }

    if (button != NULL) {
        if (publishedValue == 0) {
            button->setToggleState(false, dontSendNotification);
        } else {
            button->setToggleState(true, dontSendNotification);
        }
    }

    if (comboBox != NULL) {
        int cvalue = publishedValue + 1;
        if (comboBox->getNumItems() <= cvalue) {
            cvalue = comboBox->getNumItems();
        }
        comboBox->setSelectedId(cvalue, dontSendNotification);
    }
}

/***************************************************************
 *
 */
void DexedAudioProcessor::initCtrl(bool backgroundOnly) {
    if (backgroundOnly)
        setupBuiltinCart();
    else
        setupStartupCart();
    currentProgram = 0;
    
    fxCutoff.reset(new CtrlFloat("Cutoff", &fx.uiCutoff));
    ctrl.add(fxCutoff.get());
    
    fxReso.reset(new CtrlFloat("Resonance", &fx.uiReso));
    ctrl.add(fxReso.get());
    
    output.reset(new CtrlFloat("Output", &fx.uiGain));
    ctrl.add(output.get());

    monoModeCtrl.reset(new CtrlMonoPoly("MonoMode", this));
    ctrl.add(monoModeCtrl.get());
    
    tune.reset(new CtrlTune("MASTER TUNE ADJ", this));
    ctrl.add(tune.get());
    
    algo.reset(new CtrlDX("ALGORITHM", 31, 134, 1));
    ctrl.add(algo.get());
    
    feedback.reset(new CtrlDX("FEEDBACK", 7, 135));
    ctrl.add(feedback.get());
    
    oscSync.reset(new CtrlDXSwitch("OSC KEY SYNC", 1, 136));
    ctrl.add(oscSync.get());
    
    lfoRate.reset(new CtrlDX("LFO SPEED", 99, 137));
    ctrl.add(lfoRate.get());
    
    lfoDelay.reset(new CtrlDX("LFO DELAY", 99, 138));
    ctrl.add(lfoDelay.get());
    
    lfoPitchDepth.reset(new CtrlDX("LFO PM DEPTH", 99, 139));
    ctrl.add(lfoPitchDepth.get());
    
    lfoAmpDepth.reset(new CtrlDX("LFO AM DEPTH", 99, 140));
    ctrl.add(lfoAmpDepth.get());
    
    lfoSync.reset(new CtrlDXSwitch("LFO KEY SYNC", 1, 141));
    ctrl.add(lfoSync.get());
    
    StringArray lbl;
    lbl.add("TRIANGE");
    lbl.add("SAW DOWN");
    lbl.add("SAW UP");
    lbl.add("SQUARE");
    lbl.add("SINE");
    lbl.add("S&HOLD");
    
    lfoWaveform.reset(new CtrlDXLabel("LFO WAVE", 5, 142, lbl));
    ctrl.add(lfoWaveform.get());
    
    transpose.reset(new CtrlDXTranspose("TRANSPOSE", 48, 144));
    ctrl.add(transpose.get());
    
    pitchModSens.reset(new CtrlDX("P MODE SENS.", 7, 143));
    ctrl.add(pitchModSens.get());
    
    for (int i=0;i<4;i++) {
        String rate;
        rate << "PITCH EG RATE " << (i+1);
        pitchEgRate[i].reset(new CtrlDX(rate, 99, 126+i));
        ctrl.add(pitchEgRate[i].get());
    }

    for (int i=0;i<4;i++) {
        String level;
        level << "PITCH EG LEVEL " << (i+1);
        pitchEgLevel[i].reset(new CtrlDX(level, 99, 130+i));
        ctrl.add(pitchEgLevel[i].get());
    }
    
    StringArray keyScaleLabels;
    keyScaleLabels.add("-LN");
    keyScaleLabels.add("-EX");
    keyScaleLabels.add("+EX");
    keyScaleLabels.add("+LN");
    
    // fill operator values;
    for (int i = 0; i < 6; i++) {
        //// In the Sysex, OP6 comes first, then OP5...
        int opTarget = (5-i) * 21;
        int opVal = i;
        String opName;
        opName << "OP" << (opVal + 1);

        for (int j = 0; j < 4; j++) {     
            String opRate;
            opRate << opName << " EG RATE " << (j + 1);
            opCtrl[opVal].egRate[j].reset(new CtrlDX(opRate, 99, opTarget + j));
            ctrl.add(opCtrl[opVal].egRate[j].get());
        }
    
        for (int j = 0; j < 4; j++) {        
            String opLevel;
            opLevel << opName << " EG LEVEL " << (j + 1);
            opCtrl[opVal].egLevel[j].reset(new CtrlDX(opLevel, 99, opTarget + j + 4));
            ctrl.add(opCtrl[opVal].egLevel[j].get());
        }
    
        String opVol;
        opVol << opName << " OUTPUT LEVEL";
        opCtrl[opVal].level.reset(new CtrlDX(opVol, 99, opTarget + 16));
        ctrl.add(opCtrl[opVal].level.get());

        String opMode;
        opMode << opName << " MODE";
        opCtrl[opVal].opMode.reset(new CtrlDXOpMode(opMode, 1, opTarget + 17));
        ctrl.add(opCtrl[opVal].opMode.get());

        String coarse;
        coarse << opName << " F COARSE";
        opCtrl[opVal].coarse.reset(new CtrlDX(coarse, 31, opTarget + 18));
        ctrl.add(opCtrl[opVal].coarse.get());

        String fine;
        fine << opName << " F FINE";
        opCtrl[opVal].fine.reset(new CtrlDX(fine, 99, opTarget + 19));
        ctrl.add(opCtrl[opVal].fine.get());

        String detune;
        detune << opName << " OSC DETUNE";
        opCtrl[opVal].detune.reset(new CtrlDX(detune, 14, opTarget + 20, -7));
        ctrl.add(opCtrl[opVal].detune.get());

        String sclBrkPt;
        sclBrkPt << opName << " BREAK POINT";
        opCtrl[opVal].sclBrkPt.reset(new CtrlDXBreakpoint(sclBrkPt, 99, opTarget + 8));
        ctrl.add(opCtrl[opVal].sclBrkPt.get());

        String sclLeftDepth;
        sclLeftDepth << opName << " L SCALE DEPTH";
        opCtrl[opVal].sclLeftDepth.reset(new CtrlDX(sclLeftDepth, 99, opTarget + 9));
        ctrl.add(opCtrl[opVal].sclLeftDepth.get());

        String sclRightDepth;
        sclRightDepth << opName << " R SCALE DEPTH";
        opCtrl[opVal].sclRightDepth.reset(new CtrlDX(sclRightDepth, 99, opTarget + 10));
        ctrl.add(opCtrl[opVal].sclRightDepth.get());

        String sclLeftCurve;
        sclLeftCurve << opName << " L KEY SCALE";
        opCtrl[opVal].sclLeftCurve.reset(new CtrlDXLabel(sclLeftCurve, 3, opTarget + 11, keyScaleLabels));
        ctrl.add(opCtrl[opVal].sclLeftCurve.get());

        String sclRightCurve;
        sclRightCurve << opName << " R KEY SCALE";
        opCtrl[opVal].sclRightCurve.reset(new CtrlDXLabel(sclRightCurve, 3, opTarget + 12, keyScaleLabels));
        ctrl.add(opCtrl[opVal].sclRightCurve.get());

        String sclRate;
        sclRate << opName << " RATE SCALING";
        opCtrl[opVal].sclRate.reset(new CtrlDX(sclRate, 7, opTarget + 13));
        ctrl.add(opCtrl[opVal].sclRate.get());

        String ampModSens;
        ampModSens << opName << " A MOD SENS.";
        opCtrl[opVal].ampModSens.reset(new CtrlDX(ampModSens, 3, opTarget + 14));
        ctrl.add(opCtrl[opVal].ampModSens.get());

        String velModSens;
        velModSens << opName << " KEY VELOCITY";
        opCtrl[opVal].velModSens.reset(new CtrlDX(velModSens, 7, opTarget + 15));
        ctrl.add(opCtrl[opVal].velModSens.get());
        
        String opSwitchLabel;
        opSwitchLabel << opName << " SWITCH";
        opCtrl[opVal].opSwitch.reset(new CtrlOpSwitch(opSwitchLabel, (char *)&(controllers.opSwitch)+(5-i), this));
        ctrl.add(opCtrl[opVal].opSwitch.get());
    }
    
    for (int i=0; i < ctrl.size(); i++) {
        ctrl[i]->idx = i;
        ctrl[i]->parent = this;
    }
}

void DexedAudioProcessor::setDxValue(int offset, int v) {
    if (offset < 0)
        return;

    if ( offset == 155 ) {
        // used on op switch that are not part of a Sysex packed cartridge, we render it
        // ourselves.
        packOpSwitch();
        v = data[155];
    } else if ( data[offset] != v ) {
        TRACE("setting dx offset=%d v=%d", offset, v);
        data[offset] = v;
    } else {
        TRACE("ignoring dx7 same values %d %d", offset, v);
        return;
    }

    refreshVoice = true;

    // MIDDLE C (transpose)
    if (offset == 144)
        panic();
    
    if (!sendSysexChange)
        return;
    
    uint8 msg[7] = { 0xF0, 0x43, 0x10, offset > 127, 0, (uint8) v, 0xF7 };
    msg[2] = 0x10 | sysexComm.getChl();
    msg[4] = offset & 0x7F;
    
    if ( sysexComm.isOutputActive() ) {
        //TRACE("SENDING SYSEX: %.2X%.2X %.2X%.2X %.2X%.2X %.2X", msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6]);
        sysexComm.send(MidiMessage(msg,7));
    }
}

void DexedAudioProcessor::unbindUI() {
    for (int i = 0; i < ctrl.size(); i++) {
        ctrl[i]->unbind();
    }
}

//==============================================================================
int DexedAudioProcessor::getNumParameters() {
    return ctrl.size();
}

float DexedAudioProcessor::getParameter(int index) {
    if (agenticParameterStore_ != nullptr)
        return static_cast<float>(agenticParameterStore_->normalizedHostValue(index));
    return ctrl[index]->getValueHost();
}

void DexedAudioProcessor::setParameter(int index, float newValue) {
    TRACE("setParameter index=%d newValue=%f", index, newValue);
    forceRefreshUI = true;
    if (agenticParameterStore_ != nullptr && !suppressAtomicHostWrite_)
    {
        agenticParameterStore_->setFromHost(index, newValue);
        if (agenticStateService_ != nullptr)
            agenticStateService_->notifyExternalMutation();
        return;
    }
    if (agenticParameterStore_ == nullptr)
        ctrl[index]->setValueHost(newValue);
}

int DexedAudioProcessor::getNumPrograms() {
    return 32;
}

int DexedAudioProcessor::getCurrentProgram() {
    return currentProgram;
}

void DexedAudioProcessor::setCurrentProgram(int index) {
    TRACE("setting program %d state", index);

    if ( lastStateSave + 2 > time(NULL) ) {
        TRACE("skipping save, storage recall to close");
        return;
    }
    
    panic();
    
    index = index > 31 ? 31 : index;
    currentCart.unpackProgram(data, index);
    unpackOpSwitch(0x3F);
    lfo.reset(data + 137);
    currentProgram = index;
    publishLegacyStateToRealtimeStore();
    triggerAsyncUpdate();
    
    // reset parameter display
    DexedAudioProcessorEditor *editor = (DexedAudioProcessorEditor *) getActiveEditor();
    if ( editor == NULL ) {
        return;
    }
    editor->setParameterMessage("");
    
    panic();
}

const String DexedAudioProcessor::getProgramName(int index) {
    if (index >= 32)
        index = 31;
    return programNames[index];
}

void DexedAudioProcessor::changeProgramName(int index, const String& newName) {
}

const String DexedAudioProcessor::getParameterName(int index) {
    return ctrl[index]->label;
}

const String DexedAudioProcessor::getParameterText(int index) {
    return ctrl[index]->getValueDisplay();
}

String DexedAudioProcessor::getParameterID(int index) {
    return getParameterName(index);
}

void DexedAudioProcessor::loadPreference() {
    File propFile = DexedAudioProcessor::dexedAppDir.getChildFile("Dexed.xml");
    PropertiesFile::Options prefOptions;
    PropertiesFile prop(propFile, prefOptions);
    
    if ( ! prop.isValidFile() ) {
        return;
    }
    
    if ( prop.containsKey( String("normalizeDxVelocity") ) ) {
        normalizeDxVelocity = prop.getIntValue( String("normalizeDxVelocity") );
    }
    
    if ( prop.containsKey( String("pitchRange") ) ) {
        controllers.values_[kControllerPitchRangeUp] = prop.getIntValue( String("pitchRange") );
    }
    
    if ( prop.containsKey( String("pitchRangeDn") ) ) {
        controllers.values_[kControllerPitchRangeDn] = prop.getIntValue( String("pitchRangeDn") );
    } else {
        controllers.values_[kControllerPitchRangeDn] = controllers.values_[kControllerPitchRangeUp];
    }
    
    if ( prop.containsKey( String("pitchStep") ) ) {
        controllers.values_[kControllerPitchStep] = prop.getIntValue( String("pitchStep") );
    }
    
    if ( prop.containsKey( String("sysexIn") ) ) {
        sysexComm.setInput( prop.getValue("sysexIn") );
    }
    
    if ( prop.containsKey( String("sysexOut") ) ) {
        sysexComm.setOutput( prop.getValue("sysexOut") );
    }
    
    if ( prop.containsKey( String("sysexChl") ) ) {
        sysexComm.setChl( prop.getIntValue( String("sysexChl") ) );
    }
    
    if ( prop.containsKey( String("engineType") ) ) {
        setEngineType(prop.getIntValue(String("engineType")));
    }

    if ( prop.containsKey( String("showKeyboard") ) ) {
        showKeyboard = prop.getIntValue( String("showKeyboard") );
    }

    if ( prop.containsKey( String("wheelMod") ) ) {
        controllers.wheel.parseConfig(prop.getValue(String("wheelMod")).toRawUTF8());
    }
    
    if ( prop.containsKey( String("footMod") ) ) {
        controllers.foot.parseConfig(prop.getValue(String("footMod")).toRawUTF8());
    }
    
    if ( prop.containsKey( String("breathMod") ) ) {
        controllers.breath.parseConfig(prop.getValue(String("breathMod")).toRawUTF8());
    }
    
    if ( prop.containsKey( String("aftertouchMod") ) ) {
        controllers.at.parseConfig(prop.getValue(String("aftertouchMod")).toRawUTF8());
    }
    
    if ( prop.containsKey( String("zoomFactor") ) ) {
        zoomFactor = prop.getDoubleValue(String("zoomFactor"));
    }

    agenticEditorPreferences.width = juce::jlimit(
        960, 2560, prop.getIntValue("agenticEditorWidth", 1280));
    agenticEditorPreferences.height = juce::jlimit(
        640, 1520, prop.getIntValue("agenticEditorHeight", 760));
    const auto storedScale = prop.getIntValue("agenticEditorScale", 100);
    agenticEditorPreferences.scalePercent =
        storedScale == 125 || storedScale == 150 || storedScale == 200
            ? storedScale : 100;
    agenticEditorPreferences.agentExpanded =
        prop.getBoolValue("agenticAgentExpanded", true);
    agenticEditorPreferences.keyboardExpanded =
        prop.getBoolValue("agenticKeyboardExpanded", showKeyboard);
    agenticEditorPreferences.reducedMotion =
        prop.getBoolValue("agenticReducedMotion", false);
    agenticEditorPreferences.selectedPage = juce::jlimit(
        0, 4, prop.getIntValue("agenticSelectedPage", 0));
    showKeyboard = agenticEditorPreferences.keyboardExpanded;
    
    controllers.refresh();
}

void DexedAudioProcessor::savePreference() {
    File propFile = DexedAudioProcessor::dexedAppDir.getChildFile("Dexed.xml");
    PropertiesFile::Options prefOptions;
    PropertiesFile prop(propFile, prefOptions);
    
    agentic_dexed::RealtimeSynthState state;
    while (!readAgenticRealtimeState(state))
        std::this_thread::yield();

    prop.setValue(String("normalizeDxVelocity"), state.performance.normalizeVelocity);
    prop.setValue(String("pitchRange"), state.performance.pitchRangeUp); // for backwards compat
    prop.setValue(String("pitchRangeUp"), state.performance.pitchRangeUp);
    prop.setValue(String("pitchRangeDn"), state.performance.pitchRangeDown);
    prop.setValue(String("pitchStep"), state.performance.pitchStep);
    
    prop.setValue(String("sysexIn"), sysexComm.getInput());
    prop.setValue(String("sysexOut"), sysexComm.getOutput());
    prop.setValue(String("sysexChl"), sysexComm.getChl());
    
    prop.setValue(String("showKeyboard"), showKeyboard);

    const auto modulationConfig = [](const agentic_dexed::RealtimeModulationState& source)
    {
        String result;
        result << source.range << " " << static_cast<int>(source.pitch)
               << " " << static_cast<int>(source.amplitude)
               << " " << static_cast<int>(source.envelope);
        return result;
    };
    prop.setValue(String("wheelMod"), modulationConfig(state.performance.modulation[0]));
    prop.setValue(String("footMod"), modulationConfig(state.performance.modulation[1]));
    prop.setValue(String("breathMod"), modulationConfig(state.performance.modulation[2]));
    prop.setValue(String("aftertouchMod"), modulationConfig(state.performance.modulation[3]));

    prop.setValue(String("engineType"), state.engineType);
    prop.setValue(String("zoomFactor"), zoomFactor);
    prop.setValue("agenticEditorWidth", agenticEditorPreferences.width);
    prop.setValue("agenticEditorHeight", agenticEditorPreferences.height);
    prop.setValue("agenticEditorScale", agenticEditorPreferences.scalePercent);
    prop.setValue("agenticAgentExpanded", agenticEditorPreferences.agentExpanded);
    prop.setValue("agenticKeyboardExpanded", agenticEditorPreferences.keyboardExpanded);
    prop.setValue("agenticReducedMotion", agenticEditorPreferences.reducedMotion);
    prop.setValue("agenticSelectedPage", agenticEditorPreferences.selectedPage);

    prop.save();
}

