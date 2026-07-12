#pragma once
#include <cstdint>
#include "parameter.h"

struct ADSR {
    enum class Stage {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };

    Parameter attack_time;
    Parameter decay_time;
    Parameter sustain_level;
    Parameter release_time;

    Stage stage = Stage::Idle;


    ADSR(float attack_time_ = 0.0f, float decay_time_ = 0.0f,
         float sustain_level_ = 0.0f, float release_time_ = 0.0f) {
        attack_time.set(attack_time_);
        decay_time.set(decay_time_);
        sustain_level.set(sustain_level_);
        release_time.set(release_time_);
    }


    float tick (float sample_rate) {
        switch (stage) {
            case Stage::Idle:
                current_level = 0.0f;
                break;

            // advance up/down amplitude gradient unless next section
            case Stage::Attack:
                if(advance_segment(sample_rate, attack_time.value())) {
                    stage = Stage::Decay;
                    segment_start = current_level;
                    segment_target = peak_level * sustain_level.value();
                    segment_elapsed = 0;
                }   
                break;
            
            case Stage::Decay:
                segment_target = peak_level * sustain_level.value();
                if (advance_segment (sample_rate, decay_time.value())) {
                    stage = Stage::Sustain;
                    current_level = peak_level * sustain_level.value();
                }
                break;
            case Stage::Sustain:
                current_level = peak_level * sustain_level.value();
                break;
            
            case Stage::Release:
                if (advance_segment(sample_rate, release_time.value())) {
                    stage = Stage::Idle;
                    current_level = 0.0f;
                }
                break;
        }
        return current_level;
    }

    // note on
    void trigger(float peak) {
        peak_level = peak;
        segment_start = current_level; // avoids pop when re-triggering
        segment_target = peak;
        segment_elapsed = 0;
        stage = Stage::Attack;
    }

    // note released
    void release() {
        if (stage == Stage::Idle) return;
        segment_start = current_level; // avoids sound getting louder before releasing
        segment_target = 0.0f;
        segment_elapsed = 0;
        stage = Stage::Release;
    }

    float get_level() const { 
        return current_level; 
    }

    float is_idle() const {
        return stage == Stage::Idle;
    }


    private: 
        float current_level = 0.0f;
        float peak_level = 0.0f;
        
        float segment_start = 0.0f;
        float segment_target = 0.0f;
        uint64_t segment_elapsed = 0;

        // change current_level by a linear ramp towards target
        // return true when ready to move to next stage
        bool advance_segment(float sample_rate, float duration_seconds) {
            segment_elapsed++;

            // if segment is instant
            if (duration_seconds <= 0.0f) {
                current_level = segment_target;
                return true;
            }
            
            // get the progress through the section as a fraction
            float elapsed_seconds = static_cast<float>(segment_elapsed) / sample_rate;
            float progress = elapsed_seconds / duration_seconds;

            // if segment has finished
            if (progress >= 1.0f) {
                current_level = segment_target;
                return true;
            }
    
            // lerp
            current_level = segment_start + (segment_target - segment_start) * progress;
            return false;
        }
};



