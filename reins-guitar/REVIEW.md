# PCB design review: REINS guitar board

Reviewed: 2026-11-06, working copy of `reins-guitar.kicad_pcb` and `reins-guitar.kicad_sch` (rev A silkscreen).
Files: `reins-guitar.kicad_sch`, `reins-guitar.kicad_pcb`, `reins-guitar.kicad_pro`, `fab/jlc/reins-guitar/`, `README.md`, `../code/tof_midi_v5/tof_midi_v5.ino`, `REINS-JLC-order/ORDER_JLCPCB.md`.
Intent: a two-layer JLCPCB board for the guitar with a XIAO ESP32-S3, OLED, encoder, button, ToF sensor, 16 LED ring and a LiPo. It fits the board cavity of a Squier Mustang.

## Summary

The review found no blockers, 5 points to fix (the SHOULD-FIX list) and 6 nits. The boards are already made, so every action below is a check, a wiring choice, hand rework or a firmware change. The copper is sound: all 16 nets with two or more pads connect, and the pin map matches the firmware.

## Power tree

- 5V: USB VBUS from XIAO pin 14, through D1 to LED_PWR.
- VBAT: J2 pin 2, through the switch on J5, to VBAT_SW.
- VBAT_SW: goes to XIAO BAT+ by a wire from TP1, through D2 to LED_PWR, and through the R4/R5 divider to D0. C4 filters the divider output.
- LED_PWR: J3 pin 1 and U1 VCCB. C2 (100 nF) and C3 (22 uF) decouple it.
- 3V3: from XIAO pin 12 to J1, J4 and U1 VCCA. C1 and C5 (100 nF) decouple it.
- GND: pours on both layers, with 71 GND vias.

## Findings

### BLOCKER

None.

### SHOULD-FIX

1. Look at your boards before you pay for the small parts. `ORDER_JLCPCB.md` orders PCB Assembly for this board, and JLCPCB solders C1 to C5, R1, R4, R5, D1, D2 and U1. If those parts are on your boards, the same parts in the radioduo.fi cart are spares (about 12 EUR of the cart). If the pads are empty, keep them.

2. The battery input has no reverse polarity protection. J2 goes directly to the switch, D2 and the XIAO BAT+ pad. `README.md` says that a reversed battery destroys the XIAO. J2 pin 1 is minus, and the bottom silkscreen is correct. Measure the battery plug with a multimeter before the first connection, because many LiPo cells have the wires the other way round.

3. J1 does not use the Qwiic pin order. J1 is GND, 3V3, SCL, SDA. A Qwiic cable is GND, 3V3, SDA, SCL. If you make the ToF cable from a Qwiic cable, or wire by colour from a cable at the Fab Lab, swap SDA and SCL. The firmware cannot fix this, because J4 shares the two lines. The silkscreen next to J1 is correct, so follow it.

4. The LED ring can pull more than the path can carry. At full white, 16 LEDs pull about 960 mA (60 mA each is the usual WS2812 figure, not a measurement on your ring). D1 and D2 are 1 A parts, and the firmware lets the brightness go to 255. The default of 76 pulls about 290 mA. A small LiPo or the XIAO's internal 5V path can limit before the diodes do. I did not check either limit. Cap the brightness in the firmware (`stripBright`, range in the `SETTINGS` table), at least on battery.

5. The XIAO antenna has no planned place. Both body cavities are lined with copper foil (seen in the body photo), and `README.md` says to keep the antenna outside any shield. The pickguard model does not show the antenna or a path for its cable. Test the range to the receiver with the plate on, before you fix the antenna in place.

### NIT

1. TP1 has a 0.8 mm hole, and a 0.64 mm square header pin is about 0.9 mm across its corners. Solder a wire into TP1, and put the pin on the wire end. `pickguard/README.md` now says this.

2. The M2 screw head at H2 sits over a bottom side track. The VBAT_SW track on B.Cu is 1.71 mm from the hole centre, and a 3.8 mm head reaches 1.9 mm. Only solder mask lies between them, and the GND pour is under the same head. Do not tighten hard, or use a nylon washer.

3. The H3 boss comes close to the socket pad M1 pin 12: the pad edge is 2.17 mm from the hole centre, and the boss radius is 2.2 mm. The boss stands 0.1 mm above the copper, so a tall solder joint can lift the plate. If the board rocks, keep this joint low or file the boss.

4. The battery reading can be wrong with the switch off and USB in. TP1 is on the switched side, so the XIAO charger output stays on VBAT_SW, and R4/R5 still read it. The firmware only treats readings under 2.5 V as "no battery", so the OLED can show a charge. I did not test this. Check it on the first board.

5. The board has no I2C pull-ups, so it depends on those on the OLED and ToF modules. The firmware runs at 400 kHz, and the ToF cable is up to 20 cm long. If the sensor drops out, lower `I2C_FREQ` first.

6. Part names differ from the parts that you buy. The schematic says S1 is PEC12R-4015F-S0024, and the pickguard uses PEC12R-4225F-S0024, with the same footprint. The schematic says D1 and D2 are B5819W, and the cart has B140HW-7 (40 V, 1 A, SOD-123). I did not read the B140HW-7 drawing. Put its band on the LED_PWR side (pad 1).

## Checked with no finding

- Net connectivity: every pad of every net reaches the others through tracks, vias or the pours. The through-hole GND pads have thermal spokes on both layers. The small F.Cu pour piece under the XIAO has two vias to the rest of GND.
- XIAO pins: the footprint pins and the firmware pin map agree for all used pins. D2, D6 and D7 are not connected, and the XIAO exposes no boot pin that this board loads.
- U1: VCCA 3V3, DIR 3V3, so the data direction is A to B. A is D10, and B goes through R1 to J3.
- Diodes: D1 and D2 stop the battery feeding the USB 5 V, and stop USB feeding the battery directly.
- Bottom silkscreen: the labels at J1, J2 and J3 match their nets.
- Fab rules: 0.3 mm minimum track in use, 0.6/0.3 mm vias, 0.2 mm clearance rule.

## Not reviewed / out of scope

- I did not run the KiCad ERC or DRC, so clearance is checked only by the rules in the project file. To get them, run both in KiCad.
- I did not inspect the real boards or compare the ordered Gerbers with this file. The Gerbers in `fab/` are newer than the layout file.
- I did not open datasheets in this review. Footprints come from the earlier check against the Bourns, Omron, JST and Harwin drawings.
- I did not check the height of the DS1023-16 strip or the M20-7820446 socket against the pickguard stack. `pickguard/README.md` says to measure them.
- I did not review the receiver boards. Hum from the wires and the LED data near the pickups needs a test on the guitar.
