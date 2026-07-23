#pragma once
#include <miniaudio.h>
#include <math.h>
#include <string_view>
#include "constants.h"

struct Oscillator {
    float phase = 0.0f;
    float frequency = 440.0f;

    Oscillator(float frequency)
        : frequency(frequency) {}

    virtual float tick(float sample_rate) = 0;
    virtual std::string_view waveform_name() const { return "Oscillator"; }
    virtual ~Oscillator() = default;
};

struct SineOscillator : Oscillator {
    using Oscillator::Oscillator;
    std::string_view waveform_name() const override { return "Sine"; }
    float tick(float sample_rate) override {
        float sample = sinf(phase);
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
    std::string_view waveform_name() const override { return "Square"; }
    float tick(float sample_rate) override {
        float sample = (phase < PI ? 1.0 : -1.0f);
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
    std::string_view waveform_name() const override { return "Triangle"; }
    float tick(float sample_rate) override {
        float t = phase / (2.0f * PI); // normalize to [0,1)

        float sample;
        if (t < 0.5f)
            sample = (t * 4.0f - 1.0f);
        else
            sample = (3.0f - t * 4.0f);

        phase += 2.0f * PI * frequency / sample_rate;
        phase = fmodf(phase, 2.0f * PI);
        if (phase < 0.0f)
            phase += 2.0f * PI;

        return sample;
    }
};

struct SawOscillator : Oscillator {
    using Oscillator::Oscillator;
    std::string_view waveform_name() const override { return "Saw   "; }
    float tick(float sample_rate) override {
        float sample = (phase / PI - 1.0f);
        phase += 2.0f * PI * frequency / sample_rate;
        phase = fmodf(phase, 2.0f * PI);
        if (phase < 0.0f)
            phase += 2.0f * PI;
        return sample;
    }
};
