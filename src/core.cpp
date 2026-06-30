#include "core.h"
#include "oscillator.h"
#include "graph.h"
#include "event.h"
#include "midi.h"
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
                for (auto& voice : pool) {
                    if (voice.is_idle()) {
                        voice.trigger(ev.note_id, ev.frequency, ev.amplitude);
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
        }
    }
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
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
}

void build_patch(AudioContext* ctx)
{
    auto mixer = std::make_shared<MixerNode>();
    mixer->ctx = ctx;

    // Create oscillators, put in mixer, put mixer in context as output node.
    auto sine = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(note_frequency("C3"), 0.02f), ctx
    );
    auto square = std::make_shared<OscillatorNode>(
        std::make_unique<SquareOscillator>(note_frequency("E3"), 0.02f), ctx
    );
    auto triangle = std::make_shared<OscillatorNode>(
        std::make_unique<TriangleOscillator>(note_frequency("G3"), 1.0f), ctx
    );
    auto saw = std::make_shared<OscillatorNode>(
        std::make_unique<SawOscillator>(note_frequency("C4"), 0.02f), ctx
    );

    auto saw_gate = std::make_shared<GateNode>(saw, ctx);
    saw_gate->active = true;

    mixer->inputs.push_back(sine);
    mixer->inputs.push_back(square);
    mixer->inputs.push_back(saw_gate);

    ctx->instrument_voice_pools.push_back({});
    int MaxVoices = 32;
    ctx->instrument_voice_pools[0].reserve(MaxVoices);
    ctx->instrument_index_names["pad"] = 0;
    
    for (int i = 0; i < MaxVoices; i++) {
        auto osc = std::make_shared<OscillatorNode>(
            std::make_unique<TriangleOscillator>(0.0f, 1.0f), ctx  // frequency set per-note via trigger()
        );
        auto envelope = std::make_shared<EnvelopeNode>(
            osc, ctx, ADSR(0.5f, 0.35f, 0.5f, 0.5f)
        );

        mixer->inputs.push_back(envelope);
        ctx->instrument_voice_pools[0].push_back(Voice{osc, envelope});
    }
    
    ctx->instrument_voice_pools.push_back({});
    constexpr int MaxPluckVoices = 16;
    ctx->instrument_voice_pools[1].reserve(MaxPluckVoices);
    ctx->instrument_index_names["pluck"] = 1;

    for (int i = 0; i < MaxPluckVoices; i++) {
        auto osc = std::make_shared<OscillatorNode>(
            std::make_unique<SawOscillator>(0.0f, 1.0f), ctx
        );
        auto envelope = std::make_shared<EnvelopeNode>(
            osc, ctx, ADSR(0.002f, 0.35f, 0.0f, 0.35f) 
        );
        mixer->inputs.push_back(envelope);
        ctx->instrument_voice_pools[1].push_back(Voice{osc, envelope});
    }

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

    // push events on queue
    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = 0,
        .instrument_index = 0,
        .note_id = 1,
        .frequency = note_frequency("C4"), // C4
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(0.5 * audio_ctx->sample_rate),
        .instrument_index = 0,
        .note_id = 2,
        .frequency = note_frequency("E4"), // E4
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(1.0 * audio_ctx->sample_rate),
        .instrument_index = 0,
        .note_id = 3,
        .frequency = note_frequency("G4"), // G4
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(2.0 * audio_ctx->sample_rate),
        .instrument_index = 0,
        .note_id = 1 // C4 release
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(2.5 * audio_ctx->sample_rate),
        .instrument_index = 0,
        .note_id = 2 // E4 release
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(3.0 * audio_ctx->sample_rate),
        .instrument_index = 0,
        .note_id = 3 // G4 release
    });

    // let's do half a 24-TET scale for funsies

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(4.0 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 4,
        .frequency = midi_to_frequency(60),
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(4.5 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 4
    });

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(5.0 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 5,
        .frequency = midi_to_frequency(60.5),
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(5.5 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 5
    });

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(6.0 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 6,
        .frequency = midi_to_frequency(61),
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(6.5 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 6
    });

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(7.0 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 7,
        .frequency = midi_to_frequency(61.5),
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(7.5 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 7
    });

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(8.0 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 8,
        .frequency = midi_to_frequency(62),
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(8.5 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 8
    });

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(9.0 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 9,
        .frequency = midi_to_frequency(62.5),
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(9.5 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 9
    });

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(10.0 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 10,
        .frequency = midi_to_frequency(63),
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(10.5 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 10
    });

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(11.0 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 11,
        .frequency = midi_to_frequency(63.5),
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(11.5 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 11
    });

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(12.0 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 12,
        .frequency = midi_to_frequency(64),
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(12.5 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 12
    });

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(13.0 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 13,
        .frequency = midi_to_frequency(64.5),
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(13.5 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 13
    });

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(14.0 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 14,
        .frequency = midi_to_frequency(65),
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(14.5 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 14
    });

    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = (uint64_t)(15.0 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 15,
        .frequency = midi_to_frequency(65.5),
        .amplitude = 0.3f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(15.5 * audio_ctx->sample_rate),
        .instrument_index = 1,
        .note_id = 15
    });

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

    std::cout << "dropped samples: " << audio_ctx->audio_log_buffer->get_dropped() << "\n";
    return 0;
}
