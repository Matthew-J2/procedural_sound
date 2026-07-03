#pragma once
#include <cstdint>
#include <functional>

enum class EventType {
    // GateOn,
    // GateOff,
    NoteOn,
    NoteOff
    // SetFrequency
};

struct ScheduledEvent {
    EventType type;
    uint64_t trigger_sample;
    int instrument_index = 0;
    int note_id = -1;
    float frequency = 0.0f;
    float amplitude = 0.0f;
};


