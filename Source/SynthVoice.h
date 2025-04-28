#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <atomic>

class SynthVoice : public juce::SynthesiserVoice
{
public:
    SynthVoice(juce::AudioProcessorValueTreeState& vts);

    /** Set host BPM for tempo-synced LFO */
    void setHostBpm(double bpm) { hostBpm = bpm; }

    //==============================================================================
    bool canPlaySound(juce::SynthesiserSound* sound) override;

    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote(float velocity, bool allowTailOff) override;

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    void renderNextBlock(juce::AudioBuffer<float>&, int startSample, int numSamples) override;

    void prepare(double sampleRate, int samplesPerBlock, int outputChannels);

    enum
    {
        gainIndex,
        filterIndex,
        shaperIndex
    };

private:
    //==============================================================================
    float computeOscSample();
    void updateParams();

    // Members
    juce::AudioProcessorValueTreeState& parameters;

    // Internal audio util objects: Gain -> LadderFilter -> WaveShaper
    juce::dsp::ProcessorChain<juce::dsp::Gain<float>, juce::dsp::LadderFilter<float>, juce::dsp::WaveShaper<float>> filterChain;
    juce::dsp::StateVariableTPTFilter<float> svFilter;

    // Smoothed parameters
    juce::LinearSmoothedValue<float> cutoffSmoothed   { 20000.0f };
    juce::LinearSmoothedValue<float> resonanceSmoothed{     0.7f };

    int currentModel = 0;   // 0=minimoog 1=prodigy 2=arp2600 3=odyssey
                            // 4=cs80 5=jupiter-4 6=ms-20 7=polymoog 8=ob-x
                            // 9=prophet‑5 10=taurus 11=model d
                            // 12=sh‑101 13=juno‑60 14=monopoly
                            // 15=voyager 16=prophet‑6 17=jupiter‑8 18=polysix 19=matrix‑12
                            // 20=ppg wave 21=ob‑6 22=dx7 23=virus 24=d‑50
                            // 25=memorymoog 26=minilogue 27=sub37 28=nord lead 2 29=blofeld
                            // 30=prophet vs 31=prophet-10 32=jx-8p 33=cz-101 34=esq-1
                            // 35=system-8 36=massive 37=microfreak 38=analog four 39=microkorg
                            // 40=tb-303 41=jp-8000 42=m1 43=wavestation 44=jd-800
                            // 45=hydrasynth 46=polybrute 47=matriarch 48=kronos 49=prophet-12
                            // 50=ob-xa 51=ob-x8 52=juno-106 53=jx-3p 54=jupiter-6 55=alpha juno
                            // 56=grandmother 57=subsequent 25 58=moog one 59=arp omni
                            // 60=cs-30 61=an1x 62=prologue 63=dw-8000 64=ms2000 65=delta
                            // 66=rev2 67=prophet x 68=microwave 69=q
                            // 70=lead-4 71=sq-80 72=cz-5000 73=system-100 74=poly evolver
                            // --- DREAMSYNTH MODELS (IDs 75-84) -----------------------
                            // 75=Nebula 76=Solstice 77=Aurora 78=Lumina 79=Cascade
                            // 80=Polaris 81=Eclipse 82=Quasar 83=Helios 84=Meteor
                            // --- MIXSYNTHS MODELS (IDs 85‑94) -----------------------
                            // 85=Fusion‑84     (Minimoog × Jupiter‑8)
                            // 86=Velvet‑CS     (CS‑80 × Juno‑60)
                            // 87=PolyProphet   (Prophet‑5 × Polysix)
                            // 88=BassMatrix    (TB‑303 × Matrix‑12)
                            // 89=WaveVoyager   (PPG Wave × Voyager)
                            // 90=StringEvo     (OB‑X × Poly Evolver)
                            // 91=MicroMass     (MicroFreak × Massive)
                            // 92=DigitalMoog   (DX7 × Moog ladder)
                            // 93=HybridLead    (Nord Lead 2 × Analog Four)
                            // 94=GlowPad       (Wavestation × SH‑101)
    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;

    double currentSampleRate = 44100.0;

    // Oscillator phase
    double phase = 0.0;
    float triangleIntegrator = 0.0f;   // per‐voice integrator for triangle

    // NEW -----------------------------------------------------------------------
    double phase2 = 0.0;                  // 2nd oscillator
    float  triangleIntegrator2 = 0.0f;
    double lfoPhase = 0.0;                // LFO phase
    
    // Noise generator
    juce::Random rnd;
    float noiseMix = 0.0f;   // updated each block from the parameter
    bool  noiseOn  = false;
    
    // Performance optimizations
    juce::AudioBuffer<float> scratchBuffer;   // reused temp buffer
    int previousModel = -1;                   // cache to skip switch

    // Parameter pointer caches to avoid per-sample lookups
    std::atomic<float>* wave1Param     = nullptr;
    std::atomic<float>* wave2Param     = nullptr;
    std::atomic<float>* pulseWidthParam= nullptr;
    std::atomic<float>* osc1VolParam   = nullptr;
    std::atomic<float>* osc2VolParam   = nullptr;
    std::atomic<float>* lfoOnParam     = nullptr;
    std::atomic<float>* lfoRateParam   = nullptr;
    std::atomic<float>* lfoDepthParam  = nullptr;
    std::atomic<float>* noiseOnParam   = nullptr;
    std::atomic<float>* noiseMixParam  = nullptr;
    std::atomic<float>* modelParam     = nullptr;
    std::atomic<float>* cutoffParam    = nullptr;
    std::atomic<float>* resonanceParam = nullptr;
    std::atomic<float>* attackParam    = nullptr;
    std::atomic<float>* decayParam     = nullptr;
    std::atomic<float>* sustainParam   = nullptr;
    std::atomic<float>* releaseParam   = nullptr;
    // ===== NEW analogue-extra parameter pointers =============================
    std::atomic<float>* freePhaseParam  = nullptr;
    std::atomic<float>* driftParam      = nullptr;
    std::atomic<float>* filterTolParam  = nullptr;
    std::atomic<float>* vcaClipParam    = nullptr;
    std::atomic<float>* analogEnvParam  = nullptr;   // NEW: sqrt envelope
    std::atomic<float>* legatoParam     = nullptr;   // NEW: single-trigger ADSR
    // === NEW detune pointers ========================================
    std::atomic<float>* osc2SemiParam  = nullptr;
    std::atomic<float>* osc2FineParam  = nullptr;
    // =========================================================================

    // -------- drift & tolerance state ----------------------------------------
    double frequency    = 440.0;
    double drift        = 0.0;
    float  cutoffTol    = 1.0f;
    float  resonanceTol = 1.0f;
    // ---------------------------------------------------------------------------

    // DSP-based LFO for improved multi-shape and sync
    juce::dsp::Oscillator<float> lfoOsc;
    double hostBpm { 120.0 };                      // current host BPM
    std::atomic<float>* lfoSyncParam = nullptr;    // tempo-sync toggle
    std::atomic<float>* lfoShapeParam = nullptr;   // LFO waveform shape
    std::atomic<float>* lfoSyncDivParam = nullptr; // LFO sync division selector
    std::atomic<float>* lfoPhaseParam = nullptr;   // LFO phase offset (0..1)
    int previousLfoShape = -1;                    // cache last applied LFO shape

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthVoice)
}; 