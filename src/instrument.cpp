#include "instrument.h"
#include "graph.h"

// factories for building instrument patches.

// when a note starts, set pitch, reset phase, trigger envelope
// when releasing a note, trigger envelope release
// takes parameter ID to be changed and forwards to ParamMap

Voice make_voice(std::shared_ptr<AudioNode> head,
                  std::shared_ptr<ParamMap> params) {
    return Voice(
        [head](const NoteEvent& ev) {
            std::unordered_set<AudioNode*> seen;
            for_each_voice_node(head.get(), seen, [&](AudioNode& node) {
                node.retrigger(ev);
                node.trigger(ev);   // no-op for anything that doesn't override it
            });
        },
        [head] {
            std::unordered_set<AudioNode*> seen;
            for_each_voice_node(head.get(), seen, [&](AudioNode& node) { node.release(); });
        },
        [head] {
            // idle only when every non-shared node that has a concept of "done" agrees
            std::unordered_set<AudioNode*> seen;
            bool idle = true;
            for_each_voice_node(head.get(), seen, [&](AudioNode& node) {
                if (!node.is_idle()) idle = false;
            });
            return idle;
        },
        [params](int param_id, float value) { params->set(param_id, value); }
    );
}

NodeFactory make_envelope_factory(ADSR envelope) {
    return [envelope](std::shared_ptr<AudioNode> input, AudioContext* ctx) -> std::shared_ptr<AudioNode> {
        return std::make_shared<EnvelopeNode>(ctx, envelope);
    };
}

// build copies of a voice
Instrument build_instrument(AudioContext* ctx,
                            std::string name,
                            int voice_count,
                            std::function<std::shared_ptr<AudioNode>(AudioContext*)> make_head,
                            std::vector<NodeFactory> chain) {
    Instrument instrument;
    instrument.name = std::move(name);
    instrument.voices.reserve(voice_count);
    instrument.voice_nodes.reserve(voice_count);

    // make envelope wrapped head
    // register parameters into ParamMap
    for (int i = 0; i < voice_count; i++) {
        auto params = std::make_shared<ParamMap>();
        auto head = make_head(ctx);
        params->add_node(head);

        std::vector<std::shared_ptr<AudioNode>> chain_nodes = {head};
        std::shared_ptr<AudioNode> chain_end = head;
        
        // add extra nodes to chain. need to make oscillator and envelope 
        // work like this too but they're special cases at the moment
        for (auto& make_node : chain) {
            auto node = make_node(chain_end, ctx);
            params->add_node(node);
            chain_nodes.push_back(node);
            chain_end = node;
        }

        instrument.voice_nodes.push_back(chain_end);

        // only save one name table since they're identical for each voice in the instrument
        if (i == 0)
            instrument.param_names = params->name_to_id;
        
        // create voice
        instrument.voices.push_back(make_voice(chain_end, params));
    }
    return instrument;
}

// add instrument data to context including a mix node that pulls in only active voices.
void register_instrument(AudioContext* ctx, Instrument instrument, std::shared_ptr<AudioNode> output_target) {
    int index = static_cast<int>(ctx->instrument_voice_pools.size());
    ctx->instrument_index_names[instrument.name] = index;
    ctx->instrument_voice_pools.push_back(std::move(instrument.voices));
    ctx->instrument_voice_nodes.push_back(std::move(instrument.voice_nodes));
    ctx->instrument_param_names.push_back(std::move(instrument.param_names));
    ctx->active_voice_indices.push_back({});

    auto mix_node = std::make_shared<InstrumentMixNode>(ctx, index);
    output_target->inputs.push_back(mix_node);
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
