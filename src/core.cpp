#include "core.h"
#include "oscillator.h"
#include "graph.h"
#include "event.h"
#include "midi.h"
#include "instrument.h"
#include <memory>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>
#include <algorithm>

using StereoFrame = AudioFrame<2>;

void dispatch_due_events(AudioContext* ctx, SPSCRingBuffer<ScheduledEvent>& queue) {
    ScheduledEvent ev;
    while (queue.peek(ev) && ev.trigger_sample <= ctx->current_sample) {
        queue.pop(ev);
        auto& pool = ctx->instrument_voice_pools[ev.instrument_index];

        switch (ev.type) {
            case EventType::NoteOn:
                for (size_t i = 0; i < pool.size(); i++) {
                    if (pool[i].is_idle()) {
                        pool[i].trigger(ev.note_id, ev.note);
                        ctx->active_voice_indices[ev.instrument_index].push_back(static_cast<int>(i));
                        break;
                    }
                }
                break;
            case EventType::NoteOff:
                for (auto& voice : pool) {
                    if (voice.note_id == ev.note_id) {
                        voice.release();
                        break;
                    }
                }
                break;
            case EventType::ParamChange:
                // if no specific voice, apply to all voices in instrument
                if (ev.note_id == -1) {
                    for (auto& voice : pool)
                        voice.set_param(ev.param_id, ev.value);
                } else {
                    for (auto& voice : pool){
                        if (voice.note_id == ev.note_id) {
                            voice.set_param(ev.param_id, ev.value);
                        break;
                        }
                    }
                }
            break;
        }
    }
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    auto t0 = std::chrono::steady_clock::now();
    // get audio context (oscillators right now)
    AudioContext* audio_ctx = (AudioContext*)pDevice->pUserData;

    // access output buffer and device parameters
    float* out = (float*)pOutput;
    ma_uint32 channels = pDevice->playback.channels;

    // fill output buffer with samples 
    for (ma_uint32 i = 0; i < frameCount; i++)
    {
        audio_ctx->current_sample++;
        dispatch_due_events(audio_ctx, *audio_ctx->event_queue);
        float sample = audio_ctx->output_node->pull();

        StereoFrame frame;
        // write to all speaker channels
        for (size_t c = 0; c < channels; c++){
            frame.samples[c] = sample;
            out[i * channels + c] = sample;
        }
        audio_ctx->audio_log_buffer->push(frame);
    }
    auto t1 = std::chrono::steady_clock::now();

    double callback_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    double budget_us = (double)frameCount / audio_ctx->sample_rate * 1'000'000.0;

    if (callback_us > budget_us * 0.6) // warn before you actually miss it
        audio_ctx->timing_log->push({audio_ctx->current_sample, callback_us, budget_us});
}

void build_patch(AudioContext* ctx)
{
    auto mixer = std::make_shared<MixerNode>();
    mixer->ctx = ctx;

    // Create oscillators, put in mixer, put mixer in context as output node.
    auto sine = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(note_frequency("C3")), ctx, 0.02f
    );
    auto square = std::make_shared<OscillatorNode>(
        std::make_unique<SquareOscillator>(note_frequency("E3")), ctx, 0.02f
    );
    auto triangle = std::make_shared<OscillatorNode>(
        std::make_unique<TriangleOscillator>(note_frequency("G3")), ctx, 1.0f
    );
    auto saw = std::make_shared<OscillatorNode>(
        std::make_unique<SawOscillator>(note_frequency("C4")), ctx, 0.02f
    );

    auto saw_gate = std::make_shared<GateNode>(saw, ctx);
    saw_gate->active.set(1.0f);

    auto lfo = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(2.0f), ctx, 1.0f
    );

    auto lfo_test = std::make_shared<OscillatorNode>(
        std::make_unique<SquareOscillator>(note_frequency("C2")),
        ctx,
        0.1f
    );

    lfo_test->frequency.modulators.push_back({
        .source = lfo,
        .amount = {20.0f, {}}
    });
    // mixer->inputs.push_back(lfo_test);

    // FM sweep test


    auto meta_depth_lfo = std::make_shared<OscillatorNode>(
        std::make_unique<TriangleOscillator>(0.2f), ctx, 1.0f
    );

    auto fm_rate_sweep = std::make_shared<OscillatorNode>(
        std::make_unique<TriangleOscillator>(10.0f), ctx, 1.0f
    );

    auto fm_depth_sweep = std::make_shared<OscillatorNode>(
        std::make_unique<TriangleOscillator>(0.25f), ctx, 1.0f
    );

    auto bell_carrier = std::make_shared<OscillatorNode>(
    std::make_unique<SineOscillator>(0.0f), ctx, 0.2f
    );

    auto bell_modulator = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(0.0f), ctx, /*amplitude = depth because modulator*/ 300.0f, 1.41f
    );

    bell_modulator->frequency.modulators.push_back({fm_rate_sweep, {10.0f, {}}});
    bell_modulator->amplitude.modulators.push_back({fm_depth_sweep, {150.0f, {}}});

    bell_carrier->frequency.modulators.push_back({bell_modulator, {1.0f, {}}});

    auto& fm_rate_sweep_depth_sweep = bell_modulator->frequency.modulators.back();

    fm_rate_sweep_depth_sweep.amount.modulators.push_back({meta_depth_lfo, {10.0f, {}}});

    auto vibrato_lfo = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(5.0f), ctx, 1.0f);

    auto one_pole_filter_lfo = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(20.0f), ctx, 1.0f);

    // pad instrument
    register_instrument(ctx, build_instrument(
        ctx, "pad", 32,
        [vibrato_lfo, one_pole_filter_lfo](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            auto osc = std::make_shared<OscillatorNode>(
                std::make_unique<TriangleOscillator>(0.0f), ctx, 1.0f
            );
            osc->frequency.modulators.push_back({vibrato_lfo, {0.0f, {}}});
            osc->frequency.modulators[0].amount.enable_smoothing(10.6f, ctx, ctx->sample_rate);
            auto one_pole_filter = std::make_shared<OnePoleNode>(osc, ctx); 
            one_pole_filter->cutoff.modulators.push_back({one_pole_filter_lfo, {500.0, {}}});
            auto envelope = std::make_shared<EnvelopeNode>(ctx, ADSR(0.5f, 0.35f, 0.5f, 0.5f));
            auto gain = std::make_shared<GainNode>(one_pole_filter, ctx, 0.0f);
            gain->amplitude.modulators.push_back({envelope, {1.0f, {}}});
            return gain;
        },
        {vibrato_lfo, one_pole_filter_lfo}
    ),
    mixer);

    //pluck
    register_instrument(ctx, build_instrument(
        ctx, "pluck", 16,
        [](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            auto osc = std::make_shared<OscillatorNode>(std::make_unique<SawOscillator>(0.0f), ctx, 1.0f);
            auto envelope = std::make_shared<EnvelopeNode>(ctx, ADSR(0.002f, 0.35f, 0.0f, 0.35f));
            auto gain = std::make_shared<GainNode>(osc, ctx, 0.0f);
            gain->amplitude.modulators.push_back({envelope, {1.0f, {}}});
            return gain;
        }
    ), 
    mixer);

    //bell
    register_instrument(ctx, build_instrument(
        ctx, "bell", 8,
        [fm_rate_sweep, fm_depth_sweep, meta_depth_lfo](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            auto modulator = std::make_shared<OscillatorNode>(std::make_unique<SineOscillator>(0.0f), ctx, 300.0f, 1.41f);
            modulator->frequency.modulators.push_back({fm_rate_sweep, {10.0f, {}}});
            modulator->amplitude.modulators.push_back({fm_depth_sweep, {150.0f, {}}});

            auto& rate_depth_sweep = modulator->frequency.modulators.back();
            rate_depth_sweep.amount.modulators.push_back({meta_depth_lfo, {10.0f, {}}});

            auto carrier = std::make_shared<OscillatorNode>(std::make_unique<SineOscillator>(0.0f), ctx, 0.2f);
            carrier->frequency.modulators.push_back({modulator, {1.0f, {}}});

            auto envelope = std::make_shared<EnvelopeNode>(ctx, ADSR(0.005f, 0.3f, 0.3f, 0.6f));
            auto gain = std::make_shared<GainNode>(carrier, ctx, 0.0f);
            gain->amplitude.modulators.push_back({envelope, {1.0f, {}}});

            return gain;
        },
        {fm_rate_sweep, fm_depth_sweep, meta_depth_lfo}
    ), 
    mixer);

    // filter stuff

    // One pole low pass sweep

    auto op_lp_lfo = std::make_shared<OscillatorNode>(
        std::make_unique<TriangleOscillator>(0.2f), ctx, 1.0f);
 
    register_instrument(ctx, build_instrument(
        ctx, "onepole_lowpass_sweep", 4,
        [op_lp_lfo](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            auto osc = std::make_shared<OscillatorNode>(std::make_unique<SawOscillator>(0.0f), ctx, 1.0f);
            auto filter = std::make_shared<OnePoleNode>(osc, ctx, 1200.0f, OnePoleNode::Mode::LowPass);
            filter->cutoff.modulators.push_back({op_lp_lfo, {1100.0f, {}}}); // ~100Hz-2300Hz
            auto envelope = std::make_shared<EnvelopeNode>(ctx, ADSR(0.05f, 0.2f, 0.8f, 0.6f));
            auto gain = std::make_shared<GainNode>(filter, ctx, 0.0f);
            gain->amplitude.modulators.push_back({envelope, {1.0f, {}}});
            return gain;
        },
        {op_lp_lfo}
    ), mixer);


    auto op_hp_lfo = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(3.0f), ctx, 1.0f);
 
    register_instrument(ctx, build_instrument(
        ctx, "onepole_highpass_gargle", 4,
        [op_hp_lfo](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            auto osc = std::make_shared<OscillatorNode>(std::make_unique<SquareOscillator>(0.0f), ctx, 0.6f);
            auto filter = std::make_shared<OnePoleNode>(osc, ctx, 600.0f, OnePoleNode::Mode::HighPass);
            filter->cutoff.modulators.push_back({op_hp_lfo, {550.0f, {}}}); // ~50Hz-1150Hz
            auto envelope = std::make_shared<EnvelopeNode>(ctx, ADSR(0.01f, 0.15f, 0.5f, 0.3f));
            auto gain = std::make_shared<GainNode>(filter, ctx, 0.0f);
            gain->amplitude.modulators.push_back({envelope, {1.0f, {}}});
            return gain;
        },
        {op_hp_lfo}
    ), mixer);


    auto svf_lp_lfo = std::make_shared<OscillatorNode>(
        std::make_unique<SquareOscillator>(4.0f), ctx, 1.0f);
 
    register_instrument(ctx, build_instrument(
        ctx, "svf_lowpass_wobble", 4,
        [svf_lp_lfo](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            auto osc = std::make_shared<OscillatorNode>(std::make_unique<SawOscillator>(0.0f), ctx, 1.0f);
            auto filter = std::make_shared<SVFNode>(osc, ctx, 500.0f, 3.5f, SVFNode::Mode::LowPass);
            filter->cutoff.modulators.push_back({svf_lp_lfo, {450.0f, {}}}); // ~50Hz-950Hz
            auto envelope = std::make_shared<EnvelopeNode>(ctx, ADSR(0.01f, 0.1f, 0.9f, 0.3f));
            auto gain = std::make_shared<GainNode>(filter, ctx, 0.0f);
            gain->amplitude.modulators.push_back({envelope, {0.6f, {}}});
            return gain;
        },
        {svf_lp_lfo}
    ), mixer);


    auto svf_hp_lfo = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(6.0f), ctx, 1.0f);
 
    register_instrument(ctx, build_instrument(
        ctx, "svf_highpass_zap", 6,
        [svf_hp_lfo](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            auto osc = std::make_shared<OscillatorNode>(std::make_unique<SquareOscillator>(0.0f), ctx, 0.3f);
            auto filter = std::make_shared<SVFNode>(osc, ctx, 2000.0f, 6.0f, SVFNode::Mode::HighPass);
            filter->cutoff.modulators.push_back({svf_hp_lfo, {1500.0f, {}}}); // ~500Hz-3500Hz
            auto envelope = std::make_shared<EnvelopeNode>(ctx, ADSR(0.001f, 0.25f, 0.0f, 0.1f)); // short zap
            auto gain = std::make_shared<GainNode>(filter, ctx, 0.0f);
            gain->amplitude.modulators.push_back({envelope, {0.7f, {}}}); // high Q already adds a lot of level
            return gain;
        },
        {svf_hp_lfo}
    ), mixer);


    auto svf_bp_lfo = std::make_shared<OscillatorNode>(
        std::make_unique<TriangleOscillator>(0.6f), ctx, 1.0f);
 
    register_instrument(ctx, build_instrument(
        ctx, "svf_bandpass_wah", 4,
        [svf_bp_lfo](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            auto osc = std::make_shared<OscillatorNode>(std::make_unique<SawOscillator>(0.0f), ctx, 1.0f);
            auto filter = std::make_shared<SVFNode>(osc, ctx, 800.0f, 3.0f, SVFNode::Mode::BandPass);
            filter->cutoff.modulators.push_back({svf_bp_lfo, {600.0f, {}}}); // ~200Hz-1400Hz
            auto envelope = std::make_shared<EnvelopeNode>(ctx, ADSR(0.02f, 0.2f, 0.8f, 0.4f));
            auto gain = std::make_shared<GainNode>(filter, ctx, 0.0f);
            gain->amplitude.modulators.push_back({envelope, {1.0f, {}}});
            return gain;
        },
        {svf_bp_lfo}
    ), mixer);


    auto svf_notch_lfo = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(0.3f), ctx, 1.0f);
 
    register_instrument(ctx, build_instrument(
        ctx, "svf_notch_phaser", 4,
        [svf_notch_lfo](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            auto osc = std::make_shared<OscillatorNode>(std::make_unique<SawOscillator>(0.0f), ctx, 1.0f);
 
            auto filter = std::make_shared<SVFNode>(osc, ctx, 1000.0f, 4.0f, SVFNode::Mode::Notch);
            filter->cutoff.modulators.push_back({svf_notch_lfo, {900.0f, {}}}); // ~100Hz-1900Hz
 
            auto wet_mix = std::make_shared<MixerNode>();
            wet_mix->ctx = ctx;
            wet_mix->inputs.push_back(osc);    // dry - osc is shared between both inputs, pull() caches it
            wet_mix->inputs.push_back(filter); // notch-filtered
 
            auto envelope = std::make_shared<EnvelopeNode>(ctx, ADSR(0.4f, 0.3f, 0.7f, 0.8f)); // pad-like, so the swirl is easy to hear
            auto gain = std::make_shared<GainNode>(wet_mix, ctx, 0.0f);
            gain->amplitude.modulators.push_back({envelope, {0.5f, {}}}); // halved since two signals are summed
            return gain;
        },
        {svf_notch_lfo}
    ), mixer);


    auto svf_peak_lfo = std::make_shared<OscillatorNode>(
        std::make_unique<TriangleOscillator>(0.4f), ctx, 1.0f);
 
    register_instrument(ctx, build_instrument(
        ctx, "svf_peak_formant", 4,
        [svf_peak_lfo](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            auto osc = std::make_shared<OscillatorNode>(std::make_unique<SawOscillator>(0.0f), ctx, 1.0f);
            auto filter = std::make_shared<SVFNode>(osc, ctx, 900.0f, 4.0f, SVFNode::Mode::Peak);
            filter->cutoff.modulators.push_back({svf_peak_lfo, {700.0f, {}}}); // ~200Hz-1600Hz
            auto envelope = std::make_shared<EnvelopeNode>(ctx, ADSR(0.03f, 0.2f, 0.75f, 0.5f));
            auto gain = std::make_shared<GainNode>(filter, ctx, 0.0f);
            gain->amplitude.modulators.push_back({envelope, {0.25f, {}}}); // peak boosts a lot, tame the overall level
            return gain;
        },
        {svf_peak_lfo}
    ), mixer);


    auto svf_ap_lfo = std::make_shared<OscillatorNode>(
        std::make_unique<SineOscillator>(0.15f), ctx, 1.0f);
 
    register_instrument(ctx, build_instrument(
        ctx, "svf_allpass_phaser", 4,
        [svf_ap_lfo](AudioContext* ctx) -> std::shared_ptr<AudioNode> {
            auto osc = std::make_shared<OscillatorNode>(std::make_unique<SawOscillator>(0.0f), ctx, 1.0f);
 
            auto filter = std::make_shared<SVFNode>(osc, ctx, 700.0f, 5.0f, SVFNode::Mode::AllPass);
            filter->cutoff.modulators.push_back({svf_ap_lfo, {650.0f, {}}}); // ~50Hz-1350Hz
 
            auto wet_mix = std::make_shared<MixerNode>();
            wet_mix->ctx = ctx;
            wet_mix->inputs.push_back(osc);
            wet_mix->inputs.push_back(filter);
 
            auto envelope = std::make_shared<EnvelopeNode>(ctx, ADSR(0.3f, 0.3f, 0.8f, 0.8f));
            auto gain = std::make_shared<GainNode>(wet_mix, ctx, 0.0f);
            gain->amplitude.modulators.push_back({envelope, {0.5f, {}}});
            return gain;
        },
        {svf_ap_lfo}
    ), mixer);

    ctx->output_node = mixer;
}

std::unique_ptr<SPSCRingBuffer<ScheduledEvent>> init_event_queue(AudioContext* ctx, size_t capacity = 64)
{
    // allocate and connect queue
    auto queue = std::make_unique<SPSCRingBuffer<ScheduledEvent>>(capacity);
    ctx->event_queue = queue.get();
    return queue;
}

int config_device()
{
    // create engine state
    auto audio_ctx = std::make_unique<AudioContext>();
    audio_ctx->sample_rate = 0.00f;
    
    // build patches and event queue
    build_patch(audio_ctx.get());
    auto event_queue = init_event_queue(audio_ctx.get(), 256);

    // set up miniaudio device to call data callback for audio samples when init
    // and started
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2; //TODO: support 5.1 7.1 surround sound one day
    config.sampleRate        = 0;
    config.dataCallback      = data_callback;
    config.pUserData         = audio_ctx.get();

    auto device = RAIIDevice(config);
    
    // defined by miniaudio device
    audio_ctx->sample_rate = (float)device->sampleRate;

    std::cout << "sample rate: " << device->sampleRate << "\n";
    std::cout << "channels: " << device->playback.channels << "\n";
    std::cout << "format: " << device->playback.format << "\n";

    // set up encoder to describe how audio should be written
    ma_encoder_config wav_encoder_config = 
        ma_encoder_config_init(
            ma_encoding_format_wav,
            device->playback.format,
            device->playback.channels,
            device->sampleRate 
    );
    
    // open encoder file for writing
    auto wav_encoder = RAIIEncoder("output.wav", wav_encoder_config) ;
    // open raw log file for writing
    std::ofstream file ("log.raw", std::ios::binary);
    if (!file) {
        std::cerr << "Warning: failed to open log.raw, no raw logs will be produced\n";
    }

    size_t sampleTime = 90;

    audio_ctx->audio_log_buffer = std::make_unique<SPSCRingBuffer<StereoFrame>>(device->sampleRate * sampleTime * 2); //FIXME: multiplying gives slack but doesn't actually solve the risk of overflow from pipewire / your api of choice acting up. this is bad and wastes tons of memory but I'm leaving it like this for now / a while because I want to do other stuff

    audio_ctx->timing_log = std::make_unique<SPSCRingBuffer<CallbackLog>>(1024);

    // push events on queue

    std::vector<ScheduledEvent> events;
 
    auto push_note = [&](int instrument_index, int note_id,
                          double on_seconds, double off_seconds,
                          float pitch, float velocity) {
        events.push_back({
            .type = EventType::NoteOn,
            .trigger_sample = (uint64_t)(on_seconds * audio_ctx->sample_rate),
            .instrument_index = instrument_index,
            .note_id = note_id,
            .note = NoteEvent{.pitch = pitch, .velocity = velocity}
        });
        events.push_back({
            .type = EventType::NoteOff,
            .trigger_sample = (uint64_t)(off_seconds * audio_ctx->sample_rate),
            .instrument_index = instrument_index,
            .note_id = note_id,
            .note = {}
        });
    };
 
    auto push_param_change = [&](double at_seconds, int instrument_index, std::string_view name, float value, int note_id = -1) {
        events.push_back({
            .type = EventType::ParamChange,
            .trigger_sample = (uint64_t)(at_seconds * audio_ctx->sample_rate),
            .instrument_index = instrument_index,
            .note_id = note_id,
            .note = {},
            .param_id = param_id(audio_ctx.get(), instrument_index, name),
            .value = value
        });
    };
    int note_id = 0;
    // pad chord: C3-E3-G3, held then released out of sync with each other
    push_note(0, note_id++, 0.0, 2.0, note_frequency("C4"), 0.3f);
    push_note(0, note_id++, 0.5, 2.5, note_frequency("E4"), 0.3f);
    push_note(0, note_id++, 1.0, 3.0, note_frequency("G4"), 0.3f);
    push_note(0, note_id++, 0.0, 2.0, note_frequency("C3"), 0.3f);
    push_note(0, note_id++, 0.0, 2.0, note_frequency("C5"), 0.3f);
    push_note(0, note_id++, 0.0, 2.0, note_frequency("E5"), 0.3f);
    push_note(0, note_id++, 0.0, 2.0, note_frequency("G5"), 0.3f);
    push_note(0, note_id++, 0.0, 2.0, note_frequency("A4"), 0.3f);
    push_note(0, note_id++, 0.0, 2.0, note_frequency("A3"), 0.3f);
 
    // half a 24-TET scale on pluck, one quarter-tone every second
    int step = 0;
    for (float midi = 60.0f; midi <= 62.5f; midi += 0.5f, note_id++, step++) {
        double on_time = 4.0 + step * 1.0;
        push_note(1, note_id, on_time, on_time + 0.5, midi_to_frequency(midi), 0.3f);
    }
 
    // partway through the scale, morph pluck's envelope into something pad-like
    push_param_change(9.75, 1, "attack", 0.5f);
    push_param_change(9.75, 1, "decay", 0.35f);
    push_param_change(9.75, 1, "sustain", 0.5f);
    push_param_change(9.75, 1, "release", 0.5f);
 
    // rest of the scale, now with the morphed envelope
    step = 0;
    for (float midi = 63.0f; midi <= 65.5f; midi += 0.5f, note_id++, step++) {
        double on_time = 10.0 + step * 1.0;
        push_note(1, note_id, on_time, on_time + 0.5, midi_to_frequency(midi), 0.3f);
    }
 
    // finish on the "bell" instrument
    push_note(2, note_id++, 16.0, 16.5, midi_to_frequency(66.0f), 0.6f);
    push_note(2, note_id++, 17.0, 17.5, midi_to_frequency(66.5f), 0.6f);

    // pad with vibrato on, turned off mid note and turned on again
    push_param_change(18.4, 0, "frequency_mod0", 20.0f);  // vibrato on, just before the note starts
    push_note(0, note_id, 18.5, 22.5, note_frequency("A4"), 0.4f);
    push_param_change(20.0, 0, "frequency_mod0", 0.0f);  // vibrato off
    push_param_change(21.5, 0, "frequency_mod0", 20.0f);  // vibrato back on
    note_id++;

    int held_note_id = note_id++;
    int untouched_note_id = note_id++;

    push_note(0, held_note_id, 23, 27.0, note_frequency("C3"), 0.4);
    push_note(0, untouched_note_id, 23.0, 27.0, note_frequency("C4"), 0.4f);
    push_param_change(24.5, 0, "frequency_mod0", 6.0f, held_note_id); // give only this voice vibrato

    // ===============filter stuff===============
    
    // onepole_lowpass_sweep (3)
    push_note(3, note_id++, 28.0, 33.5, note_frequency("C2"), 0.5f);
    push_note(3, note_id++, 29.5, 33.5, note_frequency("G2"), 0.35f);


    // onepole_highpass (4):
    push_note(4, note_id++, 34.5, 34.9, note_frequency("C4"), 0.5f);
    push_note(4, note_id++, 35.1, 35.5, note_frequency("E4"), 0.5f);
    push_note(4, note_id++, 35.7, 36.1, note_frequency("G4"), 0.5f);
    push_note(4, note_id++, 36.3, 37.0, note_frequency("C5"), 0.5f);


    // svf_lowpass_wobble (5):
    push_note(5, note_id++, 38.0, 42.5, note_frequency("E2"), 0.6f);

    // svf_highpass_zap (6):
    push_note(6, note_id++, 43.5, 43.8, note_frequency("A4"), 0.5f);
    push_note(6, note_id++, 44.0, 44.3, note_frequency("C5"), 0.5f);
    push_note(6, note_id++, 44.5, 44.8, note_frequency("E5"), 0.5f);


    // svf_bandpass_wah (7):
    push_note(7, note_id++, 45.8, 46.2, note_frequency("C3"), 0.5f);
    push_note(7, note_id++, 46.3, 46.7, note_frequency("C3"), 0.5f);
    push_note(7, note_id++, 46.9, 47.4, note_frequency("D#3"), 0.5f);
    push_note(7, note_id++, 47.6, 48.2, note_frequency("C3"), 0.5f);
    push_note(7, note_id++, 48.4, 49.5, note_frequency("A#2"), 0.5f);


    // svf_notch_phaser (8):
    push_note(8, note_id++, 50.0, 55.0, note_frequency("C3"), 0.4f);
    push_note(8, note_id++, 50.0, 55.0, note_frequency("G3"), 0.3f);
 
    // svf_peak_formant (9):
    push_note(9, note_id++, 56.0, 58.0, note_frequency("C4"), 0.5f);
    push_note(9, note_id++, 58.2, 60.5, note_frequency("E4"), 0.5f);
 
    // svf_allpass_phaser (10):
    push_note(10, note_id++, 61.0, 66.0, note_frequency("A3"), 0.4f);
    push_note(10, note_id++, 61.0, 66.0, note_frequency("E4"), 0.3f);
 
    std::stable_sort(events.begin(), events.end(),
        [](const ScheduledEvent& a, const ScheduledEvent& b) {
            return a.trigger_sample < b.trigger_sample;
        });
 
    for (auto& ev : events)
        event_queue->push(ev);


    StereoFrame frame;
        // start audio device - callback now active
        if (ma_device_start(device.get()) != MA_SUCCESS) {
            std::cerr << "Failed to start miniaudio playback device.\n";
            return -1;
        }

    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(sampleTime);
    //FIXME: busy waiting will be a problem in the future
    // while callback runs, write frames to wav and raw file every 10ms
    while (std::chrono::steady_clock::now() < end) {
        while (audio_ctx->audio_log_buffer->pop(frame)) {
            file.write(reinterpret_cast<const char*>(&frame), sizeof(frame));
            ma_encoder_write_pcm_frames(
                wav_encoder.get(),
                frame.samples,
                1,
                nullptr
            );

        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // stop audio device - callback no longer active
    ma_device_stop(device.get());

    // drain buffer
    while (audio_ctx->audio_log_buffer->pop(frame)) {
        file.write(reinterpret_cast<const char*>(&frame), sizeof(frame));
        ma_encoder_write_pcm_frames(
            wav_encoder.get(),
            frame.samples,
            1,
            nullptr
        );
    }

    CallbackLog t;
    while (audio_ctx->timing_log->pop(t)) {
        std::cerr << "callback overrun at sample " << t.sample_index
                  << ": " << t.callback_us << "us / " << t.budget_us << "us budget\n";
    }

    std::cout << "dropped samples: " << audio_ctx->audio_log_buffer->get_dropped() << "\n";
    return 0;
}
