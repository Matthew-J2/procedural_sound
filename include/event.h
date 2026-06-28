#pragma once
#include <cstdint>

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
    int note_id = -1;
    float frequency = 0.0f;
    float amplitude = 0.0f;
};
