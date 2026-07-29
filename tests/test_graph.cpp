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


TEST(Graph, OscillatorFrequencyModulatorDrivesPitch) {
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto osc_node = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(0.0f), &ctx, 1.0f);
    osc_node->frequency.modulators.push_back({
        std::make_shared<StubNode>(220.0f, &ctx), {1.0f, {}}
    });
 
    SineOscillator reference(220.0f);
 
    for (int i = 0; i < 10; i++) {
        ctx.current_sample++;
        float sample = osc_node->pull();
        float expected = reference.tick(ctx.sample_rate);
        EXPECT_NEAR(sample, expected, 1e-5f) << "mismatch at sample " << i;
    }
}


TEST(Graph, OscillatorNodeAppliesAmplitude) {
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;

    auto osc_node = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(440.0f), &ctx, 0.25f);

    for (int i = 0; i < 10000; i++) {
        ctx.current_sample++;
        float sample = osc_node->pull();
        EXPECT_GE(sample, -0.25f);
        EXPECT_LE(sample, 0.25f);
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

TEST(Graph, OnePoleComplementaryIdentity) {
    // Low pass and high pass output must equal original input

    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;

    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    OnePoleNode lp(src, &ctx, 500.0f, OnePoleNode::Mode::LowPass);
    OnePoleNode hp(src, &ctx, 500.0f, OnePoleNode::Mode::HighPass);

    
    for (int i = 0; i < 2000; i++) {
        ctx.current_sample++;
        src->value = std::sin(2.0f * (float)M_PI * 300.0f * i / ctx.sample_rate);
        float o_lp = lp.pull();
        float o_hp = hp.pull();
        EXPECT_NEAR(o_lp + o_hp, src->value, 1e-5f) << "sample " << i;
    }
}

TEST(Graph, OnePoleDCLowPass){
    // low pass DC gain is 1 because frequency is 0
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.6f, &ctx);
    OnePoleNode f(src, &ctx, 300.0f, OnePoleNode::Mode::LowPass);
 
    float out = 0.0f;
    for (int i = 0; i < 5000; i++) { ctx.current_sample++; out = f.pull(); }
    EXPECT_NEAR(out, 0.6f, 1e-4f);
}

TEST(Graph, OnePoleDCHighPass){
    // high pass DC gain is 0 because frequency is 0
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.6f, &ctx);
    OnePoleNode f(src, &ctx, 300.0f, OnePoleNode::Mode::HighPass);
 
    float out = 1.0f;
    for (int i = 0; i < 5000; i++) { ctx.current_sample++; out = f.pull(); }
    EXPECT_NEAR(out, 0.0f, 1e-4f);
}

TEST(Graph, OnePoleLowCutoff){
    // a low cutoff should heavily attenuate a tone well above it
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    OnePoleNode f(src, &ctx, 100.0f, OnePoleNode::Mode::LowPass);
 
    float peak = 0.0f;
    for (int i = 0; i < 4410; i++) {
        ctx.current_sample++;
        src->value = std::sin(2.0f * (float)M_PI * 2000.0f * i / ctx.sample_rate);
        float out = f.pull();
        if (i > 2000) peak = std::max(peak, std::fabs(out)); // steady-state tail only
    }
    EXPECT_LT(peak, 0.2f);
}

TEST(Graph, OnePoleHighCutoff){
    // a cutoff well above the test tone should barely attenuate it at all
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    OnePoleNode f(src, &ctx, 15000.0f, OnePoleNode::Mode::LowPass);
 
    float peak = 0.0f;
    for (int i = 0; i < 4410; i++) {
        ctx.current_sample++;
        src->value = std::sin(2.0f * (float)M_PI * 2000.0f * i / ctx.sample_rate);
        float out = f.pull();
        if (i > 2000) peak = std::max(peak, std::fabs(out));
    }
    EXPECT_GT(peak, 0.85f);
}

TEST(Graph, OnePoleResetStateTrigger){
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(1.0f, &ctx);
    OnePoleNode f(src, &ctx, 200.0f, OnePoleNode::Mode::LowPass);
    f.reset_state_on_retrigger = true;
 
    for (int i = 0; i < 50; i++) { ctx.current_sample++; f.pull(); } // let it settle near 1.0
    f.retrigger(NoteEvent{});
    ctx.current_sample++; // need next sample
    float after = f.pull();
    EXPECT_LT(after, 0.05f); // starting from zero state again, first sample should be small
 
    // reset_state_on_retrigger = false shouldn't reset.
    // compare against a filter that was never retriggered.
    AudioContext ctx2;
    ctx2.sample_rate = 44100.0f;
    ctx2.current_sample = 0;
    auto srcA = std::make_shared<StubNode>(1.0f, &ctx2);
    auto srcB = std::make_shared<StubNode>(1.0f, &ctx2);
    OnePoleNode a(srcA, &ctx2, 200.0f, OnePoleNode::Mode::LowPass);
    OnePoleNode b(srcB, &ctx2, 200.0f, OnePoleNode::Mode::LowPass);
    for (int i = 0; i < 50; i++) { ctx2.current_sample++; a.pull(); b.pull(); }
    a.retrigger(NoteEvent{}); // flag is false by default -> should be a no-op
    ctx2.current_sample++;
    EXPECT_FLOAT_EQ(a.pull(), b.pull());
}

TEST(Graph, OnePoleStableModulation){
    // sweep cutoff every sample with a worst-case Nyquist input.
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    OnePoleNode f(src, &ctx, 500.0f, OnePoleNode::Mode::LowPass);
 
    for (int i = 0; i < 20000; i++) {
        ctx.current_sample++;
        src->value = (i % 2 == 0) ? 1.0f : -1.0f;
        f.cutoff.set(1.0f + std::fmod((float)i * 137.0f, ctx.sample_rate * 0.6f));
        float out = f.pull();
        ASSERT_TRUE(std::isfinite(out)) << "went non-finite at sample " << i;
    }
}

TEST(Graph, OnePoleCutoffModulationWorks){
    // modulating cutoff externally should change behavior just like setting
    // it directly 
    // a low base cutoff up via a modulator should make a 2kHz tone pass through 
    // instead of being crushed.
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    auto mod = std::make_shared<StubNode>(5000.0f, &ctx);
    OnePoleNode f(src, &ctx, 200.0f, OnePoleNode::Mode::LowPass);
    f.cutoff.modulators.push_back({mod, {1.0f, {}}});
 
    float peak = 0.0f;
    for (int i = 0; i < 4410; i++) {
        ctx.current_sample++;
        src->value = std::sin(2.0f * (float)M_PI * 2000.0f * i / ctx.sample_rate);
        float out = f.pull();
        if (i > 2000) peak = std::max(peak, std::fabs(out));
    }
    EXPECT_GT(peak, 0.8f);
}

TEST(Graph, OnePoleImpulse) {
    // impulse response of a one-pole filter:
    // (1 - alpha) * alpha^n. This checks the recursion/state update itself
    // matches that shape, not just the steady-state frequency response.
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    OnePoleNode f(src, &ctx, 500.0f, OnePoleNode::Mode::LowPass);
 
    float response_time = 1.0f / (2.0f * PI * 500.0f);
    float sample_period = 1.0f / ctx.sample_rate;
    float alpha = response_time / (response_time + sample_period);
 
    for (int n = 0; n < 20; n++) {
        ctx.current_sample++;
        src->value = (n == 0) ? 1.0f : 0.0f;
        float out = f.pull();
        float expected = (1.0f - alpha) * std::pow(alpha, (float)n);
        EXPECT_NEAR(out, expected, 1e-6f) << "sample " << n;
    }
}

TEST(Graph, OnePoleAtNyquist){
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    OnePoleNode lp(src, &ctx, 500.0f, OnePoleNode::Mode::LowPass);
    OnePoleNode hp(src, &ctx, 500.0f, OnePoleNode::Mode::HighPass);
 
    float lp_tail = 0.0f, hp_tail = 0.0f;
    for (int n = 0; n < 2000; n++) {
        ctx.current_sample++;
        src->value = (n % 2 == 0) ? 1.0f : -1.0f;
        float o_lp = lp.pull();
        float o_hp = hp.pull();
        if (n > 1900) {
            lp_tail = std::max(lp_tail, std::fabs(o_lp));
            hp_tail = std::max(hp_tail, std::fabs(o_hp));
        }
    }
    EXPECT_LT(lp_tail, 0.1f);
    EXPECT_GT(hp_tail, 0.9f);
}

TEST(Graph, SVFDCLowPass){
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.6f, &ctx);
    SVFNode f(src, &ctx, 300.0f, 0.707f, SVFNode::Mode::LowPass);
 
    float out = 0.0f;
    for (int i = 0; i < 5000; i++) { ctx.current_sample++; out = f.pull(); }
    EXPECT_NEAR(out, 0.6f, 1e-3f);
}

TEST(Graph, SVFDCHighPass){
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.6f, &ctx);
    SVFNode f(src, &ctx, 300.0f, 0.707f, SVFNode::Mode::HighPass);
 
    float out = 1.0f;
    for (int i = 0; i < 5000; i++) { ctx.current_sample++; out = f.pull(); }
    EXPECT_NEAR(out, 0.0f, 1e-3f);
}

TEST(Graph, SVFLowCutoff){
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    SVFNode f(src, &ctx, 200.0f, 0.707f, SVFNode::Mode::LowPass);
 
    float peak = 0.0f;
    for (int i = 0; i < 4410; i++) {
        ctx.current_sample++;
        src->value = std::sin(2.0f * (float)M_PI * 2000.0f * i / ctx.sample_rate);
        float out = f.pull();
        if (i > 2000) peak = std::max(peak, std::fabs(out));
    }
    EXPECT_LT(peak, 0.15f);
}

TEST(Graph, SVFHighCutoff){
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    SVFNode f(src, &ctx, 15000.0f, 0.707f, SVFNode::Mode::LowPass);
 
    float peak = 0.0f;
    for (int i = 0; i < 4410; i++) {
        ctx.current_sample++;
        src->value = std::sin(2.0f * (float)M_PI * 2000.0f * i / ctx.sample_rate);
        float out = f.pull();
        if (i > 2000) peak = std::max(peak, std::fabs(out));
    }
    EXPECT_GT(peak, 0.85f);
}

TEST(Graph, SVFResetStateTrigger){
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    SVFNode f(src, &ctx, 440.0f, 5.0f, SVFNode::Mode::BandPass);
    f.reset_state_on_retrigger = true;
 
    for (int i = 0; i < 50; i++) {
        ctx.current_sample++;
        src->value = std::sin(2.0f * (float)M_PI * 440.0f * i / ctx.sample_rate);
        f.pull();
    }
    float before = f.pull(); // nonzero, filter has accumulated state
    f.retrigger(NoteEvent{});
    ctx.current_sample++;
    src->value = 0.0f;
    float after = f.pull(); // should now start from zero internal state
    EXPECT_NE(before, 0.0f);
    EXPECT_NEAR(after, 0.0f, 1e-4f);
 
    // reset_state_on_retrigger = false (default) must not reset state
    AudioContext ctx2;
    ctx2.sample_rate = 44100.0f;
    ctx2.current_sample = 0;
    auto srcA = std::make_shared<StubNode>(0.0f, &ctx2);
    auto srcB = std::make_shared<StubNode>(0.0f, &ctx2);
    SVFNode a(srcA, &ctx2, 440.0f, 5.0f, SVFNode::Mode::LowPass);
    SVFNode b(srcB, &ctx2, 440.0f, 5.0f, SVFNode::Mode::LowPass);
    for (int i = 0; i < 50; i++) {
        ctx2.current_sample++;
        srcA->value = srcB->value = std::sin(2.0f * (float)M_PI * 440.0f * i / ctx2.sample_rate);
        a.pull(); b.pull();
    }
    a.retrigger(NoteEvent{}); // flag false -> no-op
    ctx2.current_sample++;
    srcA->value = srcB->value = 0.3f;
    EXPECT_FLOAT_EQ(a.pull(), b.pull());
}

TEST(Graph, SVFStableModulation){
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    SVFNode f(src, &ctx, 1000.0f, 5.0f, SVFNode::Mode::LowPass);
 
    for (int i = 0; i < 20000; i++) {
        ctx.current_sample++;
        src->value = (i % 2 == 0) ? 1.0f : -1.0f;
        f.cutoff.set(1.0f + std::fmod((float)i * 137.0f, ctx.sample_rate * 0.6f));
        f.resonance.set(0.001f + std::fmod((float)i * 913.0f, 9000.0f));
        float out = f.pull();
        ASSERT_TRUE(std::isfinite(out)) << "went non-finite at sample " << i;
    }
}

TEST(Graph, SVFComplementaryIdentity){
    // LowPass + k*BandPass + HighPass == input
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    float fc = 800.0f, Q = 3.0f, k = 1.0f / Q;
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    SVFNode lp(src, &ctx, fc, Q, SVFNode::Mode::LowPass);
    SVFNode hp(src, &ctx, fc, Q, SVFNode::Mode::HighPass);
    SVFNode bp(src, &ctx, fc, Q, SVFNode::Mode::BandPass);
 
    uint32_t rng = 12345;
    for (int n = 0; n < 2000; n++) {
        ctx.current_sample++;
        rng = rng * 1664525u + 1013904223u;
        src->value = ((int32_t)rng / (float)INT32_MAX) * 0.8f; // deterministic noise-like input
        float o_lp = lp.pull(), o_hp = hp.pull(), o_bp = bp.pull();
        EXPECT_NEAR(o_lp + k * o_bp + o_hp, src->value, 1e-4f) << "sample " << n;
    }
}

TEST(Graph, SVFLowHighPassSumIdentity) {
    // emergent identity: LowPass + HighPass == Notch (both equal input - k*BandPass)
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    float fc = 800.0f, Q = 3.0f;
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    SVFNode lp(src, &ctx, fc, Q, SVFNode::Mode::LowPass);
    SVFNode hp(src, &ctx, fc, Q, SVFNode::Mode::HighPass);
    SVFNode notch(src, &ctx, fc, Q, SVFNode::Mode::Notch);
 
    uint32_t rng = 54321;
    for (int n = 0; n < 2000; n++) {
        ctx.current_sample++;
        rng = rng * 1664525u + 1013904223u;
        src->value = ((int32_t)rng / (float)INT32_MAX) * 0.8f;
        float o_lp = lp.pull(), o_hp = hp.pull(), o_notch = notch.pull();
        EXPECT_NEAR(o_lp + o_hp, o_notch, 1e-4f) << "sample " << n;
    }
}

TEST(Graph, SVFPeakIdentity){
    // Peak == LowPass - HighPass
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    float fc = 800.0f, Q = 3.0f;
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    SVFNode lp(src, &ctx, fc, Q, SVFNode::Mode::LowPass);
    SVFNode hp(src, &ctx, fc, Q, SVFNode::Mode::HighPass);
    SVFNode peak(src, &ctx, fc, Q, SVFNode::Mode::Peak);
 
    uint32_t rng = 99999;
    for (int n = 0; n < 2000; n++) {
        ctx.current_sample++;
        rng = rng * 1664525u + 1013904223u;
        src->value = ((int32_t)rng / (float)INT32_MAX) * 0.8f;
        float o_lp = lp.pull(), o_hp = hp.pull(), o_peak = peak.pull();
        EXPECT_NEAR(o_lp - o_hp, o_peak, 1e-4f) << "sample " << n;
    }
}

TEST(Graph, SVFAllPassIdentity){
    // AllPass == input - 2*k*BandPass
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    float fc = 800.0f, Q = 3.0f, k = 1.0f / Q;
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    SVFNode bp(src, &ctx, fc, Q, SVFNode::Mode::BandPass);
    SVFNode allp(src, &ctx, fc, Q, SVFNode::Mode::AllPass);
 
    uint32_t rng = 24680;
    for (int n = 0; n < 2000; n++) {
        ctx.current_sample++;
        rng = rng * 1664525u + 1013904223u;
        src->value = ((int32_t)rng / (float)INT32_MAX) * 0.8f;
        float o_bp = bp.pull(), o_allp = allp.pull();
        EXPECT_NEAR(src->value - 2.0f * k * o_bp, o_allp, 1e-4f) << "sample " << n;
    }
}

TEST(Graph, SVFNotchIdentity){
    // Notch's own defining formula: input - k*BandPass
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    float fc = 800.0f, Q = 3.0f, k = 1.0f / Q;
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    SVFNode bp(src, &ctx, fc, Q, SVFNode::Mode::BandPass);
    SVFNode notch(src, &ctx, fc, Q, SVFNode::Mode::Notch);
 
    uint32_t rng = 11111;
    for (int n = 0; n < 2000; n++) {
        ctx.current_sample++;
        rng = rng * 1664525u + 1013904223u;
        src->value = ((int32_t)rng / (float)INT32_MAX) * 0.8f;
        float o_bp = bp.pull(), o_notch = notch.pull();
        EXPECT_NEAR(src->value - k * o_bp, o_notch, 1e-4f) << "sample " << n;
    }
}

TEST(Graph, SVFResonanceWorks){
    // higher Q should show a measurably larger peak near the cutoff frequency
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src_lowq = std::make_shared<StubNode>(0.0f, &ctx);
    SVFNode f_lowq(src_lowq, &ctx, 1000.0f, 0.707f, SVFNode::Mode::LowPass);
    float peak_lowq = 0.0f;
    for (int i = 0; i < 4410; i++) {
        ctx.current_sample++;
        src_lowq->value = std::sin(2.0f * (float)M_PI * 1000.0f * i / ctx.sample_rate);
        float out = f_lowq.pull();
        if (i > 2000) peak_lowq = std::max(peak_lowq, std::fabs(out));
    }
 
    AudioContext ctx2;
    ctx2.sample_rate = 44100.0f;
    ctx2.current_sample = 0;
    auto src_hiq = std::make_shared<StubNode>(0.0f, &ctx2);
    SVFNode f_hiq(src_hiq, &ctx2, 1000.0f, 8.0f, SVFNode::Mode::LowPass);
    float peak_hiq = 0.0f;
    for (int i = 0; i < 4410; i++) {
        ctx2.current_sample++;
        src_hiq->value = std::sin(2.0f * (float)M_PI * 1000.0f * i / ctx2.sample_rate);
        float out = f_hiq.pull();
        if (i > 2000) peak_hiq = std::max(peak_hiq, std::fabs(out));
    }
 
    EXPECT_GT(peak_hiq, peak_lowq * 2.0f);
}

TEST(Graph, SVFCutoffModulationWorks){
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    auto mod = std::make_shared<StubNode>(15000.0f, &ctx);
    SVFNode f(src, &ctx, 200.0f, 0.707f, SVFNode::Mode::LowPass);
    f.cutoff.modulators.push_back({mod, {1.0f, {}}});
 
    float peak = 0.0f;
    for (int i = 0; i < 4410; i++) {
        ctx.current_sample++;
        src->value = std::sin(2.0f * (float)M_PI * 2000.0f * i / ctx.sample_rate);
        float out = f.pull();
        if (i > 2000) peak = std::max(peak, std::fabs(out));
    }
    EXPECT_GT(peak, 0.9f);
}

TEST(Graph, SVFResonanceModulationWorks){
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    auto mod = std::make_shared<StubNode>(7.3f, &ctx); // base 0.7 + mod 7.3 = effective Q ~8
    SVFNode f(src, &ctx, 1000.0f, 0.7f, SVFNode::Mode::LowPass);
    f.resonance.modulators.push_back({mod, {1.0f, {}}});
 
    float peak = 0.0f;
    for (int i = 0; i < 4410; i++) {
        ctx.current_sample++;
        src->value = std::sin(2.0f * (float)M_PI * 1000.0f * i / ctx.sample_rate);
        float out = f.pull();
        if (i > 2000) peak = std::max(peak, std::fabs(out));
    }
    EXPECT_GT(peak, 2.0f); // well past unity, showing the resonant boost kicked in
}

TEST(Graph, SVFImpulseAtNormalResonance){
    // at default (non-resonant) Q, the impulse response should be gone
    // before 30 cycles. the baseline the self-oscillation test below contrasts with.
    AudioContext ctx;
    ctx.sample_rate = 48000.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    SVFNode f(src, &ctx, 440.0f, 0.707f, SVFNode::Mode::BandPass);
 
    int period = (int)(ctx.sample_rate / 440.0f);
    float last = 0.0f;
    for (int n = 0; n < period * 30; n++) {
        ctx.current_sample++;
        src->value = (n == 0) ? 1.0f : 0.0f;
        last = f.pull();
    }
    EXPECT_NEAR(last, 0.0f, 1e-4f);
}

TEST(Graph, SVFImpulseAtHighResonance){
    // test self oscillation
    AudioContext ctx;
    ctx.sample_rate = 48000.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    SVFNode f(src, &ctx, 440.0f, 6000.0f, SVFNode::Mode::BandPass);
 
    int period = (int)(ctx.sample_rate / 440.0f);
    float peak_early = 0.0f, peak_late = 0.0f;
    for (int n = 0; n < period * 60; n++) {
        ctx.current_sample++;
        src->value = (n == 0) ? 1.0f : 0.0f;
        float out = f.pull();
        if (n >= period * 4 && n < period * 6) peak_early = std::max(peak_early, std::fabs(out));
        if (n >= period * 48 && n < period * 50) peak_late = std::max(peak_late, std::fabs(out));
    }
    ASSERT_GT(peak_early, 0.0f);
    EXPECT_GT(peak_late, peak_early * 0.9f);
}

TEST(Graph, SVFAtNyquist){
    AudioContext ctx;
    ctx.sample_rate = 44100.0f;
    ctx.current_sample = 0;
 
    auto src = std::make_shared<StubNode>(0.0f, &ctx);
    SVFNode lp(src, &ctx, 500.0f, 0.707f, SVFNode::Mode::LowPass);
    SVFNode hp(src, &ctx, 500.0f, 0.707f, SVFNode::Mode::HighPass);
 
    float lp_tail = 0.0f, hp_tail = 0.0f;
    for (int n = 0; n < 2000; n++) {
        ctx.current_sample++;
        src->value = (n % 2 == 0) ? 1.0f : -1.0f;
        float o_lp = lp.pull();
        float o_hp = hp.pull();
        if (n > 1900) {
            lp_tail = std::max(lp_tail, std::fabs(o_lp));
            hp_tail = std::max(hp_tail, std::fabs(o_hp));
        }
    }
    EXPECT_LT(lp_tail, 0.01f);
    EXPECT_GT(hp_tail, 0.99f);
}
