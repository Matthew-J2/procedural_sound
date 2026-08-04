# procedural_sound

## What is this?
This is an audio engine I've been creating to learn more about audio programming and DSP. 

## What can it do?
As of 04/08/2026:

### Input/Output and real-time details
- Uses miniaudio for cross platform audio playback.
- Communicates between main thread and audio callback using a single producer single consumer lock free ring buffer to avoid dropping samples.
- The SPSC ring buffer pads its read/write atomics to avoid false sharing between producer and consumer threads.
- The audio callback times itself against its budgeted sample time and logs to a second SPSC buffer if it gets within a set percentage of the budget.
- Implements RAII wrappers around miniaudio handles for memory safety.
- Once the graph is built, the audio callback is allocation-free with pre-reserved pool sizes.
- Logging through a WAV output and binary stream. 

### Audio graph
- Implements a pull based directed arbitrary audio graph supporting cycles to route audio nodes.
- This graph resolves cycles via a one sample delay cache stored in each node.
- These nodes include oscillators, a mixer, an ADSR envelope, a gain node, a one pole filter, a state variable two pole filter, and they all expose their own parameters through reflection.
- Nodes can override idle, trigger, release, and shared vs non-shared behaviour.
- Meaning nodes can define their own behaviour for signalling they are idle, what happens when the node is triggered, what happens when the node is triggered again while not idle,  and whether the node is controlled by its instrument or voice.
- Implements modulation on all parameters used in nodes, and recursive modulation on the amount of modulation used (parameters are first class citizens).
- Parameters can also be smoothed towards a target so that changes in parameter during runtime don't produce broadband transient clicks.
- Discovers and exposes parameters from arbitrary graphs through a flat parameter map.

### Nodes
- A sine, square, saw, and triangle oscillator are implemented.
- A mixer to sum inputs is implemented.
- A hard on/off switch gate node is implemented.
- A fixed value constant source is implemented (which can be used as an impulse source).
- A gain node which scales a signal by a modulatable parameter.
- A full ADSR envelope which does not click on retrigger.
- A one pole IIR filter which support low and high pass modes.
- A two pole state variable filter with six modes: low pass, high pass, band pass, notch, peak, and all pass. Its cutoff and resonance are modulatable. It also supports self oscillation at a pitch set by the cutoff.
- Implements a sample accurate event scheduler and dispatcher to play and release notes, as well as schedule events including stopping and starting modulation or changing parameters once.
- A node used to sum only currently active voices and prune inactive ones - necessary for performance reasons.

### Voices
- Implements voice based polyphony with voice pools.
- Voices implement a pitch and velocity abstraction instead of e.g. frequency and amplitude.
- Pitch comes from a continuous MIDI style formula so microtonal tuning is native.
- These voices can be targetted with scheduled parameter changes, and note on and note off events to control them. Each voice in an instrument, or an individual voice can be targetted.

### Instruments
- An instrument is a named pool of voices built from the same subgraph.
- Nodes can be marked shared within an instrument rather than duplicated per voice.
- Voice stealing policy is simple at the moment and picks the next idle voice.
- An arbitrary number of instruments can compose into a single mixer output


## Demo 
A demo is stored in core.cpp, demonstrating some of the capabilities of the audio engine in real time. 
It comprises of 12 named instruments, demonstrating 3 levels deep nested FM (an LFO modulates the depth of a sweep that modulates the frequency of an FM modulator), a live vibrato toggle mid note, a parameter change that targets one specific held voice while an identical simultaneous note is untouched, microtones being used, filter resonance, and filter self oscillation.

## Test coverage
Audio graph, core engine, oscillators, instrument, filters and voice abstraction.
There are 62 tests ran via GTest.

The DSP tests aim to check mathematical correctness. Some things checked by the tests:
- One-pole and SVF filters are checked against their own impulse response and DC gain. 
- Filter mode identities are cross-checked against each other algebraically (LowPass + HighPass = Notch, Peak = LowPass − HighPass, AllPass = input − 2k * BandPass). 
- Self-oscillation is checked: the impulse response's peak amplitude at high Q is shown not to decay over time. 
- Stability fuzzing: cutoff and resonance swept by a per-sample pseudo-random amount for 20,000 samples, asserting the output stays finite throughout. 
- No reallocation of the active-voice list inside the callback 
- One voice's lifecycle can't affect another's.


## How to build it?
Just run run.sh with your desired preset name as an argument then run the executable generated in build/. Dependencies are fetched automatically with FetchContent.
You will need a compiler and libc++/libstdc++ that implements C++23. You will need CMake and Ninja. <br><br>Builds and tests pass on openSUSE Tumbleweed (GCC/Clang) (the distribution I use on my machine, not included in CI), Ubuntu latest (GCC/Clang), Windows (MSVC), and macOS (Apple Clang).

## Output
Running the generator produces two files in the project folder:
- output.wav - a .wav file of the played audio
- log.raw - the same as interleaved float32 stereo samples

