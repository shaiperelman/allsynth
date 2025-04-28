#pragma once
#include <JuceHeader.h>
#include <cmath> // For std::abs

struct AnalogueDrive
{
    float pregain { 5.0f };
    float dryWet { 0.8f }; // Mix between dry and wet signal
    float postgain { 5.5f }; // Increased compensation for volume
    float hpState[2] {0.0f}, lpState[2] {0.0f};

    void reset() noexcept
    {
        std::fill (std::begin (hpState), std::end (hpState), 0.0f);
        std::fill (std::begin (lpState), std::end (lpState), 0.0f);
    }

    static float clip (float x) noexcept
    {
        if (std::abs (x) > 1.0f)
            return (x > 0.0f) ? 1.0f : -1.0f;

        return x * (1.5f - 0.5f * std::abs (x));
    }

    float process (int ch, float x) noexcept
    {
        // Store dry signal
        float dry = x;
        
        // Assuming 2x oversampling (fs approx 88.2k or 96k)
        const float hpA = 0.9978f; // HP Cutoff ~30Hz @ 88.2k
        x -= hpState[ch]; hpState[ch] = x + hpA * hpState[ch];

        // Apply drive with pregain
        x = clip (pregain * x);
        x = clip (x); // Apply clipping twice

        const float lpA = 0.35f;  // LP Cutoff ~6kHz @ 88.2k
        lpState[ch] = lpA * x + (1.0f - lpA) * lpState[ch];
        
        // Apply postgain to the wet signal
        float wet = postgain * lpState[ch];
        
        // Mix dry and wet signals
        return (1.0f - dryWet) * dry + dryWet * wet;
    }
};