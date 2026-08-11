#pragma once

#include <cstddef>

// 22050 Hz is what the Cardputer's speaker and CPU budget will carry once
// six voices and two chaos cores are running. Desktop and web use the same
// rate so everything sounds identical everywhere.
inline constexpr int kSampleRate = 22050;

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
