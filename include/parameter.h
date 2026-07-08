#pragma once
#include <vector>
#include <memory>

struct ModulationSource;
struct AudioNode;

struct Parameter {
    float base = 0.0f;

    float value();

    void set(float value) {
        base = value;
    }

    std::vector<ModulationSource> modulators;
};

struct ModulationSource {
    std::shared_ptr<AudioNode> source;
    float amount;
};

