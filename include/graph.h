#pragma once
#include <vector>
#include <memory>   
#include "core.h"
#include "oscillator.h"
#include "envelope.h"
#include "parameter.h"

struct AudioNode {
    std::vector<std::shared_ptr<AudioNode>> inputs;
    AudioContext* ctx = nullptr;

    virtual float process() = 0;
    virtual ~AudioNode() = default;


    virtual std::vector<std::pair<std::string_view, Parameter*>> parameters() { return {}; }

    // on new note
    virtual void retrigger(const NoteEvent&) {}

    // on e.g. adsr envelope start, release, and idle. these 3 are needed so idle voices aren't 
    // calculated by the graph and so envelope isn't special cased down the line in instruments.
    // if you don't implement these 3 in your instrument there will be a click when the note 
    // turns on/off.

    virtual void trigger(const NoteEvent&) { active_ = true; }
    virtual void release() { active_ = false; }
    virtual bool is_idle() const { return !active_; }

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
        bool active_ = false;

};

struct OscillatorNode : AudioNode {
    std::unique_ptr<Oscillator> osc;

    // frequency it would run at with no scaling
    float base_freq = 0.0f;
    // scaling for e.g. fm synthesis - play it modulated frequency at base frequency * ratio
    Parameter ratio;

    Parameter frequency;
    Parameter amplitude;

    OscillatorNode(std::unique_ptr<Oscillator> o, AudioContext* ctx, float initial_amplitude = 1.0f, float initial_ratio = 1.0f) {
        osc = std::move(o);
        this->ctx = ctx;

        base_freq = osc->frequency;
        frequency.set(osc->frequency);
        amplitude.set(initial_amplitude);
        ratio.set(initial_ratio);
    }

    float process() override {
        frequency.base = base_freq * ratio.value();
        osc->frequency = frequency.value();
        return osc->tick(ctx->sample_rate) * amplitude.value();
    }

    std::vector<std::pair<std::string_view, Parameter*>> parameters() override {
        return {
            {"frequency", &frequency},
            {"amplitude", &amplitude},
            {"ratio", &ratio}
        };
    }
    
    // reset state for new note
    void retrigger(const NoteEvent& ev) override {
        base_freq = ev.pitch;
        osc->phase = 0.0f;
    }
};

// struct FMNode : AudioNode {
//     std::unique_ptr<Oscillator> carrier;
//     float base_freq = 0.0f;
//     float ratio = 1.0f; // modulator frequency / base frequency = ratio
//     float depth = 0.0f; // swing in Hz
//     float mod_phase = 0.0f;
//
//     FMNode(std::unique_ptr<Oscillator> carrier_osc, AudioContext* ctx,
//                    float ratio = 1.0f, float depth = 0.0f)
//         : carrier(std::move(carrier_osc)), ratio(ratio), depth(depth) {
//         this->ctx = ctx;
//     }
//
//     float process() override {
//         float mod_freq = base_freq * ratio;
//         float modulator = sinf(mod_phase) * depth;
//
//         mod_phase += 2.0f * PI * mod_freq / ctx->sample_rate;
//         mod_phase = fmodf(mod_phase, 2.0f * PI);
//         if (mod_phase < 0.0f)
//             mod_phase += 2.0f * PI;
//
//         carrier->frequency = base_freq + modulator;
//         return carrier->tick(ctx->sample_rate);
//     }
//
//     int param_count() const override { return 2; }
//
//     void set_param(int local_index, float value) override {
//         if (local_index == 0) ratio = value;
//         else if (local_index == 1) depth = value;
//     }
//
//     std::string_view param_name(int local_index) const override {
//         switch (local_index) {
//             case 0: return "fm_ratio";
//             case 1: return "fm_depth";
//             default: return {};
//         }
//     }
//
//     // reset state for new note
//     void retrigger(float frequency) override {
//         base_freq = frequency;
//         carrier->phase = 0.0f;
//         mod_phase = 0.0f;
//     }
// };

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
    Parameter active;

    GateNode(std::shared_ptr<AudioNode> source, AudioContext* ctx) {
        inputs.push_back(source);
        this->ctx = ctx;
    }

    float process() override {
        // oscillator keeps running while gate is closed. CPU inefficient
        // but more accurate to how synths work
        float sample = inputs[0]->pull();
        return active.value() != 0.0f ? sample : 0.0f;
    }
    

    std::vector<std::pair<std::string_view, Parameter*>> parameters() override {
        return {{"active", &active}};
    }

};

struct ConstantNode : AudioNode {
    float value;

    ConstantNode(float value, AudioContext* ctx) : value(value) {
        this->ctx = ctx;
    }

    float process() override { return value; }
};

struct GainNode : AudioNode {
    Parameter amplitude;

    GainNode(std::shared_ptr<AudioNode> source, AudioContext* ctx, float initial_amplitude = 0.0f) {
        inputs.push_back(source);
        this->ctx = ctx;
        amplitude.set(initial_amplitude);
    }

    float process() override {
        return inputs[0]->pull() * amplitude.value();
    }
    

    std::vector<std::pair<std::string_view, Parameter*>> parameters() override {
        return {{"gain", &amplitude}};
    }

};

// one input. trigger, release and tick are handled by ADSR struct
struct EnvelopeNode : AudioNode {
    ADSR adsr;

    EnvelopeNode(AudioContext* ctx, ADSR envelope = ADSR()): adsr(envelope) {
        this->ctx = ctx;
    }

    void trigger(const NoteEvent& ev) override {
        adsr.trigger(ev.velocity);
    }

    void release() override {
        adsr.release();
    }

    bool is_idle() const override {
        return adsr.is_idle();
    }

    float process() override {
        return adsr.tick(ctx->sample_rate);
    }

    std::vector<std::pair<std::string_view, Parameter*>> parameters() override {
        return {
            {"attack", &adsr.attack_time},
            {"decay", &adsr.decay_time},
            {"sustain", &adsr.sustain_level},
            {"release", &adsr.release_time}
        };
    }
};

// node to sum only currently active voices and prune inactive ones once their envelope is idle.
struct InstrumentMixNode: AudioNode {
    int instrument_index;
    
    InstrumentMixNode(AudioContext* ctx, int instrument_index)
    : instrument_index(instrument_index) {
        this->ctx = ctx;
    }       

    float process() override {
        auto& indices = ctx->active_voice_indices[instrument_index];
        auto& nodes = ctx->instrument_voice_nodes[instrument_index];
        auto& pool = ctx->instrument_voice_pools[instrument_index];

        float sum = 0.0f;
        for (size_t k = 0; k < indices.size(); ) {
            int i = indices[k];
            sum += nodes[i]->pull(); // advances the envelope for each active node

            if (pool[i].is_idle()) {
                indices[k] = indices.back();
                indices.pop_back(); // swap-and-pop, order doesn't matter
            } else {
                k++;
            }
        }
        return sum;
    }
};

