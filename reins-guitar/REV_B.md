# REINS guitar board rev B: draft plan

This is a plan, not an edit. `reins-guitar.kicad_pcb` and `reins-guitar.kicad_sch` are unchanged. Source marks: [D] data sheet or vendor page, [M] measured from the files, [E] estimate, [R] rule of thumb or shop claim.

Goals: JLCPCB solders every SMD part, the ring has a wire tab you can reach, and a real footswitch works reliably.

## What needs a new board, and what does not

The first item is the big one. You can do it without touching the layout.

| # | Change | New board? | Hand soldering saved |
| --- | --- | --- | --- |
| 1 | JLCPCB places the two XIAO sockets | No, export script only | 14 joints and the DS1023-16 strip |
| 2 | JLCPCB places SW1 | No, export script only | 4 joints |
| 3 | Footswitch input with filter and ESD part | Yes | none, but it is the reliability fix |
| 4 | Reverse battery protection | Yes | none |
| 5 | Ring cable to the new ring tab | No | the NeoPixel pad joints are now easy |

## 1. XIAO sockets placed by JLCPCB (no layout change)

1. The guitar board uses the same `Module_XIAO_Generic_SocketSMD` footprint as the receiver. The 14 pads are identical in both libraries [M]. Both are 2 x 1 mm pads at x = 9.27 mm on both sides, 2.54 mm pitch.
2. The receiver export script already splits that footprint into two 1 x 7 sockets, M1A and M1B. It uses LCSC C46635843 and rotation offsets 270 and 90 (`../../heat/kicad/export_jlc.py`, `SPLIT`) [M]. The offsets came from the JLCPCB placement preview of the receiver [M].
3. Plan: copy the `SPLIT` block into `export_jlc.py` here, before the `HasField('LCSC')` test. The footprint has attribute `through_hole`, so the current script skips it [M]. Then the BOM gets one more line (2 x C46635843) and the CPL gets M1A and M1B.
4. I did not confirm C46635843 on the LCSC site. A search found no page for that number [E]. The receiver notes call it a hanxia 8.5 mm SMD socket [M]. Open the part in the JLCPCB cart before you order.
5. Height: this socket is 8.5 mm high [M, receiver notes]. The pickguard notes say to measure the socket against the stack [M]. Do that with the real part. The XIAO sits higher on this socket than on a low header.
6. Cost: one more BOM line. Whether it is Basic or Extended decides a fee. Make sure that you read the fee in the cart. If you order the receiver at the same time, this part adds no new part type [E].
7. The wire from TP1 to BAT+ under the XIAO stays a hand job. The tall socket gives room for the wire [E].

## 2. SW1 placed by JLCPCB (no layout change)

1. The footprint `SW_Omron_B3SN_6x6mm_Diagonal` has attribute `smd` [M]. It has no LCSC field on the board [M], so the export skips it.
2. LCSC numbers: B3SN-3012P is C306881 and B3SN-3012 is C2733649. JLCPCB lists assembly for C306881 [D, JLCPCB part page]. The page for C2733649 lists a 3.1 mm actuator height [D]. Your README names B3SN-3012, so C2733649 is the matching suffix [R]. The button cap in the pickguard uses `BTN_PROUD` of 2.8 mm. That value depends on the actuator height. Read the Omron drawing for both parts before you choose [E].
3. Plan: add an LCSC field to SW1 in the layout, or add an override table in the export script. A rotation offset is needed too, because the footprint works at four rotations. Make sure that it matches the placement preview.
4. If the footswitch replaces SW1 (item 3), skip this item and keep SW1 off the BOM.

## 3. Footswitch input, filter and ESD (new board)

Why: a footswitch cable is long and picks up static. The input D1 has only the XIAO internal pull-up [M].

1. Add a 2 pad wire footprint in parallel with SW1, so SW1 and the footswitch both pull D1 to GND. The board already has `WirePads_1x02_P2.54mm` for the battery switch J5, so the footprint exists [M].
2. Add a 1 kohm series resistor in line with D1 and a 10 nF capacitor from D1 to GND, close to the XIAO. Both are 0603 parts that JLCPCB places. The corner is about 16 kHz, so bounce above that is filtered. Firmware debounce handles the rest [E].
3. Add one ESD diode from the footswitch pad to GND, a 5 V bidirectional type in SOD-323. I do not name a part number. Pick one in the JLCPCB library and read its data sheet [E].
4. Without a new board: solder the footswitch wires across the two SW1 pads, and solder a 10 nF 0603 across D1 and GND by hand. This gives the same filter with no ESD part [E].

## 4. Reverse battery protection (new board)

1. J2 goes straight to the switch, D2 and the XIAO BAT+ pad [M, review 2026-11-06]. A reversed LiPo destroys the XIAO [M].
2. Fix: a P-channel MOSFET (SOT-23) in series with the battery plus, source toward the battery side, gate to ground through a resistor. The reverse battery then blocks. This is a standard circuit [R].
3. Make sure of one thing before you draw it: the XIAO charges the battery through BAT+ and TP1, so the charge current flows in the reverse direction through the FET. With the gate at ground, the channel conducts both ways. Simulate or measure this on a bench board [E].
4. Cost: one FET and one resistor, 2 more BOM lines.

## 5. Ring cable to the new ring tab

1. The new ring (`../reins-ring/`) has three through hole pads: 5V, GND, DIN [M].
2. The cable is J3 pin 1 supply, pin 2 data in, pin 3 GND [M, README table]. This maps one to one: J3 pin 1 to ring 5V, J3 pin 2 to ring DIN, J3 pin 3 to ring GND.
3. J3 is on the bottom side with the JST PH 3 pin header, and its place does not change [M]. Solder the three wires into the ring tab from the top, then crimp the PH housing at the other end.
4. The pickguard eye slot is 5.0 mm wide, and the tab is 5.9 mm wide. The slot must be at least 6.3 mm wide [M]. The pod model is being redrawn for this.

## What stays by hand

1. JST PH headers J1, J2, J3: through hole parts on the bottom side [M]. JLCPCB can place through hole parts, but it charges for hand soldering or selective soldering. I did not quote it [E]. Three headers are easy to solder.
2. Encoder PEC12R, OLED module, battery switch wires and TP1 wire: stay by hand [M].
3. The XIAO module itself: plugs into the sockets, so it stays removable. Direct castellated soldering needs a new footprint, and I recommend against it for now [E].

## Cost, estimate only

I did not get a JLCPCB quote. Items 1, 2 and 5 are export script and BOM changes, so they need only an assembly order for the existing board design. The boards you hold are bare [M], and assembly needs new boards from JLCPCB, so the PCB is a new charge either way [E]. Items 3 and 4 add four to five 0603 or SOT-23 parts [E]. Put the new files in the cart for the real price.

## Order of work

1. Do items 1, 2 and 5 first. They change no copper, so the DRC result stays the same [E].
2. Draw items 3 and 4 on a copy of the board, as the pcb-rework steps say, then run the DRC.
3. After you pay, compare the JLCPCB placement images with the board for D1, D2, U1, M1A, M1B and SW1, as in the grid128 polarity checklist.
