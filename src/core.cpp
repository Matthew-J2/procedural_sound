#include "core.h"
#include "oscillator.h"
#include "graph.h"
#include "event.h"
#include <memory>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>

using StereoFrame = AudioFrame<2>;

void dispatch_due_events(AudioContext* ctx, 
                         SPSCRingBuffer<ScheduledEvent>& queue,
                         std::shared_ptr<OscillatorNode>& osc, 
                         std::shared_ptr<GateNode>& gate) {
    // Looks at next scheduled event and if it exists and is due, 
    // run event and remove from queue
    ScheduledEvent ev;
    while (queue.peek(ev) && ev.trigger_sample <= ctx->current_sample) {
        queue.pop(ev);

        switch (ev.type) {
            case EventType::NoteOn:
                osc->osc->frequency = ev.frequency;
                osc->osc->amplitude = ev.amplitude;
                osc->osc->phase = 0.0f;
                gate->active = true;
                break;
            case EventType::NoteOff:
                gate->active = false;
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
        dispatch_due_events(audio_ctx, *audio_ctx->event_queue, audio_ctx->osc_node, audio_ctx->gate);
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
        std::make_unique<SineOscillator>(130.81f, 0.02f), ctx
    );
    auto square = std::make_shared<OscillatorNode>(
        std::make_unique<SquareOscillator>(164.81f, 0.02f), ctx
    );
    auto triangle = std::make_shared<OscillatorNode>(
        std::make_unique<TriangleOscillator>(196.00f, 0.02f), ctx
    );
    auto saw = std::make_shared<OscillatorNode>(
        std::make_unique<SawOscillator>(261.63f, 0.02f), ctx
    );

    auto saw_gate = std::make_shared<GateNode>(saw, ctx);
    saw_gate->active = true;

    ctx->osc_node = saw;
    ctx->gate = saw_gate;

    mixer->inputs.push_back(sine);
    mixer->inputs.push_back(square);
    mixer->inputs.push_back(triangle);
    mixer->inputs.push_back(saw_gate);

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

    ma_device device;
    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
        std::cerr << "Failed to init miniaudio device.";
        return -1;
    }
    
    // defined by miniaudio device
    audio_ctx->sample_rate = (float)device.sampleRate;

    std::cout << "sample rate: " << device.sampleRate << "\n";
    std::cout << "channels: " << device.playback.channels << "\n";
    std::cout << "format: " << device.playback.format << "\n";

    // set up encoder to describe how audio should be written
    ma_encoder_config wav_encoder_config = 
        ma_encoder_config_init(
            ma_encoding_format_wav,
            device.playback.format,
            device.playback.channels,
            device.sampleRate 
    );
    ma_encoder wav_encoder;
    
    // open encoder file for writing
    if (ma_encoder_init_file("output.wav", &wav_encoder_config, &wav_encoder) != MA_SUCCESS) {
        std::cerr << "Failed to init wav encoder.\n";
        ma_device_uninit(&device);
        return -1;
    }
    // open raw log file for writing
    std::ofstream file ("log.raw", std::ios::binary);
    if (!file) {
        std::cerr << "Warning: failed to open log.raw, no raw logs will be produced\n";
    }

    size_t sampleTime = 5;

    audio_ctx->audio_log_buffer = std::make_unique<SPSCRingBuffer<StereoFrame>>(device.sampleRate * sampleTime * 2); //FIXME: multiplying gives slack but doesn't actually solve the risk of overflow from pipewire / your api of choice acting up. this is bad and wastes tons of memory but I'm leaving it like this for now / a while because I want to do other stuff

    // push events on queue
    event_queue->push({
        .type = EventType::NoteOn,
        .trigger_sample = 0,
        .frequency = 261.63f,
        .amplitude = 0.02f
    });

    event_queue->push({
        .type = EventType::NoteOff,
        .trigger_sample = (uint64_t)(2.5 * audio_ctx->sample_rate)
    });

    StereoFrame frame;
    // start audio device - callback now active
    if (ma_device_start(&device) != MA_SUCCESS) {
        std::cerr << "Failed to start wav miniaudio device.\n";
        ma_encoder_uninit(&wav_encoder);
        ma_device_uninit(&device);
        return -1;
    }

    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    //FIXME: busy waiting will be a problem in the future
    // while callback runs, write frames to wav and raw file every 10ms
    while (std::chrono::steady_clock::now() < end) {
        while (audio_ctx->audio_log_buffer->pop(frame)) {
            file.write(reinterpret_cast<const char*>(&frame), sizeof(frame));
            ma_encoder_write_pcm_frames(
                &wav_encoder,
                frame.samples,
                1,
                nullptr
            );

        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // stop audio device - callback no longer active
    ma_device_stop(&device);

    // drain buffer
    while (audio_ctx->audio_log_buffer->pop(frame)) {
        file.write(reinterpret_cast<const char*>(&frame), sizeof(frame));
        ma_encoder_write_pcm_frames(
            &wav_encoder,
            frame.samples,
            1,
            nullptr
        );
    }
    
    // close resources
    file.close();
    ma_encoder_uninit(&wav_encoder); //TODO: RAII
    ma_device_uninit(&device);

    std::cout << "dropped samples: " << audio_ctx->audio_log_buffer->get_dropped() << "\n";
    return 0;
}
