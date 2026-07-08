#include <gtest/gtest.h>
#include "oscillator.h"

/*============SINE==============*/
TEST(Oscillator, SineInBounds) {
    SineOscillator osc(440.0f);

    float sample_rate = 44100.0f;

    for (int i = 0; i < 10000; i++)
    {
        float sample = osc.tick(sample_rate);

        EXPECT_GE(sample, -1.0f);
        EXPECT_LE(sample,  1.0f);
    }
}

TEST(Oscillator, SineRespectsAmplitude)
{
    SineOscillator osc(440.0f);
    float sample_rate = 44100.0f;

    for (int i = 0; i < 10000; i++) {
        float sample = osc.tick(sample_rate);
        EXPECT_GE(sample, -0.25f);
        EXPECT_LE(sample,  0.25f);
    }
}

TEST(Oscillator, SineIsDeterministic)
{
    SineOscillator a(440.0f);
    SineOscillator b(440.0f);

    float sample_rate = 44100.0f;

    for (int i = 0; i < 1000; i++) {
        EXPECT_NEAR(a.tick(sample_rate), b.tick(sample_rate), 1e-6f);
    }
}

TEST(Oscillator, SineFrequencyCorrect)
{
    SineOscillator osc(440.0f);
    float sr = 44100.0f;

    int samples_per_cycle = static_cast<int>(sr / 440.0f);

    float first = osc.tick(sr);

    for (int i = 0; i < samples_per_cycle - 1; i++)
        osc.tick(sr);

    float second = osc.tick(sr);

    EXPECT_NEAR(first, second, 0.05f);
}

/*============SQUARE==============*/
TEST(Oscillator, SquareInBounds)
{
    SquareOscillator osc(440.0f);
    float sr = 44100.0f;

    for (int i = 0; i < 10000; i++) {
        float s = osc.tick(sr);
        EXPECT_GE(s, -1.0f);
        EXPECT_LE(s,  1.0f);
    }
}

TEST(Oscillator, SquarePhaseSplit)
{
    SquareOscillator osc(440.0f);
    float sr = 44100.0f;

    osc.phase = 0.0f;
    EXPECT_NEAR(osc.tick(sr), 1.0f, 1e-6f);

    osc.phase = PI - 0.001f;
    EXPECT_NEAR(osc.tick(sr), 1.0f, 1e-6f);

    osc.phase = PI + 0.001f;
    EXPECT_NEAR(osc.tick(sr), -1.0f, 1e-6f);
}

TEST(Oscillator, SquareRespectsAmplitude)
{
    SquareOscillator osc(440.0f);
    float sr = 44100.0f;

    for (int i = 0; i < 1000; i++) {
        float s = osc.tick(sr);
        EXPECT_GE(s, -0.3f);
        EXPECT_LE(s,  0.3f);
    }
}

/*============TRIANGLE==============*/
TEST(Oscillator, TriangleInBounds)
{
    TriangleOscillator osc(440.0f);
    float sr = 44100.0f;

    for (int i = 0; i < 10000; i++) {
        float s = osc.tick(sr);
        EXPECT_GE(s, -1.0f);
        EXPECT_LE(s,  1.0f);
    }
}

TEST(Oscillator, TriangleKeyPoints)
{
    TriangleOscillator osc(440.0f);
    float sr = 44100.0f;

    osc.phase = 0.0f;
    EXPECT_NEAR(osc.tick(sr), -1.0f, 1e-3f);

    osc.phase = PI;
    EXPECT_NEAR(osc.tick(sr), 1.0f, 1e-3f);

    osc.phase = 2.0f * PI - 0.001f;
    EXPECT_NEAR(osc.tick(sr), -1.0f, 1e-2f);
}

/*============SAW==============*/
TEST(Oscillator, SawInBounds)
{
    SawOscillator osc(440.0f);
    float sr = 44100.0f;

    for (int i = 0; i < 10000; i++) {
        float s = osc.tick(sr);
        EXPECT_GE(s, -1.0f);
        EXPECT_LE(s,  1.0f);
    }
}

TEST(Oscillator, SawEndpoints)
{
    SawOscillator osc(440.0f);
    float sr = 44100.0f;

    osc.phase = 0.0f;
    EXPECT_NEAR(osc.tick(sr), -1.0f, 1e-6f);

    osc.phase = 2.0f * PI - 0.001f;
    EXPECT_GT(osc.tick(sr), 0.8f);
}

TEST(Oscillator, SawHasPositiveSlopeWithinCycle)
{
    SawOscillator osc(440.0f);
    float sr = 44100.0f;

    float prev = osc.tick(sr);

    for (int i = 0; i < 1000; i++) {
        float cur = osc.tick(sr);

        float diff = cur - prev;

        // allow wrap
        if (diff < -0.5f) {
            prev = cur;
            continue;
        }

        EXPECT_GT(diff, -0.01f);
        prev = cur;
    }
}
