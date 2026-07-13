#pragma once
#include <functional>
#include "event.h"

struct Voice {
    int note_id = -1;
    std::function<void(const NoteEvent&)> note_on;
    std::function<void()> note_off;
    std::function<bool()> is_idle;
    std::function<void(int param_id, float value)> set_param;

    Voice(std::function<void(const NoteEvent&)> on,
          std::function<void()> off,
          std::function<bool()> idle,
          std::function<void(int, float)> set_p = [](int, float) {})
        : note_on(std::move(on)), note_off(std::move(off)), is_idle(std::move(idle)),
          set_param(std::move(set_p)) {}

    void trigger(int id, const NoteEvent& ev) {
        note_id = id;
        note_on(ev);
    }
    void release() { note_off(); }
};
