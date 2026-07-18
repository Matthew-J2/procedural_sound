# procedural_sound

## What is this?
This is an audio engine I've been creating to learn more about audio programming and DSP. 

## What can it do?
As of 18/07/2026, it
- Uses miniaudio for cross platform audio playback
- Communicates between main thread and audio callback using a single producer single consumer lock free ring buffer to avoid dropping samples
- Implements a pull based directed arbitrary audio graph supporting cycles to route audio nodes.
- These nodes include some basic oscillators, a basic mixer, an ADSR envelope, a gain node, and expose their own parameters through reflection.
- Nodes implement idle, trigger, release, and retrigger behaviour, shared vs non-shared behaviour.
- Implements modulation on all parameters used in nodes, and recursive modulation on the amount of modulation used (parameters are first class citizens).
- Discovers and exposes parameters from arbitrary graphs through a flat parameter map.
- Implements RAII wrappers around miniaudio handles for memory safety.
- Implements a sample accurate event scheduler and dispatcher to play and release notes, as well as schedule events including stopping and starting modulation or changing parameters once.
- Implements voice based polyphony with voice pools.
- Voices implement a pitch and velocity abstraction instead of e.g. frequency and amplitude.
- Implements an instrument abstraction over voice graphs (for multiple voices at once, e.g. a pad, a pluck, using an arbitrary subgraph of the main directed audio graph).
- An arbitrary number of instruments can compose into a single mixer output
- Implements an abstraction from note names / MIDI codes to frequencies.
- Also implements microtones through this abstraction (continuously not just in steps).
- Logging through a WAV output and binary stream.

## Test coverage
Audio graph, core engine, oscillators, instrument and voice abstraction.

## How to build it?
Just run run.sh with your desired preset name as an argument. Dependencies are fetched automatically.
You will need a compiler and libc++/libstdc++ that implements C++23. You will need CMake and Ninja. <br><br>Builds and tests pass on openSUSE Tumbleweed (GCC/Clang), Ubuntu latest (GCC/Clang), Windows (MSVC), and macOS (Apple Clang).

## Output
Running the generator produces two files in the project folder:
- output.wav - a .wav file of the played audio
- log.raw - the same as interleaved float32 stereo samples
