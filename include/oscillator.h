#pragma once
#include <miniaudio.h>
#include <math.h>
constexpr float PI = 3.14159265358979323846f;

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
        phase += 2.0f * PI * frequency / sample_rate;
        if (phase > 2.0f * PI)
            phase -= 2.0f * PI;
        return sample;
    }
};

struct SquareOscillator : Oscillator {
    using Oscillator::Oscillator;
    float tick(float sample_rate) override {
        float sample = (phase < PI ? 1.0 : -1.0f) * amplitude;
        phase += 2.0 * PI * frequency / sample_rate;
        if (phase > 2.0f * PI)
            phase -= 2.0f * PI;
        return sample;
    }
};

struct TriangleOscillator : Oscillator {
    using Oscillator::Oscillator;
    float tick(float sample_rate) override {
        float sample = (2.0f * fabsf(phase / PI - 1.0f) - 1.0f) * amplitude;
        phase += 2.0 * PI * frequency / sample_rate;
        if (phase > 2.0f * PI)
            phase -= 2.0f * PI;
        return sample;
    }
};

struct SawOscillator : Oscillator {
    using Oscillator::Oscillator;
    float tick(float sample_rate) override {
        float sample = (phase / M_PI - 1.0f) * amplitude;
        phase += 2.0f * M_PI * frequency / sample_rate;
        if (phase > 2.0f * M_PI)
            phase -= 2.0f * M_PI;
        return sample;
    }
};
