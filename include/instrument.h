#pragma once
#include <memory>
#include "graph.h"

enum class EnvelopeParam : int { Attack, Decay, Sustain, Release };

Voice make_oscillator_envelope_voice(std::shared_ptr<OscillatorNode> osc, std::shared_ptr<EnvelopeNode> env);

struct Instrument {
    std::string name;
    std::vector<Voice> voices;
};

Instrument build_instrument(AudioContext* ctx,
                             std::shared_ptr<AudioNode> output_target,
                             std::string name,
                             int voice_count,
                             std::function<std::unique_ptr<Oscillator>()> make_osc,
                             ADSR envelope);

void register_instrument(AudioContext* ctx, Instrument instrument);
