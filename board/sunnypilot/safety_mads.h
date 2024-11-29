#pragma once

extern bool enable_mads;
bool enable_mads = false;

extern bool disengage_lateral_on_brake;
bool disengage_lateral_on_brake = false;

extern bool disengaged_from_brakes;
bool disengaged_from_brakes = false;

extern bool controls_allowed_lat;
bool controls_allowed_lat = false;

extern bool lkas_button;
bool lkas_button = false;

extern int temp_debug;
int temp_debug = 0;

extern bool acc_main_on_prev;
bool acc_main_on_prev = false;

extern bool main_button_prev;
bool main_button_prev = false;

extern uint32_t acc_main_on_mismatches;
uint32_t acc_main_on_mismatches = 0;

// Enable the ability to enable sunnypilot Automatic Lane Centering and ACC/SCC independently of each other. This
// will enable MADS and allow other features to be used.
// Enable the ability to re-engage sunnypilot Automatic Lane Centering only (NOT ACC/SCC) on brake release while MADS
// is enabled.
#define ALT_EXP_ENABLE_MADS 1024

// Enable the ability to disable disengaging lateral control on brake press while MADS is enabled.
#define ALT_EXP_DISABLE_DISENGAGE_LATERAL_ON_BRAKE 2048

void mads_set_state(bool state) {
  controls_allowed_lat = state;
  disengaged_from_brakes = state;
}

void _mads_check_lkas_button(void) {
  if (lkas_button && enable_mads) {
    controls_allowed_lat = true;
    lkas_button = false;
  }
}

void mads_check_states(void) {
  if (acc_main_on && enable_mads) {
    controls_allowed_lat = true;
  }

  if (!acc_main_on && acc_main_on_prev) {
    // controls_allowed = false; // THIS CAUSES THE OTHER TESTS TO FAIL
    mads_set_state(false);
  }
  acc_main_on_prev = acc_main_on;
  _mads_check_lkas_button();
}

static void mads_exit_controls(void) {
  if (controls_allowed_lat) {
    disengaged_from_brakes = true;
    controls_allowed_lat = false;
  }
}

static void mads_resume_controls(void) {
  if (disengaged_from_brakes) {
    controls_allowed_lat = true;
    disengaged_from_brakes = false;
  }
}

void mads_check_braking_condition(bool state, bool state_prev) {
  if (state && (!state_prev || vehicle_moving)) {
    if (disengage_lateral_on_brake) {
      mads_exit_controls();
    }
  } else if (!state && disengage_lateral_on_brake) {
    mads_resume_controls();
  } else {
  }
}

void mads_reset_acc_main(const bool acc_main_on_tx) {
  if (acc_main_on && !acc_main_on_tx) {
    acc_main_on_mismatches += 1U;
    if (acc_main_on_mismatches >= 25U) {
      acc_main_on = acc_main_on_tx;
      mads_check_states();
    }
  } else {
    acc_main_on_mismatches = 0U;
  }
}
