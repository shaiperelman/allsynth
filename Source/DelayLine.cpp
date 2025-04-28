#include "DelayLine.h"
#include <cmath>

//----------------------------------------------------------------------
// prepare
void DelayLine::prepare (double sampleRate, int maximumDelaySamples)
{
    fs = sampleRate;
    buffer.setSize (1, maximumDelaySamples);
    buffer.clear();
    bufferSize = buffer.getNumSamples();
    writePosition = 0;
    smoothedDelayTimeSamples = 0.0f;
    previousLowPass = 0.f;
}

//----------------------------------------------------------------------
// parameter setters (delayTime clamped inside)
void DelayLine::setDelayTime (float seconds)
{
    float maxDelaySecs = static_cast<float> (bufferSize - 3) / static_cast<float> (fs);
    seconds = juce::jlimit (0.f, maxDelaySecs, seconds);
    targetDelayTimeSamples = seconds * static_cast<float> (fs);
}

//----------------------------------------------------------------------
// process mono block – DIGITAL flavour only
void DelayLine::processBlock (juce::AudioBuffer<float>& buf, DelayType)
{
    const int numSamples = buf.getNumSamples();
    constexpr float smooth = 0.01f;
    for (int i = 0; i < numSamples; ++i)
    {
        smoothedDelayTimeSamples = (1.f - smooth)*smoothedDelayTimeSamples
                                 + smooth * targetDelayTimeSamples;

        float readPos = float(writePosition) - smoothedDelayTimeSamples;
        if (readPos < 0) readPos += bufferSize;

        float delayedSample = getCubicInterpolatedSample (readPos);

        // very simple LP "digital" tone-shaping
        const float alphaLP  = 0.35f;
        delayedSample = alphaLP * delayedSample + (1.f - alphaLP) * previousLowPass;
        previousLowPass = delayedSample;

        float in  = buf.getSample (0, i);
        float wet = delayedSample;

        float out = in * (1.f - mix) + wet * mix;
        buf.setSample (0, i, out);

        float feedbackSample = juce::jlimit (-1.5f, 1.5f, in + wet * feedback);
        buffer.setSample (0, writePosition, feedbackSample);

        if (++writePosition >= bufferSize) writePosition = 0;
    }
}

//----------------------------------------------------------------------
// helpers --------------------------------------------------------------
float DelayLine::getBufferSample (int index)
{
    index %= bufferSize;
    if (index < 0) index += bufferSize;
    return buffer.getSample (0, index);
}

float DelayLine::getCubicInterpolatedSample (float readPos)
{
    int idx = (int) std::floor (readPos);
    float frac = readPos - idx;

    float s0 = getBufferSample (idx - 1);
    float s1 = getBufferSample (idx);
    float s2 = getBufferSample (idx + 1);
    float s3 = getBufferSample (idx + 2);

    float a = -0.5f*s0 + 1.5f*s1 - 1.5f*s2 + 0.5f*s3;
    float b =  s0     - 2.5f*s1 + 2.0f*s2 - 0.5f*s3;
    float c = -0.5f*s0           + 0.5f*s2;
    float d =  s1;

    return ((a*frac + b)*frac + c)*frac + d;
} 