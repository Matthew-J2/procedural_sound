#pragma once
#include "oscillator.h"
#include "event.h"
#include <miniaudio.h>
#include <vector>
#include <atomic>
#include <bit>
#include <new>
#include <memory>

struct RAIIDevice {
    RAIIDevice(ma_device_config& cfg) {
        if (ma_device_init(NULL, &cfg, &device) != MA_SUCCESS)
            throw std::runtime_error("Failed to init miniaudio device.");
    }

    ~RAIIDevice() {
        ma_device_uninit(&device);
    }

    RAIIDevice(const RAIIDevice&) = delete;

    RAIIDevice& operator=(const RAIIDevice&) = delete;

    ma_device* get() {
        return &device;
    }

    ma_device* operator->() {
        return &device;
    }
    private:
        ma_device device;

};

struct RAIIEncoder {
    RAIIEncoder(const char* filename, ma_encoder_config& cfg) {
        if (ma_encoder_init_file(filename, &cfg, &encoder) != MA_SUCCESS)
            throw std::runtime_error("Failed to init miniaudio encoder.");
    }
    ~RAIIEncoder() {
        ma_encoder_uninit(&encoder);
    }
    RAIIEncoder(const RAIIEncoder&) = delete;
    RAIIEncoder& operator=(const RAIIEncoder&) = delete;
    ma_encoder* get() {
        return &encoder;
    }
    ma_encoder* operator->() {
        return &encoder;
    }
    private:
        ma_encoder encoder;
};

template<size_t Channels>
struct AudioFrame
{
    float samples[Channels];
};

struct AudioNode;
struct OscillatorNode;
struct GateNode;
struct EnvelopeNode;
template <typename T> struct SPSCRingBuffer;

using StereoFrame = AudioFrame<2>;
struct AudioContext {
    // contains state the audio callback needs
    float sample_rate;
    uint64_t current_sample = 0; // used for feedback loops
    std::shared_ptr<AudioNode> output_node;

    SPSCRingBuffer<ScheduledEvent>* event_queue = nullptr;
    std::shared_ptr<OscillatorNode> osc_node;
    std::shared_ptr<GateNode> gate;
    std::shared_ptr<EnvelopeNode> envelope;
    std::unique_ptr<SPSCRingBuffer<StereoFrame>> audio_log_buffer;
};

template <typename T>
struct SPSCRingBuffer{
    // lock free queue for communicating between main thread and audio callback
    private:
        std::vector<T> data;
        size_t size; // capacity is size - 1 (one slot is always unused)
        size_t mask;
        // avoid false sharing - force onto different cache lines
        alignas(std::hardware_destructive_interference_size) std::atomic<size_t> write {0};
        alignas(std::hardware_destructive_interference_size) std::atomic<size_t> read {0};

        std::atomic<size_t> dropped {0};

    public:
        SPSCRingBuffer(size_t requested_size){
        // size needs to be power of 2 for modulo trick
            if (requested_size < 2)
                requested_size = 2;

            size = std::bit_ceil(requested_size);
            data.resize(size);
            mask = size -1;
        }

        bool push(const T&);
        bool pop(T&);
        bool peek(T&) const;
        size_t get_dropped() const;
};

// used by producer thread only
template <typename T>
bool SPSCRingBuffer<T>::push(const T& in){
    size_t current_write = write.load(std::memory_order_relaxed);
    size_t current_read = read.load(std::memory_order_acquire);


    size_t next_write = (current_write + 1) & mask;

    if (next_write == current_read){
        dropped.fetch_add(1, std::memory_order_relaxed);
        return false; // buffer is full - add to dropped sample count
    }   

    data[current_write] = in;
    write.store(next_write, std::memory_order_release);
    return true;
}

// used by consumer thread only
template <typename T>
bool SPSCRingBuffer<T>::pop(T& out){
    size_t current_write = write.load(std::memory_order_acquire);
    size_t current_read = read.load(std::memory_order_relaxed);

    if (current_write == current_read)
        return false; // buffer is empty

    out = data[current_read];
    read.store((current_read + 1) & mask, std::memory_order_release);
    return true;
}

// read without advancing
template <typename T>
bool SPSCRingBuffer<T>::peek(T& out) const {
    size_t current_write = write.load(std::memory_order_acquire);
    size_t current_read = read.load(std::memory_order_relaxed);
    if (current_write == current_read)
        return false;
    out = data[current_read];
    return true;
}

template <typename T>
size_t SPSCRingBuffer<T>::get_dropped() const {
    return dropped.load(std::memory_order_relaxed);
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
int config_device();
