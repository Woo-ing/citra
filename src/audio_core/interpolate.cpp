// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include "audio_core/interpolate.h"
#include "common/assert.h"

namespace AudioCore::AudioInterp {

// Calculations are done in fixed point with 24 fractional bits.
// (This is not verified. This was chosen for minimal error.)
constexpr u64 scale_factor = 1 << 24;
constexpr u64 scale_mask = scale_factor - 1;

/// Here we step over the input in steps of rate, until we consume all of the input.
/// Three adjacent samples are passed to fn each step.
template <typename Function>
static void StepOverSamples(State& state, StereoBuffer16& input, float rate, StereoFrame16& output,
                            std::size_t& outputi, Function fn) {
    ASSERT(rate > 0);

    if (input.empty())
        return;

    //input.emplace_front(state.xn2);
    input.emplace_front(state.xn1);

    const u64 step_size = static_cast<u64>(rate * scale_factor);
    u64 fposition = state.fposition;
    std::size_t inputi = 0;
#ifdef _USE_SSE
    alignas(16) std::array<std::array<s16, 2>, samples_per_frame> inputTmp0;
    alignas(16) std::array<std::array<s16, 2>, samples_per_frame> inputTmp1;
    std::array<u64, samples_per_frame> fractions;

	size_t outputp = outputi;
    for (; outputp < output.size(); ++outputp) {
        inputi = static_cast<std::size_t>(fposition / scale_factor);

        if (inputi + 1 >= input.size()) {
            inputi = input.size() - 1;
            break;
        }

        u64 fraction = fposition & scale_mask;
        fractions[outputp] = fraction;
        inputTmp0[outputp] = input[inputi];
        inputTmp1[outputp] = input[inputi + 1];

        fposition += step_size;
    }

    size_t i = outputi;
    for (; i < outputp / 4 * 4; i += 4) {
        // x0[0] + fraction * (x1[0] - x0[0]) / scale_factor
        // x0[1] + fraction * (x1[1] - x0[1]) / scale_factor
        const __m128i* x0 = (__m128i*)&inputTmp0[i][0];
        const __m128i* x1 = (__m128i*)&inputTmp1[i][0];
        __m128i r0 = _mm_load_si128(x0);     // r0 = x0[0], x0[1], ...
        __m128i r1 = _mm_load_si128(x1);     // r1 = x1[0], x1[1], ...
        __m128i r2 = _mm_subs_epi16(r1, r0); // r2 = x1[0] - x0[0], x1[1] - x0[1], ...

        output[i] = {r0.m128i_i16[0] + short(r2.m128i_i16[0] * fractions[0] / scale_factor),
                     r0.m128i_i16[1] + short(r2.m128i_i16[1] * fractions[0] / scale_factor)};
        output[i + 1] = {r0.m128i_i16[2] + short(r2.m128i_i16[2] * fractions[1] / scale_factor),
                         r0.m128i_i16[3] + short(r2.m128i_i16[3] * fractions[1] / scale_factor)};
        output[i + 2] = {r0.m128i_i16[4] + short(r2.m128i_i16[4] * fractions[2] / scale_factor),
                         r0.m128i_i16[5] + short(r2.m128i_i16[5] * fractions[2] / scale_factor)};
        output[i + 3] = {r0.m128i_i16[6] + short(r2.m128i_i16[6] * fractions[3] / scale_factor),
                         r0.m128i_i16[7] + short(r2.m128i_i16[7] * fractions[3] / scale_factor)};
    }
    for (; i < outputp; ++i) {
        output[i] = fn(fractions[i], inputTmp0[i], inputTmp1[i]);
    }
#else
    for (; outputi < output.size(); ++outputi) {
        inputi = static_cast<std::size_t>(fposition / scale_factor);

        if (inputi + 1 >= input.size()) {
            inputi = input.size() - 1;
            break;
        }

        u64 fraction = fposition & scale_mask;
        output[outputi] = fn(fraction, input[inputi], input[inputi + 1]);

        fposition += step_size;
    }
#endif

    //state.xn2 = input[inputi];
    // state.xn1 = input[inputi + 1];
    state.xn1 = input[inputi];
    state.fposition = fposition - inputi * scale_factor;

    input.erase(input.begin(), std::next(input.begin(), inputi + 1));
}

void None(State& state, StereoBuffer16& input, float rate, StereoFrame16& output,
          std::size_t& outputi) {
    StepOverSamples(
        state, input, rate, output, outputi,
        [](u64 fraction, const auto& x0, const auto& x1) { return x0; });
}

void Linear(State& state, StereoBuffer16& input, float rate, StereoFrame16& output,
            std::size_t& outputi) {
    // Note on accuracy: Some values that this produces are +/- 1 from the actual firmware.
    StepOverSamples(state, input, rate, output, outputi,
                    [](u64 fraction, const auto& x0, const auto& x1) {
                        // This is a saturated subtraction. (Verified by black-box fuzzing.)
                        s64 delta0 = std::clamp<s64>(x1[0] - x0[0], -32768, 32767);
                        s64 delta1 = std::clamp<s64>(x1[1] - x0[1], -32768, 32767);

                        return std::array<s16, 2>{
                            static_cast<s16>(x0[0] + fraction * delta0 / scale_factor),
                            static_cast<s16>(x0[1] + fraction * delta1 / scale_factor),
                        };
                    });
}

} // namespace AudioCore::AudioInterp
