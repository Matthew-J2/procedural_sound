#include <gtest/gtest.h>
#include <memory>
#include "core.h"
#include "graph.h"
#include "instrument.h"
 
static std::shared_ptr<AudioNode> make_test_voice_graph(AudioContext* ctx, const ADSR& envelope_shape) {
    auto source = std::make_shared<ConstantNode>(1.0f, ctx);
    auto envelope = std::make_shared<EnvelopeNode>(ctx, envelope_shape);
    auto gain = std::make_shared<GainNode>(source, ctx, 0.0f);
    gain->amplitude.modulators.push_back({envelope, {1.0f, {}}});
    return gain;
}
 
TEST(Voice, IdleBeforeFirstTrigger) {
// new voice must be idle for note_on
    AudioContext ctx;
    ctx.sample_rate = 1000.0f;
    ctx.current_sample = 0;
 
    auto head = make_test_voice_graph(&ctx, ADSR(0.01f, 0.01f, 0.5f, 0.01f));
    auto params = std::make_shared<ParamMap>();
    params->add_graph(head);
    Voice voice = make_voice(head, params);
 
    EXPECT_TRUE(voice.is_idle());
}
 
TEST(Voice, LifecycleTracksEnvelopeShape) {
    // checks ADSR shape is correct
    AudioContext ctx;
    ctx.sample_rate = 1000.0f; // chosen because round number
    ctx.current_sample = 0;
 
    auto source = std::make_shared<ConstantNode>(1.0f, &ctx);
    auto envelope = std::make_shared<EnvelopeNode>(&ctx, ADSR(0.01f, 0.01f, 0.5f, 0.01f));
    auto gain = std::make_shared<GainNode>(source, &ctx, 0.0f);
    gain->amplitude.modulators.push_back({envelope, {1.0f, {}}});
 
    auto params = std::make_shared<ParamMap>();
    params->add_graph(gain);
    Voice voice = make_voice(gain, params);
 
    voice.trigger(1, NoteEvent{.pitch = 440.0f, .velocity = 0.8f});
    EXPECT_FALSE(voice.is_idle());
 
    // attack: should climb towards peak volume
    float last = 0.0f;
    for (int i = 0; i < 10; i++) {
        ctx.current_sample++;
        float sample = gain->pull();
        EXPECT_GE(sample, last - 1e-6f);
        last = sample;
    }
    EXPECT_NEAR(last, 0.8f, 1e-3f);
 
    // decay: 10 samples, settles at peak * sustain_level = 0.8 * 0.5 = 0.4
    for (int i = 0; i < 10; i++) {
        ctx.current_sample++;
        last = gain->pull();
    }
    EXPECT_NEAR(last, 0.4f, 1e-3f);
 
    // sustain: holds steady no matter how long we wait
    for (int i = 0; i < 50; i++) {
        ctx.current_sample++;
        last = gain->pull();
    }
    EXPECT_NEAR(last, 0.4f, 1e-3f);
    EXPECT_FALSE(voice.is_idle());
 
    // release: 10 samples: not idle until it fully completes
    voice.release();
    for (int i = 0; i < 9; i++) {
        ctx.current_sample++;
        gain->pull();
        EXPECT_FALSE(voice.is_idle());
    }
    ctx.current_sample++;
    float final_sample = gain->pull();
    EXPECT_NEAR(final_sample, 0.0f, 1e-3f);
    EXPECT_TRUE(voice.is_idle());
}
 
TEST(Voice, RetriggerKeepsCurrentLevel) {
    // check on retrigger that the voice's level doesn't go back to zero
    AudioContext ctx;
    ctx.sample_rate = 1000.0f;
    ctx.current_sample = 0;
 
    auto head = make_test_voice_graph(&ctx, ADSR(0.1f, 0.0f, 1.0f, 0.1f));
    auto params = std::make_shared<ParamMap>();
    params->add_graph(head);
    Voice voice = make_voice(head, params);
 
    voice.trigger(1, NoteEvent{.pitch = 440.0f, .velocity = 1.0f});
    // partway through a slow attack
    float mid_level = 0.0f;
    for (int i = 0; i < 20; i++) {
        ctx.current_sample++;
        mid_level = head->pull();
    }
    ASSERT_GT(mid_level, 0.0f);
    ASSERT_LT(mid_level, 1.0f);
 
    // retriggering (e.g. the same voice reused for a new note) should
    // continue from the current level, not snap back to 0 first
    voice.trigger(2, NoteEvent{.pitch = 220.0f, .velocity = 1.0f});
    ctx.current_sample++;
    float just_after_retrigger = head->pull();
    EXPECT_NEAR(just_after_retrigger, mid_level, 0.05f);
}
 
TEST(Instrument, MixerPrunesIdleVoiceAndResetsNoteId) {
    
    AudioContext ctx;
    ctx.sample_rate = 1000.0f;
    ctx.current_sample = 0;
 
    auto instrument = build_instrument(&ctx, "test", 2,
        [](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            return make_test_voice_graph(ctx, ADSR(0.01f, 0.01f, 0.6f, 0.01f));
        });
 
    auto mixer = std::make_shared<MixerNode>();
    mixer->ctx = &ctx;
    register_instrument(&ctx, instrument, mixer);
 
    // simulate what dispatch_due_events does on NoteOn, without going
    // through the event queue itself
    auto& pool = ctx.instrument_voice_pools[0];
    pool[0].trigger(7, NoteEvent{.pitch = 440.0f, .velocity = 1.0f});
    ctx.active_voice_indices[0].push_back(0);
 
    EXPECT_EQ(ctx.active_voice_indices[0].size(), 1u);
 
    // advance through attack + decay into sustain
    for (int i = 0; i < 30; i++) {
        ctx.current_sample++;
        mixer->pull();
    }
    EXPECT_EQ(pool[0].note_id, 7);
    EXPECT_EQ(ctx.active_voice_indices[0].size(), 1u);
 
    pool[0].release();
 
    // release is 10 samples - must not be pruned before it actually completes
    for (int i = 0; i < 9; i++) {
        ctx.current_sample++;
        mixer->pull();
        EXPECT_EQ(ctx.active_voice_indices[0].size(), 1u);
    }
 
    // the sample that completes the release should prune it out of the
    // active list and reset note_id, so a later stray NoteOff for id 7
    // can't accidentally match this (now-unrelated) voice again
    ctx.current_sample++;
    mixer->pull();
    EXPECT_EQ(ctx.active_voice_indices[0].size(), 0u);
    EXPECT_EQ(pool[0].note_id, -1);
}
 
TEST(Instrument, SecondVoiceUnaffectedByFirstVoicesLifecycle) {
    // polyphony isolation check
    AudioContext ctx;
    ctx.sample_rate = 1000.0f;
    ctx.current_sample = 0;
 
    auto instrument = build_instrument(&ctx, "test", 2,
        [](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            return make_test_voice_graph(ctx, ADSR(0.0f, 0.0f, 1.0f, 0.01f));
        });
 
    auto mixer = std::make_shared<MixerNode>();
    mixer->ctx = &ctx;
    register_instrument(&ctx, instrument, mixer);
 
    auto& pool = ctx.instrument_voice_pools[0];
 
    pool[0].trigger(1, NoteEvent{.pitch = 440.0f, .velocity = 0.4f});
    ctx.active_voice_indices[0].push_back(0);
    pool[1].trigger(2, NoteEvent{.pitch = 220.0f, .velocity = 0.4f});
    ctx.active_voice_indices[0].push_back(1);
 
    ctx.current_sample++;
    float both = mixer->pull(); // instant attack/decay -> both already at peak
    EXPECT_NEAR(both, 0.8f, 1e-3f); // 0.4 + 0.4
 
    pool[0].release();
 
    // release voice 0 fully (10 samples); voice 1 should be untouched throughout
    for (int i = 0; i < 10; i++) {
        ctx.current_sample++;
        mixer->pull();
    }
    EXPECT_EQ(ctx.active_voice_indices[0].size(), 1u);
    EXPECT_EQ(ctx.active_voice_indices[0][0], 1); // voice 1 is the one still active
    EXPECT_EQ(pool[0].note_id, -1);
    EXPECT_EQ(pool[1].note_id, 2);
 
    ctx.current_sample++;
    float remaining = mixer->pull();
    EXPECT_NEAR(remaining, 0.4f, 1e-3f); // only voice 1 left
}

TEST(Instrument, ModulatorAmountAddressableAtRuntime) {
    AudioContext ctx;
    ctx.sample_rate = 1000.0f;
    ctx.current_sample = 0;

    
    auto lfo = std::make_shared<ConstantNode>(10.0f, &ctx);
    auto node = std::make_shared<ConstantNode>(0.0f, &ctx);

    auto gain = std::make_shared<GainNode>(node, &ctx, 1.0f);
    gain->amplitude.modulators.push_back({lfo, {0.5f, {}}}); // amount starts at 0.5
 
    auto params = std::make_shared<ParamMap>();
    params->add_graph(gain);


    int mod_id = params->id_for("gain_mod0");
 
    // base(1.0) + 0.5 * 10.0 = 6.0
    EXPECT_NEAR(gain->amplitude.value(), 6.0f, 1e-5f);


    // turn the modulation off entirely by driving its amount to 0, exactly
    // as a runtime ParamChange event would via param_id()/set()
    params->set(mod_id, 0.0f);
    EXPECT_NEAR(gain->amplitude.value(), 1.0f, 1e-5f); // base only, modulator contributes nothing
 
    // turn it back on at a different amount
    params->set(mod_id, 2.0f);
    EXPECT_NEAR(gain->amplitude.value(), 21.0f, 1e-5f); // 1.0 + 2.0*10.0
}


TEST(Dispatch, ParamChangeTargetsSingleVoiceByNoteId) {
    AudioContext ctx;
    ctx.sample_rate = 1000.0f;
    ctx.current_sample = 0;
 
    auto instrument = build_instrument(&ctx, "test", 2,
        [](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            return std::make_shared<GainNode>(
                std::make_shared<ConstantNode>(1.0f, ctx), ctx, 0.5f);
        });
 
    auto mixer = std::make_shared<MixerNode>();
    mixer->ctx = &ctx;
    register_instrument(&ctx, instrument, mixer);
 
    SPSCRingBuffer<ScheduledEvent> queue(16);
    ctx.event_queue = &queue;
 
    int gain_id = param_id(&ctx, 0, "gain");
 
    // trigger two notes into two different voices, both due immediately
    queue.push({
        .type = EventType::NoteOn, .trigger_sample = 0, .instrument_index = 0,
        .note_id = 1, .note = NoteEvent{.pitch = 440.0f, .velocity = 1.0f},
        .param_id = 0, .value = 0.0f
    });
    queue.push({
        .type = EventType::NoteOn, .trigger_sample = 0, .instrument_index = 0,
        .note_id = 2, .note = NoteEvent{.pitch = 440.0f, .velocity = 1.0f},
        .param_id = 0, .value = 0.0f
    });
    dispatch_due_events(&ctx, queue);
 
    auto& pool = ctx.instrument_voice_pools[0];
    ASSERT_EQ(pool[0].note_id, 1);
    ASSERT_EQ(pool[1].note_id, 2);
 
    auto gain0 = std::dynamic_pointer_cast<GainNode>(ctx.instrument_voice_nodes[0][0]);
    auto gain1 = std::dynamic_pointer_cast<GainNode>(ctx.instrument_voice_nodes[0][1]);
    ASSERT_TRUE(gain0 && gain1);
 
    // targeted change (note_id = 2): only that voice should change
    queue.push({
        .type = EventType::ParamChange, .trigger_sample = 0, .instrument_index = 0,
        .note_id = 2, .note = {}, .param_id = gain_id, .value = 0.9f
    });
    dispatch_due_events(&ctx, queue);
 
    EXPECT_NEAR(gain0->amplitude.base, 0.5f, 1e-6f); // untouched
    EXPECT_NEAR(gain1->amplitude.base, 0.9f, 1e-6f); // changed
 
    // broadcast change (note_id = -1, the existing behavior): both change
    queue.push({
        .type = EventType::ParamChange, .trigger_sample = 0, .instrument_index = 0,
        .note_id = -1, .note = {}, .param_id = gain_id, .value = 0.2f
    });
    dispatch_due_events(&ctx, queue);
 
    EXPECT_NEAR(gain0->amplitude.base, 0.2f, 1e-6f);
    EXPECT_NEAR(gain1->amplitude.base, 0.2f, 1e-6f);
}

TEST(ParamMap, UniqueNamesForNodes) {
    AudioContext ctx;
    ctx.sample_rate = 1000.0f;
    ctx.current_sample = 0;
 
    auto modulator = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(0.0f), &ctx, 300.0f, 1.41f);
    auto carrier = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(0.0f), &ctx, 0.2f);
    carrier->frequency.modulators.push_back({modulator, {1.0f, {}}});
 
    ParamMap params;
    params.add_graph(carrier);
 
    int carrier_freq_id = params.id_for("frequency");
    EXPECT_EQ(params.entries[carrier_freq_id].node.get(), carrier.get());
 
    // the modulator's own "frequency" collides with the carrier's and must fall
    // back to a unique name instead of disappearing from the map entirely
    int modulator_freq_id = -1;
    ASSERT_NO_THROW(modulator_freq_id = params.id_for("frequency_2"))
        << "modulator's 'frequency' parameter collided with the carrier's and was "
           "never registered under a fallback name - it is now unreachable by name "
           "(e.g. this is why the bell instrument's FM modulator can't be addressed "
           "by param name today)";
 
    EXPECT_NE(modulator_freq_id, carrier_freq_id);
    EXPECT_EQ(params.entries[modulator_freq_id].node.get(), modulator.get());
}
