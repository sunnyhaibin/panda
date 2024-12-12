/***
The MIT License

Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

Last updated: July 29, 2024
***/

#pragma once

#include "sunnypilot/safety_mads_declarations.h"

// ===============================
// Global Variables
// ===============================

ButtonState mads_button_press = MADS_BUTTON_UNAVAILABLE;
MADSState m_mads_state;

// state for mads controls_allowed_lat timeout logic
bool heartbeat_engaged_mads = false;             // MADS enabled, passed in heartbeat USB command
uint32_t heartbeat_engaged_mads_mismatches = 0;  // count of mismatches between heartbeat_engaged_mads and controls_allowed_lat

// ===============================
// State Update Helpers
// ===============================

static EdgeTransition m_get_edge_transition(bool current, bool last) {
  EdgeTransition state;

  if (current && !last) {
    state = MADS_EDGE_RISING;
  } else if (!current && last) {
    state = MADS_EDGE_FALLING;
  } else {
    state = MADS_EDGE_NO_CHANGE;
  }

  return state;
}

static void m_mads_state_init(void) {
  m_mads_state.is_vehicle_moving = NULL;
  m_mads_state.acc_main.current = NULL;
  m_mads_state.mads_button.current = MADS_BUTTON_UNAVAILABLE;

  m_mads_state.system_enabled = false;
  m_mads_state.disengage_lateral_on_brake = false;
  m_mads_state.main_cruise_allowed = false;

  m_mads_state.acc_main.previous = false;
  m_mads_state.acc_main.transition = MADS_EDGE_NO_CHANGE;

  m_mads_state.mads_button.last = MADS_BUTTON_UNAVAILABLE;
  m_mads_state.mads_button.transition = MADS_EDGE_NO_CHANGE;


  m_mads_state.current_disengage.reason = MADS_DISENGAGE_REASON_NONE;
  m_mads_state.previous_disengage = m_mads_state.current_disengage;

  m_mads_state.controls_requested_lat = false;
  m_mads_state.controls_allowed_lat = false;
}

static void m_update_button_state(ButtonStateTracking *button_state) {
  if (button_state->current != MADS_BUTTON_UNAVAILABLE) {
    button_state->transition = m_get_edge_transition(
      button_state->current == MADS_BUTTON_PRESSED,
      button_state->last == MADS_BUTTON_PRESSED
    );

    button_state->last = button_state->current;
  }
}

static void m_update_binary_state(BinaryStateTracking *state) {
  state->transition = m_get_edge_transition(state->current, state->previous);
  state->previous = state->current;
}

static void m_mads_try_allow_controls_lat(void) {
  if (m_mads_state.controls_requested_lat && !m_mads_state.controls_allowed_lat) {
    m_mads_state.controls_allowed_lat = true;
    m_mads_state.previous_disengage = m_mads_state.current_disengage;
    m_mads_state.current_disengage.reason = MADS_DISENGAGE_REASON_NONE;
  }
}

// Use buttons or main cruise state transition properties to request lateral control
static void m_mads_update_state(void) {
  // Main cruise
  if ((m_mads_state.acc_main.transition == MADS_EDGE_RISING) && m_mads_state.main_cruise_allowed) {
    m_mads_state.controls_requested_lat = true;
  } else if (m_mads_state.acc_main.transition == MADS_EDGE_FALLING) {
    m_mads_state.controls_requested_lat = false;
    mads_exit_controls(MADS_DISENGAGE_REASON_ACC_MAIN_OFF);
  } else {
  }

  // MADS button
  if (m_mads_state.mads_button.transition == MADS_EDGE_RISING) {
    m_mads_state.controls_requested_lat = true;
  }

  // Disengage lateral on brake
  if (m_mads_state.disengage_lateral_on_brake) {
    if ((m_mads_state.braking.transition == MADS_EDGE_RISING) || (m_mads_state.braking.current && m_mads_state.is_vehicle_moving)) {
      m_mads_state.controls_requested_lat = false;
      mads_exit_controls(MADS_DISENGAGE_REASON_BRAKE);
    } else if ((m_mads_state.current_disengage.reason == MADS_DISENGAGE_REASON_BRAKE) && !m_mads_state.braking.current) {
      m_mads_state.controls_requested_lat = true;
    }
    else {
  
    }
  }
}

static void m_mads_heartbeat_engaged_check(void) {
  if (m_mads_state.controls_allowed_lat && !heartbeat_engaged_mads) {
    heartbeat_engaged_mads_mismatches += 1U;
    if (heartbeat_engaged_mads_mismatches >= 3U) {
      mads_exit_controls(MADS_DISENGAGE_REASON_HEARTBEAT_ENGAGED_MISMATCH);
    }
  } else {
    heartbeat_engaged_mads_mismatches = 0U;
  }
}

// ===============================
// Function Implementations
// ===============================

inline void mads_set_alternative_experience(const int *mode) {
  bool mads_enabled = (*mode & ALT_EXP_ENABLE_MADS) != 0;
  bool disengage_lateral_on_brake = (*mode & ALT_EXP_DISENGAGE_LATERAL_ON_BRAKE) != 0;
  bool main_cruise_allowed = (*mode & ALT_EXP_MAIN_CRUISE_ALLOWED) != 0;

  mads_set_system_state(mads_enabled, disengage_lateral_on_brake, main_cruise_allowed);
}

inline void mads_set_system_state(bool enabled, bool disengage_lateral_on_brake, bool main_cruise_allowed) {
  m_mads_state_init();
  m_mads_state.system_enabled = enabled;
  m_mads_state.disengage_lateral_on_brake = enabled && disengage_lateral_on_brake;
  m_mads_state.main_cruise_allowed = enabled && main_cruise_allowed;
}

inline void mads_exit_controls(DisengageReason reason) {
  if (m_mads_state.controls_allowed_lat) {
    m_mads_state.previous_disengage = m_mads_state.current_disengage;
    m_mads_state.current_disengage.reason = reason;
    m_mads_state.controls_allowed_lat = false;
  }
}

inline bool mads_is_lateral_control_allowed_by_mads(void) {
  return m_mads_state.system_enabled && m_mads_state.controls_allowed_lat;
}

inline void mads_state_update(const bool op_vehicle_moving, const bool op_acc_main, const bool op_allowed, const bool is_braking) {
  m_mads_state.is_vehicle_moving = op_vehicle_moving;
  m_mads_state.mads_button.current = mads_button_press;
  m_mads_state.acc_main.current = op_acc_main;
  m_mads_state.op_controls_allowed.current = op_allowed;
  m_mads_state.braking.current = is_braking;

  m_update_binary_state(&m_mads_state.acc_main);
  m_update_binary_state(&m_mads_state.op_controls_allowed);
  m_update_binary_state(&m_mads_state.braking);
  m_update_button_state(&m_mads_state.mads_button);

  m_mads_update_state();

  m_mads_try_allow_controls_lat();
  m_mads_heartbeat_engaged_check();
}
