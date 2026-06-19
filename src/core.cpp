#include "core.h"
#include "oscillator.h"
#include "graph.h"
#include <memory>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>

using StereoFrame = AudioFrame<2>;
static std::unique_ptr<SPSCRingBuffer<StereoFrame>> audio_log_buffer;

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

    mixer->inputs.push_back(sine);
    mixer->inputs.push_back(square);
    mixer->inputs.push_back(triangle);
    mixer->inputs.push_back(saw);
    audio_ctx->output_node = mixer;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2; //TODO: support 5.1 7.1 surround sound one day
    config.sampleRate        = 0;
    config.dataCallback      = data_callback;
    config.pUserData         = audio_ctx.get();

    ma_device device;
    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
        return -1;  // Failed to initialize the device.
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
    
    ma_encoder_init_file(
        "output.wav",
        &wav_encoder_config,
        &wav_encoder
    );

    size_t sampleTime = 5;

    audio_log_buffer = std::make_unique<SPSCRingBuffer<StereoFrame>>(device.sampleRate * sampleTime * 2); //FIXME: multiplying gives slack but doesn't actually solve the risk of overflow from pipewire / your api of choice acting up. this is bad and wastes tons of memory but I'm leaving it like this for now / a while because I want to do other stuff

    ma_device_start(&device);

    std::ofstream file ("log.raw", std::ios::binary);

    StereoFrame frame;

    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);

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
    //TODO: binary stream instead of this giant text file of floats
    return 0;
}
