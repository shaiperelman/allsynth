#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <map>
#include <unordered_map>
#include <vector>
#include <string>

class AllSynthPluginAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit AllSynthPluginAudioProcessorEditor(AllSynthPluginAudioProcessor&);
    ~AllSynthPluginAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    AllSynthPluginAudioProcessor& processor;

    // UI controls
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
    juce::Slider cutoffSlider, resonanceSlider;
    juce::ComboBox waveformBox, waveform2Box;  // 2nd osc selector
    juce::Slider pulseWidthSlider;
    // Volumes
    juce::Slider osc1VolSlider, osc2VolSlider;
    // === NEW: 2nd oscillator detune controls ===
    juce::Slider osc2SemiSlider, osc2FineSlider;
    juce::Label  osc2SemiLabel,  osc2FineLabel;
    // === NEW: Master gain control ===
    juce::Slider masterGainSlider;
    juce::ComboBox modelBox;
    juce::ComboBox companyBox;
    // LFO
    juce::TextButton  lfoToggle     { "LFO" },
                      noiseToggle   { "Noise" },
                      driveToggle   { "Drive" },
                      delayToggle   { "Delay" },
                      reverbToggle  { "Reverb" },
                      delaySyncToggle{ "Sync" },
                      consoleToggle { "Fat" };
    juce::Slider       lfoRateSlider, lfoDepthSlider;
    juce::TextButton   lfoSyncToggle { "Sync" };   // Tempo-sync toggle for LFO
    juce::ComboBox     lfoShapeBox;                   // LFO shape selector
    juce::Label        lfoShapeLabel;
    // NEW: LFO sync division and phase offset
    juce::ComboBox     lfoSyncDivBox;
    juce::Label        lfoSyncDivLabel;
    juce::Slider       lfoPhaseSlider;
    juce::Label        lfoPhaseLabel;
    // NEW: LFO routing toggles
    juce::TextButton   lfoToPitchToggle { "Pitch" };
    juce::TextButton   lfoToCutoffToggle{ "Cutoff" };
    juce::TextButton   lfoToAmpToggle   { "Amp" };
    // Noise & Drive
    juce::Slider       noiseMixSlider, driveAmtSlider;
    // FX
    juce::Slider       delayMixSlider, reverbMixSlider, delayTimeSlider, delayFeedbackSlider;
    juce::Slider       reverbSizeSlider;    // NEW
    juce::ComboBox     reverbTypeBox;
    // Console
    juce::ComboBox     consoleModelBox;          // NEW
    juce::Label        consoleModelLabel;        // NEW
    juce::ComboBox     delaySyncDivBox;     // delay sync division selector
    juce::Label        delaySyncDivLabel;   // delay sync division label

    // ===== Oversampling selector =====================================
    juce::ComboBox  filterOsBox;
    juce::Label     filterOsLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
                     filterOsAttachment;
    // =========================================================================

    // --- NEW: Preset selectors -----------------------------------------------
    juce::ComboBox presetCategoryBox, presetBox;
    juce::Label    presetCategoryLabel, presetLabel;
    // --- NEW: Arrow buttons ---
    juce::TextButton presetCategoryUpButton{ "^" }, presetCategoryDownButton{ "v" };
    juce::TextButton presetUpButton{ "^" }, presetDownButton{ "v" };
    juce::TextButton companyUpButton{ "^" }, companyDownButton{ "v" };
    juce::TextButton modelUpButton{ "^" }, modelDownButton{ "v" };
    // --------------------------

    // Labels
    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel;
    juce::Label cutoffLabel, resonanceLabel, waveformLabel, pulseWidthLabel, modelLabel;
    juce::Label companyLabel;
    juce::Label osc1VolLabel, osc2VolLabel, waveform2Label;
    juce::Label lfoRateLabel, lfoDepthLabel, delayMixLabel, reverbMixLabel,
                delayTimeLabel, delayFbLabel;
    juce::Label noiseMixLabel, driveAmtLabel;
    juce::Label reverbSizeLabel, reverbTypeLabel;   // NEW size label
    juce::Label masterGainLabel;                    // Master gain label

    // Attachments (unique_ptr)
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment, decayAttachment, sustainAttachment, releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> cutoffAttachment, resonanceAttachment, pulseWidthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveformAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> osc1VolAttachment, osc2VolAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> osc2SemiAttachment, osc2FineAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveform2Attachment;
    
    // Slider attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>  
        lfoRateAttachment, 
        lfoDepthAttachment,
        delayMixAttachment, 
        reverbMixAttachment,
        reverbSizeAttachment,
        delayTimeAttachment, 
        delayFbAttachment,
        noiseMixAttachment, 
        driveAmtAttachment,
        lfoPhaseAttachment;   // LFO phase offset slider attachment
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        masterGainAttachment;   // Master gain
    
    // ComboBox attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>  
        reverbTypeAttachment,
        consoleModelAttachment,
        lfoShapeAttachment,
        lfoSyncDivAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> delaySyncDivAttachment; // delay sync division attachment
    
    // ===== NEW analogue-extras toggles ========================================
    juce::TextButton freePhaseToggle{"FreePhase"}, 
                     driftToggle{"Drift"}, 
                     filterTolToggle{"FiltTol"},
                     vcaClipToggle{"VCA Clip"}, 
                     humToggle{"Hum"}, 
                     crossToggle{"Bleed"},
                     analogEnvToggle{"A‑Env"}, 
                     legatoToggle{"Legato"}; // NEW: Analog Env & Legato toggles
                     
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        freePhaseAtt, driftAtt, filterTolAtt, vcaClipAtt, humAtt, crossAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> analogEnvAtt, legatoAtt; // NEW attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lfoToggleAttachment, noiseToggleAttachment, driveToggleAttachment, delayToggleAttachment, consoleToggleAttachment, delaySyncAttachment, reverbToggleAttachment, lfoSyncAttachment, lfoToPitchAttachment, lfoToCutoffAttachment, lfoToAmpAttachment;
    // =========================================================================

    // Map of companies to synths and ID map
    std::map<std::string, std::vector<std::string>> companyToSynths;
    std::unordered_map<std::string, int> synthIdMap;

    // NEW ----------------------------------------------------------------------
    std::map<std::string, std::vector<int>> categoryToPresetIndices;
    void updatePresetDropDown(bool shouldLoadPreset = true);
    // -------------------------------------------------------------------------

    void updateModelList();

    // Cache and draw the vintage synth background to avoid flicker
    juce::Image backgroundImage;
    void drawVintageBackground(juce::Graphics& g);

    // Tap-tempo controls for unsynced delay
    juce::TextButton   tapTempoButton{"Tap"};
    juce::Label        tempoLabel;
    std::vector<double> tapTimes;         // timestamps for tap-tempo

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AllSynthPluginAudioProcessorEditor)
}; 