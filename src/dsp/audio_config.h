#pragma once

#include <cstddef>

// Arduino builds compile every source file separately, so a define placed in
// the sketch does not reach the DSP translation units. Derive the hardware
// profile from the toolchain's build-wide marker instead.
#if defined(ARDUINO) && !defined(PHX_EMBEDDED)
#define PHX_EMBEDDED 1
#endif

// Two different numbers, and confusing them is the whole hazard here.
//
// kSampleRate is what the machine runs at, and on desktop and web it is chosen
// at run time. kMaxSampleRate is what the *buffers* are sized for, which cannot
// be chosen at run time: a delay line is an array, and an array that holds one
// second at 22 kHz holds less than half a second at 48. Anything that is a
// capacity takes the second one; anything that is a duration takes the first.
//
// 22050 Hz is what the Cardputer's speaker and CPU budget will carry once six
// voices and two chaos cores are running, and it is the default everywhere so
// that a patch sounds the same wherever it is opened. It is also the only rate
// the hardware build has room for -- at 48 kHz the delay, the looper and the
// reverb alone want half a megabyte.
#if defined(PHX_EMBEDDED)
inline constexpr int kMaxSampleRate = 22050;
#else
inline constexpr int kMaxSampleRate = 48000;
#endif
inline constexpr int kSampleRate = 22050;

// The rates the desktop and the browser offer. The low ones are not a
// compatibility concession -- at 8 kHz the comparator's pulse train aliases
// into something the machine cannot make any other way, and that is an
// instrument setting, not a degradation.
inline constexpr int kRateCount = 7;
inline constexpr int kRates[kRateCount] = {
    8000, 11025, 16000, 22050, 32000, 44100, 48000};

// A length that was measured in samples at 22050, expressed at whatever rate
// this build is sized for. Rounds up, because a buffer one sample short is a
// buffer that wraps early.
inline constexpr int atMaxRate(int samples_at_22050) {
  return static_cast<int>(
      (static_cast<long long>(samples_at_22050) * kMaxSampleRate + 22049) /
      22050);
}

// Interleaved stereo. The Cardputer's speaker is mono but its headphone jack
// is not, and DELAY and SPACE are the only things on the machine that make the
// channels differ. Named because the frames-versus-samples contract otherwise
// gets restated as a literal 2 in every platform.
inline constexpr int kChannels = 2;
inline constexpr size_t kBlockSize = 256;

// Chaos cores are slow by nature; running them every sample would be waste.
inline constexpr int kChaosStride = 16;

// Control decisions (sequencer advance, fate, drum triggers) land on exact
// sample boundaries rather than block boundaries, so a 16th at 120bpm is not
// smeared across 11ms.
inline constexpr int kStepsPerBeat = 4;
