#include "brushed_dc.hpp"
#include <board.h>

#include <algorithm>
#include <cmath>

namespace {
// Leaves at least 10% duty at each end of both active legs. Besides limiting
// overmodulation, this preserves a useful low-side-current sampling window.
constexpr float kMaxBrushedModulation = 0.80f;
}

void BrushedDCController::set_dynamic_brake(bool enabled) {
    CRITICAL_SECTION() {
        if (dynamic_brake_requested_ != enabled) {
            // Do not carry a motoring-current integrator value into or out of
            // the zero-voltage braking state.
            voltage_integral_ = 0.0f;
            dynamic_brake_requested_ = enabled;
        }
    }
}

void BrushedDCController::reset() {
    voltage_integral_ = 0.0f;
    vbus_voltage_ = std::nullopt;
    current_measured_ = std::nullopt;

    if (Id_measured_report_) {
        *Id_measured_report_ = 0.0f;
    }
    if (Iq_measured_report_) {
        *Iq_measured_report_ = 0.0f;
    }
    if (power_report_) {
        *power_report_ = 0.0f;
    }
}

ODriveIntf::MotorIntf::Error BrushedDCController::on_measurement(
        std::optional<float> vbus_voltage,
        std::optional<std::array<float, 3>> currents,
        uint32_t input_timestamp) {
    current_timestamp_ = input_timestamp;
    vbus_voltage_ = vbus_voltage;

    if (currents.has_value()) {
        // B and C are both measured directly on the MKS board. Averaging the
        // two opposite-signed readings rejects their residual common offset.
        current_measured_ = 0.5f * ((*currents)[1] - (*currents)[2]);
        if (Id_measured_report_) {
            *Id_measured_report_ = 0.0f;
        }
        if (Iq_measured_report_) {
            *Iq_measured_report_ = *current_measured_;
        }
    } else {
        current_measured_ = std::nullopt;
        if (Id_measured_report_) {
            *Id_measured_report_ = 0.0f;
        }
        if (Iq_measured_report_) {
            *Iq_measured_report_ = 0.0f;
        }
    }

    return ODriveIntf::MotorIntf::ERROR_NONE;
}

ODriveIntf::MotorIntf::Error BrushedDCController::get_output(
        uint32_t output_timestamp,
        float (&pwm_timings)[3],
        std::optional<float>* ibus) {
    using MotorIntf = ODriveIntf::MotorIntf;

    if (!vbus_voltage_.has_value() || !current_measured_.has_value()) {
        return MotorIntf::ERROR_CONTROLLER_INITIALIZING;
    } else if (abs((int32_t)(current_timestamp_ - ctrl_timestamp_)) >
               MAX_CONTROL_LOOP_UPDATE_TO_CURRENT_UPDATE_DELTA) {
        return MotorIntf::ERROR_BAD_TIMING;
    } else if (!pi_gains_.has_value()
               || !std::isfinite(pi_gains_->first) || !(pi_gains_->first > 0.0f)
               || !std::isfinite(pi_gains_->second) || pi_gains_->second < 0.0f) {
        return MotorIntf::ERROR_UNKNOWN_GAINS;
    } else if (!std::isfinite(*vbus_voltage_) || *vbus_voltage_ <= 0.0f) {
        return MotorIntf::ERROR_UNKNOWN_VBUS_VOLTAGE;
    } else if (!std::isfinite(*current_measured_)) {
        return MotorIntf::ERROR_UNKNOWN_CURRENT_MEASUREMENT;
    } else if (dynamic_brake_requested_) {
        if (!current_limit_ || !std::isfinite(*current_limit_)
                || *current_limit_ < 0.0f) {
            return MotorIntf::ERROR_UNKNOWN_CURRENT_COMMAND;
        }

        // Zero differential voltage shorts the motor through matched B/C leg
        // states. If generated armature current exceeds the normal motor
        // current limit, the same PI gains introduce opposing differential
        // voltage to regulate the current back toward the limit. The existing
        // hard current trip remains the final backup.
        const float current_limit = *current_limit_;
        const float measured_current = *current_measured_;
        float modulation = 0.0f;

        if (std::abs(measured_current) > current_limit) {
            const float current_setpoint = std::copysign(
                    current_limit, measured_current);
            const float current_error = current_setpoint - measured_current;
            auto [p_gain, i_gain] = *pi_gains_;
            float motor_voltage = voltage_integral_ + current_error * p_gain;
            modulation = motor_voltage / *vbus_voltage_;

            if (!std::isfinite(modulation)) {
                return MotorIntf::ERROR_MODULATION_IS_NAN;
            }

            const float limited_modulation = std::clamp(
                    modulation, -kMaxBrushedModulation,
                    kMaxBrushedModulation);
            if (limited_modulation != modulation) {
                voltage_integral_ *= 0.99f;
                modulation = limited_modulation;
            } else {
                voltage_integral_ += current_error * i_gain
                                   * current_meas_period;
            }
        } else {
            // Below the limit, enforce a true zero differential-voltage short.
            voltage_integral_ = 0.0f;
        }

        // Equal timings are the dynamic-brake state. If current limiting is
        // active, the differential offset reduces the excessive brake current.
        // Complementary outputs and timer deadtime remain enabled on both legs.
        pwm_timings[0] = 0.5f;
        pwm_timings[1] = 0.5f - 0.5f * modulation;
        pwm_timings[2] = 0.5f + 0.5f * modulation;

        *ibus = modulation * measured_current;
        if (power_report_) {
            *power_report_ = *vbus_voltage_ * ibus->value();
        }
        return MotorIntf::ERROR_NONE;
    } else if (!Idq_setpoint_.has_value()) {
        return MotorIntf::ERROR_UNKNOWN_CURRENT_COMMAND;
    } else if (!Vdq_setpoint_.has_value()) {
        return MotorIntf::ERROR_UNKNOWN_VOLTAGE_COMMAND;
    }

    const float current_setpoint = Idq_setpoint_->second;
    const float voltage_feedforward = Vdq_setpoint_->second;
    if (!std::isfinite(current_setpoint) || !std::isfinite(voltage_feedforward)) {
        return MotorIntf::ERROR_UNKNOWN_CURRENT_COMMAND;
    }

    auto [p_gain, i_gain] = *pi_gains_;
    float current_error = current_setpoint - *current_measured_;
    if (current_control_deadband_ > 0.0f &&
            std::abs(current_error) < current_control_deadband_) {
        current_error = 0.0f;
    }

    float motor_voltage = voltage_feedforward + voltage_integral_
                        + current_error * p_gain;
    float modulation = motor_voltage / *vbus_voltage_;

    if (!std::isfinite(modulation)) {
        return MotorIntf::ERROR_MODULATION_IS_NAN;
    }

    const float limited_modulation = std::clamp(
            modulation, -kMaxBrushedModulation, kMaxBrushedModulation);
    if (limited_modulation != modulation) {
        // Decay the integrator while voltage-limited to unwind saturation.
        voltage_integral_ *= 0.99f;
        modulation = limited_modulation;
    } else {
        voltage_integral_ += current_error * i_gain * current_meas_period;
    }

    // Locked-antiphase H-bridge. CH1/phase A is physically output-disabled.
    pwm_timings[0] = 0.5f;
    // TIM1/TIM8 use PWM mode 2, so a smaller compare value means more
    // high-side on-time. Positive modulation therefore drives B relative to C.
    pwm_timings[1] = 0.5f - 0.5f * modulation;
    pwm_timings[2] = 0.5f + 0.5f * modulation;

    // Average DC-link current follows Pbus = Vmotor * Imotor.
    *ibus = modulation * *current_measured_;
    if (power_report_) {
        *power_report_ = *vbus_voltage_ * ibus->value();
    }

    return MotorIntf::ERROR_NONE;
}

void BrushedDCController::update(uint32_t timestamp) {
    CRITICAL_SECTION() {
        ctrl_timestamp_ = timestamp;
        Idq_setpoint_ = Idq_setpoint_src_.present();
        Vdq_setpoint_ = Vdq_setpoint_src_.present();
    }
}
