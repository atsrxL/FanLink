# BETAFPV Nano 2.4GHz RX fan firmware

This custom build keeps ExpressLRS 4.1.0's existing PWM mode value `6`, but
changes that mode from 10 kHz push-pull duty output to a 25 kHz open-drain fan
control signal on ESP8285 receivers.

The bundled hardware layout is based on the official `Generic 2400 PA` target,
with UART0 disabled and GPIO1 exposed as the sole PWM output. The receiver's
existing binding UID and non-secret firmware options are copied from the user's
matching 4.1.0 BETAFPV Nano build during artifact generation; the binding phrase
itself is never stored here.

After flashing, use this output mapping:

- Output Channel: `1`
- Input Channel: `1`
- Mode: `25kHzFan` (mode 6; an old saved UI may still call it `10kHzDuty`)
- Inverted: off

For thermal safety, set the output failsafe to 100% duty if the fan should run at
full speed when the RF link is lost.

## Electrical safety

Power the fan from its rated supply and connect the fan and receiver grounds.
Before connecting the control wire directly to GPIO1, disconnect it from the
receiver, power the fan, and measure that wire to ground. Some four-wire fans
pull the PWM input up to about 5 V, while the ESP8285 GPIO is not 5 V tolerant.
If the released control line exceeds 3.3 V, use an external NPN transistor or
3.3 V-safe open-collector/open-drain level shifter between GPIO1 and the fan.
