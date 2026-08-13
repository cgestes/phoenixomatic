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
#include "func_gen.h"
#include "osc.h"

class PhoenixEngine {
 public:
  PhoenixEngine(PhoenixModel& model, float sample_rate);

  // Rebuilds every buffer and coefficient for a new rate. Silences the
  // machine while it does: nothing that was in flight survives.
  void setSampleRate(float sample_rate);
  float sampleRate() const { return sample_rate_; }

  // Interleaved stereo, 16-bit: `frames` frames, so 2 * frames samples written.
  // The Cardputer's speaker is mono but its headphone jack is not, and reverb
  // is the one thing that really needs two channels.
  void render(int16_t* out, size_t frames);

 private:
  void applyParams();
  void publishBus();
  // True if `gate_src` has a rising edge this sample.
  bool gateEdge(uint8_t gate_src) const;
  // That source's period in milliseconds, or 0 if it has not pulsed twice yet.
  float gatePeriodMs(uint8_t gate_src) const;
  // The pulse to lock to when a module has no gate of its own: the clock where
  // there is one, the comparator where there is not.
  float mainPeriodMs() const;
  void measureGates();
  // Where a voice joins the chain, clamped: the field is a uint8_t and an
  // out-of-range one would index past the accumulators.
  int route(int inst) const {
    // The classic instrument is two oscillators, a comparator and a filter.
    // Distortion, a looper, delay and reverb are all later ideas, so in that
    // mode every voice goes straight to the master. Done here rather than by
    // rewriting the routing, so the routing you set up in a bigger mode is
    // still there when you go back to it.
    if (model_.machine_mode == MODE_CLASSIC) return ENTRY_DRY;
    uint8_t r = model_.route[inst];
    return r < ENTRY_COUNT ? r : ENTRY_DIRT;
  }
  // One method per chain stage, each taking the running stereo pair in place,
  // so render() can walk them in whatever order the panel is set to.
  void stageDirt(float* l, float* r);
  void stageFx(float* l, float* r);
  void stageLoop(float* l, float* r, bool* written);
  void stageDelay(float* l, float* r);
  void stageSpace(float* l, float* r);
  // The effect at a position, clamped: the order is eight-bit fields the UI
  // writes, and a bad one would run off the end of the switch.
  uint8_t chainAt(int pos) const {
    uint8_t fx = model_.chain[pos];
    return fx < kChainStages ? fx : static_cast<uint8_t>(pos);
  }
  void tickClock();
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
  FuncGen func_[kFuncGens];
  // How long the gate stays open after an edge. Every gate source on this
  // machine is an instant, and an envelope needs a duration.
  int func_hold_[kFuncGens] = {0, 0};
  // Pulses counted, so a shape longer than one pulse is only restarted on
  // the pulse where a new one is actually due.
  int func_pulse_[kFuncGens] = {0, 0};
  float func_trace_phase_[kFuncGens] = {0.0f, 0.0f};
  Dirt dirt_;
  // One per channel. DIRT is a transfer function rather than a send, so it has
  // no mono wet to mix back in and has to run twice or silently mono the
  // signal the moment it is moved after something stereo.
  Dirt dirt_r_;
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

  // Clock. The phase accumulator runs in sixteenths; the two dividers count
  // those. Outputs are one-sample pulses, and the hold counters exist only so
  // the UI's LEDs last long enough for an eye to catch them.
  float clk_phase_ = 0.0f;
  bool clk_edge_ = false;
  bool clk_div_edge_[kClockDividers] = {false};
  int clk_div_count_[kClockDividers] = {0};
  int clk_hold_ = 0;
  int clk_div_hold_[kClockDividers] = {0};
  // GLITCH measures the gap between its own gate pulses, so SYNC can make a
  // slice exactly one gate long without being told a tempo.
  // How far apart every gate source's pulses are, measured rather than worked
  // out from the tempo. One counter and one answer per source, updated once a
  // sample, so anything that wants to lock to something can ask what that
  // something's period actually is -- including in modes that have no clock at
  // all, where the comparator is the only pulse there is.
  //
  // GLITCH already worked this way and it was the better of the two
  // mechanisms: FUNC computed its length from BPM and the divider, which is
  // only correct for real clocks and only in ADVANCED.
  int gate_gap_[GATE_COUNT] = {};
  int gate_period_[GATE_COUNT] = {};
  int glitch_gap_ = 0;
  int glitch_period_ = 0;
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
