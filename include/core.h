#pragma once
#include <miniaudio.h>

struct test_audio_data{
    float phase;
    float frequency;
    float volume;
};
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
int config_device();
