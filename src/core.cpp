#include "core.h"
#include "oscillator.h"
#include "graph.h"
#include "event.h"
#include "midi.h"
#include "instrument.h"
#include <memory>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>

using StereoFrame = AudioFrame<2>;

void dispatch_due_events(AudioContext* ctx, SPSCRingBuffer<ScheduledEvent>& queue) {
    ScheduledEvent ev;
    while (queue.peek(ev) && ev.trigger_sample <= ctx->current_sample) {
        queue.pop(ev);
        auto& pool = ctx->instrument_voice_pools[ev.instrument_index];

        switch (ev.type) {
            case EventType::NoteOn:
                for (size_t i = 0; i < pool.size(); i++) {
                    if (pool[i].is_idle()) {
                        pool[i].trigger(ev.note_id, ev.frequency, ev.amplitude);
                        ctx->active_voice_indices[ev.instrument_index].push_back(static_cast<int>(i));
                        break;
                    }
                }
                break;
            case EventType::NoteOff:
                for (auto& voice : pool) {
                    if (voice.note_id == ev.note_id) {
                        voice.release();
                        break;
                    }
                }
                break;
            case EventType::ParamChange:
                for (auto& voice : pool)
                    voice.set_param(ev.param_id, ev.value);
                break;
        }
    }
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    auto t0 = std::chrono::steady_clock::now();
    // get audio context (oscillators right now)
    AudioContext* audio_ctx = (AudioContext*)pDevice->pUserData;

    // access output buffer and device parameters
    float* out = (float*)pOutput;
    ma_uint32 channels = pDevice->playback.channels;

    // fill output buffer with samples 
    for (ma_uint32 i = 0; i < frameCount; i++)
    {
        audio_ctx->current_sample++;
        dispatch_due_events(audio_ctx, *audio_ctx->event_queue);
        float sample = audio_ctx->output_node->pull();

        StereoFrame frame;
        // write to all speaker channels
        for (size_t c = 0; c < channels; c++){
            frame.samples[c] = sample;
            out[i * channels + c] = sample;
        }
        audio_ctx->audio_log_buffer->push(frame);
    }
    auto t1 = std::chrono::steady_clock::now();

    double callback_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    double budget_us = (double)frameCount / audio_ctx->sample_rate * 1'000'000.0;

    if (callback_us > budget_us * 0.8) // warn before you actually miss it
        audio_ctx->timing_log->push({audio_ctx->current_sample, callback_us, budget_us});
}

void build_patch(AudioContext* ctx)
{
    auto mixer = std::make_shared<MixerNode>();
    mixer->ctx = ctx;

    // Create oscillators, put in mixer, put mixer in context as output node.
    auto sine = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(note_frequency("C3")), ctx, 0.02f
    );
    auto square = std::make_shared<OscillatorNode>(
        std::make_unique<SquareOscillator>(note_frequency("E3")), ctx, 0.02f
    );
    auto triangle = std::make_shared<OscillatorNode>(
        std::make_unique<TriangleOscillator>(note_frequency("G3")), ctx, 1.0f
    );
    auto saw = std::make_shared<OscillatorNode>(
        std::make_unique<SawOscillator>(note_frequency("C4")), ctx, 0.02f
    );

    auto saw_gate = std::make_shared<GateNode>(saw, ctx);
    saw_gate->active = true;

    auto lfo = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(2.0f), ctx, 1.0f
    );

    auto lfo_test = std::make_shared<OscillatorNode>(
        std::make_unique<SquareOscillator>(note_frequency("C2")),
        ctx,
        0.1f
    );

    lfo_test->frequency.modulators.push_back({
        .source = lfo,
        .amount = 20.0f
    });
    mixer->inputs.push_back(lfo_test);

    // mixer->inputs.push_back(sine);
    // mixer->inputs.push_back(square);
    // mixer->inputs.push_back(saw_gate);

    // gate recipe
    // std::vector<NodeFactory> gated = {
    //     [](std::shared_ptr<AudioNode> input, AudioContext* ctx) -> std::shared_ptr<AudioNode> {
    //         auto gate = std::make_shared<GateNode>(input, ctx);
    //         gate->active = true; // on by default
    //         return gate;
    //     }
    // };
    //
    // // take envelope recipe and glue it to gated
    // auto gate_after_test = [&](NodeFactory envelope_factory) {
    //     std::vector<NodeFactory> chain = { envelope_factory };
    //     chain.insert(chain.end(), gated.begin(), gated.end());
    //     return chain;
    // };
    //
    // register_instrument(ctx, build_instrument(
    //     ctx, "pad", 32,
    //     [](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
    //         return std::make_shared<OscillatorNode>(std::make_unique<TriangleOscillator>(0.0f), ctx, 1.0f);
    //     },
    //     gate_after_test(make_envelope_factory(ADSR(0.5f, 0.35f, 0.5f, 0.5f)))
    // ), mixer);
    //
    // register_instrument(ctx, build_instrument(
    //     ctx, "pluck", 16,
    //     [](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
    //         return std::make_shared<OscillatorNode>(std::make_unique<SawOscillator>(0.0f), ctx, 1.0f);
    //     },
    //     gate_after_test(make_envelope_factory(ADSR(0.002f, 0.35f, 0.0f, 0.35f)))
    // ), mixer);             
    //
    // register_instrument(ctx, build_instrument(
    //     ctx, "bell", 8,
    //     [](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
    //         return std::make_shared<FMNode>(
    //             std::make_unique<SineOscillator>(0.0f), ctx,
    //             1.41f,   // ratio
    //             300.0f   // depth in Hz
    //         );
    //     },
    // gate_after_test(make_envelope_factory(ADSR(0.005f, 0.3f, 0.3f, 0.6f)))
    // ), mixer);
    ctx->output_node = mixer;
}

std::unique_ptr<SPSCRingBuffer<ScheduledEvent>> init_event_queue(AudioContext* ctx, size_t capacity = 64)
{
    // allocate and connect queue
    auto queue = std::make_unique<SPSCRingBuffer<ScheduledEvent>>(capacity);
    ctx->event_queue = queue.get();
    return queue;
}

int config_device()
{
    // create engine state
    auto audio_ctx = std::make_unique<AudioContext>();
    audio_ctx->sample_rate = 0.00f;
    
    // build patches and event queue
    build_patch(audio_ctx.get());
    auto event_queue = init_event_queue(audio_ctx.get());

    // set up miniaudio device to call data callback for audio samples when init
    // and started
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2; //TODO: support 5.1 7.1 surround sound one day
    config.sampleRate        = 0;
    config.dataCallback      = data_callback;
    config.pUserData         = audio_ctx.get();

    auto device = RAIIDevice(config);
    
    // defined by miniaudio device
    audio_ctx->sample_rate = (float)device->sampleRate;

    std::cout << "sample rate: " << device->sampleRate << "\n";
    std::cout << "channels: " << device->playback.channels << "\n";
    std::cout << "format: " << device->playback.format << "\n";

    // set up encoder to describe how audio should be written
    ma_encoder_config wav_encoder_config = 
        ma_encoder_config_init(
            ma_encoding_format_wav,
            device->playback.format,
            device->playback.channels,
            device->sampleRate 
    );
    
    // open encoder file for writing
    auto wav_encoder = RAIIEncoder("output.wav", wav_encoder_config) ;
    // open raw log file for writing
    std::ofstream file ("log.raw", std::ios::binary);
    if (!file) {
        std::cerr << "Warning: failed to open log.raw, no raw logs will be produced\n";
    }

    size_t sampleTime = 20;

    audio_ctx->audio_log_buffer = std::make_unique<SPSCRingBuffer<StereoFrame>>(device->sampleRate * sampleTime * 2); //FIXME: multiplying gives slack but doesn't actually solve the risk of overflow from pipewire / your api of choice acting up. this is bad and wastes tons of memory but I'm leaving it like this for now / a while because I want to do other stuff

    audio_ctx->timing_log = std::make_unique<SPSCRingBuffer<CallbackLog>>(1024);

    // push events on queue
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = 0,
    //     .instrument_index = 0,
    //     .note_id = 1,
    //     .frequency = note_frequency("C4"), // C4
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(0.5 * audio_ctx->sample_rate),
    //     .instrument_index = 0,
    //     .note_id = 2,
    //     .frequency = note_frequency("E4"), // E4
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(1.0 * audio_ctx->sample_rate),
    //     .instrument_index = 0,
    //     .note_id = 3,
    //     .frequency = note_frequency("G4"), // G4
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(2.0 * audio_ctx->sample_rate),
    //     .instrument_index = 0,
    //     .note_id = 1 // C4 release
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(2.5 * audio_ctx->sample_rate),
    //     .instrument_index = 0,
    //     .note_id = 2 // E4 release
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(3.0 * audio_ctx->sample_rate),
    //     .instrument_index = 0,
    //     .note_id = 3 // G4 release
    // });
    //
    // // let's do half a 24-TET scale for funsies
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(4.0 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 4,
    //     .frequency = midi_to_frequency(60),
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(4.5 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 4
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(5.0 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 5,
    //     .frequency = midi_to_frequency(60.5),
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(5.5 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 5
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(6.0 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 6,
    //     .frequency = midi_to_frequency(61),
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(6.5 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 6
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(7.0 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 7,
    //     .frequency = midi_to_frequency(61.5),
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(7.5 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 7
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(8.0 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 8,
    //     .frequency = midi_to_frequency(62),
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(8.5 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 8
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(9.0 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 9,
    //     .frequency = midi_to_frequency(62.5),
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(9.5 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 9
    // });
    //
    //
    // // halfway through the scale: set events to change from pluck to a pad
    // event_queue->push({
    //     .type = EventType::ParamChange,
    //     .trigger_sample = (uint64_t)(9.75 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .param_id = param_id(audio_ctx.get(), 1, "attack"),
    //     .value = 0.5f
    // });
    //
    // event_queue->push({
    //     .type = EventType::ParamChange,
    //     .trigger_sample = (uint64_t)(9.75 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .param_id = param_id(audio_ctx.get(), 1, "decay"),
    //     .value = 0.35f
    // });
    //
    // event_queue->push({
    //     .type = EventType::ParamChange,
    //     .trigger_sample = (uint64_t)(9.75 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .param_id = param_id(audio_ctx.get(), 1, "sustain"),
    //     .value = 0.5f
    // });
    //
    // event_queue->push({
    //     .type = EventType::ParamChange,
    //     .trigger_sample = (uint64_t)(9.75 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //    .param_id = param_id(audio_ctx.get(), 1, "release"),
    //     .value = 0.5f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(10.0 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 10,
    //     .frequency = midi_to_frequency(63),
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(10.5 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 10
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(11.0 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 11,
    //     .frequency = midi_to_frequency(63.5),
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(11.5 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 11
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(12.0 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 12,
    //     .frequency = midi_to_frequency(64),
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(12.5 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 12
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(13.0 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 13,
    //     .frequency = midi_to_frequency(64.5),
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(13.5 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 13
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(14.0 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 14,
    //     .frequency = midi_to_frequency(65),
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(14.5 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 14
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(15.0 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 15,
    //     .frequency = midi_to_frequency(65.5),
    //     .amplitude = 0.3f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(15.5 * audio_ctx->sample_rate),
    //     .instrument_index = 1,
    //     .note_id = 15
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(16.0 * audio_ctx->sample_rate),
    //     .instrument_index = 2,
    //     .note_id = 16,
    //     .frequency = midi_to_frequency(66.0),
    //     .amplitude = 0.6f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(16.5 * audio_ctx->sample_rate),
    //     .instrument_index = 2,
    //     .note_id = 16
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOn,
    //     .trigger_sample = (uint64_t)(17.0 * audio_ctx->sample_rate),
    //     .instrument_index = 2,
    //     .note_id = 17,
    //     .frequency = midi_to_frequency(66.5),
    //     .amplitude = 0.6f
    // });
    //
    // event_queue->push({
    //     .type = EventType::NoteOff,
    //     .trigger_sample = (uint64_t)(17.5 * audio_ctx->sample_rate),
    //     .instrument_index = 2,
    //     .note_id = 17
    // });

        StereoFrame frame;
        // start audio device - callback now active
        if (ma_device_start(device.get()) != MA_SUCCESS) {
            std::cerr << "Failed to start miniaudio playback device.\n";
            return -1;
        }

    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(sampleTime);
    //FIXME: busy waiting will be a problem in the future
    // while callback runs, write frames to wav and raw file every 10ms
    while (std::chrono::steady_clock::now() < end) {
        while (audio_ctx->audio_log_buffer->pop(frame)) {
            file.write(reinterpret_cast<const char*>(&frame), sizeof(frame));
            ma_encoder_write_pcm_frames(
                wav_encoder.get(),
                frame.samples,
                1,
                nullptr
            );

        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // stop audio device - callback no longer active
    ma_device_stop(device.get());

    // drain buffer
    while (audio_ctx->audio_log_buffer->pop(frame)) {
        file.write(reinterpret_cast<const char*>(&frame), sizeof(frame));
        ma_encoder_write_pcm_frames(
            wav_encoder.get(),
            frame.samples,
            1,
            nullptr
        );
    }

    CallbackLog t;
    while (audio_ctx->timing_log->pop(t)) {
        std::cerr << "callback overrun at sample " << t.sample_index
                  << ": " << t.callback_us << "us / " << t.budget_us << "us budget\n";
    }

    std::cout << "dropped samples: " << audio_ctx->audio_log_buffer->get_dropped() << "\n";
    return 0;
}
