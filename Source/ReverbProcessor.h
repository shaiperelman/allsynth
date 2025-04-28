#pragma once
#include <JuceHeader.h>

class ReverbProcessor
{
public:
    ReverbProcessor() = default;

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        reverb.prepare (spec);
        dryWet.prepare  (spec);
        dryWet.setWetLatency (0);
        dryWet.setWetMixProportion (mix);
        reset();
    }

    void reset()
    {
        reverb.reset();
        dryWet.reset();
    }

    void setMix (float m)
    {
        mix = juce::jlimit (0.f, 1.f, m);
        dryWet.setWetMixProportion (mix);
    }

    void setParameters(const juce::Reverb::Parameters& p)
    {
        reverb.setParameters(p);
    }

    void processBlock (juce::AudioBuffer<float>& buffer)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        dryWet.pushDrySamples (block);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (block));
        dryWet.mixWetSamples (block);
    }

private:
    juce::dsp::Reverb                 reverb;
    juce::dsp::DryWetMixer<float>     dryWet;
    float mix { 0.3f };
}; 