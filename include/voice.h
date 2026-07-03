#pragma once
#include <functional>

struct Voice {
    int note_id = -1;
    std::function<void(float frequency, float amplitude)> note_on;
    std::function<void()> note_off;
    std::function<bool()> is_idle;

    Voice(std::function<void(float, float)> on,
          std::function<void()> off,
          std::function<bool()> idle)
        : note_on(std::move(on)), note_off(std::move(off)), is_idle(std::move(idle)) {}

    void trigger(int id, float frequency, float amplitude) {
        note_id = id;
        note_on(frequency, amplitude);
    }
    void release() { note_off(); }
};
