#pragma once
#include <vector>
#include <memory>   
#include "core.h"
#include "oscillator.h"
#include "envelope.h"

struct AudioNode {
    std::vector<std::shared_ptr<AudioNode>> inputs;
    AudioContext* ctx = nullptr;

    virtual float process() = 0;
    virtual ~AudioNode() = default;

    float pull() {
        if (ctx->current_sample == last_ticked_sample)
            return cached_output; // if there's a feedback loop calculate it next sample
        last_ticked_sample = ctx->current_sample;
        cached_output = process();
        return cached_output;
    }

    private:
        uint64_t last_ticked_sample = UINT64_MAX;
        float cached_output = 0.0f;


};

struct OscillatorNode : AudioNode {
    std::unique_ptr<Oscillator> osc;

    OscillatorNode(std::unique_ptr<Oscillator> o, AudioContext* ctx) {
        osc = std::move(o);
        this->ctx = ctx;
    }

    // inputs condition for FM synthesis
    float process() override {
        if (!inputs.empty())
            osc->frequency = inputs[0]->pull();
        return osc->tick(ctx->sample_rate);
    }
};

struct MixerNode : AudioNode {
    float process() override {
        float sum = 0.0f;
        for (auto& input : inputs)
            sum += input->pull();
        return sum;
    }
};

// On/off switch for the graph. Blocks input if not active.
struct GateNode : AudioNode {
    bool active = false;

    GateNode(std::shared_ptr<AudioNode> source, AudioContext* ctx) {
        inputs.push_back(source);
        this->ctx = ctx;
    }

    float process() override {
        // oscillator keeps running while gate is closed. CPU inefficient
        // but more accurate to how synths work
        float sample = inputs[0]->pull();
        return active ? sample : 0.0f;
    }
};

// one input. trigger, release and tick are handled by ADSR struct
struct EnvelopeNode : AudioNode {
    ADSR adsr;

    EnvelopeNode(std::shared_ptr<AudioNode> source, AudioContext* ctx, ADSR envelope = ADSR()): adsr(envelope) {
        inputs.push_back(source);
        this->ctx = ctx;
    }

    void trigger(float peak) {
        adsr.trigger(peak);
    }

    void release() {
        adsr.release();
    }

    float process() override {
        return inputs[0]-> pull() * adsr.tick(ctx->sample_rate);
    }
};

struct Voice {
    std::shared_ptr<OscillatorNode> osc;
    std::shared_ptr<EnvelopeNode> envelope;

    int note_id = -1; // unassigned

    void trigger(int id, float frequency, float amplitude) {
        note_id = id;
        osc->osc->frequency = frequency;
        osc->osc->phase = 0.0f;
        envelope->trigger(amplitude);
    }

    void release() {
        envelope->release();
    }

    bool is_idle() const {
        return envelope->adsr.is_idle(); // checkADSR release is done
    }
};
