#include "parameter.h"
#include "graph.h"

float Parameter::value()
{
    float result = base;

    for (auto& modulator : modulators) {
        result += modulator.amount.value() * modulator.source->pull();
    }

    return result;
}
