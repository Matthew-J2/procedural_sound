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
static std::unique_ptr<SPSCRingBuffer<StereoFrame>> audio_log_buffer;

void dispatch_due_events(AudioContext* ctx, SPSCRingBuffer<ScheduledEvent>& queue,
                          std::shared_ptr<OscillatorNode>& osc, std::shared_ptr<GateNode>& gate)
{
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
        audio_log_buffer->push(frame);
    }
}

int config_device()
{
    auto audio_ctx = std::make_unique<AudioContext>();
    audio_ctx->sample_rate = 0.00f;
    
    auto mixer = std::make_shared<MixerNode>();
    mixer->ctx = audio_ctx.get();

    auto sine = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(130.81f, 0.02f), audio_ctx.get()
    );

    auto square = std::make_shared<OscillatorNode>(
        std::make_unique<SquareOscillator>(164.81f, 0.02f), audio_ctx.get()
    );

    auto triangle = std::make_shared<OscillatorNode>(
        std::make_unique<TriangleOscillator>(196.00f, 0.02f), audio_ctx.get()
    );

    auto saw = std::make_shared<OscillatorNode>(
        std::make_unique<SawOscillator>(261.63f, 0.02f), audio_ctx.get()
    );

    auto saw_gate = std::make_shared<GateNode>(
        saw, audio_ctx.get()
    );

    saw_gate->active = true;

    auto event_queue = std::make_unique<SPSCRingBuffer<ScheduledEvent>>(64);
    //TODO: put this in audio context
    audio_ctx->osc_node = saw;
    audio_ctx->gate = saw_gate;
    audio_ctx->event_queue = event_queue.get();

    mixer->inputs.push_back(sine);
    mixer->inputs.push_back(square);
    mixer->inputs.push_back(triangle);
    mixer->inputs.push_back(saw_gate);
    audio_ctx->output_node = mixer;



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

    audio_ctx->sample_rate = (float)device.sampleRate;

    std::cout << "sample rate: " << device.sampleRate << "\n";
    std::cout << "channels: " << device.playback.channels << "\n";
    std::cout << "format: " << device.playback.format << "\n";

    ma_encoder wav_encoder;

    ma_encoder_config wav_encoder_config = 
        ma_encoder_config_init(
            ma_encoding_format_wav,
            device.playback.format,
            device.playback.channels,
            device.sampleRate 
    );
    
    if (ma_encoder_init_file("output.wav", &wav_encoder_config, &wav_encoder) != MA_SUCCESS) {
        std::cerr << "Failed to init wav encoder.\n";
        ma_device_uninit(&device);
        return -1;
    }

    size_t sampleTime = 5;

    audio_log_buffer = std::make_unique<SPSCRingBuffer<StereoFrame>>(device.sampleRate * sampleTime * 2); //FIXME: multiplying gives slack but doesn't actually solve the risk of overflow from pipewire / your api of choice acting up. this is bad and wastes tons of memory but I'm leaving it like this for now / a while because I want to do other stuff

    // start audio device
    if (ma_device_start(&device) != MA_SUCCESS) {
        std::cerr << "Failed to start wav miniaudio device.\n";
        ma_encoder_uninit(&wav_encoder);
        ma_device_uninit(&device);
        return -1;
    }

    std::ofstream file ("log.raw", std::ios::binary);

    if (!file) {
        std::cerr << "Warning: failed to open log.raw, no raw logs will be produced\n";
    }

    StereoFrame frame;

    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);

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

    //FIXME: busy waiting will be a problem in the future
    while (std::chrono::steady_clock::now() < end) {
        while (audio_log_buffer->pop(frame)) {
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

    // stop audio device
    ma_device_stop(&device);

    // drain buffer
    while (audio_log_buffer->pop(frame)) {
        file.write(reinterpret_cast<const char*>(&frame), sizeof(frame));
        ma_encoder_write_pcm_frames(
            &wav_encoder,
            frame.samples,
            1,
            nullptr
        );
    }
    file.close();
    ma_encoder_uninit(&wav_encoder); //TODO: RAII
    ma_device_uninit(&device);

    std::cout << "dropped samples: " << audio_log_buffer->get_dropped() << "\n";
    return 0;
}
