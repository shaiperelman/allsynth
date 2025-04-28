#pragma once
#include <JuceHeader.h>

class DelayLine
{
public:
    enum class DelayType { Digital };

    DelayLine() = default;

    void prepare (double sampleRate, int maximumDelaySamples);
    void processBlock (juce::AudioBuffer<float>&, DelayType type = DelayType::Digital);

    void setDelayTime (float seconds);
    void setFeedback  (float fb)   { feedback = juce::jlimit (0.f, 0.98f, fb); }
    void setMix       (float m)    { mix      = juce::jlimit (0.f, 1.f,  m ); }

private:
    // ----------------------------------------------------------------
    float getCubicInterpolatedSample (float readPos);
    float getBufferSample (int index);

    // --- internal state --------------------------------------------
    juce::AudioBuffer<float> buffer;
    int     bufferSize          { 0 };
    int     writePosition       { 0 };
    double  fs                  { 44100.0 };

    // smoothed delay‑time
    float   targetDelayTimeSamples   { 0.f };
    float   smoothedDelayTimeSamples { 0.f };

    // cheap 1‑pole LP for digital flavour
    float previousLowPass { 0.f };

    // user parameters
    float feedback { 0.5f };
    float mix      { 0.3f };

    // helper
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayLine)
}; 