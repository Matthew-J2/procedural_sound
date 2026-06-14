#pragma once
#include <miniaudio.h>
#include <vector>
#include <atomic>
#include <bit>
#include <new>

template<size_t Channels>
struct AudioFrame
{
    float samples[Channels];
};

template <typename T>
struct SPSCRingBuffer{
    private:
        std::vector<T> data;
        size_t size; // capacity is size - 1 (one slot is always unused)
        size_t mask;
        alignas(std::hardware_destructive_interference_size) std::atomic<size_t> write {0};
        alignas(std::hardware_destructive_interference_size) std::atomic<size_t> read {0};

        std::atomic<size_t> dropped {0};

    public:
        SPSCRingBuffer(size_t requested_size){
            if (requested_size < 2)
                requested_size = 2;

            size = std::bit_ceil(requested_size);
            data.resize(size);
            mask = size -1;
        }

        bool push(const T&);
        bool pop(T&);
        size_t get_dropped() const;
};

// used by producer
template <typename T>
bool SPSCRingBuffer<T>::push(const T& in){
    size_t current_write = write.load(std::memory_order_relaxed);
    size_t current_read = read.load(std::memory_order_acquire);


    size_t next_write = (current_write + 1) & mask;

    if (next_write == current_read){
        dropped.fetch_add(1, std::memory_order_relaxed);
        return false; // buffer is full
    }   

    data[current_write] = in;
    write.store(next_write, std::memory_order_release);
    return true;
}

// used by consumer
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

template <typename T>
size_t SPSCRingBuffer<T>::get_dropped() const {
    return dropped.load(std::memory_order_relaxed);
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
int config_device();
