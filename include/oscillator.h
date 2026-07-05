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

    // this isn't inherited because it could be different
    // for different oscillators e.g. noise, wavetable, PM

        phase = fmodf(phase, 2.0f * PI);
        if (phase < 0.0f)
            phase += 2.0f * PI;

        return sample;
    }
};

// TODO: Rename to IdealSquareOscillator or something like that
// and use PolyBLEP or something for SquareOscillator
struct SquareOscillator : Oscillator {
    using Oscillator::Oscillator;
    float tick(float sample_rate) override {
        float sample = (phase < PI ? 1.0 : -1.0f) * amplitude;
        phase += 2.0 * PI * frequency / sample_rate;
        phase = fmodf(phase, 2.0f * PI);
        if (phase < 0.0f)
            phase += 2.0f * PI;
        return sample;
    }
};

// TODO: Again, needs PolyBLEP or something
struct TriangleOscillator : Oscillator {
    using Oscillator::Oscillator;
    float tick(float sample_rate) override {
        float t = phase / (2.0f * PI); // normalize to [0,1)

        float sample;
        if (t < 0.5f)
            sample = (t * 4.0f - 1.0f);
        else
            sample = (3.0f - t * 4.0f);

        sample *= amplitude;

        phase += 2.0f * PI * frequency / sample_rate;
        phase = fmodf(phase, 2.0f * PI);
        if (phase < 0.0f)
            phase += 2.0f * PI;

        return sample;
    }
};

struct SawOscillator : Oscillator {
    using Oscillator::Oscillator;
    float tick(float sample_rate) override {
        float sample = (phase / PI - 1.0f) * amplitude;
        phase += 2.0f * PI * frequency / sample_rate;
        phase = fmodf(phase, 2.0f * PI);
        if (phase < 0.0f)
            phase += 2.0f * PI;
        return sample;
    }
};
