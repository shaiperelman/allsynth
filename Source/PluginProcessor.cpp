#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SynthSound.h"
#include "SynthVoice.h"

//==============================================================================
AllSynthPluginAudioProcessor::AllSynthPluginAudioProcessor()
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
      ),
      parameters(*this, nullptr, juce::Identifier("AllSynthParams"), createParameterLayout()),
      ccParamMap()
{
    // Create voices and sound
    const int numVoices = 8;
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice(new SynthVoice(parameters));

    synth.addSound(new SynthSound());
    
    setupMidiCCMapping();   // NEW: build CC → parameter map
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AllSynthPluginAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Synth model selection
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "MODEL",
        "Synth Model",
        juce::StringArray{
            "Minimoog","Prodigy","ARP 2600","Odyssey",
            "CS-80","Jupiter-4","MS-20","Polymoog","OB-X",
            "Prophet-5","Taurus","Model D",
            "SH-101","Juno-60","MonoPoly",
            "Voyager","Prophet-6","Jupiter-8","Polysix","Matrix-12",
            "PPG Wave","OB-6","DX7","Virus","D-50",
            "Memorymoog","Minilogue","Sub 37","Nord Lead 2","Blofeld",
            "Prophet VS","Prophet-10","JX-8P","CZ-101","ESQ-1",
            "System-8","Massive","MicroFreak","Analog Four","MicroKorg",
            "TB-303","JP-8000","M1","Wavestation","JD-800",
            "Hydrasynth","PolyBrute","Matriarch","Kronos","Prophet-12",
            "OB-Xa","OB-X8",
            "Juno-106","JX-3P","Jupiter-6","Alpha Juno",
            "Grandmother","Subsequent 25","Moog One",
            "ARP Omni",
            "CS-30","AN1x",
            "Prologue","DW-8000","MS2000","Delta",
            "Rev2","Prophet X",
            "Microwave","Q",
            "Lead 4",
            "SQ-80",
            "CZ-5000",
            "System-100",
            "Poly Evolver",
            // --- NEW DREAMSYNTH MODELS ---
            "Nebula","Solstice","Aurora","Lumina","Cascade",
            "Polaris","Eclipse","Quasar","Helios","Meteor",
            // --- MIXSYNTHS MODELS ---
            "Fusion-84","Velvet-CS","PolyProphet","BassMatrix","WaveVoyager",
            "StringEvo","MicroMass","DigitalMoog","HybridLead","GlowPad"
            },
        0));

    // Waveforms ---------------------------------------------------------------
    params.push_back(std::make_unique<juce::AudioParameterChoice>("WAVEFORM",  "Waveform 1", juce::StringArray({"Saw","Square","Pulse","Triangle","Sine"}), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("WAVEFORM2", "Waveform 2", juce::StringArray({"Saw","Square","Pulse","Triangle","Sine"}), 0));

    // Osc volumes -------------------------------------------------------------
    params.push_back(std::make_unique<juce::AudioParameterFloat>("OSC1_VOLUME", "Osc 1 Vol",
                                 juce::NormalisableRange<float>(0.000f, 0.150f, 0.0001f), 0.10f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("OSC2_VOLUME", "Osc 2 Vol",
                                 juce::NormalisableRange<float>(0.000f, 0.150f, 0.0001f), 0.10f));

    // Pulse width (used when waveform is pulse)
    params.push_back(std::make_unique<juce::AudioParameterFloat>("PULSE_WIDTH", "Pulse Width", juce::NormalisableRange<float>(0.05f, 0.95f, 0.001f), 0.5f));

    // === NEW 2-OSC DETUNE =============================================
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "OSC2_SEMI", "Osc2 Semi",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "OSC2_FINE", "Osc2 Fine",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f));
    // ------------------------------------------------------------------

    // Filter cutoff and resonance
    params.push_back(std::make_unique<juce::AudioParameterFloat>("CUTOFF", "Cutoff", juce::NormalisableRange<float>(20.0f, 20000.0f, 0.01f, 0.5f), 20000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("RESONANCE", "Resonance", juce::NormalisableRange<float>(0.1f, 0.95f, 0.001f, 0.5f), 0.7f));

    // LFO ---------------------------------------------------------------------
    params.push_back(std::make_unique<juce::AudioParameterBool> ("LFO_ON",   "LFO On", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("LFO_RATE", "LFO Rate",
                                 juce::NormalisableRange<float>(0.10f,20.0f,0.01f,0.5f), 5.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("LFO_DEPTH","LFO Depth",
                                 juce::NormalisableRange<float>(0.0f,1.0f,0.001f), 0.0f));

    // LFO waveform shape selector
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "LFO_SHAPE", "LFO Shape",
        juce::StringArray{"Sine", "Triangle", "Saw", "Square"},
        0));

    // LFO sync division (1/1 ... dotted 1/8)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "LFO_SYNC_DIV", "LFO Sync Div",
        juce::StringArray{"1/1","1/2","1/4","1/8","1/16","1/4.","1/8."},
        2)); // default: 1/4

    // LFO phase offset (0..1)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "LFO_PHASE", "LFO Phase",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f));

    // LFO routing toggles
    params.push_back(std::make_unique<juce::AudioParameterBool>("LFO_TO_PITCH",  "LFO → Pitch",  false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("LFO_TO_CUTOFF","LFO → Cutoff", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("LFO_TO_AMP",   "LFO → Amp",    false));

    // Noise & Drive ----------------------------------------------------------
    params.push_back(std::make_unique<juce::AudioParameterBool> ("NOISE_ON",  "Noise On",  false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("NOISE_MIX", "Noise Mix",
                                 juce::NormalisableRange<float>(0.0f,1.0f,0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool> ("DRIVE_ON",  "Drive On",   false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DRIVE_AMT", "Drive Amt",
                                 juce::NormalisableRange<float>(0.0f,7.0f,0.01f), 3.0f));

    // ADSR envelope
    params.push_back(std::make_unique<juce::AudioParameterFloat>("ATTACK", "Attack", juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.5f), 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DECAY", "Decay", juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.5f), 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SUSTAIN", "Sustain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("RELEASE", "Release", juce::NormalisableRange<float>(0.001f, 10.0f, 0.001f, 0.5f), 0.2f));

    // Delay / Reverb ----------------------------------------------------------
    params.push_back(std::make_unique<juce::AudioParameterBool>   ("DELAY_ON",     "Delay On",  false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>  ("DELAY_MIX",    "Delay Mix",
                                     juce::NormalisableRange<float>(0.0f,1.0f,0.001f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>  ("DELAY_TIME",   "Delay Time ms",
                                     juce::NormalisableRange<float>(1.0f,2000.0f,1.0f), 500.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>  ("DELAY_FB",     "Delay Feedback",
                                     juce::NormalisableRange<float>(0.0f,0.95f,0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterBool>   ("DELAY_SYNC",   "Delay Sync", false));
    // Delay sync division choices
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "DELAY_SYNC_DIV", "Delay Sync Div",
        juce::StringArray{"1/1","1/2","1/4","1/8","1/16","1/4.","1/8."},
        2));
    params.push_back(std::make_unique<juce::AudioParameterBool>   ("REVERB_ON",    "Reverb On", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>  ("REVERB_MIX",   "Reverb Mix",
                                     juce::NormalisableRange<float>(0.0f,1.0f,0.001f), 0.3f));

    // === NEW : Reverb type selector ============================================
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "REVERB_TYPE", "Reverb Type",
        juce::StringArray{ "Classic", "Hall", "Plate", "Shimmer",
                          "Spring", "Room", "Cathedral", "Gated" }, 0));

    // === NEW : global size scale (0.1 … 2.0, default 1.0) =========
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "REVERB_SIZE", "Reverb Size",
        juce::NormalisableRange<float>(0.1f, 2.0f, 0.001f, 1.0f), 1.0f));
    // ----------------------------------------------------------------

    // ===== NEW "Analogue Extras" =============================================
    params.push_back(std::make_unique<juce::AudioParameterBool>("ANA_FREE",     "Free Phase",    false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("ANA_DRIFT",    "VCO Drift",     false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("ANA_FILT_TOL", "Filter Tol",    false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("ANA_VCA_CLIP", "VCA Clip",      false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("HUM_ON",       "Hum / Hiss",    false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("CROSS_ON",     "Stereo Bleed",  false));
    // =============================================================================

    // NEW ───── 70‑s console toggle ────────────────────────────────────────────
    params.push_back(std::make_unique<juce::AudioParameterBool>("CONSOLE_ON", "Fat On", false));

    // NEW – choose fatness flavour --------------------------------------------
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "CONSOLE_MODEL", "Fat Mode",
        juce::StringArray{
            "Tape Thick",      // 0
            "Warm Tube",       // 1
            "Deep Console",    // 2
            "Punch Glue",      // 3
            "Sub Boom",        // 4
            "Opto Smooth",     // 5
            "Tube Crunch",     // 6
            "X-Former Fat",    // 7
            "Bus Glue",        // 8
            "Vintage Tape",    // 9
            "Neve 1073",       // 10 NEW
            "API 312/550A",    // 11 NEW
            "Helios 69",       // 12 NEW
            "Studer A80",      // 13 NEW
            "EMI TG12345",     // 14 NEW
            "SSL 4K-Bus",      // 15 NEW
            "LA-2A",           // 16 NEW
            "Fairchild 670",   // 17 NEW
            "Pultec EQP-1A",   // 18 NEW
            "Quad-Eight",      // 19 NEW
            "Harrison 32",     // 20 NEW
            "MCI JH-636",      // 21 NEW
            "API 2500",        // 22 NEW
            "Ampex 440",       // 23 NEW
            "Moog Ladder Out"}, // 24 NEW
        0));

    // ===== Analogue ADSR + Legato ==========================================
    params.push_back(std::make_unique<juce::AudioParameterBool>("ANA_ENV",    "Analog Env", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("ANA_LEGATO", "Legato",     false));
    // =======================================================================

    // Master Gain ---------------------------------------------------------
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MASTER_GAIN", "Master Gain",
        juce::NormalisableRange<float>(0.0f, 1.5f, 0.001f), 1.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
void AllSynthPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
            v->prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    // NEW FX -------------------------------------------------------------------
    const int maxDelay = int(sampleRate * 5.0);  // 5‑second max
    delayL.prepare(sampleRate, maxDelay);
    delayR.prepare(sampleRate, maxDelay);

    // Allocate delay scratch buffers once
    delayTmpL.setSize(1, samplesPerBlock);
    delayTmpR.setSize(1, samplesPerBlock);

    juce::dsp::ProcessSpec spec { sampleRate,
                                  static_cast<uint32>(samplesPerBlock),
                                  static_cast<uint32>(getTotalNumOutputChannels()) };
    reverb.prepare(spec);
    
    // Drive oversampling
    driveOS.reset();
    driveOS.initProcessing(static_cast<uint32>(samplesPerBlock));
    
    // Reset AnalogueDrive filter states
    anaDriveL.reset();
    anaDriveR.reset();

    // --- fatness chain --------------------------------------------------------
    fatChain.reset();
    fatChain.prepare(spec);

    fatChain.get<0>().setGainLinear(1.0f);          // pre
    fatChain.get<1>().coefficients =                // tone 1 (bypassed ‑ flat)
        juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 24000.f);
    fatChain.get<2>().coefficients =                // tone 2 (bypassed ‑ flat) - NEW
        juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 24000.f);

    fatChain.get<3>().setRatio(4.0f);               // comp (index is now 3)
    fatChain.get<3>().setAttack(5.f);
    fatChain.get<3>().setRelease(60.f);
    
    fatChain.get<4>().functionToUse = [](float x) { return x; }; // sat (index is now 4)
    fatChain.get<5>().setGainLinear(1.0f);          // post (index is now 5)
    
    // --- cache parameter pointers -------------------------------------------
    driveOnParam   = parameters.getRawParameterValue("DRIVE_ON");
    driveAmtParam  = parameters.getRawParameterValue("DRIVE_AMT");
    fatOnParam     = parameters.getRawParameterValue("CONSOLE_ON");
    fatModeParam   = parameters.getRawParameterValue("CONSOLE_MODEL");
    delayOnParam   = parameters.getRawParameterValue("DELAY_ON");
    reverbOnParam  = parameters.getRawParameterValue("REVERB_ON");
    reverbTypeParam= parameters.getRawParameterValue("REVERB_TYPE");
    reverbSizeParam= parameters.getRawParameterValue("REVERB_SIZE");   // NEW
    humOnParam     = parameters.getRawParameterValue("HUM_ON");
    crossOnParam   = parameters.getRawParameterValue("CROSS_ON");
    masterGainParam = parameters.getRawParameterValue("MASTER_GAIN");
    // -------------------------------------------------------------------------
}

void AllSynthPluginAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool AllSynthPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    // Must have same number of input and output channels
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
        layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif
    return true;
#endif
}
#endif

//==============================================================================
void AllSynthPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    // NEW: respond to MIDI‑CC messages -----------------------------------------
    for (const auto metadata : midiMessages)
        if (metadata.getMessage().isController())
            handleMidiCC(metadata.getMessage());
    // --------------------------------------------------------------------------
    
    buffer.clear();

    // Pass host BPM to voices for LFO sync
    double hostBpm = 120.0;
    if (auto* ph = getPlayHead()) {
        juce::AudioPlayHead::CurrentPositionInfo pos;
        if (ph->getCurrentPosition(pos) && pos.bpm > 0.0)
            hostBpm = pos.bpm;
    }
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
            v->setHostBpm(hostBpm);

    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());

    // === Analogue Extras: Hum + Crosstalk ====================================
    if (humOnParam && *humOnParam > 0.5f)
    {
        static double humPhase = 0.0;
        const double twoPi = juce::MathConstants<double>::twoPi;
        const double humInc = 50.0 / getSampleRate();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float hum  = 0.0015f * std::sin(float(twoPi * humPhase));
            float hiss = 0.0006f * (juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.getWritePointer(ch)[i] += hum + hiss;
            humPhase += humInc;
            if (humPhase >= 1.0) humPhase -= 1.0;
        }
    }
    if (crossOnParam && *crossOnParam > 0.5f && buffer.getNumChannels() > 1)
    {
        auto* L = buffer.getWritePointer(0);
        auto* R = buffer.getWritePointer(1);
        auto  N = buffer.getNumSamples();
        for (size_t i = 0; i < (size_t)N; ++i)
        {
            float l = L[i], r = R[i];
            L[i] = 0.97f * l + 0.03f * r;
            R[i] = 0.97f * r + 0.03f * l;
        }
    }

    // ----------------------  GLOBAL FX  --------------------------------------
    const bool   delayOn   = *delayOnParam > 0.5f;
    const bool   syncOn    = parameters.getRawParameterValue("DELAY_SYNC")->load() > 0.5f;
    const float  delayMix  = parameters.getRawParameterValue("DELAY_MIX")->load();
    const float  fb        = parameters.getRawParameterValue("DELAY_FB" )->load();
    const float  timeMsPar = parameters.getRawParameterValue("DELAY_TIME")->load();

    // ---- calc (possibly BPM‑synced) delay time ------------------------------
    double delaySeconds = timeMsPar * 0.001;
    if (syncOn)
        if (auto* ph = getPlayHead())
        {
            juce::AudioPlayHead::CurrentPositionInfo pos;
            if (ph->getCurrentPosition(pos) && pos.bpm > 0)
                delaySeconds = 60.0 / pos.bpm;          // quarter‑note
        }

    // process L & R separately
    if (delayOn)
    {
        delayL.setMix((float)delayMix);
        delayR.setMix((float)delayMix);
        delayL.setFeedback((float)fb);
        delayR.setFeedback((float)fb);
        delayL.setDelayTime((float)delaySeconds);
        delayR.setDelayTime((float)delaySeconds);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto& mono = (ch == 0 ? delayTmpL : delayTmpR);
            mono.setSize(1, buffer.getNumSamples(), false, false, true);
            mono.copyFrom(0, 0, buffer, ch, 0, buffer.getNumSamples());

            (ch == 0 ? delayL : delayR).processBlock(mono);    // digital flavour
            buffer.copyFrom(ch, 0, mono, 0, 0, buffer.getNumSamples());
        }
    }

    // ----- Reverb ------------------------------------------------------------
    const bool  revOn  = *reverbOnParam > 0.5f;
    const float revMix = parameters.getRawParameterValue("REVERB_MIX")->load();

    // === NEW : choose and (re)‑configure the reverb when the user changes type
    const int  revType = static_cast<int>(*reverbTypeParam);
    const float sizeScale = juce::jlimit(0.1f, 2.0f, reverbSizeParam->load());   // NEW
    
    if (revType != previousReverbType || sizeScale != previousSizeScale)
    {
        juce::Reverb::Parameters p;
        p.freezeMode = 0.0f;          // common
        switch (revType)
        {
            case 0:   // Classic  (legacy setting)
                p.roomSize = 0.60f;  p.damping  = 0.40f;
                p.width    = 1.00f;  p.wetLevel = 0.33f; p.dryLevel = 0.67f;
                break;

            case 1:   // HQ‑Hall  – very large, lush, longer tail
                p.roomSize = 0.95f;  p.damping  = 0.70f;
                p.width    = 1.00f;  p.wetLevel = 0.40f; p.dryLevel = 0.60f;
                break;

            case 2:   // HQ‑Plate – dense early reflections, airy top
                p.roomSize = 0.75f;  p.damping  = 0.25f;
                p.width    = 0.90f;  p.wetLevel = 0.38f; p.dryLevel = 0.62f;
                break;

            case 3:   // HQ‑Shimmer – bright, long, almost "pad‑like"
                p.roomSize = 0.85f;  p.damping  = 0.10f;
                p.width    = 1.00f;  p.wetLevel = 0.45f; p.dryLevel = 0.55f;
                break;
                
            // ---------- NEW TYPES ----------
            case 4:   // Spring (ARP‑2600‑style)
                p.roomSize = 0.55f;  p.damping  = 0.45f;
                p.width    = 0.70f;  p.wetLevel = 0.35f; p.dryLevel = 0.65f;
                break;
                
            case 5:   // Room
                p.roomSize = 0.40f;  p.damping  = 0.50f;
                p.width    = 0.90f;  p.wetLevel = 0.32f; p.dryLevel = 0.68f;
                break;
                
            case 6:   // Cathedral
                p.roomSize = 1.00f;  p.damping  = 0.60f;
                p.width    = 1.00f;  p.wetLevel = 0.50f; p.dryLevel = 0.50f;
                break;
                
            case 7:   // Gated / Reverse‑like
                p.roomSize = 0.30f;  p.damping  = 0.20f;
                p.width    = 1.00f;  p.wetLevel = 0.42f; p.dryLevel = 0.58f;
                break;
        }
        
        // Apply size scaling
        p.roomSize = juce::jlimit(0.0f, 1.0f, p.roomSize * sizeScale);
        
        reverb.setParameters(p);
        previousReverbType = revType;
        previousSizeScale = sizeScale;  // NEW
    }

    if (revOn)
    {
        reverb.setMix(revMix);
        reverb.processBlock(buffer);
    }
    
    // ----- High-Quality Drive ---------------------------------------------------
    driveOn  = *driveOnParam > 0.5f;
    driveAmt = *driveAmtParam; // Use cached drive amount

    if (driveOn)
    {
        // Update pregain for both L/R drive instances
        anaDriveL.pregain = driveAmt;
        anaDriveR.pregain = driveAmt;
        
        // Set dryWet mix based on drive amount - starting higher and increasing with drive
        // Map from drive range (0.0-7.0) to mix range (0.2-0.9)
        float mixAmount = juce::jmap(driveAmt, 0.0f, 7.0f, 0.2f, 0.9f);
        anaDriveL.dryWet = mixAmount;
        anaDriveR.dryWet = mixAmount;
        
        // Set postgain to compensate for volume - increase as drive increases
        // Map from drive range (0.0-7.0) to gain range (1.0-1.3)
        float postGain = juce::jmap(driveAmt, 0.0f, 7.0f, 1.0f, 1.3f);
        anaDriveL.postgain = postGain;
        anaDriveR.postgain = postGain;

        // Oversample -> process -> downsample
        auto block = juce::dsp::AudioBlock<float>(buffer);
        auto osBlock = driveOS.processSamplesUp(block);

        // Process each sample with the corresponding AnalogueDrive instance
        for (int ch = 0; ch < (int)osBlock.getNumChannels(); ++ch)
        {
            float* samples = osBlock.getChannelPointer(ch);
            auto& currentDrive = (ch == 0) ? anaDriveL : anaDriveR;
            
            for (int i = 0; i < (int)osBlock.getNumSamples(); ++i)
            {
                samples[i] = currentDrive.process(ch, samples[i]);
            }
        }

        driveOS.processSamplesDown(block); // back to normal rate
    }
    // -------------------------------------------------------------------------

    // ---------------- fatness block (post‑FX) ----------------------------------
    const bool fatOn   = *fatOnParam > 0.5f;
    const int  fatMode = static_cast<int>(*fatModeParam);

    if (fatOn)
    {
        auto& pre   = fatChain.get<0>();
        auto& tone1 = fatChain.get<1>();
        auto& tone2 = fatChain.get<2>();
        auto& comp  = fatChain.get<3>();
        auto& sat   = fatChain.get<4>();
        auto& post  = fatChain.get<5>();
        const double sr = getSampleRate(); // Get sample rate once

        // Only rebuild the chain if the mode has changed
        if (fatMode != previousFatMode) 
        {
            previousFatMode = fatMode;
            
            // --- Reset bypass states before setting for current mode ---
            fatChain.setBypassed<1>(false); // Assume tone1 is used unless bypassed below
            fatChain.setBypassed<2>(true);  // Assume tone2 is NOT used unless enabled below
            fatChain.setBypassed<3>(true);  // Assume comp is NOT used unless enabled below

            switch (fatMode)
            {
                case 0:   // ───── Tape Thick ──────────────────────────────────────
                    pre.setGainLinear(1.20f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>
                                        ::makeLowShelf(sr, 200.f, 0.7f, 1.5f);
                    // tone2 bypassed by default
                    // comp bypassed by default
                    sat.functionToUse = [](float x)
                    {
                        return 0.6f * x + 0.4f * std::tanh(1.8f * x);
                    };
                    post.setGainLinear(0.83f);
                    break;

                case 1:   // ───── Warm Tube ──────────────────────────────────────
                    pre.setGainLinear(1.30f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>
                                        ::makeLowPass(sr, 14000.f);   // soft HF
                    // tone2 bypassed by default
                    // comp bypassed by default
                    sat.functionToUse = [](float x)
                    {
                        return 0.4f * x + 0.6f * std::tanh(2.5f * x);  // richer
                    };
                    post.setGainLinear(0.80f);
                    break;

                case 2:   // ───── Deep Console ───────────────────────────────────
                    pre.setGainLinear(1.25f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>
                                        ::makeHighShelf(sr, 6000.f, 0.8f, 0.9f); // tiny HF dip
                    // tone2 bypassed by default
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setThreshold(-12.f);
                    comp.setRatio(2.0f);
                    sat.functionToUse = [](float x)
                    {
                        return 0.55f * std::tanh(2.0f * x) + 0.45f * x;
                    };
                    post.setGainLinear(0.90f);
                    break;

                case 3:   // ───── Punch Glue ────────────────────────────────────
                    pre.setGainLinear(1.10f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>
                                        ::makeHighShelf(sr, 5000.f, 0.8f, 1.25f);
                    // tone2 bypassed by default
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setThreshold(-18.f);
                    comp.setRatio(4.0f);
                    comp.setAttack(5.f);
                    comp.setRelease(60.f);
                    sat.functionToUse = [](float x)
                    { return 0.5f * (x + std::tanh(2.2f * x)); };
                    post.setGainLinear(1.00f);
                    break;

                case 4:   // ───── Sub Boom ──────────────────────────────────────
                    pre.setGainLinear(1.15f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>
                                        ::makeLowShelf(sr, 80.f, 0.7f, 1.8f);
                    // tone2 bypassed by default
                    // comp bypassed by default
                    sat.functionToUse = [](float x)
                    { return 0.7f * x + 0.3f * juce::jlimit(-1.f, 1.f, x * x * x); };
                    post.setGainLinear(0.80f);
                    break;

                case 5:   // ── Opto Smooth ──────────────────────────────────────
                    pre.setGainLinear(1.12f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>
                                        ::makeHighShelf(sr, 7000.f, 0.7f, 1.10f);
                    // tone2 bypassed by default
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setRatio(3.0f);
                    comp.setThreshold(-16.f);
                    comp.setAttack(10.f); // Slower opto attack
                    comp.setRelease(150.f); // Slower opto release
                    sat.functionToUse = [](float x)
                    {   return 0.5f * x + 0.5f * std::tanh(2.0f * x); };
                    post.setGainLinear(0.92f);
                    break;

                case 6:   // ── Tube Crunch ──────────────────────────────────────
                    pre.setGainLinear(1.40f);
                    // Approximate tilt with a high shelf cut
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>
                                        ::makeHighShelf(sr, 1200.f, 0.7f, 0.8f); // Gentle high cut
                    // tone2 bypassed by default
                    // comp bypassed by default
                    sat.functionToUse = [](float x)
                    {   float y = std::tanh(3.5f * x);
                        y = 0.6f * y + 0.4f * std::tanh(1.2f * y);     // two stage
                        return y;
                    };
                    post.setGainLinear(0.78f);
                    break;

                case 7:   // ── X-Former Fat ─────────────────────────────────────
                    pre.setGainLinear(1.25f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>
                                        ::makeLowShelf(sr, 110.f, 0.7f, 1.7f);
                    // tone2 bypassed by default
                    // comp bypassed by default
                    sat.functionToUse = [](float x)
                    {   return 0.55f * std::tanh(2.8f * x)
                             + 0.45f * std::tanh(0.9f * x); }; // Blend two tanh stages
                    post.setGainLinear(0.85f);
                    break;

                case 8:   // ── Bus Glue ──────────────────────────────────────────
                    pre.setGainLinear(1.10f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>
                                        ::makeHighShelf(sr, 9000.f, 0.8f, 0.95f); // slight dip
                    // tone2 bypassed by default
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setRatio(1.8f);
                    comp.setThreshold(-10.f);
                    comp.setAttack(2.f);
                    comp.setRelease(80.f);
                    sat.functionToUse = [](float x)
                    {   return 0.6f * x + 0.4f * std::tanh(1.6f * x); };
                    post.setGainLinear(0.95f);
                    break;

                case 9:   // ── Vintage Tape ─────────────────────────────────────
                    pre.setGainLinear(1.30f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>
                                        ::makeLowPass(sr, 15000.f); // Tape HF roll-off
                    // tone2 bypassed by default
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setRatio(2.5f);
                    comp.setThreshold(-15.f);
                    comp.setAttack(5.f);
                    comp.setRelease(60.f);
                    sat.functionToUse = [](float x)
                    {   // soft knee + HF head bump (simulated via blend)
                        float core = std::tanh(2.2f * x);
                        return 0.7f * core + 0.3f * x;
                    };
                    post.setGainLinear(0.85f);
                    break;

                // --- NEW 70s Gear Emulations (Cases 10-24) ---

                case 10:   // ── Neve 1073 ────────────────────────────────────────
                    pre.setGainLinear(1.25f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr,  80.f, 0.7f, 1.6f); // Low shelf
                    fatChain.setBypassed<2>(false); // Enable tone2
                    tone2.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, 12000.f,0.8f,1.15f); // High shelf
                    // comp bypassed by default
                    sat.functionToUse = [](float x){ return 0.55f*x + 0.45f*std::tanh(2.8f*x); };
                    post.setGainLinear(0.88f);
                    break;

                case 11:   // ── API 312 + 550A ───────────────────────────────────
                    pre.setGainLinear(1.20f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr,  50.f, 0.7f, 1.5f); // Low shelf
                    fatChain.setBypassed<2>(false); // Enable tone2
                    tone2.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 3500.f,1.0f,1.25f); // Mid peak
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setThreshold(-14.f); comp.setRatio(3.f); comp.setAttack(1.f); comp.setRelease(50.f); // API comp settings
                    sat.functionToUse = [](float x){ return 0.5f*x + 0.5f*std::tanh(3.2f*x); };
                    post.setGainLinear(0.90f);
                    break;

                case 12:   // ── Helios 69 ────────────────────────────────────────
                    pre.setGainLinear(1.15f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr,10000.f,0.7f,1.25f); // High Shelf
                    fatChain.setBypassed<2>(false); // Enable tone2
                    tone2.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 700.f,1.4f,0.8f); // Mid dip
                    // comp bypassed by default
                    sat.functionToUse = [](float x){ return std::tanh(2.0f*x)*(1.0f-0.1f*x*x); }; // Triode-like
                    post.setGainLinear(0.85f);
                    break;

                case 13:   // ── Studer A80 15 IPS ────────────────────────────────
                    pre.setGainLinear(1.30f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr, 45.f,0.7f,1.8f); // Head bump
                    fatChain.setBypassed<2>(false); // Enable tone2
                    tone2.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr,15000.f); // HF roll-off
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setThreshold(-17.f); comp.setRatio(2.2f); comp.setAttack(5.f); comp.setRelease(60.f);
                    sat.functionToUse = [](float x)
                    {   float y = 0.6f*std::tanh(2.4f*x) + 0.4f*std::tanh(0.9f*x); // Two-stage tape sat
                        return y; };
                    post.setGainLinear(0.82f);
                    break;

                case 14:   // ── EMI TG-12345 ─────────────────────────────────────
                    pre.setGainLinear(1.18f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 30.f); // HPF
                    fatChain.setBypassed<2>(false); // Enable tone2
                    tone2.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, 5000.f,0.8f,1.2f); // Presence
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setThreshold(-12.f); comp.setRatio(2.f); comp.setAttack(5.f); comp.setRelease(100.f); // TG comp
                    sat.functionToUse = [](float x)
                    {   return 0.5f*x + 0.5f*std::tanh(1.8f*x); };
                    post.setGainLinear(0.90f);
                    break;

                case 15: // ── SSL 4K-Bus ─────────────────────────────────────────
                    pre.setGainLinear(1.08f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 18500.f); // Slight HF roll-off
                    // tone2 bypassed by default
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setRatio(2.0f);  comp.setThreshold(-12.f);
                    comp.setAttack(3.f);  comp.setRelease(100.f); // SSL Bus Comp settings
                    sat.functionToUse = [](float x){ return 0.4f*x+0.6f*std::tanh(1.8f*x); };
                    post.setGainLinear(0.93f);
                    break;

                case 16: // ── LA-2A ─────────────────────────────────────────────
                    pre.setGainLinear(1.20f);
                    // No significant EQ on LA-2A, use flat setting
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 22000.f);
                    // tone2 bypassed by default
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setRatio(3.5f);  comp.setThreshold(-14.f);
                    comp.setAttack(10.f); comp.setRelease(200.f); // Slower opto release
                    sat.functionToUse = [](float x){ return 0.5f*x+0.5f*std::tanh(2.3f*x); }; // Gentle tube sat
                    post.setGainLinear(0.88f);
                    break;

                case 17: // ── Fairchild 670 ────────────────────────────────────
                    pre.setGainLinear(1.25f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 16000.f); // Gentle HF roll-off
                    // tone2 bypassed by default
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setRatio(6.f);   comp.setThreshold(-10.f);
                    comp.setAttack(0.8f); comp.setRelease(300.f); // Very slow release
                    sat.functionToUse = [](float x){ return 0.45f*x+0.55f*std::tanh(3.f*x); }; // Rich tube sat
                    post.setGainLinear(0.83f);
                    break;

                case 18: // ── Pultec EQP-1A ────────────────────────────────────
                    pre.setGainLinear(1.15f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr, 30.f,0.7f,1.8f); // Low boost
                    fatChain.setBypassed<2>(false); // Enable tone2
                    tone2.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr,5000.f,0.8f,1.2f); // High boost
                    // comp bypassed by default
                    sat.functionToUse = [](float x){ return 0.65f*x+0.35f*std::tanh(1.6f*x); }; // Light saturation
                    post.setGainLinear(0.85f);
                    break;

                case 19: // ── Quad-Eight ───────────────────────────────────────
                    pre.setGainLinear(1.22f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr,100.f,0.9f,1.6f); // Broad low boost
                    // tone2 bypassed by default
                    // comp bypassed by default
                    sat.functionToUse = [](float x){ return 0.55f*std::tanh(2.4f*x)+0.45f*x; }; // Opamp/transformer sat
                    post.setGainLinear(0.87f);
                    break;

                case 20: // ── Harrison 32 ──────────────────────────────────────
                    pre.setGainLinear(1.10f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr,8000.f,0.8f,1.15f); // Airy presence
                    // tone2 bypassed by default
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setRatio(1.7f);  comp.setThreshold(-11.f);
                    comp.setAttack(2.f);  comp.setRelease(90.f); // Mix bus comp
                    sat.functionToUse = [](float x){ return 0.5f*x+0.5f*std::tanh(1.7f*x); };
                    post.setGainLinear(0.95f);
                    break;

                case 21: // ── MCI JH-636 ───────────────────────────────────────
                    pre.setGainLinear(1.18f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr,60.f,0.7f,1.4f); // Tight low boost
                    // tone2 bypassed by default
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setRatio(4.f);   comp.setThreshold(-15.f);
                    comp.setAttack(1.5f); comp.setRelease(70.f); // Faster VCA style
                    sat.functionToUse = [](float x){ return 0.45f*x+0.55f*std::tanh(2.2f*x); }; // Transformer sat
                    post.setGainLinear(0.88f);
                    break;

                case 22: // ── API 2500 ──────────────────────────────────────────
                    pre.setGainLinear(1.25f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr,90.f,0.8f,1.5f); // API Thrust-like low end
                    // tone2 bypassed by default
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setRatio(3.f);   comp.setThreshold(-12.f);
                    comp.setAttack(0.8f); comp.setRelease(60.f); // Punchy comp
                    sat.functionToUse = [](float x){ return 0.4f*x+0.6f*std::tanh(2.6f*x); }; // API Opamp sat
                    post.setGainLinear(0.86f);
                    break;

                case 23: // ── Ampex 440 ────────────────────────────────────────
                    pre.setGainLinear(1.28f);
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr,50.f,0.7f,1.7f); // 30 IPS bump
                    fatChain.setBypassed<2>(false); // Enable tone2
                    tone2.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr,14000.f,0.8f,0.9f); // Slight HF cut pre-sat
                    fatChain.setBypassed<3>(false); // Enable comp
                    comp.setRatio(2.f);   comp.setThreshold(-16.f);
                    comp.setAttack(5.f); comp.setRelease(60.f); // Subtle tape comp
                    sat.functionToUse = [](float x){ return 0.7f*std::tanh(2.1f*x)+0.3f*x; }; // Tape sat
                    post.setGainLinear(0.84f);
                    break;

                case 24: // ── Moog Ladder Out ──────────────────────────────────
                    pre.setGainLinear(1.30f); // Drive it
                    tone1.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr,17000.f); // Transformer roll-off
                    // tone2 bypassed by default
                    // comp bypassed by default
                    sat.functionToUse = [](float x){ return std::tanh(3.0f*x); }; // Simple strong tanh for ladder drive
                    post.setGainLinear(0.80f);
                    break;

                default: // Should not happen, but provide a fallback (e.g., bypass or first mode)
                     // Option 1: Bypass all fatness stages
                     fatChain.setBypassed<0>(true);
                     fatChain.setBypassed<1>(true);
                     fatChain.setBypassed<2>(true);
                     fatChain.setBypassed<3>(true);
                     fatChain.setBypassed<4>(true);
                     fatChain.setBypassed<5>(true);
                     // Option 2: Default to mode 0 (Tape Thick)
                     // pre.setGainLinear(1.20f); ... etc
                    break;
            }
        }

        juce::dsp::AudioBlock<float> blk(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(blk);
        fatChain.process(ctx);
    }
    // ---------------------------------------------------------------------------

    // Apply master gain at the end of the signal chain
    if (masterGainParam)
        buffer.applyGain(*masterGainParam);
}

//==============================================================================
juce::AudioProcessorEditor* AllSynthPluginAudioProcessor::createEditor()
{
    return new AllSynthPluginAudioProcessorEditor(*this);
}

void AllSynthPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Save the entire state of the plugin
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AllSynthPluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(parameters.state.getType()))
    {
        // Replace the entire state with the loaded state
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
        
        // Make sure missing parameters get reasonable defaults
        if (!parameters.state.hasProperty("CONSOLE_ON"))
        {
            // CONSOLE_ON - default to off (false/0)
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(parameters.getParameter("CONSOLE_ON")))
            {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost(rp->convertTo0to1(0.0f)); // 0 = false
                rp->endChangeGesture();
            }
        }
        
        if (!parameters.state.hasProperty("CONSOLE_MODEL"))
        {
            // CONSOLE_MODEL - default to 0 (first model)
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(parameters.getParameter("CONSOLE_MODEL")))
            {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost(rp->convertTo0to1(0.0f)); // First model
                rp->endChangeGesture();
            }
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AllSynthPluginAudioProcessor();
}

//==============================================================================
// NEW: Load Preset function
//==============================================================================
void AllSynthPluginAudioProcessor::loadPreset(int index)
{
    using namespace PresetData;
    if (index < 0 || index >= (int)presets.size())
        return;

    static const std::array<const char*, NumParameters> paramIDs =
    { "MODEL","WAVEFORM","WAVEFORM2","OSC1_VOLUME","OSC2_VOLUME","PULSE_WIDTH",
      "CUTOFF","RESONANCE","LFO_ON","LFO_RATE","LFO_DEPTH",
      "NOISE_ON","NOISE_MIX","DRIVE_ON","DRIVE_AMT",
      "ATTACK","DECAY","SUSTAIN","RELEASE",
      "DELAY_ON","DELAY_MIX","DELAY_TIME","DELAY_FB","DELAY_SYNC",
      "REVERB_ON","REVERB_MIX","REVERB_TYPE",          // NEW
      "CONSOLE_ON","CONSOLE_MODEL" };

    const auto& preset = presets[(size_t)index];

    // Handle difference between saved preset parameter count and actual parameter count
    size_t presetParamCount = preset.v.size();
    
    // Load the parameters from the preset (as many as are available)
    for (size_t i = 0; i < presetParamCount && i < paramIDs.size(); ++i)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(parameters.getParameter(paramIDs[i])))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost(rp->convertTo0to1(preset.v[i]));
            rp->endChangeGesture();
        }
    
    // Apply default values for any parameters not in the preset
    // (this handles the case where presets were created with fewer parameters than we now have)
    if (presetParamCount < paramIDs.size())
    {
        // Set default values for missing parameters
        if (presetParamCount <= 26) // Older presets missing the console parameters
        {
            // CONSOLE_ON - default to off (false/0)
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(parameters.getParameter("CONSOLE_ON")))
            {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost(rp->convertTo0to1(0.0f)); // 0 = false
                rp->endChangeGesture();
            }
            
            // CONSOLE_MODEL - default to 0 (first model)
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(parameters.getParameter("CONSOLE_MODEL")))
            {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost(rp->convertTo0to1(0.0f)); // First model
                rp->endChangeGesture();
            }
        }
    }
}

//==============================================================================
// NEW:  MIDI‑CC helpers
//==============================================================================
void AllSynthPluginAudioProcessor::setupMidiCCMapping()
{
    auto add = [this](int cc, const char* id)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameters.getParameter(id)))
            ccParamMap[cc] = p;
    };

    // Map knobs 1-7 on Novation Launchkey (they send CC 21-27)
    add(21, "CUTOFF");          // Knob 1 -> Cutoff
    add(22, "RESONANCE");       // Knob 2 -> Resonance
    add(23, "OSC1_VOLUME");     // Knob 3 -> Osc 1 Vol
    add(24, "OSC2_VOLUME");     // Knob 4 -> Osc 2 Vol
    add(25, "DELAY_MIX");       // Knob 5 -> Delay Mix
    add(26, "DELAY_FB");        // Knob 6 -> Delay Feedback
    add(27, "REVERB_MIX");      // Knob 7 -> Reverb Mix
}

void AllSynthPluginAudioProcessor::handleMidiCC(const juce::MidiMessage& msg)
{
    const int cc  = msg.getControllerNumber();
    const int val = msg.getControllerValue();   // 0‑127

    if (auto it = ccParamMap.find(cc); it != ccParamMap.end())
    {
        const float norm = static_cast<float>(val) / 127.0f;
        it->second->setValueNotifyingHost(norm);
    }
} 