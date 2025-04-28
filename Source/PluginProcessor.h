#pragma once

#include <JuceHeader.h>
#include "DelayLine.h"
#include "ReverbProcessor.h"
#include "AnalogueDrive.h"
#include "Presets.h"
#include <unordered_map>

// Forward declarations
class SynthSound;
class SynthVoice;
class AllSynthPluginAudioProcessorEditor;

class AllSynthPluginAudioProcessor : public juce::AudioProcessor
{
public:
    AllSynthPluginAudioProcessor();
    ~AllSynthPluginAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    // NEW ----------------------------------------------------------------------
    void loadPreset(int index);
    const std::vector<PresetData::Preset>& getPresets() const { return PresetData::presets; }
    //------------------------------------------------------------------------

private:
    //==============================================================================
    juce::Synthesiser synth;

    juce::AudioProcessorValueTreeState parameters;

    // NEW – temp buffers for the delay (no per-block alloc)
    juce::AudioBuffer<float> delayTmpL, delayTmpR;

    // NEW – FX processors --------------------------------------------------------
    DelayLine        delayL, delayR;
    ReverbProcessor  reverb;
    juce::dsp::Oversampling<float> driveOS { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };
    AnalogueDrive    anaDriveL, anaDriveR;
    float driveAmt { 3.0f };
    bool  driveOn  { false };
    
    // Parameter pointer caches
    std::atomic<float>* driveOnParam   = nullptr;
    std::atomic<float>* driveAmtParam  = nullptr;
    std::atomic<float>* fatOnParam     = nullptr;
    std::atomic<float>* fatModeParam   = nullptr;
    std::atomic<float>* delayOnParam   = nullptr;
    std::atomic<float>* reverbOnParam  = nullptr;
    std::atomic<float>* reverbTypeParam = nullptr;
    std::atomic<float>* reverbSizeParam = nullptr;
    // ---------------------------------------------------------------------------

    // NEW – MIDI‑CC mapping -----------------------------------------------------
    std::unordered_map<int, juce::RangedAudioParameter*> ccParamMap; // CC → param
    void setupMidiCCMapping();                                       // build map
    void handleMidiCC(const juce::MidiMessage& msg);                 // react to CC
    // ---------------------------------------------------------------------------

    // NEW : switchable fatness processor
    //  index 0 – pre‑gain
    //  index 1 – tone EQ 1 (IIR shelf/peak/pass)
    //  index 2 – tone EQ 2 (IIR shelf/peak/pass)
    //  index 3 – compressor
    //  index 4 – saturator
    //  index 5 – post‑gain
    using FatChain = juce::dsp::ProcessorChain<
        juce::dsp::Gain<float>,
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Filter<float>,
        juce::dsp::Compressor<float>,
        juce::dsp::WaveShaper<float>,
        juce::dsp::Gain<float>>;
    FatChain fatChain;
    int previousFatMode = -1; // Cache to avoid rebuilding chain on every buffer

    // --- small cache so we only rebuild reverb when the user changes the mode --
    int previousReverbType = -1;
    float previousSizeScale = 0.0f;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ===== NEW – global analogue extras ======================================
    std::atomic<float>* humOnParam    = nullptr;
    std::atomic<float>* crossOnParam  = nullptr;
    std::atomic<float>* masterGainParam = nullptr;
    // =========================================================================

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AllSynthPluginAudioProcessor)
}; 