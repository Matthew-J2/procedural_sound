#include "parameter.h"
#include "graph.h"

float Parameter::value()
{

    if (ctx && alpha > 0.0f && ctx->current_sample != last_ticked_sample) {
        base = alpha * base + (1.0f - alpha) * target;
        last_ticked_sample = ctx->current_sample;
    }

    float result = base;

    for (auto& modulator : modulators) {
        result += modulator.amount.value() * modulator.source->pull();
    }

    return result;
}
