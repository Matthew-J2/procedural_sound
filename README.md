# procedural_sound

## What is this?
This is an audio engine I've been creating to learn more about audio programming and DSP. 

## What can it do?
As of 30/06/2026, it
- Uses miniaudio for cross platform audio playback
- Communicates between main thread and audio callback using a single producer single consumer lock free ring buffer to avoid dropping samples
- Implements an audio graph to route audio nodes.
- These nodes include some basic oscillators, a basic mixer, an ADSR envelope
- Implements frequency modulation (FM)
- Implements RAII wrappers around miniaudio handles for memory safety
- Implements a sample accurate event scheduler and dispatcher to play and release notes.
- Implements voice based polyphony with voice pools.
- Implements an instrument abstraction over voices (for multiple voices at once, e.g. a pad, a pluck)
- Implements an abstraction from note names / MIDI codes to frequencies.
- Also implements microtones through this abstraction.
- Logging through a WAV output and binary stream

## How to build it?
Just run run.sh with either release or debug as an argument.
You will need a compiler and libc++/libstdc++ that implements C++23. You will need CMake and Ninja. <br><br>I haven't tested if it builds on anything but openSUSE Tumbleweed with clang++ and libstdc++, so pretty much the most recent release of both of those. <br><br>I don't see why it wouldn't work with GCC or MSVC but again, haven't tested it. I'll set up some CI runners when I get around to it.
