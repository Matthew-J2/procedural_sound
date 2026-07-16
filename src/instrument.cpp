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
            for_each_voice_node(head, seen, [&](const std::shared_ptr<AudioNode>& node) {
                node->retrigger(ev);
                node->trigger(ev);   // no-op for anything that doesn't override it
            });
        },
        [head] {
            std::unordered_set<AudioNode*> seen;
            for_each_voice_node(head, seen, [&](const std::shared_ptr<AudioNode>& node) { 
                node->release(); 
            });
        },
        [head] {
            // idle only when every non-shared node that has a concept of "done" agrees
            std::unordered_set<AudioNode*> seen;
            bool idle = true;
            for_each_voice_node(head, seen, [&](const std::shared_ptr<AudioNode>& node) {
                if (!node->is_idle()) idle = false;
            });
            return idle;
        },
        [params](int param_id, float value) { params->set(param_id, value); }
    );
}


// build copies of a voice
// builds an arbitrary graph
Instrument build_instrument(AudioContext* ctx,
                            std::string name,
                            int voice_count,
                            std::function<std::shared_ptr<AudioNode>(AudioContext*)> make_voice_graph,
                            std::vector<std::shared_ptr<AudioNode>> shared_nodes) {
    Instrument instrument;
    instrument.name = std::move(name);
    instrument.voices.reserve(voice_count);
    instrument.voice_nodes.reserve(voice_count);

    for (auto& node : shared_nodes)
        node->shared = true;

    // make envelope wrapped head
    // register parameters into ParamMap
    for (int i = 0; i < voice_count; i++) {
        auto params = std::make_shared<ParamMap>();
        auto head = make_voice_graph(ctx);
        params->add_graph(head);

        // register shared nodes into every voice's param map
        for (auto& node : shared_nodes)
            params->add_node(node);

        instrument.voice_nodes.push_back(head);

        // only save one name table since they're identical for each voice in the instrument
        if (i == 0)
            instrument.param_names = params->name_to_id;
        
        // create voice
        instrument.voices.push_back(make_voice(head, params));
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
