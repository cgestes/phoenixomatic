// Small maths every DSP module needs, in one place.
//
// These were an anonymous-namespace copy per translation unit — clamp1 six
// times, clamp01 twice, pi under three spellings. Six copies of a two-line
// function is not a cost worth paying attention to on its own; six *chances*
// to change one of them and not the others is.
#pragma once

#include <cmath>

inline constexpr float kPi = 3.14159265f;
inline constexpr float kTwoPi = 6.28318531f;
inline constexpr float kHalfPi = 1.57079633f;
// For a constant-power pan applied to a signal that is already stereo:
// cos and sin are both 1/sqrt(2) in the middle, so a centred source would
// otherwise come out three decibels down.
inline constexpr float kSqrt2 = 1.41421356f;

inline float clamp1(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// The one-pole smoothing coefficient DELAY and SPACE share for DAMP: 0 keeps
// the highs, 1 takes them. Both modules mapped 0..1 onto the same 0.05..0.95
// span independently, so changing the feel of DAMP in one silently disagreed
// with the other.
inline float dampCoeff(float damp) { return 0.05f + 0.9f * clamp01(damp); }

// Advance a circular index by one. The obvious `(i + 1) % n` is a real integer
// division when n is a runtime value — tens of cycles on the Cardputer, and
// this runs a dozen times a sample.
inline void wrapInc(int* i, int n) {
  int v = *i + 1;
  *i = v >= n ? 0 : v;
}
