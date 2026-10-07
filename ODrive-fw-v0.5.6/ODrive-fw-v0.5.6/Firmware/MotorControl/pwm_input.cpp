
#include "pwm_input.hpp"
#include "odrive_main.h"

static bool is_digital_input_mode(ODriveIntf::GpioMode mode) {
    return mode == ODriveIntf::GPIO_MODE_DIGITAL
        || mode == ODriveIntf::GPIO_MODE_DIGITAL_PULL_UP
        || mode == ODriveIntf::GPIO_MODE_DIGITAL_PULL_DOWN;
}

// Writes an RC value to the configured target. Channels 1-3 and target 0 use
// the generic fibre endpoint; targets 1-3 write axis0's inputs directly so the
// mapping can be configured over ASCII (endpoint refs can't be set there).
static bool write_target(int channel, float value) {
    if (channel == 0 && odrv.config_.rc_pwm_target != 0) {
        Controller& ctrl = odrv.get_axis(0).controller_;
        switch (odrv.config_.rc_pwm_target) {
            case 1: ctrl.input_torque_ = value; return true;
            case 2: ctrl.input_vel_ = value; return true;
            case 3: ctrl.set_input_pos(value); return true;
            default: return false;
        }
    }
    return fibre::set_endpoint_from_float(odrv.config_.pwm_mappings[channel].endpoint, value);
}

static bool has_target(int channel) {
    if (channel == 0 && odrv.config_.rc_pwm_target != 0)
        return odrv.config_.rc_pwm_target <= 3;
    return fibre::is_endpoint_ref_valid(odrv.config_.pwm_mappings[channel].endpoint);
}

void PwmInput::init() {
    TIM_IC_InitTypeDef sConfigIC;
    sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_BOTHEDGE;
    sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
    sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
    sConfigIC.ICFilter = 15;

    uint32_t channels[] = {TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3, TIM_CHANNEL_4};

    for (size_t i = 0; i < 4; ++i) {
        Stm32Gpio input_gpio = get_gpio(gpios_[i]);
        if (!input_gpio)
            continue;
        if (i == 0 && odrv.config_.rc_pwm_target > 3) {
            odrv.misconfigured_ = true;
            continue;
        }
        if (!has_target(i))
            continue;
        if (odrv.config_.gpio_modes[gpios_[i]] != ODriveIntf::GPIO_MODE_PWM) {
            odrv.misconfigured_ = true;
            continue;
        }
        if (i == 0 && odrv.config_.rc_pwm_direction_gpio != 0) {
            uint16_t direction_gpio = odrv.config_.rc_pwm_direction_gpio;
            if (direction_gpio >= GPIO_COUNT
                    || !get_gpio(direction_gpio)
                    || !is_digital_input_mode(odrv.config_.gpio_modes[direction_gpio])) {
                odrv.misconfigured_ = true;
                continue;
            }
        }
        if (HAL_TIM_IC_ConfigChannel(htim_, &sConfigIC, channels[i]) != HAL_OK
                || HAL_TIM_IC_Start_IT(htim_, channels[i]) != HAL_OK) {
            odrv.misconfigured_ = true;
            continue;
        }
        channel_active_[i] = true;
        last_valid_pulse_ms_[i] = HAL_GetTick();
        timeout_applied_[i] = false;
    }
}

void PwmInput::update() {
    uint32_t timeout_ms = odrv.config_.rc_pwm_timeout_ms;
    if (!channel_active_[0] || timeout_ms == 0) {
        odrv.get_axis(0).motor_.set_dynamic_brake(false);
        return;
    }

    if (timeout_applied_[0]) {
        // Mirror runtime changes to the option while the signal is absent.
        odrv.get_axis(0).motor_.set_dynamic_brake(
                odrv.config_.rc_pwm_timeout_dynamic_brake);
        return;
    }

    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_valid_pulse_ms_[0]) < timeout_ms)
        return;

    // Zero means "stop" for velocity and torque commands, but for a position
    // command it would drive the axis to position 0. In position control the
    // last commanded position is held instead.
    if (odrv.get_axis(0).controller_.config_.control_mode
            != Controller::CONTROL_MODE_POSITION_CONTROL) {
        if (!write_target(0, 0.0f))
            odrv.misconfigured_ = true;
    }
    odrv.get_axis(0).motor_.set_dynamic_brake(
            odrv.config_.rc_pwm_timeout_dynamic_brake);
    timeout_applied_[0] = true;
}

//TODO: These expressions have integer division by 1MHz, so it will be incorrect for clock speeds of not-integer MHz
#define TIM_2_5_CLOCK_HZ        TIM_APB1_CLOCK_HZ
#define PWM_MIN_HIGH_TIME          ((TIM_2_5_CLOCK_HZ / 1000000UL) * 1000UL) // 1ms high is considered full reverse
#define PWM_MAX_HIGH_TIME          ((TIM_2_5_CLOCK_HZ / 1000000UL) * 2000UL) // 2ms high is considered full forward
#define PWM_MIN_LEGAL_HIGH_TIME    ((TIM_2_5_CLOCK_HZ / 1000000UL) * 500UL) // ignore high periods shorter than 0.5ms
#define PWM_MAX_LEGAL_HIGH_TIME    ((TIM_2_5_CLOCK_HZ / 1000000UL) * 2500UL) // ignore high periods longer than 2.5ms
#define PWM_INVERT_INPUT        false

/**
 * @param channel: A channel number in [0, 3]
 */
void PwmInput::handle_pulse(int channel, uint32_t high_time) {
    if (high_time < PWM_MIN_LEGAL_HIGH_TIME || high_time > PWM_MAX_LEGAL_HIGH_TIME)
        return;

    if (high_time < PWM_MIN_HIGH_TIME)
        high_time = PWM_MIN_HIGH_TIME;
    if (high_time > PWM_MAX_HIGH_TIME)
        high_time = PWM_MAX_HIGH_TIME;
    float fraction = (float)(high_time - PWM_MIN_HIGH_TIME) / (float)(PWM_MAX_HIGH_TIME - PWM_MIN_HIGH_TIME);
    float value = odrv.config_.pwm_mappings[channel].min +
                  (fraction * (odrv.config_.pwm_mappings[channel].max - odrv.config_.pwm_mappings[channel].min));

    if (channel == 0 && odrv.config_.rc_pwm_direction_gpio != 0) {
        uint16_t direction_gpio_num = odrv.config_.rc_pwm_direction_gpio;
        if (direction_gpio_num >= GPIO_COUNT
                || !get_gpio(direction_gpio_num)
                || !is_digital_input_mode(odrv.config_.gpio_modes[direction_gpio_num])) {
            odrv.misconfigured_ = true;
            return;
        }
        bool direction_state = get_gpio(direction_gpio_num).read();
        if (direction_state != odrv.config_.rc_pwm_direction_active_high)
            value = -value;
    }

    if (write_target(channel, value)) {
        last_valid_pulse_ms_[channel] = HAL_GetTick();
        timeout_applied_[channel] = false;
        if (channel == 0)
            odrv.get_axis(0).motor_.set_dynamic_brake(false);
    }
}

/**
 * @param channel: A channel number in [0, 3]
 */
void PwmInput::on_capture(int channel, uint32_t timestamp) {
    static uint32_t last_timestamp[4] = { 0 };
    static bool last_pin_state[4] = { false };
    static bool last_sample_valid[4] = { false };

    if (channel >= 4)
        return;
    Stm32Gpio gpio = get_gpio(gpios_[channel]);
    if (!gpio)
        return;
    bool current_pin_state = gpio.read();

    if (last_sample_valid[channel]
        && (last_pin_state[channel] != PWM_INVERT_INPUT)
        && (current_pin_state == PWM_INVERT_INPUT)) {
        handle_pulse(channel, timestamp - last_timestamp[channel]);
    }

    last_timestamp[channel] = timestamp;
    last_pin_state[channel] = current_pin_state;
    last_sample_valid[channel] = true;
}

void PwmInput::on_capture() {
    if(__HAL_TIM_GET_FLAG(htim_, TIM_FLAG_CC1)) {
        __HAL_TIM_CLEAR_IT(htim_, TIM_IT_CC1);
        on_capture(0, htim_->Instance->CCR1);
    }
    if(__HAL_TIM_GET_FLAG(htim_, TIM_FLAG_CC2)) {
        __HAL_TIM_CLEAR_IT(htim_, TIM_IT_CC2);
        on_capture(1, htim_->Instance->CCR2);
    }
    if(__HAL_TIM_GET_FLAG(htim_, TIM_FLAG_CC3)) {
        __HAL_TIM_CLEAR_IT(htim_, TIM_IT_CC3);
        on_capture(2, htim_->Instance->CCR3);
    }
    if(__HAL_TIM_GET_FLAG(htim_, TIM_FLAG_CC4)) {
        __HAL_TIM_CLEAR_IT(htim_, TIM_IT_CC4);
        on_capture(3, htim_->Instance->CCR4);
    }
}
