#include "instrument.h"

// factories for building instrument patches.

// when a note starts, set pitch, reset phase, trigger envelope
// when releasing a note, trigger envelope release
// takes parameter ID to be changed and forwards to ParamMap
Voice make_voice(std::shared_ptr<AudioNode> head,
                                     std::shared_ptr<EnvelopeNode> env,
                                     std::shared_ptr<ParamMap> params) {
    return Voice(
        [head, env](float frequency, float amplitude) {
            head->retrigger(frequency);
            env->trigger(amplitude);
        },
        [env] { env->release(); },
        [env] { return env->adsr.is_idle(); },
        [params](int param_id, float value) { params->set(param_id, value); }
    );
}

// build copies of a voice
Instrument build_instrument(AudioContext* ctx,
                             std::shared_ptr<AudioNode> output_target,
                             std::string name,
                             int voice_count,
                            std::function<std::shared_ptr<AudioNode>(AudioContext*)> make_head,
                            ADSR envelope,
                            std::vector<NodeFactory> extra_nodes) {
    Instrument instrument;
    instrument.name = std::move(name);
    instrument.voices.reserve(voice_count);

    // make envelope wrapped head
    // register parameters into ParamMap
    for (int i = 0; i < voice_count; i++) {
        auto head = make_head(ctx);
        auto env = std::make_shared<EnvelopeNode>(head, ctx, envelope);

        auto params = std::make_shared<ParamMap>();
        params->add_node(env);

        std::shared_ptr<AudioNode> chain_end = env;
        
        // add extra nodes to chain. need to make oscillator and envelope 
        // work like this too but they're special cases at the moment
        for (auto& make_node : extra_nodes) {
            auto node = make_node(chain_end, ctx);
            params->add_node(node);
            chain_end = node;
        }

        output_target->inputs.push_back(chain_end);

        // only save one name table since they're identical for each voice in the instrument
        if (i == 0)
            instrument.param_names = params->name_to_id;
        
        // create voice
        instrument.voices.push_back(make_voice(head, env, params));
    }
    return instrument;
}

// add instrument data to context
void register_instrument(AudioContext* ctx, Instrument instrument) {
    int index = static_cast<int>(ctx->instrument_voice_pools.size());
    ctx->instrument_index_names[instrument.name] = index;
    ctx->instrument_voice_pools.push_back(std::move(instrument.voices));
    ctx->instrument_param_names.push_back(std::move(instrument.param_names));
}

// lookup for parameters
int param_id(AudioContext* ctx, int instrument_index, std::string_view name) {
    if (instrument_index < 0 || static_cast<size_t>(instrument_index) >= ctx->instrument_param_names.size())
        throw std::invalid_argument("param_id: unknown instrument index");

    auto& names = ctx->instrument_param_names[instrument_index];
    auto it = names.find(std::string(name));
    if (it == names.end())
        throw std::invalid_argument("param_id: unknown parameter name for this instrument");
    return it->second;
}
