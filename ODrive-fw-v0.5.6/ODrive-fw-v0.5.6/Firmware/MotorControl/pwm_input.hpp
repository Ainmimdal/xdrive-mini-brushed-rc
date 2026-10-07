#ifndef __PWM_INPUT_HPP
#define __PWM_INPUT_HPP

#include <tim.h>
#include <array>

class PwmInput {
public:
    PwmInput(TIM_HandleTypeDef* htim, std::array<uint16_t, 4> gpios)
            : htim_(htim), gpios_(gpios) {}

    void init();
    void update();
    void on_capture();

private:
    void handle_pulse(int channel, uint32_t high_time);
    void on_capture(int channel, uint32_t timestamp);

    TIM_HandleTypeDef* htim_;
    std::array<uint16_t, 4> gpios_;
    bool channel_active_[4] = {};
    volatile uint32_t last_valid_pulse_ms_[4] = {};
    volatile bool timeout_applied_[4] = {};
};

#endif // __PWM_INPUT_HPP
