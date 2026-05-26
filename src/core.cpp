#include "core.h"
#include <memory>
#include <math.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <fstream>

static std::unique_ptr<test_audio_data> audio_data;
static std::vector<float> recorded_samples;
static std::atomic<size_t> write_index = 0;

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
        for (ma_uint32 c = 0; c < channels; c++)
            out[i * channels + c] = sample;
        
        // write to log
        if (write_index < recorded_samples.size()) //FIXME: this just drops samples when it's full. no bueno. it's also just fragile in general
        {
            recorded_samples[write_index++] = sample;
        }
    }
}

int config_device()
{
    audio_data = std::make_unique<test_audio_data>(0.0f, 200.0f, 0.02f);
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;   //TODO: change code to either use f32 not float or use the OS default and do some really fun templating 
    config.playback.channels = 0;
    config.sampleRate        = 0;
    config.dataCallback      = data_callback;
    config.pUserData         = audio_data.get();

    ma_device device;
    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
        return -1;  // Failed to initialize the device.
    }

    std::cout << "sample rate:" << device.sampleRate << "\n";
    std::cout << "channels" << device.playback.channels << "\n";
    std::cout << "format:" << device.playback.format << "\n";
    
    size_t sampleTime = 5;

    write_index = 0;
    recorded_samples.clear();
    recorded_samples.resize(device.sampleRate * sampleTime); //FIXME: the writes depend on pipewire this only like roughly tracks it

    ma_device_start(&device);

    // while (true) {
    //     std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // }


    std::this_thread::sleep_for(std::chrono::milliseconds(5000));

    ma_device_uninit(&device);


    std::ofstream file ("log.txt");

    for (size_t i = 0; i < write_index; i++){
        file << recorded_samples[i] << "\n";
    }
//TODO: wav as well
    return 0;
}
