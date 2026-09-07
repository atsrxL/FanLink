# Hardware questions for the owner

These questions do not reopen the FanLink functional requirements. Product
models are now known; the unanswered rows below are the minimum inputs needed
to turn the compiled profiles into exact flashable board targets.

## TX board

- Brand and exact model: JHEMCU 2.4GHz Lite (official target is RX hardware,
  repurposed here as FanLink TX)
- PCB revision/markings:
- Clear front/back photos:
- Original ExpressLRS target name or firmware target:
- MCU and flash size:
- Radio IC (SX1280/SX1281/SX127x/other):
- USB/UART wiring and boot method:
- PA/LNA/antenna switch/TCXO details:
- Intended power source:

## RX board(s)

- Brand and exact model: BETAFPV 2.4GHz Nano RX
- PCB revision/markings:
- Clear front/back photos:
- Original ExpressLRS target or `hardware.json`:
- MCU and radio IC:
- Which labelled pad is original TX/GPIO1:
- Which labelled pad is original RX/GPIO3:
- Any components already connected to those pads:
- Flash/recovery pads and procedure:
- PA/LNA/RF switch/TCXO details:

## Fan interface

- Fan brand/model and maximum current:
- Confirm standard four-pin order or provide datasheet:
- External N-MOS/NPN part and resistor values:
- Measured fan Pin 4 idle voltage:
- Measured fan Pin 3 Tach high voltage:
- Whether Tach is shared with another controller:
- Required minimum duty/stop behavior:
- Expected maximum RPM and pulses per revolution:

## Deployment

- Country/region of operation:
- Indoor enclosure or open environment:
- Maximum TX-to-RX distance:
- Metal obstruction/enclosure details:
- Number of simultaneously active nodes:
- Required control update and RPM freshness targets:
- Independent over-temperature shutdown available:

Codex should ask these in one consolidated message only when hardware adaptation begins.
