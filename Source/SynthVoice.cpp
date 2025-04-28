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
    currentSampleRate = sampleRate;

    dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<uint32>(samplesPerBlock);
    spec.numChannels = 1;

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
    float lfoDpParam = *lfoDepthParam; // Get raw 0-1 param
    float depth  = lfoDpParam * lfoDpParam * 0.08f; // Quadratic scaling, max ~8%

    // --- LFO using dsp::Oscillator with optional tempo-sync ---
    float lfoVal = 0.0f;
    if (lfoOn)
    {
        // Determine LFO rate (Hz)
        double rateHz = lfoRt;
        if (lfoSyncParam && *lfoSyncParam > 0.5f)
        {
            // Quarter-note sync: LFO frequency = BPM / 60
            rateHz = hostBpm / 60.0;
        }
        lfoOsc.setFrequency((float)rateHz);
        // process LFO sample (-1..1)
        float lfoSample = lfoOsc.processSample(0.0f);
        lfoVal = lfoSample * depth;
    }

    const double baseFreq = juce::MidiMessage::getMidiNoteInHertz(getCurrentlyPlayingNote());
    
    const double freqMod = baseFreq * (1.0 + lfoVal);
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

    if (!useSVF)
    {
        // Just clear the pre-allocated buffer; we'll only process [0..numSamples)
        auto& tempBuffer = scratchBuffer;
        tempBuffer.clear();  

        // Fill temp buffer with oscillator output
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float oscSample = computeOscSample();
            tempBuffer.setSample(0, sample, oscSample);
        }

        // Process only the first numSamples via getSubBlock()
        auto block = juce::dsp::AudioBlock<float>(tempBuffer)
                        .getSubBlock(0, (size_t)numSamples);
        juce::dsp::ProcessContextReplacing<float> context(block);
        filterChain.process(context);

        // Apply ADSR and copy to output buffer
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float filtered = tempBuffer.getSample(0, sample);
            float env = adsr.getNextSample();
            if (analogEnvParam && *analogEnvParam > 0.5f)
                env = std::sqrt(env);   // RC‑style analog curve
            float currentSample = filtered * env;

            // Add to all output channels
            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                outputBuffer.addSample(channel, startSample + sample, currentSample);
        }
    }
    else // State Variable Filter path (remains unchanged)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float osc = computeOscSample();
            float filt = svFilter.processSample(0, osc);
            filt = std::tanh(1.4f * filt);     // simple drive
            float env = adsr.getNextSample();
            if (analogEnvParam && *analogEnvParam > 0.5f)
                env = std::sqrt(env);   // RC‑style analog curve
            float currentSample = filt * env;

            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                outputBuffer.addSample(channel, startSample + sample, currentSample);
        }
    }

    if (!adsr.isActive())
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
    // Update filter parameters
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