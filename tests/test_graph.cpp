#include <gtest/gtest.h>
#include <memory>
#include "core.h"
#include "graph.h"
#include "oscillator.h"

// stub node to implement AudioNode

struct StubNode : AudioNode {
    float value;
    StubNode(float v, AudioContext* c) : value(v) { ctx = c;}
    float process() override { return value; }
};

TEST(Graph, MixerSums) {
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
    MixerNode mixer;
    mixer.ctx = &ctx;

    mixer.inputs.push_back(std::make_shared<StubNode>(0.3f, &ctx));
    mixer.inputs.push_back(std::make_shared<StubNode>(0.5f, &ctx));
    EXPECT_NEAR(mixer.pull(), 0.8f, 1e-6f);
}

TEST(Graph, EmptyMixer) {
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
    MixerNode mixer;
    mixer.ctx = &ctx;

    EXPECT_FLOAT_EQ(mixer.pull(), 0.0f);
}

TEST(Graph, GateBlocks) {
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;

    auto source = std::make_shared<StubNode>(0.7f, &ctx);
    GateNode gate(source, &ctx);

    gate.active.set(0.0f);
    EXPECT_FLOAT_EQ(gate.pull(), 0.0f);
}

TEST(Graph, GatePasses) {
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;

    auto source = std::make_shared<StubNode>(0.7f, &ctx);
    GateNode gate(source, &ctx);

    gate.active.set(1.0f);
    EXPECT_FLOAT_EQ(gate.pull(), 0.7f);
}

TEST(Graph, OscillatorAppliesFM) {
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;

    auto osc_node = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(440.0f), &ctx, 1.0f);
    osc_node->inputs.push_back(std::make_shared<StubNode>(220.0f, &ctx));

    SineOscillator reference(220.0f);

    for (int i = 0; i < 10; i++) {
        ctx.current_sample++;
        float sample = osc_node->pull();
        float expected = reference.tick(ctx.sample_rate);
        EXPECT_NEAR(sample, expected, 1e-5f) << "mismatch at sample " << i;
    }
}

TEST(Graph, OscillatorNodeOwnFrequencyNoInput) {
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;

    auto osc_node = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(440.0f), &ctx, 1.0f);

    float sample = osc_node->pull();

    SineOscillator reference(440.0f);
    float expected = reference.tick(ctx.sample_rate);

    EXPECT_NEAR(sample, expected, 1e-5f);
}
