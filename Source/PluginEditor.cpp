#include "PluginProcessor.h"
#include "PluginEditor.h"

AllSynthPluginAudioProcessorEditor::AllSynthPluginAudioProcessorEditor(AllSynthPluginAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    auto& vts = processor.getValueTreeState();

    // --- Setup data structures ---
    struct SynthEntry { const char* name; const char* company; };
    const SynthEntry synthModels[] = {
        {"Minimoog","Moog"}, {"Prodigy","Moog"}, {"Taurus","Moog"},
        {"Model D","Moog"}, {"Memorymoog","Moog"}, {"Sub 37","Moog"},
        {"Matriarch","Moog"},
        // ── Newly added so they actually appear ────────────────────────────────
        {"Polymoog","Moog"}, {"Voyager","Moog"},
        {"ARP 2600","ARP"}, {"Odyssey","ARP"},
        {"CS-80","Yamaha"}, {"DX7","Yamaha"},
        {"Jupiter-4","Roland"},{"Jupiter-8","Roland"},{"SH-101","Roland"},{"Juno-60","Roland"},{"TB-303","Roland"},{"JP-8000","Roland"},{"JD-800","Roland"},
        {"M1","Korg"},{"Wavestation","Korg"},{"Kronos","Korg"},{"MS-20","Korg"},{"Polysix","Korg"},{"MonoPoly","Korg"},{"Minilogue","Korg"},{"MicroKorg","Korg"},
        {"Prophet-5","Sequential"},{"Prophet-6","Sequential"},{"Prophet-10","Sequential"},{"Prophet-12","Sequential"},{"Prophet VS","Sequential"},
        {"OB-X","Oberheim"},{"OB-6","Oberheim"},{"Matrix-12","Oberheim"},
        {"PolyBrute","Arturia"},{"MicroFreak","Arturia"},{"Analog Four","Elektron"},{"Massive","Native Instruments"},{"Nord Lead 2","Clavia"},{"Blofeld","Waldorf"},
        {"PPG Wave","PPG"},{"CZ-101","Casio"},{"ESQ-1","Ensoniq"},{"Hydrasynth","ASM"},
        {"OB-Xa","Oberheim"},{"OB-X8","Oberheim"},
        {"Juno-106","Roland"},{"JX-3P","Roland"},{"Jupiter-6","Roland"},{"Alpha Juno","Roland"},
        {"Grandmother","Moog"},{"Subsequent 25","Moog"},{"Moog One","Moog"},
        {"ARP Omni","ARP"},
        {"CS-30","Yamaha"},{"AN1x","Yamaha"},
        {"Prologue","Korg"},{"DW-8000","Korg"},{"MS2000","Korg"},{"Delta","Korg"},
        {"Rev2","Sequential"},{"Prophet X","Sequential"},
        {"Microwave","Waldorf"},{"Q","Waldorf"},
        {"Lead 4","Clavia"},
        {"SQ-80","Ensoniq"},
        {"CZ-5000","Casio"},
        {"System-100","Roland"},
        {"Poly Evolver","Sequential"},
        // --- DreamSynth models (fictional synths) ---
        {"Nebula","DreamSynth"},{"Solstice","DreamSynth"},{"Aurora","DreamSynth"},
        {"Lumina","DreamSynth"},{"Cascade","DreamSynth"},{"Polaris","DreamSynth"},
        {"Eclipse","DreamSynth"},{"Quasar","DreamSynth"},{"Helios","DreamSynth"},
        {"Meteor","DreamSynth"},
        // --- MixSynths models (hybrid inspirations) ---
        {"Fusion-84","MixSynths"},{"Velvet-CS","MixSynths"},{"PolyProphet","MixSynths"},
        {"BassMatrix","MixSynths"},{"WaveVoyager","MixSynths"},{"StringEvo","MixSynths"},
        {"MicroMass","MixSynths"},{"DigitalMoog","MixSynths"},{"HybridLead","MixSynths"},
        {"GlowPad","MixSynths"}
    };
    // build map
    for (auto& e : synthModels) companyToSynths[e.company].push_back(e.name);
    // build id map from parameter choices
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(vts.getParameter("MODEL")))
        for (int i=0;i<param->choices.size();++i) synthIdMap[param->choices[i].toStdString()] = i;
    // Company dropdown
    int cid=1; for (auto& cp: companyToSynths) companyBox.addItem(cp.first, cid++);
    companyBox.onChange = [this] { updateModelList(); };
    addAndMakeVisible(companyBox);
    companyLabel.setText("Company", juce::dontSendNotification);
    companyLabel.attachToComponent(&companyBox, false);
    companyLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(companyLabel);
    addAndMakeVisible(companyUpButton);   // Make visible
    addAndMakeVisible(companyDownButton); // Make visible
    companyBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(30,30,30));
    companyBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    companyBox.setSelectedId(1);
    // initial model list
    updateModelList();
    // --- Add model button visibility ---
    addAndMakeVisible(modelUpButton);
    addAndMakeVisible(modelDownButton);
    // ---------------------------------

    // --- Envelope Section ---
    attackSlider.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
    attackSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(attackSlider);
    attackLabel.setText("Attack", juce::dontSendNotification);
    attackLabel.attachToComponent(&attackSlider, false);
    attackLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(attackLabel);
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "ATTACK", attackSlider);

    decaySlider.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
    decaySlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(decaySlider);
    decayLabel.setText("Decay", juce::dontSendNotification);
    decayLabel.attachToComponent(&decaySlider, false);
    decayLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(decayLabel);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "DECAY", decaySlider);

    sustainSlider.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
    sustainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(sustainSlider);
    sustainLabel.setText("Sustain", juce::dontSendNotification);
    sustainLabel.attachToComponent(&sustainSlider, false);
    sustainLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(sustainLabel);
    sustainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "SUSTAIN", sustainSlider);

    releaseSlider.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
    releaseSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(releaseSlider);
    releaseLabel.setText("Release", juce::dontSendNotification);
    releaseLabel.attachToComponent(&releaseSlider, false);
    releaseLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(releaseLabel);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "RELEASE", releaseSlider);

    // --- Filter Section ---
    cutoffSlider.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
    cutoffSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(cutoffSlider);
    cutoffLabel.setText("Cutoff", juce::dontSendNotification);
    cutoffLabel.attachToComponent(&cutoffSlider, false);
    cutoffLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(cutoffLabel);
    cutoffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "CUTOFF", cutoffSlider);

    resonanceSlider.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
    resonanceSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(resonanceSlider);
    resonanceLabel.setText("Resonance", juce::dontSendNotification);
    resonanceLabel.attachToComponent(&resonanceSlider, false);
    resonanceLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(resonanceLabel);
    resonanceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "RESONANCE", resonanceSlider);

    // --- Oscillator Section ---
    waveformBox.addItemList({"Saw", "Square", "Pulse", "Triangle", "Sine"}, 1);
    addAndMakeVisible(waveformBox);
    waveformLabel.setText("Wave 1", juce::dontSendNotification);
    waveformLabel.attachToComponent(&waveformBox, false);
    waveformLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(waveformLabel);
    waveformAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(vts, "WAVEFORM", waveformBox);

    pulseWidthSlider.setSliderStyle(juce::Slider::SliderStyle::LinearHorizontal);
    pulseWidthSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(pulseWidthSlider);
    pulseWidthLabel.setText("Pulse Width", juce::dontSendNotification);
    pulseWidthLabel.attachToComponent(&pulseWidthSlider, false);
    pulseWidthLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(pulseWidthLabel);
    pulseWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "PULSE_WIDTH", pulseWidthSlider);

    // --- NEW SECOND OSC -------------------------------------------------------
    waveform2Box.addItemList({"Saw","Square","Pulse","Triangle","Sine"},1);
    addAndMakeVisible(waveform2Box);
    waveform2Label.setText("Wave 2", juce::dontSendNotification);
    waveform2Label.attachToComponent(&waveform2Box,false); waveform2Label.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(waveform2Label);
    waveform2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(vts,"WAVEFORM2",waveform2Box);

    osc1VolSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    osc1VolSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,50,20);
    addAndMakeVisible(osc1VolSlider);
    osc1VolLabel.setText("Vol 1", juce::dontSendNotification);
    osc1VolLabel.attachToComponent(&osc1VolSlider,false); osc1VolLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(osc1VolLabel);
    osc1VolAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts,"OSC1_VOLUME",osc1VolSlider);

    osc2VolSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    osc2VolSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,50,20);
    addAndMakeVisible(osc2VolSlider);
    osc2VolLabel.setText("Vol 2", juce::dontSendNotification);
    osc2VolLabel.attachToComponent(&osc2VolSlider,false); osc2VolLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(osc2VolLabel);
    osc2VolAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts,"OSC2_VOLUME",osc2VolSlider);

    // ===== NEW – Osc‑2 Detune (Semi) ==========================================
    osc2SemiSlider.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
    osc2SemiSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(osc2SemiSlider);
    osc2SemiLabel.setText("Semi 2", juce::dontSendNotification);
    osc2SemiLabel.attachToComponent(&osc2SemiSlider, false);
    osc2SemiLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(osc2SemiLabel);
    osc2SemiAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "OSC2_SEMI", osc2SemiSlider);

    // ===== NEW – Osc‑2 Detune (Fine) =========================================
    osc2FineSlider.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
    osc2FineSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(osc2FineSlider);
    osc2FineLabel.setText("Fine 2", juce::dontSendNotification);
    osc2FineLabel.attachToComponent(&osc2FineSlider, false);
    osc2FineLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(osc2FineLabel);
    osc2FineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "OSC2_FINE", osc2FineSlider);

    // --- Model Section ---
    modelBox.addSectionHeading("Moog");
    modelBox.addItem("Minimoog", 1);
    modelBox.addItem("Prodigy", 2);
    modelBox.addItem("Taurus", 3);
    modelBox.addItem("Model D", 4);
    modelBox.addItem("Memorymoog", 5);
    modelBox.addItem("Sub 37", 6);
    modelBox.addItem("Matriarch", 7);
    modelBox.addSectionHeading("ARP");
    modelBox.addItem("ARP 2600", 8);
    modelBox.addItem("Odyssey", 9);
    modelBox.addSectionHeading("Yamaha");
    modelBox.addItem("CS-80", 10);
    modelBox.addItem("DX7", 11);
    modelBox.addSectionHeading("Roland");
    modelBox.addItem("Jupiter-4", 12);
    modelBox.addItem("Jupiter-8", 13);
    modelBox.addItem("SH-101", 14);
    modelBox.addItem("Juno-60", 15);
    modelBox.addItem("TB-303", 16);
    modelBox.addItem("JP-8000", 17);
    modelBox.addItem("JD-800", 18);
    modelBox.addSectionHeading("Korg");
    modelBox.addItem("M1", 19);
    modelBox.addItem("Wavestation", 20);
    modelBox.addItem("Kronos", 21);
    modelBox.addItem("MS-20", 22);
    modelBox.addItem("Polysix", 23);
    modelBox.addItem("MonoPoly", 24);
    modelBox.addItem("Minilogue", 25);
    modelBox.addItem("MicroKorg", 26);
    modelBox.addSectionHeading("Sequential");
    modelBox.addItem("Prophet-5", 27);
    modelBox.addItem("Prophet-6", 28);
    modelBox.addItem("Prophet-10", 29);
    modelBox.addItem("Prophet-12", 30);
    modelBox.addItem("Prophet VS", 31);
    modelBox.addSectionHeading("Oberheim");
    modelBox.addItem("OB-X", 32);
    modelBox.addItem("OB-6", 33);
    modelBox.addItem("Matrix-12", 34);
    modelBox.addSectionHeading("Arturia");
    modelBox.addItem("PolyBrute", 35);
    modelBox.addItem("MicroFreak", 36);
    modelBox.addSectionHeading("Elektron");
    modelBox.addItem("Analog Four", 37);
    modelBox.addSectionHeading("Native Instruments");
    modelBox.addItem("Massive", 38);
    modelBox.addSectionHeading("Clavia");
    modelBox.addItem("Nord Lead 2", 39);
    modelBox.addSectionHeading("Waldorf");
    modelBox.addItem("Blofeld", 40);
    modelBox.addSectionHeading("PPG");
    modelBox.addItem("PPG Wave", 41);
    modelBox.addSectionHeading("Casio");
    modelBox.addItem("CZ-101", 42);
    modelBox.addSectionHeading("Ensoniq");
    modelBox.addItem("ESQ-1", 43);
    modelBox.addSectionHeading("ASM");
    modelBox.addItem("Hydrasynth", 44);
    addAndMakeVisible(modelBox);
    modelLabel.setText("Synth Model", juce::dontSendNotification);
    modelLabel.attachToComponent(&modelBox, false); // Attach above
    modelLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(modelLabel);
    modelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(vts, "MODEL", modelBox);

    // --- LFO ------------------------------------------------------------------
    addAndMakeVisible(lfoToggle);
    lfoToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts,"LFO_ON",lfoToggle);

    lfoRateSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    lfoRateSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,50,20);
    addAndMakeVisible(lfoRateSlider);
    lfoRateLabel.setText("Rate", juce::dontSendNotification);
    lfoRateLabel.attachToComponent(&lfoRateSlider,false); lfoRateLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(lfoRateLabel);
    lfoRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts,"LFO_RATE",lfoRateSlider);

    lfoDepthSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    lfoDepthSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,50,20);
    addAndMakeVisible(lfoDepthSlider);
    lfoDepthLabel.setText("Depth", juce::dontSendNotification);
    lfoDepthLabel.attachToComponent(&lfoDepthSlider,false); lfoDepthLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(lfoDepthLabel);
    lfoDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts,"LFO_DEPTH",lfoDepthSlider);

    // --- Noise & Drive ---------------------------------------------------------
    addAndMakeVisible(noiseToggle);
    noiseToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts,"NOISE_ON",noiseToggle);

    noiseMixSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    noiseMixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,50,20);
    addAndMakeVisible(noiseMixSlider);
    noiseMixLabel.setText("N-Mix", juce::dontSendNotification);
    noiseMixLabel.attachToComponent(&noiseMixSlider,false);
    noiseMixLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(noiseMixLabel);
    noiseMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts,"NOISE_MIX",noiseMixSlider);

    addAndMakeVisible(driveToggle);
    driveToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts,"DRIVE_ON",driveToggle);

    driveAmtSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    driveAmtSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,50,20);
    addAndMakeVisible(driveAmtSlider);
    driveAmtLabel.setText("Drive Amt", juce::dontSendNotification);
    driveAmtLabel.attachToComponent(&driveAmtSlider,false);
    driveAmtLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(driveAmtLabel);
    driveAmtAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts,"DRIVE_AMT",driveAmtSlider);

    // --- Console toggle --------------------------------------------------------
    addAndMakeVisible(consoleToggle);
    consoleToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts,"CONSOLE_ON",consoleToggle);

    // NEW:  Pre‑amp model selector ---------------------------------------------
    consoleModelBox.addItemList({ "Tape Thick","Warm Tube","Deep Console",
                                  "Punch Glue","Sub Boom","Opto Smooth",
                                  "Tube Crunch","X-Former Fat","Bus Glue",
                                  "Vintage Tape","Neve 1073","API 312/550A",
                                  "Helios 69","Studer A80","EMI TG12345",
                                  "SSL 4K-Bus","LA-2A","Fairchild 670",
                                  "Pultec EQP-1A","Quad-Eight",
                                  "Harrison 32","MCI JH-636",
                                  "API 2500","Ampex 440","Moog Ladder Out" }, 1);
    addAndMakeVisible(consoleModelBox);
    consoleModelLabel.setText("Mode", juce::dontSendNotification);
    consoleModelAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(vts,
                                                                "CONSOLE_MODEL", consoleModelBox);

    // ===== initialise analogue-extra toggles =================================
    using APVTS = juce::AudioProcessorValueTreeState;

    for (auto* t : { &freePhaseToggle, &driftToggle, &filterTolToggle,
                     &vcaClipToggle, &humToggle, &crossToggle,
                     &analogEnvToggle, &legatoToggle })
    {
        addAndMakeVisible(*t);
    }

    // Attachments
    freePhaseAtt = std::make_unique<APVTS::ButtonAttachment>(vts, "ANA_FREE",     freePhaseToggle);
    driftAtt     = std::make_unique<APVTS::ButtonAttachment>(vts, "ANA_DRIFT",    driftToggle);
    filterTolAtt = std::make_unique<APVTS::ButtonAttachment>(vts, "ANA_FILT_TOL", filterTolToggle);
    vcaClipAtt   = std::make_unique<APVTS::ButtonAttachment>(vts, "ANA_VCA_CLIP", vcaClipToggle);
    humAtt       = std::make_unique<APVTS::ButtonAttachment>(vts, "HUM_ON",       humToggle);
    crossAtt     = std::make_unique<APVTS::ButtonAttachment>(vts, "CROSS_ON",     crossToggle);
    analogEnvAtt = std::make_unique<APVTS::ButtonAttachment>(vts, "ANA_ENV",      analogEnvToggle);
    legatoAtt    = std::make_unique<APVTS::ButtonAttachment>(vts, "ANA_LEGATO",   legatoToggle);
    // =========================================================================

    // ===== Master Gain Control ==============================================
    masterGainSlider.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
    masterGainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(masterGainSlider);
    masterGainLabel.setText("Master", juce::dontSendNotification);
    masterGainLabel.attachToComponent(&masterGainSlider, false);
    masterGainLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(masterGainLabel);
    masterGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "MASTER_GAIN", masterGainSlider);
    // =========================================================================

    // ----------  PRESET UI ----------------------------------------------------
    const auto& presets = processor.getPresets();
    for (size_t i = 0; i < presets.size(); ++i)
        categoryToPresetIndices[presets[i].category].push_back((int)i);

    int cid_preset = 1;
    for (const auto& cp : categoryToPresetIndices)
        presetCategoryBox.addItem(cp.first, cid_preset++);

    presetCategoryBox.onChange = [this]{ updatePresetDropDown(true); };
    addAndMakeVisible(presetCategoryBox);
    // --- Add category button visibility ---
    addAndMakeVisible(presetCategoryUpButton);
    addAndMakeVisible(presetCategoryDownButton);
    // ------------------------------------
    presetCategoryLabel.setText("Category", juce::dontSendNotification);
    presetCategoryLabel.attachToComponent(&presetCategoryBox, false);
    presetCategoryLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(presetCategoryLabel);

    addAndMakeVisible(presetBox);
    // --- Add preset button visibility ---
    addAndMakeVisible(presetUpButton);
    addAndMakeVisible(presetDownButton);
    // ----------------------------------
    presetLabel.setText("Preset", juce::dontSendNotification);
    presetLabel.attachToComponent(&presetBox, false);
    presetLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(presetLabel);

    presetBox.onChange = [this,&p=processor]
    {
        int id = presetBox.getSelectedId() - 1;     // itemId = presetIndex+1
        p.loadPreset(id);
        
        // Fix - Update company and model dropdowns to match the MODEL parameter value
        auto* modelParam = p.getValueTreeState().getParameter("MODEL");
        if (modelParam) {
            int modelValue = (int)modelParam->convertFrom0to1(modelParam->getValue());
            
            // We need to find which model name corresponds to this model value
            std::string modelName;
            for (const auto& entry : synthIdMap) {
                if (entry.second == modelValue) {
                    modelName = entry.first;
                    break;
                }
            }
            
            if (!modelName.empty()) {
                // Find which company this model belongs to
                std::string companyName;
                for (const auto& comp : companyToSynths) {
                    auto it = std::find(comp.second.begin(), comp.second.end(), modelName);
                    if (it != comp.second.end()) {
                        companyName = comp.first;
                        break;
                    }
                }
                
                if (!companyName.empty()) {
                    // Select company in the dropdown (triggers updateModelList)
                    for (int i = 1; i <= companyBox.getNumItems(); ++i) {
                        if (companyBox.getItemText(i-1).toStdString() == companyName) {
                            companyBox.setSelectedId(i, juce::sendNotification);
                            // After model list is updated, select the correct model
                            for (int j = 1; j <= modelBox.getNumItems(); ++j) {
                                if (modelBox.getItemText(j-1).toStdString() == modelName) {
                                    modelBox.setSelectedId(modelBox.getItemId(j-1), juce::dontSendNotification);
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }
    };

    presetCategoryBox.setSelectedId(1, juce::dontSendNotification);  // Set the ID without triggering notification
    updatePresetDropDown(false);  // Initialize the preset dropdown without loading a preset
    
    // Initialize UI with current parameter values
    auto* modelParam = processor.getValueTreeState().getParameter("MODEL");
    if (modelParam) {
        int modelValue = (int)modelParam->convertFrom0to1(modelParam->getValue());
        
        // Find which model name corresponds to this model value
        std::string modelName;
        for (const auto& entry : synthIdMap) {
            if (entry.second == modelValue) {
                modelName = entry.first;
                break;
            }
        }
        
        if (!modelName.empty()) {
            // Find which company this model belongs to
            std::string companyName;
            for (const auto& comp : companyToSynths) {
                auto it = std::find(comp.second.begin(), comp.second.end(), modelName);
                if (it != comp.second.end()) {
                    companyName = comp.first;
                    break;
                }
            }
            
            if (!companyName.empty()) {
                // Select company in the dropdown (triggers updateModelList)
                for (int i = 1; i <= companyBox.getNumItems(); ++i) {
                    if (companyBox.getItemText(i-1).toStdString() == companyName) {
                        companyBox.setSelectedId(i, juce::sendNotification);
                        // After model list is updated, select the correct model
                        for (int j = 1; j <= modelBox.getNumItems(); ++j) {
                            if (modelBox.getItemText(j-1).toStdString() == modelName) {
                                modelBox.setSelectedId(modelBox.getItemId(j-1), juce::dontSendNotification);
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    // --- Delay / Reverb (add Time, FB, Sync controls)-------------------------
    addAndMakeVisible(delayToggle);
    delayToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts,"DELAY_ON",delayToggle);

    delayMixSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    delayMixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,50,20);
    addAndMakeVisible(delayMixSlider);
    delayMixLabel.setText("D-Mix", juce::dontSendNotification);
    delayMixLabel.attachToComponent(&delayMixSlider,false);
    delayMixLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(delayMixLabel);
    delayMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts,"DELAY_MIX",delayMixSlider);

    delayTimeSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    delayTimeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,50,20);
    addAndMakeVisible(delayTimeSlider);
    delayTimeLabel.setText("Time", juce::dontSendNotification);
    delayTimeLabel.attachToComponent(&delayTimeSlider,false);
    delayTimeLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(delayTimeLabel);
    delayTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts,"DELAY_TIME",delayTimeSlider);

    delayFeedbackSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    delayFeedbackSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,50,20);
    addAndMakeVisible(delayFeedbackSlider);
    delayFbLabel.setText("FB", juce::dontSendNotification);
    delayFbLabel.attachToComponent(&delayFeedbackSlider,false);
    delayFbLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(delayFbLabel);
    delayFbAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts,"DELAY_FB",delayFeedbackSlider);

    addAndMakeVisible(delaySyncToggle);
    delaySyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts,"DELAY_SYNC",delaySyncToggle);

    addAndMakeVisible(reverbToggle);
    reverbToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts,"REVERB_ON",reverbToggle);

    reverbMixSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    reverbMixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,50,20);
    addAndMakeVisible(reverbMixSlider);
    reverbMixLabel.setText("R-Mix", juce::dontSendNotification);
    reverbMixLabel.attachToComponent(&reverbMixSlider,false);
    reverbMixLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(reverbMixLabel);
    reverbMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts,"REVERB_MIX",reverbMixSlider);

    // NEW – Size slider
    reverbSizeSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    reverbSizeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,50,20);
    addAndMakeVisible(reverbSizeSlider);
    reverbSizeLabel.setText("R-Size", juce::dontSendNotification);
    reverbSizeLabel.attachToComponent(&reverbSizeSlider,false);
    reverbSizeLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(reverbSizeLabel);
    reverbSizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts,"REVERB_SIZE",reverbSizeSlider);

    // --- NEW : Reverb Type selector -----------------------------------
    reverbTypeBox.clear(juce::dontSendNotification);
    reverbTypeBox.addItemList({ "Classic", "Hall", "Plate", "Shimmer",
                                "Spring", "Room", "Cathedral", "Gated" }, 1);
    addAndMakeVisible(reverbTypeBox);
    reverbTypeLabel.setText("R-Type", juce::dontSendNotification);
    reverbTypeLabel.attachToComponent(&reverbTypeBox, false);
    reverbTypeLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(reverbTypeLabel);
    reverbTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        vts, "REVERB_TYPE", reverbTypeBox);

    // --- LFO Sync toggle & Shape selector ----------------------------------
    addAndMakeVisible(lfoSyncToggle);
    lfoSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        vts, "LFO_SYNC", lfoSyncToggle);

    // Clear then populate the LFO‐shape dropdown exactly once:
    lfoShapeBox.clear(juce::dontSendNotification);
    lfoShapeBox.addItemList({"Sine", "Triangle", "Saw", "Square"}, 1);
    addAndMakeVisible(lfoShapeBox);
    lfoShapeLabel.setText("Shape", juce::dontSendNotification);
    lfoShapeLabel.attachToComponent(&lfoShapeBox, false);
    addAndMakeVisible(lfoShapeLabel);
    lfoShapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        vts, "LFO_SHAPE", lfoShapeBox);

    // --- LFO sync division and phase offset and routing ---------------------
    // Populate the sync‐division dropdown so it actually has items:
    lfoSyncDivBox.clear(juce::dontSendNotification);
    lfoSyncDivBox.addItemList({"1/1", "1/2", "1/4", "1/8", "1/16", "1/4.", "1/8."}, 1);
    addAndMakeVisible(lfoSyncDivBox);
    lfoSyncDivLabel.setText("Div", juce::dontSendNotification);
    lfoSyncDivLabel.attachToComponent(&lfoSyncDivBox, false);
    addAndMakeVisible(lfoSyncDivLabel);
    lfoSyncDivAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        vts, "LFO_SYNC_DIV", lfoSyncDivBox);

    addAndMakeVisible(lfoPhaseSlider);
    lfoPhaseSlider.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
    lfoPhaseSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    lfoPhaseLabel.setText("Phase", juce::dontSendNotification);
    lfoPhaseLabel.attachToComponent(&lfoPhaseSlider, false);
    lfoPhaseLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(lfoPhaseLabel);
    lfoPhaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "LFO_PHASE", lfoPhaseSlider);

    addAndMakeVisible(lfoToPitchToggle);
    lfoToPitchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "LFO_TO_PITCH", lfoToPitchToggle);
    addAndMakeVisible(lfoToCutoffToggle);
    lfoToCutoffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "LFO_TO_CUTOFF", lfoToCutoffToggle);
    addAndMakeVisible(lfoToAmpToggle);
    lfoToAmpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "LFO_TO_AMP", lfoToAmpToggle);

    // --- Delay / Reverb (add Time, FB, Sync controls)-------------------------
    addAndMakeVisible(delaySyncToggle);
    delaySyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts,"DELAY_SYNC",delaySyncToggle);
    // Delay sync division selector
    delaySyncDivBox.addItemList({"1/1","1/2","1/4","1/8","1/16","1/4.","1/8."}, 1);
    addAndMakeVisible(delaySyncDivBox);
    delaySyncDivLabel.setText("Div", juce::dontSendNotification);
    delaySyncDivLabel.attachToComponent(&delaySyncDivBox, false);
    addAndMakeVisible(delaySyncDivLabel);
    delaySyncDivAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(vts, "DELAY_SYNC_DIV", delaySyncDivBox);

    addAndMakeVisible(reverbToggle);
    reverbToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts,"REVERB_ON",reverbToggle);

    reverbMixSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    reverbMixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,50,20);
    addAndMakeVisible(reverbMixSlider);
    reverbMixLabel.setText("R-Mix", juce::dontSendNotification);
    reverbMixLabel.attachToComponent(&reverbMixSlider,false);
    reverbMixLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(reverbMixLabel);
    reverbMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts,"REVERB_MIX",reverbMixSlider);

    // NEW – Size slider
    reverbSizeSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    reverbSizeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,50,20);
    addAndMakeVisible(reverbSizeSlider);
    reverbSizeLabel.setText("R-Size", juce::dontSendNotification);
    reverbSizeLabel.attachToComponent(&reverbSizeSlider,false);
    reverbSizeLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(reverbSizeLabel);
    reverbSizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts,"REVERB_SIZE",reverbSizeSlider);

    // ---------- NEW : Reverb Type selector -----------------------------------
    reverbTypeBox.addItemList({ "Classic","Hall","Plate","Shimmer",
                               "Spring","Room","Cathedral","Gated" },1);
    addAndMakeVisible(reverbTypeBox);
    reverbTypeLabel.setText("R-Type", juce::dontSendNotification);
    reverbTypeLabel.attachToComponent(&reverbTypeBox,false);
    reverbTypeLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(reverbTypeLabel);
    reverbTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(vts,
                                                                                                    "REVERB_TYPE",
                                                                                                    reverbTypeBox);

    // --- LFO Sync toggle & Shape selector ----------------------------------
    addAndMakeVisible(lfoSyncToggle);
    lfoSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        vts, "LFO_SYNC", lfoSyncToggle);

    // Clear then populate the LFO‐shape dropdown exactly once:
    lfoShapeBox.clear(juce::dontSendNotification);
    lfoShapeBox.addItemList({"Sine", "Triangle", "Saw", "Square"}, 1);
    addAndMakeVisible(lfoShapeBox);
    lfoShapeLabel.setText("Shape", juce::dontSendNotification);
    lfoShapeLabel.attachToComponent(&lfoShapeBox, false);
    addAndMakeVisible(lfoShapeLabel);
    lfoShapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        vts, "LFO_SHAPE", lfoShapeBox);

    // --- LFO sync division and phase offset and routing ---------------------
    // Populate the sync‐division dropdown so it actually has items:
    lfoSyncDivBox.clear(juce::dontSendNotification);
    lfoSyncDivBox.addItemList({"1/1", "1/2", "1/4", "1/8", "1/16", "1/4.", "1/8."}, 1);
    addAndMakeVisible(lfoSyncDivBox);
    lfoSyncDivLabel.setText("Div", juce::dontSendNotification);
    lfoSyncDivLabel.attachToComponent(&lfoSyncDivBox, false);
    addAndMakeVisible(lfoSyncDivLabel);
    lfoSyncDivAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        vts, "LFO_SYNC_DIV", lfoSyncDivBox);

    addAndMakeVisible(lfoPhaseSlider);
    lfoPhaseSlider.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
    lfoPhaseSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    lfoPhaseLabel.setText("Phase", juce::dontSendNotification);
    lfoPhaseLabel.attachToComponent(&lfoPhaseSlider, false);
    lfoPhaseLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(lfoPhaseLabel);
    lfoPhaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "LFO_PHASE", lfoPhaseSlider);

    addAndMakeVisible(lfoToPitchToggle);
    lfoToPitchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "LFO_TO_PITCH", lfoToPitchToggle);
    addAndMakeVisible(lfoToCutoffToggle);
    lfoToCutoffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "LFO_TO_CUTOFF", lfoToCutoffToggle);
    addAndMakeVisible(lfoToAmpToggle);
    lfoToAmpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "LFO_TO_AMP", lfoToAmpToggle);

    // --- tap-tempo & tempo display (unsynced delay only) -------------
    addAndMakeVisible(tapTempoButton);
    tapTempoButton.setTooltip("Tap to set delay time (quarter-note)");
    tapTempoButton.onClick = [this]()
    {
        auto now = juce::Time::getMillisecondCounterHiRes();
        tapTimes.push_back(now);
        if (tapTimes.size() > 4)
            tapTimes.erase(tapTimes.begin());  // keep last 4 taps

        if (tapTimes.size() >= 2)
        {
            double sum = 0;
            for (size_t i = 1; i < tapTimes.size(); ++i)
                sum += (tapTimes[i] - tapTimes[i-1]);
            double avgMs = sum / (tapTimes.size() - 1);
            double bpm   = 60000.0 / avgMs;

            tempoLabel.setText(juce::String(bpm, 1) + " BPM", juce::dontSendNotification);

            // set delay time slider to quarter-note in ms
            delayTimeSlider.setValue(avgMs, juce::sendNotification);
        }
    };

    addAndMakeVisible(tempoLabel);
    tempoLabel.setJustificationType(juce::Justification::centred);
    tempoLabel.setText("-- BPM", juce::dontSendNotification);

    // show/hide tap controls: only hide when syncing to host BPM, otherwise show for tap sync or manual
    auto updateTapVis = [this]()
    {
        bool syncOn = delaySyncToggle.getToggleState();
        bool hostSync = false;
        if (syncOn)
        {
            if (auto* ph = processor.getPlayHead())
            {
                juce::AudioPlayHead::CurrentPositionInfo pos;
                if (ph->getCurrentPosition(pos) && pos.bpm > 0.0)
                    hostSync = true;
            }
        }
        // hide tap controls if we are syncing to host; show otherwise (manual or tap sync)
        tapTempoButton.setVisible(!hostSync);
        tempoLabel    .setVisible(!hostSync);
    };
    updateTapVis();
    delaySyncToggle.onClick = updateTapVis;

    // After creating all the sliders but before styling
    
    // Set up larger slider sizes
    const auto setupRotarySlider = [](juce::Slider& slider)
    {
        slider.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
        slider.setSize(80, 80); // Make the slider larger
    };
    
    // Apply to all rotary sliders
    for (auto* slider : {&attackSlider, &decaySlider, &sustainSlider, &releaseSlider,
                        &cutoffSlider, &resonanceSlider, 
                        &osc1VolSlider, &osc2VolSlider, &osc2SemiSlider, &osc2FineSlider,
                        &lfoRateSlider, &lfoDepthSlider,
                        &delayMixSlider, &reverbMixSlider, &reverbSizeSlider,
                        &delayTimeSlider, &delayFeedbackSlider,
                        &noiseMixSlider,&driveAmtSlider,
                        &masterGainSlider})
    {
        setupRotarySlider(*slider);
    }
    
    // Make the horizontal slider larger too
    pulseWidthSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    
    // Modern UI color scheme
    const juce::Colour bgColor = juce::Colours::black;
    const juce::Colour accentColor = juce::Colour(0, 215, 187);     // Teal/cyan
    const juce::Colour secondaryColor = juce::Colour(88, 97, 224);  // Purple/blue
    const juce::Colour textColor = juce::Colour(240, 240, 240);     // Almost white
    const juce::Colour controlBgColor = juce::Colour(30, 30, 40);   // Dark blue-gray
    const juce::Colour sliderTrackColor = juce::Colour(60, 60, 70); // Lighter gray

    getLookAndFeel().setColour(juce::ResizableWindow::backgroundColourId, bgColor);
    
    // Style labels
    for (auto* lbl: {&attackLabel,&decayLabel,&sustainLabel,&releaseLabel,&cutoffLabel,&resonanceLabel,
                    &waveformLabel,&pulseWidthLabel,&modelLabel,&osc1VolLabel,&osc2VolLabel,&waveform2Label,
                    &lfoRateLabel,&lfoDepthLabel,&delayMixLabel,&delayTimeLabel,&delayFbLabel,
                    &delaySyncDivLabel,
                    &reverbMixLabel, &reverbTypeLabel, &reverbSizeLabel,
                    &noiseMixLabel,&driveAmtLabel,&companyLabel,
                    &presetCategoryLabel,&presetLabel,&consoleModelLabel,&masterGainLabel}) {
        lbl->setColour(juce::Label::textColourId, textColor);
        lbl->setFont(juce::Font(16.0f).withStyle(juce::Font::bold));
    }
    
    // Style combo boxes
    for (auto* cb: {&companyBox,&modelBox,&waveformBox,&waveform2Box,&lfoShapeBox,&presetCategoryBox,&presetBox,&consoleModelBox, &delaySyncDivBox}) {
        cb->setColour(juce::ComboBox::backgroundColourId, controlBgColor);
        cb->setColour(juce::ComboBox::textColourId, textColor);
        cb->setColour(juce::ComboBox::arrowColourId, accentColor);
        cb->setColour(juce::ComboBox::outlineColourId, controlBgColor.darker(0.5f));
        cb->setColour(juce::ComboBox::buttonColourId, secondaryColor.darker(0.2f));
    }
    
    // Style toggle buttons
    for (auto* b : { &lfoToggle, &lfoSyncToggle, &lfoToPitchToggle, &lfoToCutoffToggle, &lfoToAmpToggle, &noiseToggle, &driveToggle,
                     &delayToggle, &reverbToggle, &delaySyncToggle, &consoleToggle,
                     &freePhaseToggle, &driftToggle, &filterTolToggle,
                     &vcaClipToggle, &humToggle, &crossToggle,
                     &analogEnvToggle, &legatoToggle })
    {
        b->setClickingTogglesState(true);               // behave like a toggle
        b->setColour(juce::TextButton::buttonColourId, controlBgColor);
        b->setColour(juce::TextButton::buttonOnColourId, accentColor);
        b->setColour(juce::TextButton::textColourOffId, textColor);
        b->setColour(juce::TextButton::textColourOnId, textColor);
        
        // Make buttons more compact - size to text width plus padding
        const int textWidth = juce::Font(16.0f).getStringWidth(b->getButtonText());
        const int buttonWidth = textWidth + 20; // add padding
        b->setSize(buttonWidth, 30);
    }
    
    // Style sliders
    for (auto* sw: {&attackSlider,&decaySlider,&sustainSlider,&releaseSlider,
                   &cutoffSlider,&resonanceSlider,&pulseWidthSlider,
                   &osc1VolSlider,&osc2VolSlider,&osc2SemiSlider,&osc2FineSlider,&lfoRateSlider,&lfoDepthSlider,
                   &delayMixSlider,&reverbMixSlider,&delayTimeSlider,&delayFeedbackSlider,
                   &noiseMixSlider,&driveAmtSlider,
                   &masterGainSlider}) {
        sw->setColour(juce::Slider::thumbColourId, accentColor);
        sw->setColour(juce::Slider::trackColourId, sliderTrackColor);
        sw->setColour(juce::Slider::rotarySliderFillColourId, secondaryColor);
        sw->setColour(juce::Slider::rotarySliderOutlineColourId, sliderTrackColor);
        sw->setColour(juce::Slider::textBoxTextColourId, textColor);
        sw->setColour(juce::Slider::textBoxOutlineColourId, controlBgColor.darker(0.5f));
        sw->setColour(juce::Slider::textBoxBackgroundColourId, controlBgColor);
    }

    // --- Button Click Logic ---
    auto setupNavButton = [](juce::ComboBox& box, juce::TextButton& upButton, juce::TextButton& downButton)
    {
        upButton.onClick = [&box]() {
            int currentId = box.getSelectedId();
            int numItems = box.getNumItems();
            if (numItems <= 1) return; // No change needed

            int currentIndex = -1;
            for(int i = 0; i < numItems; ++i) {
                if (box.getItemId(i) == currentId) {
                    currentIndex = i;
                    break;
                }
            }

            if (currentIndex != -1) {
                int nextIndex = (currentIndex - 1 + numItems) % numItems;
                box.setSelectedId(box.getItemId(nextIndex), juce::sendNotification);
            } else if (numItems > 0) { // If no valid ID selected, go to last item
                 box.setSelectedId(box.getItemId(numItems - 1), juce::sendNotification);
            }
        };

        downButton.onClick = [&box]() {
            int currentId = box.getSelectedId();
            int numItems = box.getNumItems();
            if (numItems <= 1) return; // No change needed

            int currentIndex = -1;
            for(int i = 0; i < numItems; ++i) {
                if (box.getItemId(i) == currentId) {
                    currentIndex = i;
                    break;
                }
            }

             if (currentIndex != -1) {
                int nextIndex = (currentIndex + 1) % numItems;
                box.setSelectedId(box.getItemId(nextIndex), juce::sendNotification);
            } else if (numItems > 0) { // If no valid ID selected, go to first item
                box.setSelectedId(box.getItemId(0), juce::sendNotification);
            }
        };
    };

    setupNavButton(presetCategoryBox, presetCategoryUpButton, presetCategoryDownButton);
    setupNavButton(presetBox, presetUpButton, presetDownButton);
    setupNavButton(companyBox, companyUpButton, companyDownButton);
    setupNavButton(modelBox, modelUpButton, modelDownButton);

    // --- Style new buttons ---
    for(auto* btn : {&presetCategoryUpButton, &presetCategoryDownButton, &presetUpButton, &presetDownButton,
                     &companyUpButton, &companyDownButton, &modelUpButton, &modelDownButton})
    {
        btn->setColour(juce::TextButton::buttonColourId, controlBgColor);
        btn->setColour(juce::TextButton::textColourOffId, accentColor);
        btn->setColour(juce::TextButton::textColourOnId, textColor); // Colour when pressed
    }
    // -------------------------
    
    // Set the window size
    setSize(1200, 850); // Increased height to accommodate all controls

    // Populate preset dropdown without loading a preset
    updatePresetDropDown(false);

    // ===== Section‑specific fill colours =====
    // (this overrides the "rotarySliderFillColourId" per control section)
    const juce::Colour oscColour    = juce::Colour(0, 215, 187);  // Teal (oscillators)
    const juce::Colour filterColour = juce::Colour(88, 97, 224);  // Purple (filter/env)
    const juce::Colour lfoColour    = juce::Colour(224, 88, 97);  // Coral (LFO/noise-drive)
    const juce::Colour fxColour     = juce::Colour(97, 224, 88);  // Lime (delay/reverb)

    // Oscillator section sliders
    for (auto* slider : {&osc1VolSlider, &osc2VolSlider, &osc2SemiSlider, &osc2FineSlider, &pulseWidthSlider}) {
        slider->setColour(juce::Slider::rotarySliderFillColourId, oscColour);
        slider->setColour(juce::Slider::thumbColourId, oscColour);
    }

    // Oscillator section ComboBoxes
    waveformBox.setColour(juce::ComboBox::arrowColourId, oscColour);
    waveform2Box.setColour(juce::ComboBox::arrowColourId, oscColour);
    waveformBox.setColour(juce::ComboBox::buttonColourId, oscColour.darker(0.2f));
    waveform2Box.setColour(juce::ComboBox::buttonColourId, oscColour.darker(0.2f));

    // Filter / Envelope sliders
    for (auto* slider : {&cutoffSlider, &resonanceSlider, &attackSlider, &decaySlider, &sustainSlider, &releaseSlider, &masterGainSlider}) {
        slider->setColour(juce::Slider::rotarySliderFillColourId, filterColour);
        slider->setColour(juce::Slider::thumbColourId, filterColour);
    }

    // LFO / Noise-Drive sliders
    for (auto* slider : {&lfoRateSlider, &lfoDepthSlider, &noiseMixSlider, &driveAmtSlider}) {
        slider->setColour(juce::Slider::rotarySliderFillColourId, lfoColour);
        slider->setColour(juce::Slider::thumbColourId, lfoColour);
    }

    // FX sliders: separate delay and reverb colours
    for (auto* slider : {&delayMixSlider, &delayTimeSlider, &delayFeedbackSlider}) {
        slider->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(88, 97, 224));  // Purple (delay)
        slider->setColour(juce::Slider::thumbColourId, juce::Colour(88, 97, 224));
    }
    for (auto* slider : {&reverbMixSlider, &reverbSizeSlider}) {
        slider->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(97, 224, 88));  // Lime (reverb)
        slider->setColour(juce::Slider::thumbColourId, juce::Colour(97, 224, 88));
    }

    // Model section ComboBoxes
    companyBox.setColour(juce::ComboBox::arrowColourId, filterColour);
    modelBox.setColour(juce::ComboBox::arrowColourId, filterColour);
    companyBox.setColour(juce::ComboBox::buttonColourId, filterColour.darker(0.2f));
    modelBox.setColour(juce::ComboBox::buttonColourId, filterColour.darker(0.2f));

    // Effect ComboBoxes
    reverbTypeBox.setColour(juce::ComboBox::arrowColourId, juce::Colour(97, 224, 88));  // Lime (reverb)
    consoleModelBox.setColour(juce::ComboBox::arrowColourId, juce::Colour(97, 224, 88));  // Lime (reverb)
    reverbTypeBox.setColour(juce::ComboBox::buttonColourId, juce::Colour(97, 224, 88).darker(0.2f));  // Darker lime
    consoleModelBox.setColour(juce::ComboBox::buttonColourId, juce::Colour(97, 224, 88).darker(0.2f));  // Darker lime

    // Toggles with matching section colours
    lfoToggle.setColour(juce::TextButton::buttonOnColourId, lfoColour);
    noiseToggle.setColour(juce::TextButton::buttonOnColourId, lfoColour);
    driveToggle.setColour(juce::TextButton::buttonOnColourId, lfoColour);

    delayToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colour(88, 97, 224));  // Purple (delay)
    delaySyncToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colour(88, 97, 224));  // Purple (delay)
    reverbToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colour(97, 224, 88));  // Lime (reverb)
    consoleToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colour(97, 224, 88));  // Lime (reverb)
    // =========================================================================

    // Build the cached background once:
    backgroundImage = juce::Image(juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics ig{ backgroundImage };  // Use brace initialization to avoid vexing parse
    drawVintageBackground(ig);

    // show/hide LFO sync controls: hide when host-sync, show otherwise
    auto updateLfoVis = [this]()
    {
        bool syncOn  = lfoSyncToggle.getToggleState();
        bool hostSync = false;
        if (syncOn)
        {
            if (auto* ph = processor.getPlayHead())
            {
                juce::AudioPlayHead::CurrentPositionInfo pos;
                if (ph->getCurrentPosition(pos) && pos.bpm > 0.0)
                    hostSync = true;
            }
        }
        bool showControls = syncOn && !hostSync;
        lfoSyncDivBox.setVisible(showControls);
        lfoPhaseSlider.setVisible(showControls);
        lfoSyncDivLabel.setVisible(showControls);
        lfoPhaseLabel.setVisible(showControls);
    };
    updateLfoVis();
    lfoSyncToggle.onClick = updateLfoVis;

    // --- Filter Oversampling ----------------------------------
    filterOsLabel.setText ("OS", juce::dontSendNotification);
    addAndMakeVisible (filterOsLabel);

    filterOsBox.addItemList ({ "Off", "2×", "4×" }, 1);
    addAndMakeVisible (filterOsBox);

    filterOsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::
                          ComboBoxAttachment>(processor.getValueTreeState(),
                                              "FILTER_OS", filterOsBox);
}

//==============================================================================
void AllSynthPluginAudioProcessorEditor::updateModelList()
{
    // Filter models by selected company
    auto compName = companyBox.getText().toStdString();
    modelBox.clear(juce::dontSendNotification);
    if (companyToSynths.count(compName) > 0) {
        for (auto& synth : companyToSynths[compName]) {
            auto it = synthIdMap.find(synth);
            if (it != synthIdMap.end())
                modelBox.addItem(synth, it->second + 1);      // make ID ≥ 1
        }
    }
    // Ensure first item selected
    if (modelBox.getNumItems() > 0)
        modelBox.setSelectedId(modelBox.getItemId(0), juce::dontSendNotification);
}

//==============================================================================
// NEW: update preset dropdown based on category
//==============================================================================
void AllSynthPluginAudioProcessorEditor::updatePresetDropDown(bool shouldLoadPreset)
{
    auto cat = presetCategoryBox.getText().toStdString();
    presetBox.clear(juce::dontSendNotification);

    if (categoryToPresetIndices.count(cat))
        for (int idx : categoryToPresetIndices[cat])
            presetBox.addItem(processor.getPresets()[idx].name, idx + 1);

    if (presetBox.getNumItems() > 0) {
        // First select the item without notification
        int firstItemId = presetBox.getItemId(0);
        presetBox.setSelectedId(firstItemId, juce::dontSendNotification);
        
        // Only load the preset if explicitly requested
        if (shouldLoadPreset) {
            // Then manually load the preset
            int presetIndex = firstItemId - 1;  // ItemId = presetIndex+1
            processor.loadPreset(presetIndex);
            
            // Update company and model dropdowns to match the MODEL parameter
            auto* modelParam = processor.getValueTreeState().getParameter("MODEL");
            if (modelParam) {
                int modelValue = (int)modelParam->convertFrom0to1(modelParam->getValue());
                
                // Find which model name corresponds to this model value
                std::string modelName;
                for (const auto& entry : synthIdMap) {
                    if (entry.second == modelValue) {
                        modelName = entry.first;
                        break;
                    }
                }
                
                if (!modelName.empty()) {
                    // Find which company this model belongs to
                    std::string companyName;
                    for (const auto& comp : companyToSynths) {
                        auto it = std::find(comp.second.begin(), comp.second.end(), modelName);
                        if (it != comp.second.end()) {
                            companyName = comp.first;
                            break;
                        }
                    }
                    
                    if (!companyName.empty()) {
                        // Select company in the dropdown (triggers updateModelList)
                        for (int i = 1; i <= companyBox.getNumItems(); ++i) {
                            if (companyBox.getItemText(i-1).toStdString() == companyName) {
                                companyBox.setSelectedId(i, juce::sendNotification);
                                // After model list is updated, select the correct model
                                for (int j = 1; j <= modelBox.getNumItems(); ++j) {
                                    if (modelBox.getItemText(j-1).toStdString() == modelName) {
                                        modelBox.setSelectedId(modelBox.getItemId(j-1), juce::dontSendNotification);
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

// Destructor is defaulted in the header

void AllSynthPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.drawImageAt(backgroundImage, 0, 0);
    // then let JUCE draw your controls on top
}

void AllSynthPluginAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(30); // Increased margin from 25 to 30

    // Define button width and gap
    const int buttonWidth = 25;
    const int buttonGap = 5;

    // Define heights for FX section elements
    const int toggleHeight = 35;
    const int sliderHeight = 95;
    const int comboBoxHeight = 35;

    // ---- Top row for presets ----
    auto presetRow = bounds.removeFromTop(70);
    auto presetHalfWidth = presetRow.getWidth() / 2;

    // Category Area (Left)
    auto categoryArea = presetRow.removeFromLeft(presetHalfWidth).reduced(20, 15);
    presetCategoryUpButton.setBounds(categoryArea.removeFromRight(buttonWidth));
    categoryArea.removeFromRight(buttonGap); // Gap
    presetCategoryDownButton.setBounds(categoryArea.removeFromRight(buttonWidth));
    categoryArea.removeFromRight(buttonGap); // Gap
    presetCategoryBox.setBounds(categoryArea);
    presetCategoryLabel.setTopLeftPosition(presetCategoryBox.getX(), presetCategoryBox.getY() - 25);

    // Preset Area (Right)
    auto presetArea = presetRow.reduced(20, 15);
    presetUpButton.setBounds(presetArea.removeFromRight(buttonWidth));
    presetArea.removeFromRight(buttonGap); // Gap
    presetDownButton.setBounds(presetArea.removeFromRight(buttonWidth));
    presetArea.removeFromRight(buttonGap); // Gap
    presetBox.setBounds(presetArea);
    presetLabel.setTopLeftPosition(presetBox.getX(), presetBox.getY() - 25);

    // Keep 4 columns, but they will be wider now
    const int columnWidth = bounds.getWidth() / 4;
    auto oscModelArea  = bounds.removeFromLeft(columnWidth);
    auto filterEnvArea = bounds.removeFromLeft(columnWidth); // Filter + Env
    auto lfoNoiseArea  = bounds.removeFromLeft(columnWidth); // LFO + Noise/Drive
    auto fxArea        = bounds;                             // Remaining width for FX

    // --- Column 1: Oscillators & Model ---
    // Allocate slightly more space relatively for controls vs labels
    auto oscSectionHeight = oscModelArea.getHeight() * 0.65f;
    auto modelSectionHeight = oscModelArea.getHeight() * 0.35f;
    auto oscArea   = oscModelArea.removeFromTop(oscSectionHeight);
    auto modelArea = oscModelArea;

    // Give rows slightly more space for 6 rows: 2 waves, volume, PW, semitone, fine
    auto oscRowHeight = oscArea.getHeight() / 6;
    
    auto oscPaddingX = 35; // horizontal padding
    auto oscPaddingY = 10; // vertical padding
    
    // 1) Wave 1
    auto waveform1Area = oscArea.removeFromTop(oscRowHeight);
    waveformBox.setBounds(waveform1Area.withSizeKeepingCentre(waveform1Area.getWidth(), waveform1Area.getHeight() * 0.7f));
    waveformLabel.setTopLeftPosition(waveformBox.getX(), waveformBox.getY() - 25);
    
    // 2) Wave 2
    auto waveform2Area = oscArea.removeFromTop(oscRowHeight);
    waveform2Box.setBounds(waveform2Area.withSizeKeepingCentre(waveform2Area.getWidth(), waveform2Area.getHeight() * 0.7f));
    waveform2Label.setTopLeftPosition(waveform2Box.getX(), waveform2Box.getY() - 25);
    
    // 3) Volume sliders
    auto volumeSliderArea = oscArea.removeFromTop(oscRowHeight);
    osc1VolSlider.setBounds(volumeSliderArea.removeFromLeft(volumeSliderArea.getWidth() / 2).reduced(5, oscPaddingY - 5));
    osc2VolSlider.setBounds(volumeSliderArea.reduced(5, oscPaddingY - 5));
    osc1VolLabel.setTopLeftPosition(osc1VolSlider.getX(), osc1VolSlider.getY() - 20);
    osc2VolLabel.setTopLeftPosition(osc2VolSlider.getX(), osc2VolSlider.getY() - 20);
    
    // 4) Pulse Width
    auto pwRow = oscArea.removeFromTop(oscRowHeight);
    pulseWidthSlider.setBounds(pwRow.reduced(oscPaddingX - 20, oscPaddingY - 5));
    pulseWidthLabel.setTopLeftPosition(pulseWidthSlider.getX(),
                                       pulseWidthSlider.getY() - 5);

    // 5) Osc‑2 Semitone
    auto semiRow = oscArea.removeFromTop(oscRowHeight);
    osc2SemiSlider.setBounds(semiRow.reduced(oscPaddingX - 20, oscPaddingY - 5));
    osc2SemiLabel.setTopLeftPosition(osc2SemiSlider.getX(),
                                     osc2SemiSlider.getY() - 20);

    // 6) Osc‑2 Fine Tune
    auto fineRow = oscArea.removeFromTop(oscRowHeight);
    osc2FineSlider.setBounds(fineRow.reduced(oscPaddingX - 20, oscPaddingY - 5));
    osc2FineLabel.setTopLeftPosition(osc2FineSlider.getX(),
                                     osc2FineSlider.getY() - 20);

    // Synth model dropdowns - also narrower
    auto modelRowHeight = modelArea.getHeight() / 2;
    auto modelDropdownWidthRatio = 0.7f; // Ratio for the dropdown itself
    auto totalModelControlWidth = modelArea.getWidth() * modelDropdownWidthRatio;
    auto modelDropdownWidth = totalModelControlWidth - (2 * buttonWidth + 2 * buttonGap); // Width for ComboBox
    
    float modelDropdownHeight = modelRowHeight * 0.6f;

    auto companyAreaFull = modelArea.removeFromTop(modelRowHeight);
    auto companyControlBounds = companyAreaFull.withSizeKeepingCentre(totalModelControlWidth, modelDropdownHeight);
    companyBox.setBounds(companyControlBounds.removeFromLeft(modelDropdownWidth));
    companyControlBounds.removeFromLeft(buttonGap);
    companyUpButton.setBounds(companyControlBounds.removeFromLeft(buttonWidth));
    companyControlBounds.removeFromLeft(buttonGap);
    companyDownButton.setBounds(companyControlBounds);
    companyLabel.setTopLeftPosition(companyBox.getX(), companyBox.getY() - 25);

    auto modelAreaFull = modelArea;
    auto modelControlBounds = modelAreaFull.withSizeKeepingCentre(totalModelControlWidth, modelDropdownHeight);
    modelBox.setBounds(modelControlBounds.removeFromLeft(modelDropdownWidth));
    modelControlBounds.removeFromLeft(buttonGap);
    modelUpButton.setBounds(modelControlBounds.removeFromLeft(buttonWidth));
    modelControlBounds.removeFromLeft(buttonGap);
    modelDownButton.setBounds(modelControlBounds);
    modelLabel.setTopLeftPosition(modelBox.getX(), modelBox.getY() - 25);

    // --- Column 2: Filter & Amp Envelope ---
    auto filterSectionHeight = filterEnvArea.getHeight() * 0.30f; // Less space for filter
    auto envSectionHeight    = filterEnvArea.getHeight() * 0.70f; // More space for Env

    auto filterArea = filterEnvArea.removeFromTop(filterSectionHeight);
    auto envArea    = filterEnvArea;

    // First row: Oversampling selector (small box centred)
    const int osBoxHeight = 25;
    auto osRow = filterArea.removeFromTop(osBoxHeight);
    const int osBoxWidth = 60;
    filterOsBox.setBounds(osRow.withSizeKeepingCentre(osBoxWidth, osBoxHeight));
    filterOsLabel.setTopLeftPosition(filterOsBox.getX(), filterOsBox.getY() - 18);

    // Remaining area for cutoff / resonance sliders
    auto filterSliderWidth = filterArea.getWidth() / 2;
    auto filterPadding = 10; // Increased padding
    cutoffSlider   .setBounds(filterArea.removeFromLeft(filterSliderWidth).reduced(filterPadding));
    resonanceSlider.setBounds(filterArea.reduced(filterPadding));

    const int numEnvSliders = 5; // add one more for master gain
    auto envSliderHeight = envArea.getHeight() / numEnvSliders;
    auto envPadding = 10; // Increase padding
    attackSlider .setBounds(envArea.removeFromTop(envSliderHeight).reduced(envPadding));
    decaySlider  .setBounds(envArea.removeFromTop(envSliderHeight).reduced(envPadding));
    sustainSlider.setBounds(envArea.removeFromTop(envSliderHeight).reduced(envPadding));
    releaseSlider.setBounds(envArea.removeFromTop(envSliderHeight).reduced(envPadding));
    masterGainSlider.setBounds(envArea.reduced(envPadding));

    // --- Column 3: LFO & Noise/Drive ---
    // Give LFO section more vertical space
    auto lfoSectionHeight = lfoNoiseArea.getHeight() * 0.60f; // increased from 0.45f to 0.60f
    auto noiseDriveSectionHeight = lfoNoiseArea.getHeight() * 0.40f; // reduced noise section to 40%

    auto lfoArea        = lfoNoiseArea.removeFromTop(lfoSectionHeight);
    auto noiseDriveArea = lfoNoiseArea;

    const int numLfoRows = 7;
    auto lfoRowHeight = lfoArea.getHeight() / numLfoRows;
    auto lfoPaddingX = 30; // More horizontal padding for toggles
    auto lfoPaddingY = 12; // increase vertical padding for LFO rows
    // Row 1: LFO On and Tempo Sync Toggle
    auto lfoToggleRow = lfoArea.removeFromTop(lfoRowHeight);
    lfoToggle.setCentrePosition(lfoToggleRow.getCentreX() - 40, lfoToggleRow.getCentreY());
    lfoSyncToggle.setCentrePosition(lfoToggleRow.getCentreX() + 40, lfoToggleRow.getCentreY());
    // Row 2: Rate, Depth, Phase sliders side-by-side
    auto slidersRow = lfoArea.removeFromTop(lfoRowHeight * 1.5f); // Allocate more height for sliders+labels
    int sliderWidth = slidersRow.getWidth() / 3;
    lfoRateSlider.setBounds(slidersRow.removeFromLeft(sliderWidth).reduced(envPadding));
    lfoDepthSlider.setBounds(slidersRow.removeFromLeft(sliderWidth).reduced(envPadding));
    lfoPhaseSlider.setBounds(slidersRow.reduced(envPadding)); // Remaining space
    // Row 3 (was 4): Shape selector
    auto shapeRow = lfoArea.removeFromTop(lfoRowHeight);
    lfoShapeBox.setBounds(shapeRow.reduced(lfoPaddingX, lfoPaddingY));
    lfoShapeLabel.setTopLeftPosition(lfoShapeBox.getX(), lfoShapeBox.getY() - 25);
    // Row 4 (was 5): Sync Division
    auto divRow = lfoArea.removeFromTop(lfoRowHeight);
    lfoSyncDivBox.setBounds(divRow.reduced(lfoPaddingX, lfoPaddingY));
    lfoSyncDivLabel.setTopLeftPosition(lfoSyncDivBox.getX(), lfoSyncDivBox.getY() - 25);
    // Row 5 (was 6): Routing toggles (Pitch, Cutoff, Amp) - Phase is moved
    auto routeRow = lfoArea.removeFromTop(lfoRowHeight);
    int toggleW = 60;
    lfoToPitchToggle.setBounds(routeRow.removeFromLeft(toggleW).withTrimmedTop(5).withTrimmedBottom(5));
    lfoToCutoffToggle.setBounds(routeRow.removeFromLeft(toggleW).withTrimmedTop(5).withTrimmedBottom(5));
    lfoToAmpToggle.setBounds(routeRow.removeFromLeft(toggleW).withTrimmedTop(5).withTrimmedBottom(5));
    // Row 6 (was 7): Noise Toggle
    auto noiseDriveRowHeight = noiseDriveArea.getHeight() / 3; // Now 3 rows in noise/drive
    auto noiseToggleRow = noiseDriveArea.removeFromTop(noiseDriveRowHeight);
    noiseToggle.setCentrePosition(noiseToggleRow.getCentreX(), noiseToggleRow.getCentreY());
    // Row 7 (was 8): Noise Mix slider
    noiseMixSlider.setBounds(noiseDriveArea.removeFromTop(noiseDriveRowHeight).reduced(envPadding));
    // Row 8 (was 9): Drive Toggle & Slider
    auto driveRow = noiseDriveArea; // Remaining area
    driveToggle.setCentrePosition(driveRow.getCentreX() - 40, driveRow.getCentreY());
    driveAmtSlider.setBounds(driveRow.reduced(envPadding).withLeft(driveRow.getCentreX() + 10)); // Place slider next to toggle

    // --- Column 4: FX ---
    auto fxPaddingX = 20; // Reduced horizontal padding for toggles
    auto fxPaddingY = 10; // Increased vertical padding for toggles
    auto fxSliderPadding = 12; // Adjusted padding for sliders
    auto fxSectionGap = 20; // Added gap between Delay and Reverb sections

    // --- DELAY SECTION ---
    // Allocate area for delay section
    auto delayArea = fxArea.removeFromTop(fxArea.getHeight() * 0.45f);

    // Position tap-tempo button and tempo label at bottom of delay section
    auto tapRowArea     = delayArea.removeFromBottom(toggleHeight);
    auto tapButtonArea  = tapRowArea.removeFromLeft(tapRowArea.getWidth() / 2);
    tapTempoButton.setBounds(tapButtonArea.reduced(5, 5));
    tempoLabel.setBounds(tapRowArea.reduced(5, 5));

    // Delay toggle in its own row at top
    auto delayToggleRow = delayArea.removeFromTop(toggleHeight);
    delayToggle.setCentrePosition(delayToggleRow.getCentreX(), delayToggleRow.getCentreY());
    
    // Split remaining delay area into two columns
    auto delayLeftColumn = delayArea.removeFromLeft(delayArea.getWidth() / 2);
    auto delayRightColumn = delayArea;
    
    // Left column: Delay Mix and Delay Time
    auto mixArea = delayLeftColumn.removeFromTop(delayLeftColumn.getHeight() * 0.7f);
    delayMixSlider.setBounds(mixArea.reduced(fxSliderPadding));
    // Delay sync toggle row
    auto syncRowArea = delayLeftColumn.removeFromTop(toggleHeight);
    delaySyncToggle.setCentrePosition(syncRowArea.getCentreX(), syncRowArea.getCentreY());
    // Delay sync division combobox row
    auto divRowArea = delayLeftColumn.removeFromTop(comboBoxHeight);
    delaySyncDivBox.setBounds(divRowArea.reduced(fxPaddingX, fxPaddingY));
    delaySyncDivLabel.setTopLeftPosition(delaySyncDivBox.getX(), delaySyncDivBox.getY() - 25);
    
    // Right column: Delay Feedback
    delayTimeSlider.setBounds(delayRightColumn.removeFromTop(delayRightColumn.getHeight() * 0.5f).reduced(fxSliderPadding));
    delayFeedbackSlider.setBounds(delayRightColumn.reduced(fxSliderPadding));

    fxArea.removeFromTop(fxSectionGap); // Add vertical space between Delay and Reverb

    // --- REVERB SECTION ---
    // Allocate area for reverb section
    auto reverbArea = fxArea.removeFromTop(fxArea.getHeight() * 0.6f);
    
    // Reverb toggle in its own row at top
    auto reverbToggleRow = reverbArea.removeFromTop(toggleHeight);
    reverbToggle.setCentrePosition(reverbToggleRow.getCentreX(), reverbToggleRow.getCentreY());
    
    // Split remaining reverb area into two columns
    auto reverbLeftColumn = reverbArea.removeFromLeft(reverbArea.getWidth() / 2);
    auto reverbRightColumn = reverbArea;
    
    // Left column: Reverb Mix
    reverbMixSlider.setBounds(reverbLeftColumn.removeFromTop(reverbLeftColumn.getHeight() * 0.7f).reduced(fxSliderPadding));
    reverbTypeBox.setBounds(reverbLeftColumn.reduced(fxPaddingX, fxPaddingY));
    
    // Right column: Reverb Size
    reverbSizeSlider.setBounds(reverbRightColumn.reduced(fxSliderPadding));

    fxArea.removeFromTop(fxSectionGap); // Add vertical space between Reverb and Console

    // --- CONSOLE SECTION ---
    auto consoleToggleRow = fxArea.removeFromTop(toggleHeight);
    consoleToggle.setCentrePosition(consoleToggleRow.getCentreX(), consoleToggleRow.getCentreY());
    consoleModelBox.setBounds(fxArea.removeFromTop(comboBoxHeight).reduced(fxPaddingX, fxPaddingY / 2));

    consoleModelLabel.setTopLeftPosition(consoleModelBox.getX(), consoleModelBox.getY() - 25);
    
    // ===== bottom row for analogue-extra toggles =============================
    {
        auto analogueRow = getLocalBounds().removeFromBottom(40).reduced(30, 5);
        const int w = analogueRow.getWidth() / 8;    // 8 toggles now

        auto positionToggleInCell = [](juce::TextButton& toggle, juce::Rectangle<int> cell) {
            toggle.setCentrePosition(cell.getCentreX(), cell.getCentreY());
        };

        positionToggleInCell(freePhaseToggle, analogueRow.removeFromLeft(w));
        positionToggleInCell(driftToggle, analogueRow.removeFromLeft(w));
        positionToggleInCell(filterTolToggle, analogueRow.removeFromLeft(w));
        positionToggleInCell(vcaClipToggle, analogueRow.removeFromLeft(w));
        positionToggleInCell(humToggle, analogueRow.removeFromLeft(w));
        positionToggleInCell(crossToggle, analogueRow.removeFromLeft(w));
        positionToggleInCell(analogEnvToggle, analogueRow.removeFromLeft(w));
        positionToggleInCell(legatoToggle, analogueRow);   // Last toggle takes remaining width
    }
    // =========================================================================
}

//==============================================================================
// New helper to capture your old paint logic:
void AllSynthPluginAudioProcessorEditor::drawVintageBackground(juce::Graphics& g)
{
    juce::Random rng{ 12345678 }; // fixed seed for deterministic background
    // Create a dark background with a subtle gradient
    juce::Colour darkBackground = juce::Colour(25, 25, 30);
    juce::Colour slightlyLighterBg = juce::Colour(35, 35, 40);
    g.setGradientFill(juce::ColourGradient(darkBackground, 0, 0, 
                                          slightlyLighterBg, getWidth(), getHeight(), 
                                          false));
    g.fillAll();
    
    // Define our theme colors - green and red for analog feel
    juce::Colour analogGreen = juce::Colour::fromRGBA(20, 180, 50, 80);  // Green with less transparency
    juce::Colour analogRed = juce::Colour::fromRGBA(200, 30, 40, 70);    // Red with less transparency
    juce::Colour dimWhite = juce::Colour::fromRGBA(200, 200, 200, 25);   // Brighter white for grid lines
    
    // Draw subtle grid pattern - vintage oscilloscope look
    g.setColour(dimWhite);
    for (int x = 0; x < getWidth(); x += 20) {
        int lineAlpha = rng.nextInt(15) + 10; // Increased base alpha for grid lines
        g.setColour(juce::Colours::white.withAlpha(lineAlpha / 255.0f));
        g.drawVerticalLine(x, 0, getHeight());
    }
    
    for (int y = 0; y < getHeight(); y += 20) {
        int lineAlpha = rng.nextInt(15) + 10; // Increased base alpha for grid lines
        g.setColour(juce::Colours::white.withAlpha(lineAlpha / 255.0f));
        g.drawHorizontalLine(y, 0, getWidth());
    }
    
    // Draw circuit-like patterns using green
    g.setColour(analogGreen);
    int numCircuitLines = 15; // Increased number of circuit patterns
    
    for (int i = 0; i < numCircuitLines; i++) {
        int startX = rng.nextInt(getWidth());
        int startY = rng.nextInt(getHeight());
        int endX = startX + rng.nextInt(300) - 150;
        int endY = startY + rng.nextInt(300) - 150;
        
        juce::Path path;
        path.startNewSubPath(startX, startY);
        
        // Create a path with a right-angle bend
        int midX = (startX + endX) / 2;
        int midY = (startY + endY) / 2;
        
        // Decide whether horizontal first or vertical first
        if (rng.nextBool()) {
            path.lineTo(midX, startY);
            path.lineTo(midX, midY);
            path.lineTo(endX, midY);
            path.lineTo(endX, endY);
        } else {
            path.lineTo(startX, midY);
            path.lineTo(midX, midY);
            path.lineTo(midX, endY);
            path.lineTo(endX, endY);
        }
        
        g.strokePath(path, juce::PathStrokeType(1.0f));
        
        // Add a node (circle) at an intersection
        g.fillEllipse(midX - 3, midY - 3, 6, 6);
        
        // Occasionally add a capacitor symbol
        if (rng.nextInt(3) == 0) {
            g.drawLine(midX - 5, midY + 10, midX - 5, midY + 20, 1.0f);
            g.drawLine(midX + 5, midY + 10, midX + 5, midY + 20, 1.0f);
        }
    }
    
    // Draw green waveform patterns
    g.setColour(analogGreen.withAlpha(0.5f)); // More visible waveforms
    for (int i = 0; i < 2; i++) {
        float y = rng.nextInt(getHeight());
        float amplitude = rng.nextInt(40) + 10.0f;
        float frequency = rng.nextFloat() * 0.02f + 0.01f;
        
        juce::Path wavePath;
        wavePath.startNewSubPath(0, y);
        
        for (float x = 0; x < getWidth(); x += 1.0f) {
            float sampleY = y + amplitude * std::sin(x * frequency * juce::MathConstants<float>::twoPi);
            wavePath.lineTo(x, sampleY);
        }
        
        g.strokePath(wavePath, juce::PathStrokeType(1.5f));
    }
    
    // Draw some red electronic elements
    g.setColour(analogRed);
    int numRedElements = 12; // Increase number of elements
    
    for (int i = 0; i < numRedElements; i++) {
        int x = rng.nextInt(getWidth());
        int y = rng.nextInt(getHeight());
        int size = rng.nextInt(20) + 15; // Larger symbols
        
        // Randomly draw different electronic symbols
        int symbol = rng.nextInt(7); // More symbol types
        
        switch (symbol) {
            case 0: { // Resistor
                juce::Path path;
                path.startNewSubPath(x - size, y);
                path.lineTo(x - size/2, y);
                
                // Zigzag part
                path.lineTo(x - size/3, y - size/4);
                path.lineTo(x - size/6, y + size/4);
                path.lineTo(x + size/6, y - size/4);
                path.lineTo(x + size/3, y + size/4);
                
                path.lineTo(x + size/2, y);
                path.lineTo(x + size, y);
                
                g.strokePath(path, juce::PathStrokeType(1.5f));
                break;
            }
            case 1: { // Capacitor
                g.drawLine(x - size/2, y - size/3, x - size/2, y + size/3, 1.5f);
                g.drawLine(x + size/2, y - size/3, x + size/2, y + size/3, 1.5f);
                g.drawLine(x - size, y, x - size/2, y, 1.5f);
                g.drawLine(x + size/2, y, x + size, y, 1.5f);
                break;
            }
            case 2: { // Diode
                juce::Path path;
                path.startNewSubPath(x - size/2, y - size/3);
                path.lineTo(x - size/2, y + size/3);
                path.lineTo(x, y);
                path.closeSubPath();
                
                g.fillPath(path);
                g.drawLine(x, y - size/3, x, y + size/3, 1.5f);
                g.drawLine(x - size, y, x - size/2, y, 1.5f);
                g.drawLine(x, y, x + size, y, 1.5f);
                break;
            }
            case 3: { // Op-amp
                juce::Path path;
                path.startNewSubPath(x - size/2, y - size/2);
                path.lineTo(x + size/2, y);
                path.lineTo(x - size/2, y + size/2);
                path.closeSubPath();
                
                g.strokePath(path, juce::PathStrokeType(1.5f));
                break;
            }
            case 4: { // Inductor/coil
                float coilSize = size * 0.8f;
                int turns = 4;
                float turnWidth = coilSize / (turns * 2);
                
                juce::Path coilPath;
                coilPath.startNewSubPath(x - coilSize/2, y);
                
                // Draw the coil
                for (int t = 0; t <= turns; t++) {
                    float xPos = x - coilSize/2 + t * coilSize/turns;
                    float yOffset = (t % 2 == 0) ? -turnWidth : turnWidth;
                    coilPath.lineTo(xPos, y + yOffset);
                }
                
                g.strokePath(coilPath, juce::PathStrokeType(1.5f));
                g.drawLine(x - coilSize/2 - size/4, y, x - coilSize/2, y, 1.5f);
                g.drawLine(x + coilSize/2, y, x + coilSize/2 + size/4, y, 1.5f);
                break;
            }
            case 5: { // Transistor (simplified NPN)
                g.drawLine(x, y - size/2, x, y + size/2, 1.5f); // Vertical line
                g.drawLine(x - size/2, y - size/3, x, y - size/6, 1.5f); // Upper diagonal
                g.drawLine(x - size/2, y + size/3, x, y + size/6, 1.5f); // Lower diagonal
                g.drawLine(x, y + size/6, x + size/2, y + size/2, 1.5f); // Collector
                g.drawLine(x, y - size/6, x + size/2, y - size/2, 1.5f); // Emitter
                
                // Arrowhead for NPN
                juce::Path arrowhead;
                arrowhead.addTriangle(x - size/10, y + size/3 - size/10,
                                     x, y + size/6,
                                     x + size/10, y + size/3 - size/10);
                g.fillPath(arrowhead);
                break;
            }
            case 6: { // Logic gate (AND)
                float gateWidth = size * 0.8f;
                float gateHeight = size;
                
                juce::Path gatePath;
                gatePath.startNewSubPath(x - gateWidth/2, y - gateHeight/2);
                gatePath.lineTo(x, y - gateHeight/2);
                gatePath.addArc(x - gateWidth/2, y - gateHeight/2, gateWidth, gateHeight, 
                               -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
                gatePath.lineTo(x - gateWidth/2, y - gateHeight/2);
                
                g.strokePath(gatePath, juce::PathStrokeType(1.5f));
                
                // Input lines
                g.drawLine(x - gateWidth/2 - size/4, y - gateHeight/4, x - gateWidth/2, y - gateHeight/4, 1.5f);
                g.drawLine(x - gateWidth/2 - size/4, y + gateHeight/4, x - gateWidth/2, y + gateHeight/4, 1.5f);
                
                // Output line
                g.drawLine(x + gateWidth/2, y, x + gateWidth/2 + size/4, y, 1.5f);
                break;
            }
        }
    }
    
    // Add some vintage knob indicators
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    for (int i = 0; i < 5; i++) {
        int x = rng.nextInt(getWidth());
        int y = rng.nextInt(getHeight());
        int size = rng.nextInt(20) + 40;
        
        g.drawEllipse(x - size/2, y - size/2, size, size, 1.0f);
        
        // Draw tick marks around circle
        int numTicks = 11;
        float tickLength = size * 0.2f;
        
        for (int t = 0; t < numTicks; t++) {
            float angle = t * juce::MathConstants<float>::twoPi / numTicks;
            float innerX = x + (size/2 - tickLength) * std::cos(angle);
            float innerY = y + (size/2 - tickLength) * std::sin(angle);
            float outerX = x + (size/2) * std::cos(angle);
            float outerY = y + (size/2) * std::sin(angle);
            
            g.drawLine(innerX, innerY, outerX, outerY, 1.0f);
        }
        
        // Draw indicator needle
        float needleAngle = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        float needleX = x + (size/2 - 5) * std::cos(needleAngle);
        float needleY = y + (size/2 - 5) * std::sin(needleAngle);
        
        g.drawLine(x, y, needleX, needleY, 1.5f);
    }
    
    // Add a subtle vignette effect
    juce::ColourGradient vignette(juce::Colours::transparentBlack, getWidth()/2, getHeight()/2,
                                 juce::Colours::black.withAlpha(0.5f), 0, 0, true);
    g.setGradientFill(vignette);
    g.fillRect(getLocalBounds());
    
    // Add some subtle scanlines for CRT effect
    g.setColour(juce::Colours::black.withAlpha(0.15f));
    for (int y = 0; y < getHeight(); y += 2) {
        g.drawHorizontalLine(y, 0, getWidth());
    }
}
