#include "core.h"
#include <memory>
#include <math.h>
#include <thread>
#include <chrono>

static std::unique_ptr<test_audio_data> audio_data;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    // In playback mode copy data to pOutput. In capture mode read data from pInput. In full-duplex mode, both
    // pOutput and pInput will be valid and you can move data from pInput into pOutput. Never process more than
    // frameCount frames.
    test_audio_data * data = (test_audio_data*)pDevice->pUserData;

        float* out = (float*)pOutput;
        float sampleRate = (float)pDevice->sampleRate;
        ma_uint32 channels = pDevice->playback.channels;

    for (ma_uint32 i = 0; i < frameCount; i++)
    {
        float sample = sinf(data->phase) * data->volume;
        data->phase += 2.0f * M_PI * data->frequency / sampleRate;


        if (data->phase > 2.0f * M_PI)
            data->phase -= 2.0f * M_PI;

       for (ma_uint32 c = 0; c < channels; c++)
            out[i * channels + c] = sample;
    }
}

int config_device()
{
    audio_data = std::make_unique<test_audio_data>(0.0f, 200.0f, 0.02f);
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;   // Set to ma_format_unknown to use the device's native format.
    config.playback.channels = 0;               // Set to 0 to use the device's native channel count.
    config.sampleRate        = 0;           // Set to 0 to use the device's native sample rate.
    config.dataCallback      = data_callback;   // This function will be called when miniaudio needs more data.
    config.pUserData         = audio_data.get();   // Can be accessed from the device object (device.pUserData).

    ma_device device;
    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
        return -1;  // Failed to initialize the device.
    }

    ma_device_start(&device);     // The device is sleeping by default so you'll need to start it manually.

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ma_device_uninit(&device);
    return 0;
}
