.. _rc-pwm-doc:

================================================================================
RC PWM input
================================================================================

.. contents::
   :depth: 1
   :local:
   
You can control the ODrive directly from a hobby RC receiver.

Any of the numerical parameters that are writable from the ODrive Tool can be hooked up to a PWM input. 
The :ref:`Pinout <pinout-chart>` tells you which pins are PWM input capable. As an example, we'll configure GPIO4 to control the angle of axis 0. 
We want the axis to move within a range of -2 to 2 turns.

#. Make sure you're able control the axis 0 angle by writing to :code:`odrv0.axis0.controller.input_pos`. 
   If you need help with this follow the :ref:`getting started guide <getting-started>`.
#. If you want to control your ODrive with the PWM input without using anything else to activate the ODrive, you can configure the ODrive such that axis 0 automatically goes operational at startup. 
   See :ref:`here <commands-startup-procedure>` for more information.
#. In ODrive Tool, configure the PWM input mapping

    .. code::  iPython
            
        odrv0.config.gpio4_mode = GPIO_MODE_PWM
        odrv0.config.gpio4_pwm_mapping.min = -2
        odrv0.config.gpio4_pwm_mapping.max = 2
        odrv0.config.gpio4_pwm_mapping.endpoint = odrv0.axis0.controller._input_pos_property

   .. note:: 
       
       you can disable the input by setting :code:`odrv0.config.gpio4_pwm_mapping.endpoint = None`

#. Save the configuration and reboot

    .. code:: iPython
            
        odrv0.save_configuration()
        odrv0.reboot()

#. With the ODrive powered off, connect the RC receiver ground to the ODrive's GND and one of the RC receiver signals to GPIO4. 
   You may try to power the receiver from the ODrive's 5V supply if it doesn't draw too much power. Power up the the RC transmitter. 
   You should now be able to control axis 0 from one of the RC sticks.

Be sure to setup the Failsafe feature on your RC Receiver so that if connection is lost between the remote and the receiver, the receiver outputs 0 for the velocity setpoint of both axes (or whatever is safest for your configuration).

The MKS XDrive Mini build uses GPIO1 as the RC PWM input and GPIO2 as a
direction input. GPIO1 is captured by TIM5 channel 1 (PA0); GPIO2 is read as
a digital input (PA1). A high GPIO2 level selects the positive mapped value by
default, and a low level selects the negative mapped value.

The MKS build also has a pulse-loss failsafe. If no valid GPIO1 pulse is
received for ``config.rc_pwm_timeout_ms`` milliseconds (100 ms by default),
the mapped endpoint is set to zero. Set that property to zero to disable the
timeout. This is a controlled zero-command stop; it does not hard-disarm the
axis. A receiver failsafe that sends a neutral pulse can still be used.

Encoderless brushed motor-driver mode
-------------------------------------

In ``MOTOR_TYPE_BRUSHED_DC``, axis 0 can run without an encoder when the
controller uses torque control with passthrough input. Connect the brushed
motor between phase B and phase C; phase A is disabled by the brushed motor
mode. Configure GPIO1's 1--2 ms pulse range as a non-negative torque command
and let GPIO2 select its sign::

    odrv0.axis0.motor.config.motor_type = MOTOR_TYPE_BRUSHED_DC
    odrv0.axis0.motor.config.phase_resistance = MOTOR_TERMINAL_RESISTANCE_OHM
    odrv0.axis0.motor.config.phase_inductance = MOTOR_TERMINAL_INDUCTANCE_H
    odrv0.axis0.motor.config.torque_constant = MOTOR_TORQUE_CONSTANT_NM_PER_A
    odrv0.axis0.motor.config.current_lim = MAX_ARMATURE_CURRENT_A
    odrv0.axis0.motor.config.pre_calibrated = True

    odrv0.axis0.controller.config.control_mode = CONTROL_MODE_TORQUE_CONTROL
    odrv0.axis0.controller.config.input_mode = INPUT_MODE_PASSTHROUGH
    odrv0.axis0.encoder.config.direction = 0

    odrv0.config.gpio1_mode = GPIO_MODE_PWM
    odrv0.config.gpio2_mode = GPIO_MODE_DIGITAL
    odrv0.config.rc_pwm_direction_gpio = 2
    odrv0.config.rc_pwm_direction_active_high = True
    odrv0.config.rc_pwm_timeout_ms = 100
    odrv0.config.rc_pwm_timeout_dynamic_brake = True
    odrv0.config.gpio1_pwm_mapping.min = 0.0
    odrv0.config.gpio1_pwm_mapping.max = MAX_COMMAND_TORQUE_NM
    odrv0.config.gpio1_pwm_mapping.endpoint = odrv0.axis0.controller._input_torque_property

    odrv0.axis0.config.startup_closed_loop_control = True
    odrv0.save_configuration()
    odrv0.reboot()

``MAX_COMMAND_TORQUE_NM`` should not exceed
``current_lim * torque_constant``. The firmware clamps the armature-current
request to ``current_lim`` regardless. With no encoder, velocity limiting,
overspeed detection, anti-cogging, back-EMF feed-forward, and spinout detection
are unavailable. Position and velocity control still require an encoder.

On pulse loss the torque endpoint is set to zero, so the current controller
drives armature current toward zero. This is a controlled zero-torque command,
not a hard disarm or a position-holding brake; a freely turning motor will
normally coast down mechanically.

To select dynamic braking on pulse loss, set
``config.rc_pwm_timeout_dynamic_brake = True``. The default is ``False``. In
dynamic-brake mode the B/C bridge first commands zero differential voltage,
which shorts the motor terminals through matched bridge states. If the measured
armature current exceeds ``motor.config.current_lim``, the current PI loop adds
opposing differential voltage to bring it back toward that limit. The existing
hard phase-current trip and thermal derating remain active as backups.

Phase A remains disabled. Dynamic braking dissipates energy mainly in the
motor winding and MOSFET path; it is distinct from the DC-bus brake resistor
and can heat the motor during repeated or externally driven braking. If the
current limiter has to apply opposing bridge voltage, some energy can return
to the DC bus, so the normal bus over-voltage protection and an appropriately
configured brake resistor are still required.
