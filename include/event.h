#pragma once
#include <cstdint>

enum class EventType {
    // GateOn,
    // GateOff,
    NoteOn,
    NoteOff,
    ParamChange
    // SetFrequency
};

struct NoteEvent {
    float pitch = 0.0f;
    float velocity = 0.0f;
};

struct ScheduledEvent {
    EventType type;
    uint64_t trigger_sample;
    int instrument_index = 0;
    int note_id = -1;
    NoteEvent note;
    int param_id = 0;
    float value = 0.0f;
};


