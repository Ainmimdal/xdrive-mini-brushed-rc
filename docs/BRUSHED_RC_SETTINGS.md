# MKS XDrive Mini firmware: non-standard config options

This fork of [Odrive-Wheel](https://github.com/eagabriel/Odrive-Wheel) (ODrive v0.5.6 for the MKS XDrive Mini) adds brushed DC motor support and RC PWM control. This page lists every setting that differs from stock ODrive v0.5.6.

## How to reach these settings

This firmware adds 7 settings and 1 motor type that stock ODrive v0.5.6 doesn't have, and ships about 25 defaults that differ from stock. Everything else behaves like stock v0.5.6.

| Route | Works on this build | How |
| --- | --- | --- |
| ODrive ASCII over USB serial | Yes | Open the board's COM port in any serial terminal or the Odrive-Wheel web tool. Read with `r <path>`, write with `w <path> <value>`, save with `ss`, reboot with `sr`. |
| Odrive-Wheel commands over the same port | Yes | Commands in the form `sys.vbusdiv?` (read) or `sys.vbusdiv=19` (write). Save with `sys.save!`. |
| odrivetool | No, not over USB | The USB descriptor only exposes HID and a serial port, not ODrive's native interface, so odrivetool can't find the board. UART_A is also off because GPIO1 and GPIO2 are used for RC PWM. The odrivetool lines below are the Python form of the same setting, for reference or if you re-enable native USB. |
| CAN (CANSimple) | Partly | v0.5.6 CANSimple only carries runtime commands (state, modes, inputs, limits). It cannot read or write any config option on this page. See the CAN section. |

Every `w` change is lost on reboot unless you run `ss` first. Settings marked "reboot" only take effect after `ss` then `sr`.

## Brushed DC motor

Set `motor_type` to 4 to drive a two-wire brushed motor connected between motor outputs B and C. Phase A stays switched off. The motor can't measure its own resistance or inductance, so you enter them, and motor calibration only checks that they are set.

| Setting | Value for brushed | ASCII (USB serial) | odrivetool |
| --- | --- | --- | --- |
| Motor type | 4 = BRUSHED_DC (0 = high current, 2 = gimbal, 3 = ACIM) | `w axis0.motor.config.motor_type 4` | `odrv0.axis0.motor.config.motor_type = 4` |
| Resistance, terminal to terminal | Ohms, must be above 0 | `w axis0.motor.config.phase_resistance 0.8` | `odrv0.axis0.motor.config.phase_resistance = 0.8` |
| Inductance, terminal to terminal | Henries, must be above 0 | `w axis0.motor.config.phase_inductance 0.0005` | `odrv0.axis0.motor.config.phase_inductance = 0.0005` |
| Torque constant | N·m/A, must be above 0 | `w axis0.motor.config.torque_constant 0.05` | `odrv0.axis0.motor.config.torque_constant = 0.05` |
| Check the values | Sets the motor as calibrated, or raises an error naming the missing value | `w axis0.requested_state 4` | `odrv0.axis0.requested_state = 4` |
| Skip the check at boot | True after a successful check | `w axis0.motor.config.pre_calibrated 1` | `odrv0.axis0.motor.config.pre_calibrated = True` |

The numbers in the commands are examples; use your motor's values. Changing `motor_type` disarms the motor immediately.

Without an encoder, only torque control with input mode passthrough works (`control_mode` 1, `input_mode` 1, and `encoder.config.direction` 0). With an encoder, velocity and position control work too. Set the encoder direction by hand, because the automatic direction find, offset calibration and index search can't run on a brushed motor:

1. Set `w axis0.encoder.config.direction 1`, `w axis0.controller.config.control_mode 1` and `w axis0.requested_state 8`.
2. Send a small torque, for example `w axis0.controller.input_torque 0.05`.
3. Read `r axis0.encoder.vel_estimate`. If it is negative, set `w axis0.encoder.config.direction -1`.
4. Save with `ss`.

## RC PWM input on GPIO1

GPIO1 reads a standard RC servo signal (1 to 2 ms pulses) and turns it into a command for axis0. GPIO1 ships in PWM mode, so you only need to pick the target and the range. All of these live under `config.`; add `odrv0.` in front for odrivetool.

| Setting | Default | What it does | ASCII (USB serial) |
| --- | --- | --- | --- |
| `rc_pwm_target` (reboot) | 0 | What the signal controls. 0 = whatever `gpio1_pwm_mapping.endpoint` points to (needs odrivetool). 1 = axis0 `input_torque`, 2 = `input_vel`, 3 = `input_pos`. | `w config.rc_pwm_target 2` |
| `gpio1_pwm_mapping.min` (stock) | 0 | Value sent at a 1 ms pulse. | `w config.gpio1_pwm_mapping.min -5` |
| `gpio1_pwm_mapping.max` (stock) | 0 | Value sent at a 2 ms pulse. | `w config.gpio1_pwm_mapping.max 5` |
| `rc_pwm_direction_gpio` (reboot) | 0 = off | GPIO whose level flips the sign of the value. GPIO2 is ready as a pulled-down input, so 2 is the usual choice. | `w config.rc_pwm_direction_gpio 2` |
| `rc_pwm_direction_active_high` | 1 | 1: pin high gives the positive value, pin low the negative. 0 reverses that. | `w config.rc_pwm_direction_active_high 0` |
| `rc_pwm_timeout_ms` | 100 | Time without a valid pulse before the failsafe acts. The failsafe sets torque or velocity to 0, and holds the last position in position control. 0 turns the failsafe off. | `w config.rc_pwm_timeout_ms 200` |
| `rc_pwm_timeout_dynamic_brake` | 0 | 1: on signal loss, a brushed motor also shorts its terminals to brake, limited to `current_lim`. Ignored for other motor types. | `w config.rc_pwm_timeout_dynamic_brake 1` |

The odrivetool form is the same path, for example `odrv0.config.rc_pwm_target = 2`. With target 0, odrivetool sets the endpoint like this: `odrv0.config.gpio1_pwm_mapping.endpoint = odrv0.axis0.controller._input_vel_property`.

For a direction pin, use a range that starts at 0 (for example min 0, max 5). With a range like -5 to 5, the pin flips a value that already has a sign.

Example: RC throttle as velocity, with direction on GPIO2:

```
w config.rc_pwm_target 2
w config.gpio1_pwm_mapping.min 0
w config.gpio1_pwm_mapping.max 5
w config.rc_pwm_direction_gpio 2
w axis0.controller.config.control_mode 2
ss
sr
```

## Other added settings and changed defaults

The overvoltage trip defaults to 30 V instead of 59.9 V. If your supply is above about 28 V, raise it before anything else, or the board faults as soon as it powers up.

Two more settings exist only in this firmware:

| Setting | Default | What it does | Command |
| --- | --- | --- | --- |
| `axis0.motor.config.current_control_deadband` | 0.1 A | Current error below this is ignored, which stops idle buzzing. 0 turns it off. | `w axis0.motor.config.current_control_deadband 0.05` (odrivetool: `odrv0.axis0.motor.config.current_control_deadband = 0.05`) |
| VBUS voltage divider | 19 | Ratio used to read the bus voltage, from 1 to 50. 19 is right for the MKS XDrive Mini, and 11 for an original ODrive v3.6 24 V. It is stored separately from the ODrive config. | `sys.vbusdiv=19` then `sys.save!` (read with `sys.vbusdiv?`). No odrivetool equivalent. |

The defaults below differ from stock v0.5.6. They only apply after flashing or after erasing the config (`se`); saved values win. Change any of them with `w <path> <value>`, or `odrv0.<path> = <value>` in odrivetool.

| Setting | Stock v0.5.6 | This firmware |
| --- | --- | --- |
| `config.dc_bus_overvoltage_trip_level` | 59.9 V | 30 V |
| `config.dc_bus_undervoltage_trip_level` | 8 V | 4 V |
| `config.enable_brake_resistor` | 0 | 1 |
| `config.brake_resistance` | 2 Ω (56 V board) | 2 Ω |
| `config.dc_max_negative_current` | -0.01 A | -0.1 A |
| `config.enable_uart_a` | 1 | 0 (GPIO1 and GPIO2 are used for RC instead) |
| `config.gpio1_mode` | UART_A | PWM (10) |
| `config.gpio2_mode` | UART_A | digital input with pull-down (2) |
| `axis0.motor.config.current_lim` | 10 A | 20 A |
| `axis0.motor.config.calibration_current` | 10 A | 3 A |
| `axis0.motor.config.resistance_calib_max_voltage` | 2 V | 8 V |
| `axis0.motor.config.current_control_bandwidth` | 1000 rad/s | 200 rad/s |
| `axis0.motor.config.phase_resistance` | 0 | 0.536 Ω |
| `axis0.motor.config.phase_inductance` | 0 | 0.001236 H |
| `axis0.motor.config.torque_constant` | 0.04 N·m/A | 0.87 N·m/A |
| `axis0.controller.config.control_mode` | 3 (position) | 1 (torque) |
| `axis0.controller.config.pos_gain` | 20 | 100 |
| `axis0.controller.config.vel_gain` | 0.167 | 0.566 |
| `axis0.controller.config.vel_integrator_gain` | 0.333 | 1.33 |
| `axis0.controller.config.vel_limit` | 2 turn/s | 5 turn/s |
| `axis0.controller.config.enable_torque_mode_vel_limit` | 1 | 0 |
| `axis0.controller.config.spinout_electrical_power_threshold` | 10 W | 50 W |
| `axis0.controller.config.spinout_mechanical_power_threshold` | -10 W | -50 W |
| `axis0.encoder.config.bandwidth` | 1000 rad/s | 200 rad/s |
| `axis0.encoder.config.phase_offset_float` | 0 | 0.174 |

The motor resistance, inductance, torque constant and encoder offset defaults were measured on the Odrive-Wheel author's own motor. Replace them with your motor's values; for a brushed motor these are the numbers you enter by hand.

## CAN bus

None of the settings on this page can be read or written over CAN. CANSimple in v0.5.6 has no generic parameter access; that arrived in v0.6. Configure over USB, save, then use CAN only for the runtime commands below.

CAN is on by default at the stock 250 kbit/s, with axis0 at node ID 0. Each frame's ID is `node_id × 32 + command`. Floats are little-endian 32-bit; byte offsets start at 0.

| Command | ID (node 0) | Data | Use |
| --- | --- | --- | --- |
| Set axis state | 0x007 | bytes 0–3: state (int32). 1 = idle, 4 = motor calibration, 8 = closed loop | Arm and disarm, run the brushed value check |
| Set controller modes | 0x00B | bytes 0–3: control mode, bytes 4–7: input mode (int32). Control 1 = torque, 2 = velocity, 3 = position; input 1 = passthrough | Switch between torque, velocity and position |
| Set input position | 0x00C | bytes 0–3: position in turns (float), bytes 4–5: velocity FF (int16, ×0.001 turn/s), bytes 6–7: torque FF (int16, ×0.001 N·m) | Position command |
| Set input velocity | 0x00D | bytes 0–3: turn/s (float), bytes 4–7: torque FF in N·m (float) | Velocity command |
| Set input torque | 0x00E | bytes 0–3: N·m (float) | Torque command. For a brushed motor, current = torque ÷ `torque_constant`. |
| Set limits | 0x00F | bytes 0–3: `vel_limit` (float), bytes 4–7: `current_lim` (float) | Only motor config value reachable over CAN |
| Clear errors | 0x018 | none | Clears axis, motor and encoder errors |
| Estop | 0x002 | none | Disarms with an estop error |
| Reboot | 0x016 | none | Reboots the board |
| Get encoder estimates | 0x009 (RTR) | reply bytes 0–3: position, bytes 4–7: velocity (float) | Read position and speed |
| Get Iq | 0x014 (RTR) | reply bytes 0–3: setpoint, bytes 4–7: measured (float) | Read motor current. For a brushed motor, measured Iq is the armature current. |
| Get bus voltage and current | 0x017 (RTR) | reply bytes 0–3: volts, bytes 4–7: amps (float) | Supply monitoring |

The CAN settings are stock and set over USB: `w axis0.config.can.node_id 0`, `w can.config.baud_rate 250000` (odrivetool: `odrv0.axis0.config.can.node_id = 0`).

Example: closed loop, then 0.2 N·m, using Linux `cansend`:

```
cansend can0 007#08000000
cansend can0 00E#CDCC4C3E
```

An RC signal and CAN both write the same input. Whichever arrives last wins, so use one or the other: set `rc_pwm_target` to 0 when commanding over CAN.

## Build-time switches

These live in `Odrive-Wheel/Makefile` and need a rebuild and reflash to change.

| Switch | Default | Effect |
| --- | --- | --- |
| `FFB_MOTOR_OUTPUT` | 0 | 0: the force-feedback code never sends torque to the motor, so RC, USB and CAN own `input_torque`. 1: the original wheel behaviour, where FFB overwrites `input_torque` every millisecond. Set it on the command line with `make FFB_MOTOR_OUTPUT=1`. |
| `HW_VERSION_MAJOR`, `_MINOR`, `_VOLTAGE` | 3, 6, 56 | Selects the ODrive v3.6 56 V board layout, which the MKS XDrive Mini copies. Leave as is. |

Adding or removing a setting changes the stored config layout. The first boot after flashing such a build resets every ODrive setting to the defaults above, so note your values first. The VBUS divider and other Odrive-Wheel `sys.` settings are stored separately and survive.
