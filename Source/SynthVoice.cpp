#include "SynthVoice.h"
#include "SynthSound.h"

using namespace juce;

// --- helpers --------------------------------------------------------
static inline float polyBlep(float t, float dt) noexcept
{
    if (t < dt) { 
        t /= dt; 
        return t + t - t * t - 1.0f; 
    }
    if (t > 1.0f - dt) { 
        t = (t - 1.0f) / dt; 
        return t * t + t + t + 1.0f; 
    }
    return 0.0f;
}

// LFO sine lookup table (2048 points, initialized once at load)
static constexpr int LFO_TABLE_SIZE = 2048;
static float lfoTable[LFO_TABLE_SIZE];
static bool lfoTableInit = []() {
    for (int i = 0; i < LFO_TABLE_SIZE; ++i)
        lfoTable[i] = std::sin(juce::MathConstants<float>::twoPi * i / LFO_TABLE_SIZE);
    return true;
}();
// --------------------------------------------------------------------

//==============================================================================
SynthVoice::SynthVoice(AudioProcessorValueTreeState& vts) : parameters(vts)
{
}

bool SynthVoice::canPlaySound(SynthesiserSound* sound)
{
    return dynamic_cast<SynthSound*>(sound) != nullptr;
}

void SynthVoice::prepare(double sampleRate, int samplesPerBlock, int /*outputChannels*/)
{
    currentSampleRate       = sampleRate;
    samplesPerBlockCached   = samplesPerBlock;
    osModeParam            = parameters.getRawParameterValue("FILTER_OS");
    configureOversampling();   // sets up `oversampler`, calls filterChain.prepare(...) & svFilter.prepare(...)
    
    // --- existing dsp::ProcessSpec at voice rate (not used when os on) ---
    dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<uint32>(samplesPerBlock);
    spec.numChannels      = 1;

    // Reset and prepare the filter chain
    filterChain.reset();
    filterChain.prepare(spec);
    
    // Configure the ladder filter in the chain
    auto& filter = filterChain.get<filterIndex>();
    filter.setMode(juce::dsp::LadderFilterMode::LPF24);

    // prepare state-variable filter
    svFilter.reset();
    svFilter.prepare(spec);
    svFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

    adsr.setSampleRate(sampleRate);

    // --- Initialize Amp Mod Smoother ---
    ampModSmoothed.reset(sampleRate, 0.005); // 5ms smoothing time
    ampModSmoothed.setCurrentAndTargetValue(1.0f); // Start at no modulation (gain = 1.0)
    // -----------------------------------

    // Allocate scratch buffer once
    scratchBuffer.setSize(1, samplesPerBlock);

    // Cache parameter pointers once (no per-sample lookup)
    wave1Param     = parameters.getRawParameterValue("WAVEFORM");
    wave2Param     = parameters.getRawParameterValue("WAVEFORM2");
    pulseWidthParam = parameters.getRawParameterValue("PULSE_WIDTH");
    osc1VolParam    = parameters.getRawParameterValue("OSC1_VOLUME");
    osc2VolParam    = parameters.getRawParameterValue("OSC2_VOLUME");
    osc2SemiParam   = parameters.getRawParameterValue("OSC2_SEMI");
    osc2FineParam   = parameters.getRawParameterValue("OSC2_FINE");
    lfoOnParam      = parameters.getRawParameterValue("LFO_ON");
    lfoRateParam    = parameters.getRawParameterValue("LFO_RATE");
    lfoDepthParam   = parameters.getRawParameterValue("LFO_DEPTH");
    noiseOnParam    = parameters.getRawParameterValue("NOISE_ON");
    noiseMixParam   = parameters.getRawParameterValue("NOISE_MIX");
    modelParam      = parameters.getRawParameterValue("MODEL");
    cutoffParam     = parameters.getRawParameterValue("CUTOFF");
    resonanceParam  = parameters.getRawParameterValue("RESONANCE");
    attackParam     = parameters.getRawParameterValue("ATTACK");
    decayParam      = parameters.getRawParameterValue("DECAY");
    sustainParam    = parameters.getRawParameterValue("SUSTAIN");
    releaseParam    = parameters.getRawParameterValue("RELEASE");
    // Cache analogue-extra pointers
    freePhaseParam  = parameters.getRawParameterValue("ANA_FREE");
    driftParam      = parameters.getRawParameterValue("ANA_DRIFT");
    filterTolParam  = parameters.getRawParameterValue("ANA_FILT_TOL");
    vcaClipParam    = parameters.getRawParameterValue("ANA_VCA_CLIP");
    analogEnvParam  = parameters.getRawParameterValue("ANA_ENV");    // NEW: sqrt-env
    legatoParam     = parameters.getRawParameterValue("ANA_LEGATO"); // NEW: single-trigger ADSR
    // NEW: cache LFO sync toggle and initialize tempo-syncable LFO
    lfoSyncParam    = parameters.getRawParameterValue("LFO_SYNC");
    lfoSyncDivParam = parameters.getRawParameterValue("LFO_SYNC_DIV");
    lfoShapeParam   = parameters.getRawParameterValue("LFO_SHAPE");
    lfoPhaseParam   = parameters.getRawParameterValue("LFO_PHASE");
    // === NEW : cache routing toggles =======================================
    lfoToPitchParam  = parameters.getRawParameterValue("LFO_TO_PITCH");
    lfoToCutoffParam = parameters.getRawParameterValue("LFO_TO_CUTOFF");
    lfoToAmpParam    = parameters.getRawParameterValue("LFO_TO_AMP");
    // -----------------------------------------------------------------------

    // Initialize and prepare LFO oscillator
    lfoOsc.initialise([](float x) { return std::sin(juce::MathConstants<float>::twoPi * x); }, 128);
    lfoOsc.prepare(spec);
    lfoOsc.reset();

    // Random per-voice tolerance (±2% cutoff, ±5% resonance)
    cutoffTol       = 1.0f + (juce::Random::getSystemRandom().nextFloat() - 0.5f) * 0.04f;
    resonanceTol    = 1.0f + (juce::Random::getSystemRandom().nextFloat() - 0.5f) * 0.10f;

    updateParams();
}

void SynthVoice::startNote(int midiNoteNumber, float velocity, SynthesiserSound*, int /*currentPitchWheelPosition*/)
{
    // Legato: retrigger only if env is idle or legato disabled
    if (!legatoParam || *legatoParam < 0.5f || !adsr.isActive())
        adsr.noteOn();

    // Set the base frequency for this voice
    frequency = MidiMessage::getMidiNoteInHertz(midiNoteNumber);

    // Free-phase toggle: reset phases/integrators only if disabled
    if (*freePhaseParam < 0.5f)
    {
        phase = 0.0;                              // primary oscillator phase
        triangleIntegrator = 0.0f;               // triangle integrator
        phase2 = 0.0;                             // secondary oscillator phase
        triangleIntegrator2 = 0.0f;              // triangle integrator 2
        lfoPhase = 0.0;                           // reset LFO phase
    }

    // Initial drift per voice
    drift = 0.0;
    if (*driftParam > 0.5f)
        drift = juce::Random::getSystemRandom().nextFloat() * 0.002f - 0.001f; // ±0.1%

    ignoreUnused(velocity);
    // Only reconfigure oversampling when a new note starts
    //configureOversampling(); // disabled to avoid stutter on note start
}

void SynthVoice::stopNote(float /*velocity*/, bool allowTailOff)
{
    adsr.noteOff();

    if (!allowTailOff || !adsr.isActive())
        clearCurrentNote();
}

float SynthVoice::computeOscSample()
{
    // Get parameters -----------------------------------------------------------
    int   wf1   = static_cast<int>(*wave1Param);
    int   wf2   = static_cast<int>(*wave2Param);
    float pw    = *pulseWidthParam;
    float vol1  = *osc1VolParam;
    float vol2  = *osc2VolParam;

    // LFO
    bool  lfoOn  = *lfoOnParam > 0.5f;
    float lfoRt  = *lfoRateParam;
    float lfoDpParam = *lfoDepthParam;                 // 0-1 slider
    float depth  = lfoDpParam * lfoDpParam * 0.08f;    // quadratic, max ≈ 8 %

    float lfoRaw = 0.0f;                 // RAW –1 … +1   (no depth applied yet)
    if (lfoOn)
    {
        // -------- 1. work out rate in Hz (free-run or beat-synced) ----------
        double rateHz = lfoRt;
        if (lfoSyncParam && *lfoSyncParam > 0.5f && hostBpm > 0.0)
        {
            static const std::array<double,7> div = {1,2,4,8,16,1.5,3};
            int idx = lfoSyncDivParam ? int(lfoSyncDivParam->load()) : 2;
            idx = juce::jlimit(0, int(div.size()-1), idx);
            rateHz = hostBpm / 60.0 / div[idx];
        }

        // -------- 2. advance internal phase ---------------------------------
        const double phaseInc = rateHz / currentSampleRate;   // cycles / sample
        lfoPhase += phaseInc;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;

        // -------- 3. add user phase-offset slider ---------------------------
        float userOff = (lfoPhaseParam ? lfoPhaseParam->load() : 0.0f); // 0-1
        double t = lfoPhase + userOff;
        if (t >= 1.0)
            t -= 1.0;                                           // wrap to 0-1

        // -------- 4. waveform ----------------------------------------------
        int shape = lfoShapeParam ? int(lfoShapeParam->load()) : 0;
        float sample = 0.0f;
        switch (shape)
        {
            case 0:  sample = std::sin(juce::MathConstants<float>::twoPi * (float)t);          break; // Sine
            case 1:  sample = (t < 0.5) ? float(4.0*t - 1.0) : float(3.0 - 4.0*t);             break; // Triangle
            case 2:  sample = float(2.0*t - 1.0);                                              break; // Saw
            case 3:  sample = (t < 0.5) ? 1.0f : -1.0f;                                        break; // Square
            default: break;
        }

        // -------- 5. store raw value ----------------------------------------
        lfoRaw = sample;                 // store raw value (-1…+1)
    }

    lastLfoValue = lfoRaw;               // cache for Amp / GUI

    // -------- PER-DESTINATION DEPTHS ---------------------------------
    const float depthLin   = *lfoDepthParam;              // 0…1 knob
    const float depthPitch = depthLin * depthLin * 0.08f; // subtle
    const float depthCut   = depthLin * 0.50f;            // ±50 %
    const float depthAmp   = depthLin * 1.00f;            // 0-200 %

    // ---------- PITCH route ------------------------------------------
    const bool pitchRouteOn = (lfoToPitchParam && *lfoToPitchParam > 0.5f);
    const double freqMod = frequency * (1.0
                         + (pitchRouteOn ? lfoRaw * depthPitch : 0.0f));
    const double phaseInc = freqMod / currentSampleRate;
    const float  dt       = static_cast<float>(phaseInc);

    // NEW: 2nd-osc detune offsets
    float semiOffset = osc2SemiParam ? osc2SemiParam->load() : 0.0f;
    float fineOffset = osc2FineParam ? osc2FineParam->load() : 0.0f;
    double detuneRatio = std::pow(2.0, (semiOffset + fineOffset * 0.01) / 12.0);
    const double phaseInc2 = phaseInc * detuneRatio;
    const float  dt2       = static_cast<float>(phaseInc2);

    auto singleOsc = [&](int waveform, double& ph, float& triInt, double inc, float dtVal) -> float
    {
        float dt = dtVal; // use per-osc detune dt
        float s = 0.0f, t = static_cast<float>(ph);
        switch (waveform)
        {
            case 0: // Saw -------------------------------------------------------
                s = 2.0f * t - 1.0f;
                s -= polyBlep(t, dt);
                break;
            case 1: // Square – loudness‑matched & sweetened -------------------
            {
                float sq = (t < 0.5f ? 1.0f : -1.0f);
                sq += polyBlep(t, dt);
                {
                    float tmod = t + 0.5f;
                    if (tmod >= 1.0f) tmod -= 1.0f;
                    sq -= polyBlep(tmod, dt);
                }
                sq  = std::tanh(0.9f * sq);   // gentle soft‑clip = rounder
                s   = sq * 0.65f;             // ≈ RMS match to saw
                break;
            }
            case 2: // Pulse – balanced & musical ------------------------------
            {
                const float dc = (2.0f * pw - 1.0f);          // remove DC
                float pl = (t < pw ? 1.0f : -1.0f) - dc;
                pl += polyBlep(t, dt);
                {
                    float tmod = t + (1.0f - pw);
                    if (tmod >= 1.0f) tmod -= 1.0f;
                    pl -= polyBlep(tmod, dt);
                }
                pl = std::tanh(0.9f * pl);    // tame the buzz a bit
                s  = pl * 0.65f;              // level‑match
                break;
            }
            case 3: // Triangle – fuller & louder (adjusted for improved lows)
            {
                // 1) Build band‑limited square as the basis
                float blSq = (t < 0.5f ? 1.0f : -1.0f);
                blSq += polyBlep(t, dt);
                float t2 = t + 0.5f; 
                if (t2 >= 1.0f) t2 -= 1.0f;
                blSq -= polyBlep(t2, dt);
    
                // 2) Integrate (with a very slow leak to avoid DC drift)
                triInt += blSq * dt;
                triInt -= triInt * 0.0005f;
    
                // 3) Scale output with increased gain for fuller lows
                s = juce::jlimit(-1.0f, 1.0f, triInt * 3.0f);
                break;
            }
            case 4: // Sine – pure sine wave
            {
                // Generate a sine wave from the phase (0 to 1 mapping)
                s = std::sin(juce::MathConstants<float>::twoPi * t);
                break;
            }
            default: break;
        }

        ph += inc;
        if (ph >= 1.0) ph -= 1.0;
        return s;
    };

    float osc1 = singleOsc(wf1, phase, triangleIntegrator, phaseInc, dt);
    float osc2 = singleOsc(wf2, phase2, triangleIntegrator2, phaseInc2, dt2);

    float out = osc1 * vol1 + osc2 * vol2;
    
    // Add raw white noise if enabled
    if (*noiseOnParam > 0.5f)
        out = out * (1.0f - *noiseMixParam) + (rnd.nextFloat() * 2.0f - 1.0f) * *noiseMixParam;

    return out;
}

void SynthVoice::renderNextBlock(AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!isVoiceActive())
        return;

    updateParams();

    const bool useSVF = (static_cast<int>(*modelParam) == 2 ||
                         static_cast<int>(*modelParam) == 3 ||
                         static_cast<int>(*modelParam) == 6);

    // ----- LADDER filter path ---------------------------------------------
    if (! useSVF)
    {
        // clear & fill scratchBuffer as before...
        auto& tmp = scratchBuffer; tmp.clear();
        for (int i = 0; i < numSamples; ++i)
            tmp.setSample (0, i, computeOscSample());

        auto hostBlock = juce::dsp::AudioBlock<float>(tmp)
                            .getSubBlock (0, (size_t) numSamples);

        if (oversampler)
        {
            auto upBlock = oversampler->processSamplesUp(hostBlock);
            juce::dsp::ProcessContextReplacing<float> ctxUp (upBlock);
            filterChain.process(ctxUp);
            oversampler->processSamplesDown(hostBlock);
        }
        else
        {
            juce::dsp::ProcessContextReplacing<float> ctx (hostBlock);
            filterChain.process(ctx);
        }

        // ... then ADSR, LFO→amp, copying to outputBuffer unchanged ...
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float filtered = tmp.getSample(0, sample);
            float env = adsr.getNextSample();
            if (analogEnvParam && *analogEnvParam > 0.5f)
                env = std::sqrt(env);   // RC-style analog curve

            // -------- LFO → AMP (Smoothed & Click-safe) ---------------------
            float targetAmpMod = 1.0f; // Default: no modulation
            if (lfoOnParam && *lfoOnParam > 0.5f &&
                lfoToAmpParam && *lfoToAmpParam > 0.5f)
            {
                const float depthValue = lfoDepthParam ? lfoDepthParam->load() : 0.0f;
                const float depth = juce::jlimit(0.0f, 0.9f, depthValue); // 0-0.9
                targetAmpMod = 1.0f + depth * lastLfoValue; // Target gain: 0.1 to 1.9
            }
            ampModSmoothed.setTargetValue(targetAmpMod); // Set the target for the smoother
            env *= ampModSmoothed.getNextValue();       // Apply the SMOOTHED value
            // ----------------------------------------------------------------

            float currentSample = filtered * env;

            // Add to all output channels
            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                outputBuffer.addSample(channel, startSample + sample, currentSample);
        }
    }
    else
    {
        // SV-filter path – similar wrapping…
        auto& tmp = scratchBuffer; tmp.clear();
        for (int i = 0; i < numSamples; ++i)
            tmp.setSample (0, i, computeOscSample());

        auto hostBlock = juce::dsp::AudioBlock<float>(tmp)
                            .getSubBlock(0, (size_t) numSamples);

        if (oversampler)
        {
            auto upBlock = oversampler->processSamplesUp(hostBlock);
            float* d = upBlock.getChannelPointer (0);
            for (size_t i = 0; i < upBlock.getNumSamples(); ++i)
                d[i] = svFilter.processSample(0, d[i]);
            oversampler->processSamplesDown(hostBlock);
        }
        else
        {
            for (int i = 0; i < numSamples; ++i)
                tmp.setSample(0, i,
                              svFilter.processSample(0, tmp.getSample(0, i)));
        }

        // ... then drive → ADSR → LFO→amp → copy unchanged …
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float filtered = tmp.getSample(0, sample);
            float env = adsr.getNextSample();
            if (analogEnvParam && *analogEnvParam > 0.5f)
                env = std::sqrt(env);   // RC-style analog curve

            // -------- LFO → AMP (Smoothed & Click-safe) ---------------------
            float targetAmpMod = 1.0f; // Default: no modulation
            if (lfoOnParam && *lfoOnParam > 0.5f &&
                lfoToAmpParam && *lfoToAmpParam > 0.5f)
            {
                const float depthValue = lfoDepthParam ? lfoDepthParam->load() : 0.0f;
                const float depth = juce::jlimit(0.0f, 0.9f, depthValue); // 0-0.9
                targetAmpMod = 1.0f + depth * lastLfoValue; // Target gain: 0.1 to 1.9
            }
            ampModSmoothed.setTargetValue(targetAmpMod); // Set the target for the smoother
            env *= ampModSmoothed.getNextValue();       // Apply the SMOOTHED value
            // ----------------------------------------------------------------

            float currentSample = filtered * env;

            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                outputBuffer.addSample(channel, startSample + sample, currentSample);
        }
    }

    if (! adsr.isActive())
        clearCurrentNote();
}

void SynthVoice::updateParams()
{
    // Get parameters
    const float cutoff    = *cutoffParam;
    const float resonance = *resonanceParam;
    currentModel          = static_cast<int>(*modelParam);

    // Noise parameters
    noiseOn  = *noiseOnParam > 0.5f;
    noiseMix = *noiseMixParam;

    // Get references to processors in the chain - do this before the early return
    auto& gain   = filterChain.get<gainIndex>();
    auto& ladder = filterChain.get<filterIndex>();
    auto& drive  = filterChain.get<shaperIndex>();
    
    // Smooth parameter changes
    cutoffSmoothed   .setTargetValue(cutoff);
    resonanceSmoothed.setTargetValue(resonance);

    // Return early if only cutoff / resonance changed
    if (currentModel == previousModel)
        goto updateFilterOnly;

    previousModel = currentModel;  // remember for next call
    
    // Configure based on synth model
    switch (currentModel)
    {
        case 0: // Minimoog
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.4f);
            gain .setGainLinear(0.9f);
            drive.functionToUse = [](float x) { return std::tanh(1.6f * x); };
            break;
        case 1: // Prodigy
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.1f);
            gain .setGainLinear(0.9f);
            drive.functionToUse = [](float x) { return std::tanh(1.3f * x); };
            break;
        case 2: // ARP 2600
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x) { return std::tanh(1.2f * x); };
            break;
        case 3: // Odyssey
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x) { return std::tanh(1.4f * x); };
            break;
        case 4: // CS-80
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x) { return std::tanh(1.25f * x); };
            break;
        case 5: // Jupiter-4
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x) { return std::tanh(1.30f * x); };
            break;
        case 6: // MS-20
            gain .setGainLinear(1.1f);
            drive.functionToUse = [](float x) { return std::tanh(1.50f * x); };
            break;
        case 7: // Polymoog
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x) { return std::tanh(1.10f * x); };
            break;
        case 8: // OB-X
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x) { return std::tanh(1.40f * x); };
            break;
        case 9: // Prophet‑5
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.30f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.25f * x); };
            break;
        case 10: // Taurus
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.60f);
            gain .setGainLinear(1.1f);
            drive.functionToUse = [](float x){ return std::tanh(1.55f * x); };
            break;
        case 11: // Model D
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.40f);
            gain .setGainLinear(0.95f);
            drive.functionToUse = [](float x){ return std::tanh(1.45f * x); };
            break;
        case 12: // SH‑101
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.20f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.20f * x); };
            break;
        case 13: // Juno‑60
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.15f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.15f * x); };
            break;
        case 14: // MonoPoly
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.30f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.30f * x); };
            break;
        case 15: // Voyager
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.35f);
            gain .setGainLinear(0.95f);
            drive.functionToUse = [](float x){ return std::tanh(1.40f * x); };
            break;
        case 16: // Prophet‑6
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.25f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.25f * x); };
            break;
        case 17: // Jupiter‑8
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.30f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.30f * x); };
            break;
        case 18: // Polysix
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.25f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.25f * x); };
            break;
        case 19: // Matrix‑12
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.20f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.20f * x); };
            break;
        case 20: // PPG Wave
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.15f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.15f * x); };
            break;
        case 21: // OB‑6
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.25f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.25f * x); };
            break;
        case 22: // DX7
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.00f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return x; };
            break;
        case 23: // Virus
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.30f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.35f * x); };
            break;
        case 24: // D‑50
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.10f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return x; };
            break;
        case 25: // Memorymoog
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.35f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.35f * x); };
            break;
        case 26: // Minilogue
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.20f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.20f * x); };
            break;
        case 27: // Sub 37
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.45f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.45f * x); };
            break;
        case 28: // Nord Lead 2
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.05f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.05f * x); };
            break;
        case 29: // Blofeld
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.10f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.10f * x); };
            break;
        case 30: // Prophet VS
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.15f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.15f * x); };
            break;
        case 31: // Prophet‑10
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.25f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.25f * x); };
            break;
        case 32: // JX‑8P
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.10f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.10f * x); };
            break;
        case 33: // CZ‑101
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.00f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return x; };
            break;
        case 34: // ESQ‑1
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.10f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.10f * x); };
            break;
        case 35: // System‑8
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.20f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.20f * x); };
            break;
        case 36: // Massive
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.00f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return x; };
            break;
        case 37: // MicroFreak
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.15f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.15f * x); };
            break;
        case 38: // Analog Four
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.30f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.30f * x); };
            break;
        case 39: // MicroKorg
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.10f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.10f * x); };
            break;
        // ---------- EXTRA MODELS ----------
        case 40: // TB‑303
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.40f);
            gain .setGainLinear(1.05f);
            drive.functionToUse = [](float x){ return std::tanh(1.40f * x); };
            break;
        case 41: // JP‑8000
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.25f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.25f * x); };
            break;
        case 42: // M1
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.00f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return x; };
            break;
        case 43: // Wavestation
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.05f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return x; };
            break;
        case 44: // JD‑800
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.15f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.15f * x); };
            break;
        case 45: // Hydrasynth
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.10f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.10f * x); };
            break;
        case 46: // PolyBrute
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.30f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.30f * x); };
            break;
        case 47: // Matriarch
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.35f);
            gain .setGainLinear(1.05f);
            drive.functionToUse = [](float x){ return std::tanh(1.35f * x); };
            break;
        case 48: // Kronos
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.00f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return x; };
            break;
        case 49: // Prophet‑12
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.25f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.25f * x); };
            break;

// ------------------ DREAMSYNTH MODELS (IDs 75-84) ----------------------
        case 75: // Nebula — silky 12‑dB LPF with gentle saturation
            ladder.setMode(juce::dsp::LadderFilterMode::LPF12);
            ladder.setDrive(1.15f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return 0.6f * x + 0.4f * std::tanh(1.8f * x); };
            break;
        case 76: // Solstice — warm 24‑dB LPF with rich drive
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.35f);
            gain .setGainLinear(0.95f);
            drive.functionToUse = [](float x){ return std::tanh(1.35f * x); };
            break;
        case 77: // Aurora — expressive 24‑dB BPF for vocal timbres
            ladder.setMode(juce::dsp::LadderFilterMode::BPF24);
            ladder.setDrive(1.25f);
            gain .setGainLinear(1.05f);
            drive.functionToUse = [](float x){ return x / (1.0f + std::fabs(x)); };
            break;
        case 78: // Lumina — bright 24‑dB HPF with airy headroom
            ladder.setMode(juce::dsp::LadderFilterMode::HPF24);
            ladder.setDrive(1.20f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return 0.7f * std::tanh(1.1f * x) + 0.3f * x; };
            break;
        case 79: // Cascade — liquid 12‑dB LPF with soft‑clip character
            ladder.setMode(juce::dsp::LadderFilterMode::LPF12);
            ladder.setDrive(1.15f);
            gain .setGainLinear(1.10f);
            drive.functionToUse = [](float x){ return juce::jlimit(-1.0f, 1.0f, x - 0.2f * x * x * x); };
            break;
        case 80: // Polaris — crystalline 12‑dB BPF
            ladder.setMode(juce::dsp::LadderFilterMode::BPF12);
            ladder.setDrive(1.18f);
            gain .setGainLinear(1.05f);
            drive.functionToUse = [](float x){ return std::tanh(1.20f * x); };
            break;
        case 81: // Eclipse — dark 24‑dB LPF with heavier drive
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.40f);
            gain .setGainLinear(0.90f);
            drive.functionToUse = [](float x){ return std::tanh(1.45f * x); };
            break;
        case 82: // Quasar — punchy 12‑dB HPF with asymmetrical clip
            ladder.setMode(juce::dsp::LadderFilterMode::HPF12);
            ladder.setDrive(1.25f);
            gain .setGainLinear(1.05f);
            drive.functionToUse = [](float x){ return juce::jlimit(-1.0f, 1.0f, x - 0.25f * x * x * x); };
            break;
        case 83: // Helios — vibrant 24‑dB BPF with hybrid saturator
            ladder.setMode(juce::dsp::LadderFilterMode::BPF24);
            ladder.setDrive(1.30f);
            gain .setGainLinear(1.00f);
            drive.functionToUse = [](float x){ return 0.5f * x + 0.5f * std::tanh(1.8f * x); };
            break;
        case 84: // Meteor — aggressive 24‑dB LPF with searing saturation
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.50f);
            gain .setGainLinear(1.10f);
            drive.functionToUse = [](float x){ return juce::jlimit(-1.0f, 1.0f, x - 0.15f * x * x * x); };
            break;

// ------------------ MIXSYNTHS MODELS (IDs 85‑94) ----------------------
        case 85: // Fusion‑84 — Moog warmth + Jupiter sheen
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.30f);
            gain .setGainLinear(1.00f);
            drive.functionToUse = [](float x){ return std::tanh(1.35f * x); };
            break;
        case 86: // Velvet‑CS — silky LPF12 with gentle tilt–sat
            ladder.setMode(juce::dsp::LadderFilterMode::LPF12);
            ladder.setDrive(1.10f);
            gain .setGainLinear(1.05f);
            drive.functionToUse = [](float x){ return 0.6f * x + 0.4f * std::tanh(2.0f * x); };
            break;
        case 87: // PolyProphet — expressive BPF12, soft limiter
            ladder.setMode(juce::dsp::LadderFilterMode::BPF12);
            ladder.setDrive(1.25f);
            gain .setGainLinear(1.00f);
            drive.functionToUse = [](float x){ return x / (1.0f + std::fabs(x)); };
            break;
        case 88: // BassMatrix — gritty LPF24 with cubic clip
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.45f);
            gain .setGainLinear(0.90f);
            drive.functionToUse = [](float x){ return juce::jlimit(-1.0f, 1.0f, x - 0.20f * x * x * x); };
            break;
        case 89: // WaveVoyager — resonant BPF24, mixed sat
            ladder.setMode(juce::dsp::LadderFilterMode::BPF24);
            ladder.setDrive(1.30f);
            gain .setGainLinear(1.00f);
            drive.functionToUse = [](float x){ return 0.5f * x + 0.5f * std::tanh(1.7f * x); };
            break;
        case 90: // StringEvo — airy LPF12, light drive
            ladder.setMode(juce::dsp::LadderFilterMode::LPF12);
            ladder.setDrive(1.20f);
            gain .setGainLinear(1.05f);
            drive.functionToUse = [](float x){ return std::tanh(1.15f * x); };
            break;
        case 91: // MicroMass — dense LPF24, "sine‑fold" style
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.35f);
            gain .setGainLinear(1.00f);
            drive.functionToUse = [](float x){ return x * std::tanh(x); };
            break;
        case 92: // DigitalMoog — FM edge + ladder smooth
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.30f);
            gain .setGainLinear(1.00f);
            drive.functionToUse = [](float x){ return 0.4f * std::tanh(1.2f * x) + 0.6f * x; };
            break;
        case 93: // HybridLead — bright HPF12, asym clip
            ladder.setMode(juce::dsp::LadderFilterMode::HPF12);
            ladder.setDrive(1.25f);
            gain .setGainLinear(1.05f);
            drive.functionToUse = [](float x){ return juce::jlimit(-1.0f, 1.0f, x - 0.25f * x * x * x); };
            break;
        case 94: // GlowPad — lush LPF24, smooth sat
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.10f);
            gain .setGainLinear(1.10f);
            drive.functionToUse = [](float x){ return std::tanh(1.05f * x); };
            break;

        // ---------- any ID ≥ 95 falls through here -----------------
        default:
            ladder.setMode(juce::dsp::LadderFilterMode::LPF24);
            ladder.setDrive(1.25f);
            gain .setGainLinear(1.0f);
            drive.functionToUse = [](float x){ return std::tanh(1.25f * x); };
            break;
    }
    
updateFilterOnly:
    // -------- LFO → CUTOFF  -----------------------------------------
    float modCutoff = cutoffSmoothed.getTargetValue();

    if (lfoOnParam && *lfoOnParam > 0.5f &&
        lfoToCutoffParam && *lfoToCutoffParam > 0.5f)
    {
        const float depthCut = (*lfoDepthParam) * 0.50f;          // ±50 %
        const float lfoSample = lastLfoValue;                     // raw
        modCutoff = juce::jlimit(20.0f, 20000.0f,
                                 modCutoff * (1.0f + depthCut * lfoSample));
    }

    cutoffSmoothed.setTargetValue(modCutoff);
    ladder.setCutoffFrequencyHz(cutoffSmoothed.getNextValue());
    ladder.setResonance(resonanceSmoothed.getNextValue());
    
    svFilter.setCutoffFrequency(cutoffSmoothed.getCurrentValue());
    svFilter.setResonance(resonanceSmoothed.getCurrentValue());

    // ADSR
    adsrParams.attack = *attackParam;
    adsrParams.decay = *decayParam;
    adsrParams.sustain = *sustainParam;
    adsrParams.release = *releaseParam;

    adsr.setParameters(adsrParams);
}

void SynthVoice::configureOversampling()
{
    const int desired = osModeParam ? int(osModeParam->load()) : 0;
    if (desired == currentOsMode)
        return;
    currentOsMode = desired;

    // Determine factor and filter type based on mode
    size_t factor = 1;
    auto   ftype  = juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR;
    switch (desired)
    {
        case 1: // 2× IIR
            factor = 2;
            ftype  = juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR;
            break;
        case 2: // 4× IIR
            factor = 4;
            ftype  = juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR;
            break;
        case 3: // 2× FIR Equiripple
            factor = 2;
            ftype  = juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple;
            break;
        case 4: // 4× FIR Equiripple
            factor = 4;
            ftype  = juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple;
            break;
        default:
            factor = 1;
            ftype  = juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR; // default
            break;
    }

    if (factor == 1)
    {
        oversampler.reset();
    }
    else
    {
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(1, factor, ftype);
        oversampler->initProcessing (static_cast<uint32>(samplesPerBlockCached));
    }

    // re-prepare both filterChain and svFilter at new (base × factor) rate
    const double srOS = currentSampleRate * double(factor);
    juce::dsp::ProcessSpec specOS {
        srOS,
        static_cast<uint32>(samplesPerBlockCached * factor),
        1
    };

    filterChain.reset();  filterChain.prepare(specOS);
    svFilter.reset();     svFilter.prepare   (specOS);
} 