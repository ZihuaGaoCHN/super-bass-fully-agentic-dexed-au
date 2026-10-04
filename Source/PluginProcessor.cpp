/**
 *
 * Copyright (c) 2013-2025 Pascal Gauthier.
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

#include <stdarg.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <optional>
#include <stdexcept>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "state/DexedParameterBackend.h"
#include "state/ParameterRegistry.h"
#include "state/SynthStateService.h"
#include "agent/AgentController.h"

#include "Dexed.h"
#include "msfa/synth.h"
#include "msfa/freqlut.h"
#include "msfa/sin.h"
#include "msfa/exp2.h"
#include "msfa/env.h"
#include "msfa/pitchenv.h"
#include "msfa/porta.h"
#include "msfa/aligned_buf.h"
#include "msfa/fm_op_kernel.h"

#if JUCE_MSVC
    #pragma comment (lib, "kernel32.lib")
    #pragma comment (lib, "user32.lib")
    #pragma comment (lib, "wininet.lib")
    #pragma comment (lib, "advapi32.lib")
    #pragma comment (lib, "ws2_32.lib")
    #pragma comment (lib, "version.lib")
    #pragma comment (lib, "shlwapi.lib")
    #pragma comment (lib, "winmm.lib")
	#pragma comment (lib, "DbgHelp.lib")
	#pragma comment (lib, "Imm32.lib")

	#ifdef _NATIVE_WCHAR_T_DEFINED
		#ifdef _DEBUG
			#pragma comment (lib, "comsuppwd.lib")
		#else
			#pragma comment (lib, "comsuppw.lib")
		#endif
	#else
		#ifdef _DEBUG
			#pragma comment (lib, "comsuppd.lib")
		#else
			#pragma comment (lib, "comsupp.lib")
		#endif
	#endif

#endif

//==============================================================================
thread_local bool DexedAudioProcessor::suppressAtomicHostWrite_ = false;

namespace
{
thread_local double initializedDexedSampleRate = 0.0;

void ensureDexedSampleRate(double sampleRate)
{
    if (initializedDexedSampleRate == sampleRate)
        return;

    Freqlut::init(sampleRate);
    Lfo::init(sampleRate);
    PitchEnv::init(sampleRate);
    Env::init_sr(sampleRate);
    Porta::init_sr(sampleRate);
    initializedDexedSampleRate = sampleRate;
}

bool submitUiTuningChange(
    DexedAudioProcessor& processor,
    const std::optional<std::string>& sclChange,
    const std::optional<std::string>& kbmChange,
    const char* reason)
{
    const auto snapshot = processor.synthStateService().snapshot(
        { agentic_dexed::SnapshotScopeKind::ids, {},
          { "tuning.scl", "tuning.kbm" } });
    const auto& currentScl = std::get<std::string>(snapshot.values.at("tuning.scl"));
    const auto& currentKbm = std::get<std::string>(snapshot.values.at("tuning.kbm"));
    const auto& sclData = sclChange.has_value() ? *sclChange : currentScl;
    const auto& kbmData = kbmChange.has_value() ? *kbmChange : currentKbm;
    if (!processor.agenticTuningDataIsValid(sclData, kbmData))
        return false;

    std::vector<agentic_dexed::PatchOperation> operations;
    if (sclChange.has_value() && currentScl != sclData)
        operations.push_back({ "tuning.scl", sclData });
    if (kbmChange.has_value() && currentKbm != kbmData)
        operations.push_back({ "tuning.kbm", kbmData });
    if (operations.empty())
        return true;

    static std::atomic<uint64_t> nextTransactionId { 1 };
    agentic_dexed::PatchRequest request;
    request.transactionId = "$ui.tuning."
        + std::to_string(nextTransactionId.fetch_add(1, std::memory_order_relaxed));
    request.baseRevision = snapshot.revision;
    request.reason = reason;
    request.source = agentic_dexed::PatchSource::ui;
    request.operations = std::move(operations);
    return processor.synthStateService().submit(request).status
        == agentic_dexed::PatchStatus::committed;
}
}

DexedAudioProcessor::DexedAudioProcessor(bool backgroundOnly)
    : AudioProcessor(BusesProperties().withOutput("output", AudioChannelSet::stereo(), true)) {
#ifdef DEBUG
    // avoid creating the log file if it is in standalone mode
    if ( !backgroundOnly && !JUCEApplication::isStandaloneApp() ) {
        Logger *tmp = Logger::getCurrentLogger();
        if ( tmp == NULL ) {
            ownedDebugLogger_.reset(FileLogger::createDateStampedLogger(
                "Dexed", "DebugSession-", "log", "DexedAudioProcessor Created"));
            Logger::setCurrentLogger(ownedDebugLogger_.get());
        }
    }
    TRACE("Hi");
#endif

    Exp2::init();
    Tanh::init();
    Sin::init();

    synthTuningState = createStandardTuning();
    synthTuningStateLast = createStandardTuning();
    
    lastStateSave = 0;
    currentNote = -1;
    engineType = -1;
    
    vuSignal = 0;
    monoMode = 0;

    if (!backgroundOnly)
        resolvAppDir();
    
    initCtrl(backgroundOnly);
    sendSysexChange = !backgroundOnly;
    normalizeDxVelocity = false;
    sysexComm.listener = backgroundOnly ? nullptr : this;
    showKeyboard = !backgroundOnly;
    
    memset(&voiceStatus, 0, sizeof(VoiceStatus));
    setEngineType(DEXED_ENGINE_MARKI);
    
    controllers.values_[kControllerPitchRangeUp] = 3;
    controllers.values_[kControllerPitchRangeDn] = 3;
    controllers.values_[kControllerPitchStep] = 0;
    controllers.masterTune = 0;
    
    if (!backgroundOnly)
        loadPreference();

    for (int note = 0; note < MAX_ACTIVE_NOTES; ++note) {
        voices[note].dx7_note = NULL;
    }
    setCurrentProgram(0);    
    nextMidi = NULL;
    midiMsg = NULL;
    
    mtsClient = backgroundOnly ? nullptr : MTS_RegisterClient();

    controllers.portamento_enable_cc = false;
    controllers.portamento_cc = 0;
    controllers.portamento_gliss_cc = false;

    agenticParameterRegistry_ = std::make_unique<agentic_dexed::ParameterRegistry>(
        agentic_dexed::ParameterRegistry::createDexed());
    agenticParameterStore_ = std::make_unique<agentic_dexed::AtomicParameterStore>(
        *agenticParameterRegistry_);
    publishLegacyStateToRealtimeStore();

    agenticParameterBackend_ = std::make_unique<agentic_dexed::DexedParameterBackend>(
        *this, *agenticParameterRegistry_);
    agenticStateService_ = std::make_unique<agentic_dexed::SynthStateService>(
        *agenticParameterRegistry_, *agenticParameterBackend_,
        agenticParameterStore_->revisionCounter());
    if (!backgroundOnly)
        agenticAgentController_ = std::make_unique<agentic_dexed::agent::AgentController>(
            *agenticParameterRegistry_, *agenticStateService_);
}

DexedAudioProcessor::~DexedAudioProcessor() {
    if (agenticAgentController_ != nullptr)
    {
        agenticAgentController_->cancel();
        agenticAgentController_.reset();
    }
    if (Logger::getCurrentLogger() == ownedDebugLogger_.get())
        Logger::setCurrentLogger(nullptr);
    ownedDebugLogger_.reset();
    TRACE("Bye");
    if (mtsClient) MTS_DeregisterClient(mtsClient);
}

//==============================================================================
void DexedAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engineSampleRate_ = sampleRate;
    ensureDexedSampleRate(engineSampleRate_);
    fx.init(sampleRate);

    constexpr double vuDropDb = -40.0;
    constexpr double vuFallSeconds = 0.3;
    vuDecayFactor = std::exp(vuDropDb / 10.0 * std::log(10.0)
                             / (vuFallSeconds * sampleRate));
    
    for (int note = 0; note < MAX_ACTIVE_NOTES; ++note) {
        voices[note].dx7_note = new Dx7Note(synthTuningState, mtsClient);
        voices[note].midi_note = -1;
        voices[note].keydown = false;
        voices[note].sustained = false;
        voices[note].live = false;
        voices[note].keydown_seq = -1;
    }

    currentNote = 0;
    nextKeydownSeq = 0;
    controllers.values_[kControllerPitch] = 0x2000;
    controllers.modwheel_cc = 0;
    controllers.foot_cc = 0;
    controllers.breath_cc = 0;
    controllers.aftertouch_cc = 0;
    controllers.portamento_enable_cc = false;
    controllers.portamento_cc = 0;
	controllers.refresh();

    sustain = false;
    extra_buf_size = 0;

    keyboardState.reset();
    
    lfo.reset(data + 137);
    
    nextMidi = new MidiMessage(0xF0);
	midiMsg = new MidiMessage(0xF0);
}

void DexedAudioProcessor::releaseResources() {
    currentNote = -1;

    for (int note = 0; note < MAX_ACTIVE_NOTES; ++note) {
        if ( voices[note].dx7_note != NULL ) {
            delete voices[note].dx7_note;
            voices[note].dx7_note = NULL;
        }
        voices[note].keydown = false;
        voices[note].sustained = false;
        voices[note].live = false;
    }

    keyboardState.reset();
    if ( nextMidi != NULL ) {
        delete nextMidi;
        nextMidi = NULL;
    }
    if ( midiMsg != NULL ) {
        delete midiMsg;
        midiMsg = NULL;
    }
}

void DexedAudioProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midiMessages) {

    juce::ScopedNoDenormals noDenormals;

    ensureDexedSampleRate(engineSampleRate_);
    consumeRealtimeStateAtBlockBoundary();

    int numSamples = buffer.getNumSamples();
    int i;
    
    if ( refreshVoice ) {
        for(i=0;i < MAX_ACTIVE_NOTES;i++) {
            if ( voices[i].live )
                voices[i].dx7_note->update(data, voices[i].midi_note, voices[i].velocity, voices[i].channel);
        }
        lfo.reset(data + 137);
        refreshVoice = false;
    }

    keyboardState.processNextMidiBuffer(midiMessages, 0, numSamples, true);
    
    MidiBuffer::Iterator it(midiMessages);
    hasMidiMessage = it.getNextEvent(*nextMidi,midiEventPos);

    float *channelData = buffer.getWritePointer(0);
  
    // flush first events
    for (i=0; i < numSamples && i < extra_buf_size; i++) {
        channelData[i] = extra_buf[i];
    }
    
    // remaining buffer is still to be processed
    if (extra_buf_size > numSamples) {
        for (int j = 0; j < extra_buf_size - numSamples; j++) {
            extra_buf[j] = extra_buf[j + numSamples];
        }
        extra_buf_size -= numSamples;
        
        // flush the events, they will be process in the next cycle
        while(getNextEvent(&it, numSamples)) {
            processMidiMessage(midiMsg);
        }
    } else {
        for (; i < numSamples; i += N) {
            AlignedBuf<int32_t, N> audiobuf;
            float sumbuf[N];
            
            while(getNextEvent(&it, i)) {
                processMidiMessage(midiMsg);
            }
            
            for (int j = 0; j < N; ++j) {
                audiobuf.get()[j] = 0;
                sumbuf[j] = 0;
            }
            int32_t lfovalue = lfo.getsample();
            int32_t lfodelay = lfo.getdelay();
            
            bool checkMTSESPRetuning = synthTuningState->is_standard_tuning() &&
                                        MTS_HasMaster(mtsClient);
            
            for (int note = 0; note < MAX_ACTIVE_NOTES; ++note) {
                if (voices[note].live) {
                    
                    if (checkMTSESPRetuning)
                        voices[note].dx7_note->updateBasePitches();
                    
                    voices[note].dx7_note->compute(audiobuf.get(), lfovalue, lfodelay, &controllers);
                    
                    for (int j=0; j < N; ++j) {
                        int32_t val = audiobuf.get()[j];
                        
                        val = val >> 4;
                        int clip_val = val < -(1 << 24) ? 0x8000 : val >= (1 << 24) ? 0x7fff : val >> 9;
                        float f = ((float) clip_val) / (float) 0x8000;
                        if( f > 1 ) f = 1;
                        if( f < -1 ) f = -1;
                        sumbuf[j] += f;
                        audiobuf.get()[j] = 0;
                    }
                }
            }
            
            int jmax = numSamples - i;
            for (int j = 0; j < N; ++j) {
                if (j < jmax) {
                    channelData[i + j] = sumbuf[j];
                } else {
                    extra_buf[j - jmax] = sumbuf[j];
                }
            }
        }
        extra_buf_size = i - numSamples;
    }
    
    while(getNextEvent(&it, numSamples)) {
        processMidiMessage(midiMsg);
    }

    fx.process(channelData, numSamples);

    for(i=0; i<numSamples; i++) {
        float s = std::abs(channelData[i]);

        if (s > vuSignal)
            vuSignal = s;
        else if (vuSignal > /*0.001f*/ 1.26E-4F) // 1.26E-4 is equivalent to -39 dB, the min amplitude associated to leftmost LED
            vuSignal *= vuDecayFactor;
        else
            vuSignal = 0;
    }
    
    // DX7 is a mono synth, but copy it to the right channel is available
    if ( buffer.getNumChannels() > 1 )
        buffer.copyFrom(1, 0, channelData, numSamples, 1);
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new DexedAudioProcessor();
}

bool DexedAudioProcessor::getNextEvent(MidiBuffer::Iterator* iter,const int samplePos) {
	if (hasMidiMessage && midiEventPos <= samplePos) {
		*midiMsg = *nextMidi;
		hasMidiMessage = iter->getNextEvent(*nextMidi, midiEventPos);
		return true;
	}
	return false;
}

void DexedAudioProcessor::processMidiMessage(const MidiMessage *msg) {
    if ( msg->isSysEx() ) {
        handleIncomingMidiMessage(NULL, *msg);
        return;
    }

    const uint8 *buf  = msg->getRawData();
    uint8_t cmd = buf[0];
    uint8_t cf0 = cmd & 0xf0;
    auto channel = msg->getChannel();


    if( controllers.mpeEnabled && channel != 1 &&
        (
            (cf0 == 0xb0 && buf[1] == 74 ) || //timbre
            (cf0 == 0xd0 ) || // aftertouch
            (cf0 == 0xe0 ) // pb
            )
        )
    {
        // OK so find my voice index
        int voiceIndex = -1;
        for( int i=0; i<MAX_ACTIVE_NOTES; ++i )
        {
            if( voices[i].keydown && voices[i].channel == channel )
            {
                voiceIndex = i;
                break;
            }
        }
        if( voiceIndex >= 0 )
        {
            int i = voiceIndex;
            switch(cf0) {
            // THIS IS COMMENTED SINCE mepTimbre and mpePressure is not used
            // case 0xb0:
            //     voices[i].mpeTimbre = (int)buf[2];
            //     voices[i].dx7_note->mpeTimbre = (int)buf[2];
            //     break;
            // case 0xd0:
            //     voices[i].mpePressure = (int)buf[1];
            //     voices[i].dx7_note->mpePressure = (int)buf[1];
            //     break;
            case 0xe0:
                voices[i].mpePitchBend = (int)( buf[1] | (buf[2] << 7) );
                voices[i].dx7_note->mpePitchBend = (int)( buf[1] | ( buf[2] << 7 ) );
                break;
            }
        }
    }
    else
    {
        switch(cmd & 0xf0) {
        case 0x80 :
            keyup(channel, buf[1], buf[2]);
        return;

        case 0x90 :
            if (!synthTuningState->is_standard_tuning() || !buf[2] ||
                !MTS_HasMaster(mtsClient) || !MTS_ShouldFilterNote(mtsClient, buf[1], channel - 1))
                keydown(channel, buf[1], buf[2]);
        return;
            
        case 0xb0 : {
            int ctrl = buf[1];
            int value = buf[2];
                switch(ctrl) {
                case 1:
                    controllers.modwheel_cc = value;
                    controllers.refresh();
                    break;
                case 2:
                    controllers.breath_cc = value;
                    controllers.refresh();
                    break;
                case 4:
                    controllers.foot_cc = value;
                    controllers.refresh();
                    break;
                case 5:
                    controllers.portamento_cc = value;
                    break;
                case 64:
                    sustain = value > 63;
                    if (!sustain) {
                        for (int note = 0; note < MAX_ACTIVE_NOTES; note++) {
                            if (voices[note].sustained && !voices[note].keydown) {
                                voices[note].dx7_note->keyup();
                                voices[note].sustained = false;
                            }
                        }
                    }
                    break;
                case 65:
                    controllers.portamento_enable_cc = value >= 64;
                    break;
                case 120:
                    panic();
                    break;
                case 123:
                    for (int note = 0; note < MAX_ACTIVE_NOTES; note++) {
                        if (voices[note].keydown)
                            keyup(channel, voices[note].midi_note, 0);
                    }
                    break;
                default:
                    TRACE("handle channel %d CC %d = %d", channel, ctrl, value);
                    int channel_cc = (channel << 8) | ctrl;
                    if ( mappedMidiCC.contains(channel_cc) ) {
                        Ctrl *linkedCtrl = mappedMidiCC[channel_cc];
                        
                        // We are not publishing this in the DSP thread, moving that in the
                        // event thread
                        linkedCtrl->publishValueAsync((float) value / 127);
                    }
                    // this is used to notify the dialog that a CC value was received.
                    lastCCUsed.setValue(channel_cc);
                }
            }
            return;
            
        case 0xc0 :
            setCurrentProgram(buf[1]);
            return;
            
        case 0xd0 :
            controllers.aftertouch_cc = buf[1];
            controllers.refresh();
            return;
        
		case 0xe0 :
			controllers.values_[kControllerPitch] = buf[1] | (buf[2] << 7);
            return;
        }
    }
}

#define ACT(v) (v.keydown ? v.midi_note : -1)

int DexedAudioProcessor::chooseNote(uint8_t pitch) {
    // order of preference:
    // 1. a note that is not playing
    // 2. a note with its key up, playing the same pitch
    // 3. a note with its key up, playing a different pitch
    // 4. a note with its key down, playing the same pitch
    // 5. a note with its key down, playing a different pitch
    // break ties by preferring note with least recent keydown
    int bestNote = currentNote;
    int bestScore = -1;
    int note = currentNote;
    for (int i=0; i<MAX_ACTIVE_NOTES; i++) {
        int score = 0;
        if ( !voices[note].dx7_note->isPlaying() ) score += 4;
        if ( !voices[note].keydown ) score += 2;
        if ( voices[note].midi_note == pitch ) score += 1;
        if ( (score > bestScore) || (score == bestScore && voices[note].keydown_seq < voices[bestNote].keydown_seq) ) {
            bestNote = note;
            bestScore = score;
        }
        note = (note + 1) % MAX_ACTIVE_NOTES;
    }
    return bestNote;
}

void DexedAudioProcessor::keydown(uint8_t channel, uint8_t pitch, uint8_t velo) {
    if ( velo == 0 ) {
        keyup(channel, pitch, velo);
        return;
    }

    pitch += tuningTranspositionShift();
    
    if ( normalizeDxVelocity ) {
        velo = ((float)velo) * 0.7874015; // 100/127
    }
  

    if( controllers.mpeEnabled ) {
        int note = currentNote;
        for( int i=0; i<MAX_ACTIVE_NOTES; ++i ) {
            if( voices[note].keydown && voices[note].channel == channel )
            {
                // If we get two keydowns on the same channel we are getting information from a non-mpe device
                controllers.mpeEnabled = false;
            }
            note = (note + 1) % MAX_ACTIVE_NOTES;
        }
    }

    bool triggerLfo = true;
    for (int i=0; i<MAX_ACTIVE_NOTES; i++) {
        if ( voices[i].keydown ) {
            triggerLfo = false;
            break;
        }
    }
    if ( triggerLfo ) {
        lfo.keydown();
    }

    int note = chooseNote(pitch);

    currentNote = (note + 1) % MAX_ACTIVE_NOTES;
    voices[note].channel = channel;
    voices[note].midi_note = pitch;
    voices[note].velocity = velo;
    voices[note].sustained = sustain;
    voices[note].keydown = true;
    voices[note].keydown_seq = nextKeydownSeq++;
    // to avoid click, don't sync oscillators when voice stealing
    bool voice_steal = voices[note].dx7_note->isPlaying();
    voices[note].dx7_note->init(data, pitch, velo, channel, &controllers);
    if ( data[136] && !voice_steal ) {
        voices[note].dx7_note->oscSync();
    }
    if ( (voices[lastActiveVoice].midi_note != -1 && controllers.portamento_enable_cc)
       && controllers.portamento_cc > 0 ) {
        voices[note].dx7_note->initPortamento(*voices[lastActiveVoice].dx7_note);
    }

    if ( monoMode ) {
        for(int i=0; i<MAX_ACTIVE_NOTES; i++) {            
            if ( voices[i].live ) {
                // all keys are up, only transfer signal
                if ( ! voices[i].keydown ) {
                    voices[i].live = false;
                    voices[note].dx7_note->transferSignal(*voices[i].dx7_note);
                    break;
                }
                if ( voices[i].midi_note < pitch ) {
                    voices[i].live = false;
                    voices[note].dx7_note->transferState(*voices[i].dx7_note);
                    break;
                }
                return;
            }
        }
    }
    else if ( !data[136] ) {
        // if another note at the same pitch is playing, transfer phase
        // to avoid unpredictable destructive interference. this can cause
        // clicking when voice stealing, but we've tried to choose a voice
        // to steal that will minimise the chances of clicking
        for(int i=0; i<MAX_ACTIVE_NOTES; i++) {
            if ( i != note && voices[i].dx7_note->isPlaying() && voices[i].midi_note == pitch ) {
                voices[note].dx7_note->transferPhase(*voices[i].dx7_note);
                break;
            }
        }
    }

 
    voices[note].live = true;
    lastActiveVoice = note;
	//TRACE("activate %d [ %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d ]", pitch, ACT(voices[0]), ACT(voices[1]), ACT(voices[2]), ACT(voices[3]), ACT(voices[4]), ACT(voices[5]), ACT(voices[6]), ACT(voices[7]), ACT(voices[8]), ACT(voices[9]), ACT(voices[10]), ACT(voices[11]), ACT(voices[12]), ACT(voices[13]), ACT(voices[14]), ACT(voices[15]));
}

void DexedAudioProcessor::keyup(uint8_t chan, uint8_t pitch, uint8_t velo) {
    pitch += tuningTranspositionShift();

    int note;
    for (note=0; note<MAX_ACTIVE_NOTES; ++note) {
        if ( ( ( controllers.mpeEnabled && voices[note].channel == chan ) || // MPE node - find voice by channel
               (!controllers.mpeEnabled && voices[note].midi_note == pitch ) ) && // regular mode find voice by pitch
             voices[note].keydown ) // but still only grab the one which is keydown
        {
            voices[note].keydown = false;
			//TRACE("deactivate %d [ %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d ]", pitch, ACT(voices[0]), ACT(voices[1]), ACT(voices[2]), ACT(voices[3]), ACT(voices[4]), ACT(voices[5]), ACT(voices[6]), ACT(voices[7]), ACT(voices[8]), ACT(voices[9]), ACT(voices[10]), ACT(voices[11]), ACT(voices[12]), ACT(voices[13]), ACT(voices[14]), ACT(voices[15]));
            break;
        }
    }
    
    // note not found ?
    if ( note >= MAX_ACTIVE_NOTES ) {
		TRACE("note found ??? %d [ %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d ]", pitch, ACT(voices[0]), ACT(voices[1]), ACT(voices[2]), ACT(voices[3]), ACT(voices[4]), ACT(voices[5]), ACT(voices[6]), ACT(voices[7]), ACT(voices[8]), ACT(voices[9]), ACT(voices[10]), ACT(voices[11]), ACT(voices[12]), ACT(voices[13]), ACT(voices[14]), ACT(voices[15]));
        return;
    }
    
    if ( monoMode ) {
        int highNote = -1;
        int target = 0;
        for (int i=0; i<MAX_ACTIVE_NOTES;i++) {
            if ( voices[i].keydown && voices[i].midi_note > highNote ) {
                target = i;
                highNote = voices[i].midi_note;
            }
        }
        
        if ( highNote != -1 && voices[note].live ) {
            voices[note].live = false;
            voices[target].live = true;
            voices[target].dx7_note->transferState(*voices[note].dx7_note);
        }
    }
    
    if ( sustain ) {
        voices[note].sustained = true;
    } else {
        voices[note].dx7_note->keyup();
    }
}

int DexedAudioProcessor::tuningTranspositionShift()
{
    if( synthTuningState->is_standard_tuning() || ! controllers.transpose12AsScale )
        return data[144] - 24;
    else
    {
        int d144 = data[144];
        if( d144 % 12 == 0 )
        {
            int oct = (d144 - 24) / 12;
            int res = oct * synthTuningState->scale_length();
            return res;
        }
        else
            return data[144] - 24;
    }
}

void DexedAudioProcessor::panic() {
    for(int i=0;i<MAX_ACTIVE_NOTES;i++) {
        voices[i].midi_note = -1;
        voices[i].keydown = false;
        voices[i].live = false;
        if ( voices[i].dx7_note != NULL ) {
            voices[i].dx7_note->oscSync();
        }
    }
    keyboardState.reset();
}

void DexedAudioProcessor::handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message) {
    if ( message.isActiveSense() ) 
        return;

#ifdef IMPLEMENT_MidiMonitor
    sysexComm.inActivity = true; // indicate to MidiMonitor that a MIDI messages (other than Active Sense) is received
#endif //IMPLEMENT_MidiMonitor

    const uint8 *buf = message.getRawData();
    int sz = message.getRawDataSize();

    //TRACE("%X %X %X %X %X %X", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

    if ( ! message.isSysEx() )
        return;

    // test if it is a Yamaha Sysex
    if ( buf[1] != 0x43 ) {
        TRACE("not a yamaha sysex %d", buf[1]);
        return;
    }

    int substatus = buf[2] >> 4;
    switch(substatus) {
        case 0 : {
            // single voice dump
            if ( buf[3] == 0 ) {
                if ( sz < 156 ) {
                    TRACE("wrong single voice datasize %d", sz);
                    return;
                }
                if ( updateProgramFromSysex(buf+6) )
                    TRACE("bad checksum when updating program from sysex message");
            }

            // 32 voice dump
            if ( buf[3] == 9 ) {
                if ( sz < 4104 ) {
                    TRACE("wrong 32 voice dump data size %d", sz);
                    return;
                }

                Cartridge received;
                if ( received.load(buf, sz) == 0 ) {
                    loadCartridge(received);
                    setCurrentProgram(0);
                }
            }
        }
        break;
        case 1 : {
            // parameter change
            if ( sz < 7 ) {
            TRACE("wrong single voice datasize %d", sz);
            return;
            }

            uint8 offset = (buf[3] << 7) + buf[4];
            uint8 value = buf[5];

            TRACE("parameter change message offset:%d value:%d", offset, value);

            if ( offset > 155 ) {
                TRACE("wrong offset size");
                return;
            }

            if ( offset == 155 ) {
                unpackOpSwitch(value);
            } else {
                data[offset] = value;
            }
            publishLegacyStateToRealtimeStore();
        }
        break;
        case 2: {
            if ( buf[3] == 0 ) {
                // single voice request
                sendCurrentSysexProgram();
            } else if ( buf[3] == 9 ) {
                // cart request
                sendCurrentSysexCartridge();
            } else {
                TRACE("Unknown voice request: %d", buf[3]);
            }
        }
        return;

        default: {
            TRACE("unknown sysex substatus: %d", substatus);
        }
        return;
    }

    forceRefreshUI = true;
    triggerAsyncUpdate();
}

int DexedAudioProcessor::getEngineType() const {
    agentic_dexed::RealtimeSynthState state;
    if (readAgenticRealtimeState(state))
        return state.engineType;
    return engineType;
}

void DexedAudioProcessor::setEngineType(int tp) {
    TRACE("settings engine %d", tp);

    if (agenticParameterStore_ != nullptr)
    {
        agentic_dexed::RealtimeAuxiliaryChanges auxiliary;
        auxiliary.engineType = tp;
        publishAgenticRealtimeBatch({}, auxiliary);
        return;
    }

    applyEngineTypeToLegacy(tp);
}

void DexedAudioProcessor::applyEngineTypeToLegacy(int tp) {
    TRACE("applying engine %d", tp);

    switch (tp)  {
        case DEXED_ENGINE_MARKI:
            controllers.core = &engineMkI;
            break;
        case DEXED_ENGINE_OPL:
            controllers.core = &engineOpl;
            break;
        default:
            controllers.core = &engineMsfa;
            break;
    }
    engineType = tp;
}

void DexedAudioProcessor::publishPerformanceConfiguration(
    const Controllers& configuration, int selectedEngine)
{
    agentic_dexed::RealtimeAuxiliaryChanges auxiliary;
    auxiliary.engineType = selectedEngine;
    auxiliary.normalizeVelocity = normalizeDxVelocity;
    auxiliary.pitchRangeUp = configuration.values_[kControllerPitchRangeUp];
    auxiliary.pitchRangeDown = configuration.values_[kControllerPitchRangeDn];
    auxiliary.pitchStep = configuration.values_[kControllerPitchStep];
    auxiliary.transposeAsScale = configuration.transpose12AsScale;
    auxiliary.mpeEnabled = configuration.mpeEnabled;
    auxiliary.mpePitchBendRange = configuration.mpePitchBendRange;
    auxiliary.portamentoTime = static_cast<int>(
        std::llround(configuration.portamento_cc * 99.0 / 127.0));
    auxiliary.portamentoGlissando = configuration.portamento_gliss_cc;
    const FmMod* modulation[] = {
        &configuration.wheel, &configuration.foot,
        &configuration.breath, &configuration.at
    };
    for (std::size_t index = 0; index < auxiliary.modulation.size(); ++index)
    {
        const auto& source = *modulation[index];
        auxiliary.modulation[index] = agentic_dexed::RealtimeModulationState {
            source.range, source.pitch, source.amp, source.eg
        };
    }
    publishAgenticRealtimeBatch({}, auxiliary);
}

float DexedAudioProcessor::agenticHostParameterNormalized(int hostIndex) const {
    if (hostIndex < 0 || hostIndex >= ctrl.size() || agenticParameterStore_ == nullptr)
        throw std::out_of_range("Agentic Dexed host parameter index");
    return static_cast<float>(agenticParameterStore_->normalizedHostValue(hostIndex));
}

void DexedAudioProcessor::setAgenticHostParameterNormalized(
    int hostIndex, float normalized) {
    if (hostIndex < 0 || hostIndex >= ctrl.size())
        throw std::out_of_range("Agentic Dexed host parameter index");
    const auto previousSuppression = suppressAtomicHostWrite_;
    suppressAtomicHostWrite_ = true;
    setParameterNotifyingHost(hostIndex, normalized);
    suppressAtomicHostWrite_ = previousSuppression;
}

agentic_dexed::AtomicBatchResult DexedAudioProcessor::publishAgenticRealtimeBatch(
    const std::vector<agentic_dexed::NormalizedChange>& hostChanges,
    const agentic_dexed::RealtimeAuxiliaryChanges& auxiliary)
{
    for (;;)
    {
        const auto baseRevision = agenticParameterStore_->revision();
        const auto result = agenticParameterStore_->tryApplyBatch(
            baseRevision, hostChanges, auxiliary);
        if (result.status != agentic_dexed::AtomicBatchStatus::conflict)
        {
            if (result.status == agentic_dexed::AtomicBatchStatus::committed
                && agenticStateService_ != nullptr)
                agenticStateService_->notifyExternalMutation();
            return result;
        }
    }
}

agentic_dexed::AtomicBatchResult DexedAudioProcessor::tryPublishAgenticRealtimeBatch(
    uint64_t baseRevision,
    const std::vector<agentic_dexed::NormalizedChange>& hostChanges,
    const agentic_dexed::RealtimeAuxiliaryChanges& auxiliary) noexcept
{
    return agenticParameterStore_->tryApplyBatch(
        baseRevision, hostChanges, auxiliary);
}

bool DexedAudioProcessor::readAgenticRealtimeState(
    agentic_dexed::RealtimeSynthState& destination) const noexcept
{
    return agenticParameterStore_ != nullptr
        && agenticParameterStore_->readStable(destination);
}

agentic_dexed::AtomicParameterStore&
DexedAudioProcessor::atomicParameterStore() noexcept
{
    return *agenticParameterStore_;
}

const agentic_dexed::AtomicParameterStore&
DexedAudioProcessor::atomicParameterStore() const noexcept
{
    return *agenticParameterStore_;
}

void DexedAudioProcessor::publishLegacyStateToRealtimeStore()
{
    if (agenticParameterStore_ == nullptr)
        return;

    std::vector<agentic_dexed::NormalizedChange> hostValues;
    hostValues.reserve(static_cast<std::size_t>(ctrl.size()));
    for (int index = 0; index < ctrl.size(); ++index)
        hostValues.push_back({ index, ctrl[index]->getValueHost() });

    agentic_dexed::RealtimeAuxiliaryChanges auxiliary;
    std::array<uint8_t, 10> patchName;
    std::copy_n(data + 145, patchName.size(), patchName.begin());
    auxiliary.patchName = patchName;
    auxiliary.engineType = static_cast<int>(engineType);
    auxiliary.normalizeVelocity = normalizeDxVelocity;
    auxiliary.pitchRangeUp = controllers.values_[kControllerPitchRangeUp];
    auxiliary.pitchRangeDown = controllers.values_[kControllerPitchRangeDn];
    auxiliary.pitchStep = controllers.values_[kControllerPitchStep];
    auxiliary.transposeAsScale = controllers.transpose12AsScale;
    auxiliary.mpeEnabled = controllers.mpeEnabled;
    auxiliary.mpePitchBendRange = controllers.mpePitchBendRange;
    auxiliary.portamentoTime = static_cast<int>(
        std::llround(controllers.portamento_cc * 99.0 / 127.0));
    auxiliary.portamentoGlissando = controllers.portamento_gliss_cc;
    const FmMod* modulation[] = {
        &controllers.wheel, &controllers.foot, &controllers.breath, &controllers.at
    };
    for (std::size_t index = 0; index < auxiliary.modulation.size(); ++index)
    {
        const auto& source = *modulation[index];
        auxiliary.modulation[index] = agentic_dexed::RealtimeModulationState {
            source.range, source.pitch, source.amp, source.eg
        };
    }

    publishAgenticRealtimeBatch(hostValues, auxiliary);
}

void DexedAudioProcessor::consumeRealtimeStateAtBlockBoundary() noexcept
{
    agentic_dexed::RealtimeSynthState next;
    if (!readAgenticRealtimeState(next)
        || next.revision == appliedRealtimeRevision_)
        return;

    const auto transposeChanged = data[144] != next.voiceBytes[144];
    bool voiceChanged = false;
    for (std::size_t offset = 0; offset <= 144; ++offset)
    {
        voiceChanged = voiceChanged || data[offset] != next.voiceBytes[offset];
        data[offset] = next.voiceBytes[offset];
    }
    std::copy_n(next.voiceBytes.begin() + 145, 10, data + 145);
    voiceChanged = voiceChanged || data[155] != next.voiceBytes[155];
    data[155] = next.voiceBytes[155];
    unpackOpSwitch(static_cast<char>(data[155]));

    fx.uiCutoff = next.filterCutoff;
    fx.uiReso = next.filterResonance;
    fx.uiGain = next.outputGain;
    monoMode = next.performance.mono;
    normalizeDxVelocity = next.performance.normalizeVelocity;
    const auto tune = static_cast<int32_t>(next.hostNormalized[4] * 0x4000) - 0x2000;
    controllers.masterTune = static_cast<int32_t>(
        (static_cast<float>(tune * (1 << 11))) * (1.0f / 12.0f));
    controllers.values_[kControllerPitchRangeUp] = next.performance.pitchRangeUp;
    controllers.values_[kControllerPitchRangeDn] = next.performance.pitchRangeDown;
    controllers.values_[kControllerPitchStep] = next.performance.pitchStep;
    controllers.transpose12AsScale = next.performance.transposeAsScale;
    controllers.mpeEnabled = next.performance.mpeEnabled;
    controllers.mpePitchBendRange = next.performance.mpePitchBendRange;
    controllers.portamento_cc = static_cast<int32_t>(
        std::llround(next.performance.portamentoTime * 127.0 / 99.0));
    controllers.portamento_enable_cc = controllers.portamento_cc > 0;
    controllers.portamento_gliss_cc = next.performance.portamentoGlissando;
    FmMod* modulation[] = {
        &controllers.wheel, &controllers.foot, &controllers.breath, &controllers.at
    };
    for (std::size_t index = 0; index < next.performance.modulation.size(); ++index)
    {
        const auto& source = next.performance.modulation[index];
        modulation[index]->range = source.range;
        modulation[index]->pitch = source.pitch;
        modulation[index]->amp = source.amplitude;
        modulation[index]->eg = source.envelope;
    }
    controllers.refresh();

    if (static_cast<int>(engineType) != next.engineType)
        applyEngineTypeToLegacy(next.engineType);
    if (transposeChanged)
        panic();
    refreshVoice = refreshVoice || voiceChanged;
    realtimeSynthState_ = next;
    appliedRealtimeRevision_ = next.revision;
}

bool DexedAudioProcessor::agenticTuningDataIsValid(
    const std::string& sclData, const std::string& kbmData) const {
    if (sclData.size() > MAX_SCL_KBM_FILE_SIZE
        || kbmData.size() > MAX_SCL_KBM_FILE_SIZE)
        return false;
    if (sclData.empty() && kbmData.empty())
        return true;
    if (kbmData.empty())
        return createTuningFromSCLData(sclData) != nullptr;
    if (sclData.empty())
        return createTuningFromKBMData(kbmData) != nullptr;
    return createTuningFromSCLAndKBMData(sclData, kbmData) != nullptr;
}

bool DexedAudioProcessor::setAgenticTuningData(
    const std::string& sclData, const std::string& kbmData) {
    if (sclData.size() > MAX_SCL_KBM_FILE_SIZE
        || kbmData.size() > MAX_SCL_KBM_FILE_SIZE)
        return false;

    std::shared_ptr<TuningState> tuning;
    if (sclData.empty() && kbmData.empty())
        tuning = createStandardTuning();
    else if (kbmData.empty())
        tuning = createTuningFromSCLData(sclData);
    else if (sclData.empty())
        tuning = createTuningFromKBMData(kbmData);
    else
        tuning = createTuningFromSCLAndKBMData(sclData, kbmData);

    if (tuning == nullptr)
        return false;

    currentSCLData = sclData;
    currentKBMData = kbmData;
    resetTuning(std::move(tuning));
    return true;
}

agentic_dexed::SynthStateService& DexedAudioProcessor::synthStateService() noexcept {
    return *agenticStateService_;
}

const agentic_dexed::SynthStateService& DexedAudioProcessor::synthStateService() const noexcept {
    return *agenticStateService_;
}

const agentic_dexed::ParameterRegistry& DexedAudioProcessor::parameterRegistry() const noexcept {
    return *agenticParameterRegistry_;
}

agentic_dexed::agent::AgentController& DexedAudioProcessor::agentController() noexcept {
    jassert(agenticAgentController_ != nullptr);
    return *agenticAgentController_;
}

const agentic_dexed::agent::AgentController& DexedAudioProcessor::agentController() const noexcept {
    jassert(agenticAgentController_ != nullptr);
    return *agenticAgentController_;
}

void DexedAudioProcessor::setMonoMode(bool mode) {
    panic();
    monoMode = mode;
}

// ====================================================================
bool DexedAudioProcessor::peekVoiceStatus() {
    if ( currentNote == -1 )
        return false;

    // we are trying to find the last "keydown" note
    int note = currentNote;
    for (int i = 0; i < MAX_ACTIVE_NOTES; i++) {
        if (voices[note].keydown) {
            voices[note].dx7_note->peekVoiceStatus(voiceStatus);
            return true;
        }
        if ( --note < 0 )
            note = MAX_ACTIVE_NOTES-1;
    }

    // not found; try a live note
    note = currentNote;
    for (int i = 0; i < MAX_ACTIVE_NOTES; i++) {
        if (voices[note].live) {
            voices[note].dx7_note->peekVoiceStatus(voiceStatus);
            return true;
        }
        if ( --note < 0 )
            note = MAX_ACTIVE_NOTES-1;
    }

    return true;
}

const String DexedAudioProcessor::getInputChannelName (int channelIndex) const {
    return String (channelIndex + 1);
}

const String DexedAudioProcessor::getOutputChannelName (int channelIndex) const {
    return String (channelIndex + 1);
}

bool DexedAudioProcessor::isInputChannelStereoPair (int index) const {
    return true;
}

bool DexedAudioProcessor::isOutputChannelStereoPair (int index) const {
    return true;
}

bool DexedAudioProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const {
    return layouts.getMainOutputChannelSet() == AudioChannelSet::mono()
                || layouts.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

bool DexedAudioProcessor::acceptsMidi() const {
    return true;
}

bool DexedAudioProcessor::producesMidi() const {
    return true;
}

bool DexedAudioProcessor::silenceInProducesSilenceOut() const {
    return false;
}

double DexedAudioProcessor::getTailLengthSeconds() const {
    return 0.0;
}

const String DexedAudioProcessor::getName() const {
    return JucePlugin_Name;
}

//==============================================================================
bool DexedAudioProcessor::hasEditor() const {
    return true; // (change this to false if you choose to not supply an editor)
}

void DexedAudioProcessor::updateUI() {
    // notify host something has changed
    updateHostDisplay();
 
    AudioProcessorEditor *editor = getActiveEditor();
    if ( editor == NULL ) {
        return;
    }
	DexedAudioProcessorEditor *dexedEditor = (DexedAudioProcessorEditor *) editor;
    dexedEditor->updateUI();
}

AudioProcessorEditor* DexedAudioProcessor::createEditor() {
    AudioProcessorEditor* editor = new DexedAudioProcessorEditor (this);
    return editor;
}

void DexedAudioProcessor::setZoomFactor(float factor) {
    zoomFactor = factor;
}

void DexedAudioProcessor::handleAsyncUpdate() {
    updateUI();
}

void dexed_trace(const char *source, const char *fmt, ...) {
    char output[4096];
    va_list argptr;
    va_start(argptr, fmt);
    vsnprintf(output, 4095, fmt, argptr);
    va_end(argptr);

    String dest;
    dest << source << " " << output;
    Logger::writeToLog(dest);
}

void DexedAudioProcessor::resetTuning(std::shared_ptr<TuningState> t)
{
    synthTuningState = t;
    synthTuningStateLast = t;
    for( int i=0; i<MAX_ACTIVE_NOTES; ++i )
        if( voices[i].dx7_note != nullptr )
            voices[i].dx7_note->tuning_state_ = synthTuningState;
}

void DexedAudioProcessor::retuneToStandard()
{
    submitUiTuningChange(
        *this, std::string {}, std::string {}, "Restore standard tuning");
}

agentic_dexed::ui::UiOperationResult
DexedAudioProcessor::applySCLTuning(File file) {
    if (!file.existsAsFile() || file.getFileExtension() != ".scl")
        return { false, String::fromUTF8("请选择有效的 .scl 文件 / Choose a valid .scl file"), false };
    if (file.getSize() <= 0 || file.getSize() > MAX_SCL_KBM_FILE_SIZE)
        return { false, String::fromUTF8("SCL 文件必须为 1–16384 字节 / SCL file must be 1–16384 bytes"), false };
    return applySCLTuning(file.loadFileAsString().toStdString());
}

agentic_dexed::ui::UiOperationResult
DexedAudioProcessor::applySCLTuning(std::string contents) {
    if (!submitUiTuningChange(*this, std::move(contents), std::nullopt,
                              "Load SCL tuning"))
        return { false, String::fromUTF8("SCL 调律内容无效 / Invalid SCL tuning data"), false };
    return { true, String::fromUTF8("SCL 调律已应用 / SCL tuning applied"), false };
}

agentic_dexed::ui::UiOperationResult
DexedAudioProcessor::applyKBMMapping(File file) {
    if (!file.existsAsFile() || file.getFileExtension() != ".kbm")
        return { false, String::fromUTF8("请选择有效的 .kbm 文件 / Choose a valid .kbm file"), false };
    if (file.getSize() <= 0 || file.getSize() > MAX_SCL_KBM_FILE_SIZE)
        return { false, String::fromUTF8("KBM 文件必须为 1–16384 字节 / KBM file must be 1–16384 bytes"), false };
    return applyKBMMapping(file.loadFileAsString().toStdString());
}

agentic_dexed::ui::UiOperationResult
DexedAudioProcessor::applyKBMMapping(std::string contents) {
    if (!submitUiTuningChange(*this, std::nullopt, std::move(contents),
                              "Load KBM mapping"))
        return { false, String::fromUTF8("KBM 映射内容无效 / Invalid KBM mapping data"), false };
    return { true, String::fromUTF8("KBM 映射已应用 / KBM mapping applied"), false };
}
