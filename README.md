# procedural_sound

## What is this?
This is an audio engine I've been creating to learn more about audio programming and DSP. 

## What can it do?
As of 07/07/2026, it
- Uses miniaudio for cross platform audio playback
- Communicates between main thread and audio callback using a single producer single consumer lock free ring buffer to avoid dropping samples
- Implements an audio graph to route audio nodes.
- These nodes include some basic oscillators, a basic mixer, an ADSR envelope
- Implements frequency modulation (FM)
- Implements RAII wrappers around miniaudio handles for memory safety
- Implements a sample accurate event scheduler and dispatcher to play and release notes.
- Implements voice based polyphony with voice pools.
- Implements an instrument abstraction over voices (for multiple voices at once, e.g. a pad, a pluck)
- An arbitrary number of instruments can compose into a single mixer output
- Implements an abstraction from note names / MIDI codes to frequencies.
- Also implements microtones through this abstraction (continuously not just in steps).
- Implements runtime parameter automation - any exposed parameter can be changed at a scheduled sample accurate time via an event.
- Logging through a WAV output and binary stream

## Test coverage
Audio graph, core engine, oscillators.

## How to build it?
Just run run.sh with your desired preset name as an argument. Dependencies are fetched automatically.
You will need a compiler and libc++/libstdc++ that implements C++23. You will need CMake and Ninja. <br><br>Builds and tests pass on openSUSE Tumbleweed (GCC/Clang), Ubuntu latest (GCC/Clang), Windows (MSVC), and macOS (Apple Clang).

## Output
Running the generator produces two files in the project folder:
- output.wav - a .wav file of the played audio
- log.raw - the same as interleaved float32 stereo samples
