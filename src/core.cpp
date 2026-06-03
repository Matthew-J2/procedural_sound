#include "core.h"
#include <memory>
#include <math.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>

static std::unique_ptr<test_audio_data> audio_data;
static std::unique_ptr<SPSCRingBuffer<float>> audio_log_buffer;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{

    // get oscillator state
    test_audio_data* data = (test_audio_data*)pDevice->pUserData;

    // access output buffer and device parameters
    float* out = (float*)pOutput;
    float sampleRate = (float)pDevice->sampleRate;
    ma_uint32 channels = pDevice->playback.channels;

    // fill output buffer with sine wave samples 
    for (ma_uint32 i = 0; i < frameCount; i++)
    {
        float sample = sinf(data->phase) * data->volume;
        data->phase += 2.0f * M_PI * data->frequency / sampleRate;


        if (data->phase > 2.0f * M_PI)
            data->phase -= 2.0f * M_PI;

        // write to all speaker channels
        for (size_t c = 0; c < channels; c++){
            float v = sample;
            out[i * channels + c] = v;
 
            // write to log
            audio_log_buffer->push(v);
        }
    }
}

int config_device()
{
    audio_data = std::make_unique<test_audio_data>(0.0f, 200.0f, 0.02f);
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;   //TODO: change code to either use f32 not float or use the OS default and do some really fun templating 
    config.playback.channels = 0; //TODO: get wav to work properly in stereo
    config.sampleRate        = 0;
    config.dataCallback      = data_callback;
    config.pUserData         = audio_data.get();

    ma_device device;
    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
        return -1;  // Failed to initialize the device.
    }

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

    audio_log_buffer = std::make_unique<SPSCRingBuffer<float>>(device.sampleRate * sampleTime * 2); //FIXME: multiplying gives slack but doesn't actually solve the risk of overflow from pipewire / your api of choice acting up. this is bad and wastes tons of memory but I'm leaving it like this for now / a while because I want to do other stuff

    ma_device_start(&device);

    std::ofstream file ("log.txt");

    float sample;

    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    std::vector<float> write_buffer;
    write_buffer.reserve(4096);

    //FIXME: this is probably slow even for the main thread
    //FIXME: this also should be fixed upstream by exposing a pop_frame() function or something
    // in the buffer
    while (std::chrono::steady_clock::now() < end) {
        while (audio_log_buffer->pop(sample)) {
            file << sample << "\n";

            write_buffer.push_back(sample);

            size_t frames = write_buffer.size() / device.playback.channels;
            size_t valid_samples = frames * device.playback.channels;

            if (frames > 0)
            {
                ma_encoder_write_pcm_frames(
                    &wav_encoder,
                    write_buffer.data(),
                    frames,
                    nullptr
                );

                write_buffer.erase(
                    write_buffer.begin(),
                    write_buffer.begin() + valid_samples
                );
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ma_device_stop(&device);

    // drain buffer
    while (audio_log_buffer->pop(sample)) {
        file << sample << "\n";

        size_t frames = write_buffer.size() / device.playback.channels;

        if (frames > 0)
        {
            ma_encoder_write_pcm_frames(
                &wav_encoder,
                write_buffer.data(),
                frames,
                nullptr
            );
        }
    }
    write_buffer.clear();
    file.close();
    ma_encoder_uninit(&wav_encoder);

    ma_device_uninit(&device);

    std::cout << "dropped samples: " << audio_log_buffer->get_dropped() << "\n";
//TODO: wav as well and also binary stream instead of this giant text file of floats
    return 0;
}
