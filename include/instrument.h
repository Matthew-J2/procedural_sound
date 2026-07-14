#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <stdexcept>
#include "graph.h"

// takes previous node, context, returns new node.
using NodeFactory = std::function<std::shared_ptr<AudioNode>(std::shared_ptr<AudioNode> input, AudioContext* ctx)>;

// turns nodes' parameter lists into one flat list of named parameters per instrument
struct ParamMap {
    struct Entry { 
        std::shared_ptr<AudioNode> node; 
        Parameter* param = nullptr;

        Parameter* modulator_owner = nullptr;
        int modulator_index = -1;
    };

    // table
    std::vector<Entry> entries;
    // lookup
    std::unordered_map<std::string, int> name_to_id;
    
    // add entry to parameter table. call for each node in graph
    void add_node(std::shared_ptr<AudioNode> node) {
        for (auto& [name, param] : node->parameters()) {
            int global_id = static_cast<int>(entries.size());
            entries.push_back({node, param, nullptr, -1});
            if (!name.empty())
                name_to_id.emplace(std::string(name), global_id);
        }
    }

    // necessary because owning node doesnt know which modulators exist on a parameter
    // register a modulation source's amount
    int add_modulator_param(std::string name, std::shared_ptr<AudioNode> owner_node, Parameter& owner, int modulator_index) {
        int global_id = static_cast<int>(entries.size());
        entries.push_back({std::move(owner_node), nullptr, &owner, modulator_index});
        if (!name.empty())
            name_to_id.emplace(std::move(name), global_id);
        return global_id;
        
    }

    // check which node to change, call with local index
    void set(int global_id, float value) const {
        if (global_id < 0 || static_cast<size_t>(global_id) >= entries.size())
            return; // unknown id
        const Entry& e = entries[global_id];
        
        // check type of entry
        if (e.modulator_owner && e.modulator_index >= 0) {
            e.modulator_owner->modulators[e.modulator_index].amount.base = value;
        } else if (e.param) {
            e.param->base = value;
        }
    }

    // check name -> id in events
    int id_for(std::string_view name) const {
        auto it = name_to_id.find(std::string(name));
        if (it == name_to_id.end())
            throw std::invalid_argument("ParamMap: unknown parameter name");
        return it->second;
    }
};

Voice make_voice(std::shared_ptr<AudioNode> head, std::shared_ptr<ParamMap> params);

// Named collection of voices and a parameter name table.
struct Instrument {
    std::string name;
    std::vector<Voice> voices;
    std::vector<std::shared_ptr<AudioNode>> voice_nodes; // end node for each voice
    std::unordered_map<std::string, int> param_names;
};

Instrument build_instrument(AudioContext* ctx,
                             std::string name,
                             int voice_count,
                             std::function<std::shared_ptr<AudioNode>(AudioContext*)> make_head,
                             std::vector<NodeFactory> extra_nodes = {});

NodeFactory make_envelope_factory(ADSR envelope);

void register_instrument(AudioContext* ctx, Instrument instrument, std::shared_ptr<AudioNode> output_target);

int param_id(AudioContext* ctx, int instrument_index, std::string_view name);
