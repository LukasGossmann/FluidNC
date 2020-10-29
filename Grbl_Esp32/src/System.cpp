/*
  System.cpp - Header for system level commands and real-time processes
  Part of Grbl
  Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC

	2018 -	Bart Dring This file was modified for use on the ESP32
					CPU. Do not use this with Grbl for atMega328P

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Grbl.h"
#include "Config.h"
#include "SettingsDefinitions.h"

// Declare system global variable structure
system_t               sys;
int32_t                sys_position[MAX_N_AXIS];        // Real-time machine (aka home) position vector in steps.
int32_t                sys_probe_position[MAX_N_AXIS];  // Last probe position in machine coordinates and steps.
volatile Probe         sys_probe_state;                 // Probing state value.  Used to coordinate the probing cycle with stepper ISR.
volatile ExecState     sys_rt_exec_state;  // Global realtime executor bitflag variable for state management. See EXEC bitmasks.
volatile ExecAlarm     sys_rt_exec_alarm;  // Global realtime executor bitflag variable for setting various alarms.
volatile ExecAccessory sys_rt_exec_accessory_override;  // Global realtime executor bitflag variable for spindle/coolant overrides.
volatile bool          cycle_stop;                      // For state transitions, instead of bitflag
#ifdef DEBUG
volatile bool sys_rt_exec_debug;
#endif
volatile Percent sys_rt_f_override;  // Global realtime executor feedrate override percentage
volatile Percent sys_rt_r_override;  // Global realtime executor rapid override percentage
volatile Percent sys_rt_s_override;  // Global realtime executor spindle override percentage

UserOutput::AnalogOutput*  myAnalogOutputs[MaxUserDigitalPin];
UserOutput::DigitalOutput* myDigitalOutputs[MaxUserDigitalPin];

xQueueHandle control_sw_queue;    // used by control switch debouncing
bool         debouncing = false;  // debouncing in process

void system_ini() {  // Renamed from system_init() due to conflict with esp32 files
    // setup control inputs

    if (ControlSafetyDoorPin->get() != Pin::UNDEFINED) {
        auto pin  = ControlSafetyDoorPin->get();
        auto attr = Pin::Attr::Input | Pin::Attr::ISR;
        if (pin.capabilities().has(Pins::PinCapabilities::PullUp)) {
            attr = attr | Pin::Attr::PullUp;
        }
        pin.setAttr(attr);
        pin.attachInterrupt(isr_control_inputs, CHANGE);
    }

    if (ControlResetPin->get() != Pin::UNDEFINED) {
        auto pin  = ControlResetPin->get();
        auto attr = Pin::Attr::Input | Pin::Attr::ISR;
        if (pin.capabilities().has(Pins::PinCapabilities::PullUp)) {
            attr = attr | Pin::Attr::PullUp;
        }
        pin.setAttr(attr);
        pin.attachInterrupt(isr_control_inputs, CHANGE);
    }

    if (ControlFeedHoldPin->get() != Pin::UNDEFINED) {
        auto pin  = ControlFeedHoldPin->get();
        auto attr = Pin::Attr::Input | Pin::Attr::ISR;
        if (pin.capabilities().has(Pins::PinCapabilities::PullUp)) {
            attr = attr | Pin::Attr::PullUp;
        }
        pin.setAttr(attr);
        pin.attachInterrupt(isr_control_inputs, CHANGE);
    }

    if (ControlCycleStartPin->get() != Pin::UNDEFINED) {
        auto pin  = ControlCycleStartPin->get();
        auto attr = Pin::Attr::Input | Pin::Attr::ISR;
        if (pin.capabilities().has(Pins::PinCapabilities::PullUp)) {
            attr = attr | Pin::Attr::PullUp;
        }
        pin.setAttr(attr);
        pin.attachInterrupt(isr_control_inputs, CHANGE);
    }

    if (MacroButton0Pin->get() != Pin::UNDEFINED) {
        auto pin  = MacroButton0Pin->get();
        auto attr = Pin::Attr::Input | Pin::Attr::ISR;
        if (pin.capabilities().has(Pins::PinCapabilities::PullUp)) {
            attr = attr | Pin::Attr::PullUp;
        }
        pin.setAttr(attr);
        pin.attachInterrupt(isr_control_inputs, CHANGE);
    }

    if (MacroButton1Pin->get() != Pin::UNDEFINED) {
        auto pin  = MacroButton1Pin->get();
        auto attr = Pin::Attr::Input | Pin::Attr::ISR;
        if (pin.capabilities().has(Pins::PinCapabilities::PullUp)) {
            attr = attr | Pin::Attr::PullUp;
        }
        pin.setAttr(attr);
        pin.attachInterrupt(isr_control_inputs, CHANGE);
    }

    if (MacroButton2Pin->get() != Pin::UNDEFINED) {
        auto pin  = MacroButton2Pin->get();
        auto attr = Pin::Attr::Input | Pin::Attr::ISR;
        if (pin.capabilities().has(Pins::PinCapabilities::PullUp)) {
            attr = attr | Pin::Attr::PullUp;
        }
        pin.setAttr(attr);
        pin.attachInterrupt(isr_control_inputs, CHANGE);
    }

    if (MacroButton3Pin->get() != Pin::UNDEFINED) {
        auto pin  = MacroButton3Pin->get();
        auto attr = Pin::Attr::Input | Pin::Attr::ISR;
        if (pin.capabilities().has(Pins::PinCapabilities::PullUp)) {
            attr = attr | Pin::Attr::PullUp;
        }
        pin.setAttr(attr);
        pin.attachInterrupt(isr_control_inputs, CHANGE);
    }

#ifdef ENABLE_CONTROL_SW_DEBOUNCE
    // setup task used for debouncing
    control_sw_queue = xQueueCreate(10, sizeof(int));
    xTaskCreate(controlCheckTask,
                "controlCheckTask",
                2048,
                NULL,
                5,  // priority
                NULL);
#endif

    //customize pin definition if needed
#if (GRBL_SPI_SS != -1) || (GRBL_SPI_MISO != -1) || (GRBL_SPI_MOSI != -1) || (GRBL_SPI_SCK != -1)
    SPI.begin(GRBL_SPI_SCK, GRBL_SPI_MISO, GRBL_SPI_MOSI, GRBL_SPI_SS);
#endif

    // Setup M62,M63,M64,M65 pins
    for (int i = 0; i < 4; ++i) {
        myDigitalOutputs[i] = new UserOutput::DigitalOutput(i, UserDigitalPin[i]->get());
    }

    // Setup M67 Pins
    myAnalogOutputs[0] = new UserOutput::AnalogOutput(0, UserAnalogPin[0]->get(), USER_ANALOG_PIN_0_FREQ);
    myAnalogOutputs[1] = new UserOutput::AnalogOutput(1, UserAnalogPin[1]->get(), USER_ANALOG_PIN_1_FREQ);
    myAnalogOutputs[2] = new UserOutput::AnalogOutput(2, UserAnalogPin[2]->get(), USER_ANALOG_PIN_2_FREQ);
    myAnalogOutputs[3] = new UserOutput::AnalogOutput(3, UserAnalogPin[3]->get(), USER_ANALOG_PIN_3_FREQ);
}

#ifdef ENABLE_CONTROL_SW_DEBOUNCE
// this is the debounce task
void controlCheckTask(void* pvParameters) {
    while (true) {
        int evt;
        xQueueReceive(control_sw_queue, &evt, portMAX_DELAY);  // block until receive queue
        vTaskDelay(CONTROL_SW_DEBOUNCE_PERIOD);                // delay a while
        ControlPins pins = system_control_get_state();
        if (pins.value) {
            system_exec_control_pin(pins);
        }
        debouncing = false;

        static UBaseType_t uxHighWaterMark = 0;
        reportTaskStackSize(uxHighWaterMark);
    }
}
#endif

void IRAM_ATTR isr_control_inputs(void*) {
#ifdef ENABLE_CONTROL_SW_DEBOUNCE
    // we will start a task that will recheck the switches after a small delay
    int evt;
    if (!debouncing) {  // prevent resending until debounce is done
        debouncing = true;
        xQueueSendFromISR(control_sw_queue, &evt, NULL);
    }
#else
    ControlPins pins = system_control_get_state();
    system_exec_control_pin(pins);
#endif
}

// Returns if safety door is ajar(T) or closed(F), based on pin state.
uint8_t system_check_safety_door_ajar() {
#ifdef ENABLE_SAFETY_DOOR_INPUT_PIN
    return system_control_get_state().bit.safetyDoor;
#else
    return false;  // Input pin not enabled, so just return that it's closed.
#endif
}

void system_flag_wco_change() {
#ifdef FORCE_BUFFER_SYNC_DURING_WCO_CHANGE
    protocol_buffer_synchronize();
#endif
    sys.report_wco_counter = 0;
}

// Returns machine position of axis 'idx'. Must be sent a 'step' array.
// NOTE: If motor steps and machine position are not in the same coordinate frame, this function
//   serves as a central place to compute the transformation.
float system_convert_axis_steps_to_mpos(int32_t* steps, uint8_t idx) {
    float pos;
    float steps_per_mm = axis_settings[idx]->steps_per_mm->get();
#ifdef COREXY
    if (idx == X_AXIS) {
        pos = (float)system_convert_corexy_to_x_axis_steps(steps) / steps_per_mm;
    } else if (idx == Y_AXIS) {
        pos = (float)system_convert_corexy_to_y_axis_steps(steps) / steps_per_mm;
    } else {
        pos = steps[idx] / steps_per_mm;
    }
#else
    pos = steps[idx] / steps_per_mm;
#endif
    return pos;
}

void system_convert_array_steps_to_mpos(float* position, int32_t* steps) {
    uint8_t idx;
    auto    n_axis = number_axis->get();
    for (idx = 0; idx < n_axis; idx++) {
        position[idx] = system_convert_axis_steps_to_mpos(steps, idx);
    }
    return;
}

// Returns control pin state as a uint8 bitfield. Each bit indicates the input pin state, where
// triggered is 1 and not triggered is 0. Invert mask is applied. Bitfield organization is
// defined by the ControlPin in System.h.
ControlPins system_control_get_state() {
    ControlPins defined_pins;
    defined_pins.value = 0;

    ControlPins pin_states;
    pin_states.value = 0;

    defined_pins.bit.safetyDoor = ControlSafetyDoorPin->get() != Pin::UNDEFINED;
    if (ControlSafetyDoorPin->get().read()) {
        pin_states.bit.safetyDoor = true;
    }

    defined_pins.bit.reset = ControlResetPin->get() != Pin::UNDEFINED;
    if (ControlResetPin->get().read()) {
        pin_states.bit.reset = true;
    }

    defined_pins.bit.feedHold = ControlFeedHoldPin->get() != Pin::UNDEFINED;
    if (ControlFeedHoldPin->get().read()) {
        pin_states.bit.feedHold = true;
    }

    defined_pins.bit.cycleStart = ControlCycleStartPin->get() != Pin::UNDEFINED;
    if (ControlCycleStartPin->get().read()) {
        pin_states.bit.cycleStart = true;
    }

    defined_pins.bit.macro0 = MacroButton0Pin->get() != Pin::UNDEFINED;
    if (MacroButton0Pin->get().read()) {
        pin_states.bit.macro0 = true;
    }

    defined_pins.bit.macro1 = MacroButton1Pin->get() != Pin::UNDEFINED;
    if (MacroButton1Pin->get().read()) {
        pin_states.bit.macro1 = true;
    }

    defined_pins.bit.macro2 = MacroButton2Pin->get() != Pin::UNDEFINED;
    if (MacroButton2Pin->get().read()) {
        pin_states.bit.macro2 = true;
    }

    defined_pins.bit.macro3 = MacroButton3Pin->get() != Pin::UNDEFINED;
    if (MacroButton3Pin->get().read()) {
        pin_states.bit.macro3 = true;
    }

#ifdef INVERT_CONTROL_PIN_MASK
    pin_states.value ^= (INVERT_CONTROL_PIN_MASK & defined_pins.value);
#endif
    return pin_states;
}

// execute the function of the control pin
void system_exec_control_pin(ControlPins pins) {
    if (pins.bit.reset) {
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "Reset via control pin");
        mc_reset();
    } else if (pins.bit.cycleStart) {
        sys_rt_exec_state.bit.cycleStart = true;
    } else if (pins.bit.feedHold) {
        sys_rt_exec_state.bit.feedHold = true;
    } else if (pins.bit.safetyDoor) {
        sys_rt_exec_state.bit.safetyDoor = true;
    } else if (pins.bit.macro0) {
        user_defined_macro(0);  // function must be implemented by user
    } else if (pins.bit.macro1) {
        user_defined_macro(1);  // function must be implemented by user
    } else if (pins.bit.macro2) {
        user_defined_macro(2);  // function must be implemented by user
    } else if (pins.bit.macro3) {
        user_defined_macro(3);  // function must be implemented by user
    }
}

// CoreXY calculation only. Returns x or y-axis "steps" based on CoreXY motor steps.
int32_t system_convert_corexy_to_x_axis_steps(int32_t* steps) {
    return (steps[A_MOTOR] + steps[B_MOTOR]) / 2;
}

int32_t system_convert_corexy_to_y_axis_steps(int32_t* steps) {
    return (steps[A_MOTOR] - steps[B_MOTOR]) / 2;
}

// io_num is the virtual pin# and has nothing to do with the actual esp32 GPIO_NUM_xx
// It uses a mask so all can be turned of in ms_reset
bool sys_io_control(uint8_t io_num_mask, bool turnOn, bool synchronized) {
    bool cmd_ok = true;
    if (synchronized)
        protocol_buffer_synchronize();

    for (uint8_t io_num = 0; io_num < MaxUserDigitalPin; io_num++) {
        if (io_num_mask & bit(io_num)) {
            if (!myDigitalOutputs[io_num]->set_level(turnOn))
                cmd_ok = false;
        }
    }
    return cmd_ok;
}

// io_num is the virtual pin# and has nothing to do with the actual esp32 GPIO_NUM_xx
// It uses a mask so all can be turned of in ms_reset
bool sys_pwm_control(uint8_t io_num_mask, float duty, bool synchronized) {
    bool cmd_ok = true;
    if (synchronized)
        protocol_buffer_synchronize();

    for (uint8_t io_num = 0; io_num < MaxUserDigitalPin; io_num++) {
        if (io_num_mask & bit(io_num)) {
            if (!myAnalogOutputs[io_num]->set_level(duty))
                cmd_ok = false;
        }
    }
    return cmd_ok;
}

/*
    This returns an unused pwm channel.
    The 8 channels share 4 timers, so pairs 0,1 & 2,3 , etc
    have to be the same frequency. The spindle always uses channel 0
    so we start counting from 2.

    There are still possible issues if requested channels use different frequencies
    TODO: Make this more robust.
*/
int8_t sys_get_next_PWM_chan_num() {
    static uint8_t next_PWM_chan_num = 2;  // start at 2 to avoid spindle
    if (next_PWM_chan_num < 8) {           // 7 is the max PWM channel number
        return next_PWM_chan_num++;
    } else {
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Error, "Error: out of PWM channels");
        return -1;
    }
}

/*
		Calculate the highest precision of a PWM based on the frequency in bits

		80,000,000 / freq = period
		determine the highest precision where (1 << precision) < period
	*/
uint8_t sys_calc_pwm_precision(uint32_t freq) {
    uint8_t precision = 0;

    // increase the precision (bits) until it exceeds allow by frequency the max or is 16
    while ((1 << precision) < (uint32_t)(80000000 / freq) && precision <= 16) {  // TODO is there a named value for the 80MHz?
        precision++;
    }

    return precision - 1;
}
void __attribute__((weak)) user_defined_macro(uint8_t index);