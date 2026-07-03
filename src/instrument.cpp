#include "instrument.h"

// factories for building instrument patches. optional to use
Voice make_oscillator_envelope_voice(std::shared_ptr<OscillatorNode> osc,
                                      std::shared_ptr<EnvelopeNode> env) {
    return Voice(
        [osc, env](float frequency, float amplitude) {
            osc->osc->frequency = frequency;
            osc->osc->phase = 0.0f;
            env->trigger(amplitude);
        },
        [env] { env->release(); },
        [env] { return env->adsr.is_idle(); }
    );
}

Instrument build_instrument(AudioContext* ctx,
                             std::shared_ptr<AudioNode> output_target,
                             std::string name,
                             int voice_count,
                             std::function<std::unique_ptr<Oscillator>()> make_osc,
                             ADSR envelope) {
    Instrument instrument;
    instrument.name = std::move(name);
    instrument.voices.reserve(voice_count);

    for (int i = 0; i < voice_count; i++) {
        auto osc = std::make_shared<OscillatorNode>(make_osc(), ctx);
        auto env = std::make_shared<EnvelopeNode>(osc, ctx, envelope);
        output_target->inputs.push_back(env);
        instrument.voices.push_back(make_oscillator_envelope_voice(osc, env));
    }
    return instrument;
}

void register_instrument(AudioContext* ctx, Instrument instrument) {
    int index = static_cast<int>(ctx->instrument_voice_pools.size());
    ctx->instrument_index_names[instrument.name] = index;
    ctx->instrument_voice_pools.push_back(std::move(instrument.voices));
}
