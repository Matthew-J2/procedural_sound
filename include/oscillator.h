#pragma once
#include <miniaudio.h>
#include <math.h>

struct Oscillator {
    float phase = 0.0f;
    float frequency = 440.0f;
    float amplitude = 0.1f;

    Oscillator(float frequency, float amplitude)
        : frequency(frequency), amplitude(amplitude) {}

    virtual float tick(float sample_rate) = 0;
    virtual ~Oscillator() = default;
};

struct SineOscillator : Oscillator {
    using Oscillator::Oscillator;
    float tick(float sample_rate) override {
        float sample = sinf(phase) * amplitude;
        phase += 2.0f * M_PI * frequency / sample_rate;
        if (phase > 2.0f * M_PI)
            phase -= 2.0f * M_PI;
        return sample;
    }
};
