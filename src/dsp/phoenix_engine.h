// The machine, wired.
//
// There is no clock. The two oscillators run, the comparator compares them,
// and its edges are the only time base the instrument has — every sequencer
// advance, every fate toss, every drum hit hangs off them. Turn the
// oscillators down and it ticks; turn them up and it screams. That is the
// benjolin idea and it is the reason the comparator is a module rather than a
// hidden utility.
//
// The engine reads parameters out of PhoenixModel and writes the live values
// back into it, so the UI reads exactly the fields it always read.
//
// Threading: render() runs on the audio thread, the UI runs on another. The
// shared fields are plain scalars — a torn read costs one frame of a wrong
// meter, which is not worth a lock in an audio callback. Nothing structural
// (array sizes, source indices) is written by the audio thread.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../core/model.h"
#include "chaos_osc.h"
#include "drum_voices.h"
#include "filter.h"
#include "delay.h"
#include "dirt.h"
#include "fx.h"
#include "looper.h"
#include "space.h"
#include "osc.h"

class PhoenixEngine {
 public:
  PhoenixEngine(PhoenixModel& model, float sample_rate);

  // Interleaved stereo, 16-bit: `frames` frames, so 2 * frames samples written.
  // The Cardputer's speaker is mono but its headphone jack is not, and reverb
  // is the one thing that really needs two channels.
  void render(int16_t* out, size_t frames);

  float sampleRate() const { return sample_rate_; }

 private:
  void applyParams();
  void publishBus();
  // True if `gate_src` has a rising edge this sample.
  bool gateEdge(uint8_t gate_src) const;
  void tickFate();
  void tickSequencers();
  void tickDrums();
  uint32_t rng();
  float randUnit();

  PhoenixModel& model_;
  float sample_rate_;

  ChaosOsc chaos_[2];
  OscVoice osc_[2];
  DrumVoice drum_[kDrumVoices];
  Filter filter_;
  Dirt dirt_;
  Fx fx_;
  Looper looper_;
  int glitch_hold_ = 0;
  MultiDelay delay_;
  Space space_;

  // The patch bus: one live value per source, exactly the eight the footer
  // shows. Mod banks read from here and nowhere else.
  float bus_[SRC_COUNT] = {0};

  int chaos_countdown_ = 0;

  // Comparator — the time base.
  bool comp_gt_ = false;
  bool comp_gt_prev_ = false;
  bool gt_edge_ = false;
  bool osc_edge_[2] = {false, false};
  bool osc_prev_[2] = {false, false};
  bool rung_edge_[2] = {false, false};
  bool lt_edge_ = false;
  float comp_out_ = 0.0f;
  // Samples since the last rising edge. Measuring the gap resolves a 4 Hz
  // comparator, which counting edges in a fixed window does not.
  int edge_gap_ = 0;

  // Sequencers (the pattern itself lives in the model).
  int seq_step_[2] = {0, 0};
  float seq_cv_[2] = {0.0f, 0.0f};
  float seq_slew_[2] = {0.0f, 0.0f};
  int seq_div_count_[2] = {0, 0};

  // Fate: divide, then decide. Outputs are one-sample pulses; the hold
  // counters are only so the UI's LEDs are visible to a human eye.
  int fate_count_[kFateChannels] = {0};
  bool fate_div_[kFateChannels] = {false};
  bool fate_a_[kFateChannels] = {false};
  bool fate_b_[kFateChannels] = {false};
  int fate_hold_[kFateChannels] = {0};
  int drum_count_[kDrumVoices] = {0};
  int drum_hold_[kDrumVoices] = {0};
  int led_hold_samples_ = 1;

  uint32_t rng_state_ = 0x2545F491u;
  float dc_block_x_ = 0.0f, dc_block_y_ = 0.0f;
  // Quantisation levels for CRUSH; 0 means off.
  float dc_block_xr_ = 0.0f, dc_block_yr_ = 0.0f;
  int space_gate_hold_ = 0;
  int gate_hold_samples_ = 1;
};
