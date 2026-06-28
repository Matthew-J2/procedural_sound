#pragma once
#include <cstdint>

enum class EventType {
    GateOn,
    GateOff,
    NoteOn,
    NoteOff
    // SetFrequency
};

struct ScheduledEvent {
    EventType type;
    uint64_t trigger_sample;
    float frequency = 0.0f;
    float amplitude = 0.0f;
};
