#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include "constants.h"


struct ModulationSource;
struct AudioNode;
struct AudioContext;

struct Parameter {
    float base = 0.0f;

    Parameter(float base_val = 0.0f);
    Parameter(float base_val, std::vector<ModulationSource> mods);

    float value();

    // without smoothing
    void set(float value) {
        base = value;
        target = value;
    }

    void set_smoothed(float value) {
        target = value;
        if (!ctx || alpha <= 0.0f)
            base = value;
    }

    void enable_smoothing(float frequency_cutoff, AudioContext* ctx_in, float sample_rate) {
        ctx = ctx_in;
        float response_time = 1.0f / (2.0 * PI * frequency_cutoff);
        float sample_period = 1.0f / sample_rate;
        alpha = response_time / (response_time + sample_period);
    }

    std::vector<ModulationSource> modulators;

    private: 
        AudioContext* ctx = nullptr;
        float target = 0.0f;
        float alpha = 0.0f; // disabled
        uint64_t last_ticked_sample = UINT64_MAX;
};

struct ModulationSource {
    std::shared_ptr<AudioNode> source;
    Parameter amount;
};

inline Parameter::Parameter(float base_val)
    : base(base_val), target(base_val) {}

inline Parameter::Parameter(float base_val, std::vector<ModulationSource> mods)
    : base(base_val), modulators(std::move(mods)), target(base_val) {}
