# PIC16F15313 dual slow-PWM example

This repository contains an XC8 firmware example for a **PIC16F15313** that implements two continuously running, low-frequency PWM-style output channels.

## Implemented behavior

- `AN0` sets the **ON time** for both channels from **0.2 s to 4 s**.
- `AN1` sets the **OFF time** for both channels from **1 minute to 30 minutes**.
- `RA2` is the trigger input for channel 1.
- `RA4` is the trigger input for channel 2.
- `RC0` is the output for channel 1.
- `RC1` is the output for channel 2.
- Each trigger input immediately restarts its own channel in the ON state.
- Initial startup times are set in code with:
  - `INITIAL_ON_TIME_MS`
  - `INITIAL_OFF_TIME_MS`

## Notes

- The firmware uses a **10 ms Timer0 scheduler tick**, which is intentionally simple because the requested timing does not need high accuracy.
- The two channels share the same analog-controlled ON and OFF durations, but each output has its own independent trigger input and runtime state machine.
- Trigger inputs are configured with weak pull-ups and are treated as **active low**.
- This is a software-generated slow PWM, not a hardware CCP PWM, because the requested pulse periods extend to many minutes.
