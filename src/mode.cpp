/*
 * Copyright (c) 2017, James Jackson and Daniel Koch, BYU MAGICC Lab
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdbool.h>

#include "mode.h"
#include "rosflight.h"

namespace rosflight_firmware
{

Mode::Mode()
{
  _armed = false;
  prev_time_ms = 0;
  time_sticks_have_been_in_arming_position_ms = 0;
}

void Mode::init(ROSflight *_rf)
{
  RF_ = _rf;
  _armed = false;
  _error_code = ERROR_NONE;
  _failsafe_active = false;
  started_gyro_calibration = false;
}

bool Mode::arm(void)
{
  // look for error codes
  if (_error_code != ERROR_NONE)
  {
    //    mavlink_log_error("Unable to arm due to error code %d", _error_state);
    return false;
  }

  // Perform a gyro calibration on ARM
  else if (RF_->params_.get_param_int(PARAM_CALIBRATE_GYRO_ON_ARM))
  {
    if (!started_gyro_calibration && !_armed)
    {
      RF_->sensors_.start_gyro_calibration();
      started_gyro_calibration = true;
      return false;
    }
    else if (RF_->sensors_.gyro_calibration_complete())
    {
      started_gyro_calibration = false;
      _armed = true;
      RF_->board_.led1_on();
      return true;
    }
    return false;
  }

  // Just arm straight away
  else
  {
    if (!_armed)
    {
      _armed = true;
      RF_->board_.led1_on();
      return true;
    }
    return false;
  }
  return false;
}

void Mode::disarm(void)
{
  _armed = false;
  RF_->board_.led1_off();
}

bool Mode::check_failsafe(void)
{
  bool failsafe = false;

  if (RF_->board_.pwm_lost())
  {
    failsafe = true;
    // set the RC Lost error flag
    set_error_code(ERROR_RC_LOST);
  }
  else
  {
    // go into failsafe if we get an invalid RC command for any channel
    for (int8_t i = 0; i<RF_->params_.get_param_int(PARAM_RC_NUM_CHANNELS); i++)
    {
      if (RF_->board_.pwm_read(i) < 900 || RF_->board_.pwm_read(i) > 2100)
      {
        failsafe = true;
      }
    }
  }

  if (failsafe)
  {
    // blink LED to let the user know we are in failsafe
    static uint8_t count = 0;
    if (count > 25)
    {
      RF_->board_.led1_toggle();
      count = 0;
    }
    count++;

    // Set the FAILSAFE bit
    _failsafe_active = true;
  }
  else
  {
    // we got a valid RC measurement for all channels and pwm is active
    // Clear the FAILSAFE bit
    _failsafe_active = false;

    // Clear the RC Lost Error
    clear_error_code(ERROR_RC_LOST);

    if (_armed)
      RF_->board_.led1_on();
    else
      RF_->board_.led1_off();
  }
  return failsafe;
}


bool Mode::update_state()
{
  uint32_t now_ms = RF_->board_.clock_millis();

  // see it has been at least 20 ms
  uint32_t dt = now_ms-prev_time_ms;
  if (dt < 20)
  {
    return false;
  }

  // if it has, then do stuff
  prev_time_ms = now_ms;

  // check for failsafe mode
  if (check_failsafe())
  {
    return true;
  }
  else
  {
    // check for arming switch
    if (!RF_->rc_.rc_switch_mapped(RC_SWITCH_ARM))
    {
      if (!_armed) // we are DISARMED
      {
        // if left stick is down and to the right
        if ((RF_->rc_.rc_stick(RC_STICK_F) < RF_->params_.get_param_float(PARAM_ARM_THRESHOLD))
            && (RF_->rc_.rc_stick(RC_STICK_Z) > (1.0f - RF_->params_.get_param_float(PARAM_ARM_THRESHOLD))))
        {
          time_sticks_have_been_in_arming_position_ms += dt;
        }
        else
        {
          time_sticks_have_been_in_arming_position_ms = 0;
        }
        if (time_sticks_have_been_in_arming_position_ms > 500)
        {
          if (arm())
            time_sticks_have_been_in_arming_position_ms = 0;
        }
      }
      else // we are ARMED
      {
        // if left stick is down and to the left
        if (RF_->rc_.rc_stick(RC_STICK_F) < RF_->params_.get_param_float(PARAM_ARM_THRESHOLD)
            && RF_->rc_.rc_stick(RC_STICK_Z) < -(1.0f - RF_->params_.get_param_float(PARAM_ARM_THRESHOLD)))
        {
          time_sticks_have_been_in_arming_position_ms += dt;
        }
        else
        {
          time_sticks_have_been_in_arming_position_ms = 0;
        }
        if (time_sticks_have_been_in_arming_position_ms > 500)
        {
          disarm();
          time_sticks_have_been_in_arming_position_ms = 0;
        }
      }
    }
    else // ARMING WITH SWITCH
    {
      if (RF_->rc_.rc_switch(RC_SWITCH_ARM))
      {
        if (!_armed)
          arm();
      }
      else
      {
        disarm();
      }
    }
  }
  return true;
}

}