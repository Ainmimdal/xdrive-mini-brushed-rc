#ifndef __BRUSHED_DC_HPP
#define __BRUSHED_DC_HPP

#include "phase_control_law.hpp"
#include "component.hpp"

/**
 * @brief Current controller for a two-terminal brushed DC motor.
 *
 * The motor must be connected between phase B and phase C. Phase A is disabled
 * by Motor::arm(). The two active legs use locked-antiphase PWM so the signed
 * differential motor voltage is modulation * vbus_voltage.
 */
class BrushedDCController : public PhaseControlLaw<3>, public ComponentBase {
public:
    void update(uint32_t timestamp) final;
    void reset() final;

    ODriveIntf::MotorIntf::Error on_measurement(
            std::optional<float> vbus_voltage,
            std::optional<std::array<float, 3>> currents,
            uint32_t input_timestamp) final;

    ODriveIntf::MotorIntf::Error get_output(
            uint32_t output_timestamp,
            float (&pwm_timings)[3],
            std::optional<float>* ibus) final;

    void set_dynamic_brake(bool enabled);

    // [V/A, V/(A*s)], calculated from terminal-to-terminal R and L.
    std::optional<float2D> pi_gains_;
    float current_control_deadband_ = 0.0f; // [A]

    InputPort<float2D> Idq_setpoint_src_;
    InputPort<float2D> Vdq_setpoint_src_;

    // Keep the existing public current-controller reports valid in brushed mode.
    float* Id_measured_report_ = nullptr;
    float* Iq_measured_report_ = nullptr;
    float* power_report_ = nullptr;
    float* current_limit_ = nullptr;

private:
    uint32_t ctrl_timestamp_ = 0;
    uint32_t current_timestamp_ = 0;
    std::optional<float2D> Idq_setpoint_;
    std::optional<float2D> Vdq_setpoint_;
    std::optional<float> vbus_voltage_;
    std::optional<float> current_measured_;
    float voltage_integral_ = 0.0f; // [V]
    volatile bool dynamic_brake_requested_ = false;
};

#endif // __BRUSHED_DC_HPP
