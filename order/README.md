# REINS: ordering

This folder lists what to order for the guitar side of REINS: the two JLCPCB boards, the cables and the small parts.

| File | Content |
| --- | --- |
| `README.md` | JLCPCB options, files to upload and the placement checks |
| `CABLES.md` | The four cables under the pickguard: parts, lengths and pin maps |
| `digikey-cart.csv` | Small parts from DigiKey: JST housings, the battery switch, the ring caps |

The receiver boards (main board and jack board) are in the separate `heat` repository, which is not public yet.

## JLCPCB: files to upload

Each export script writes its files next to the board. To make them again, see `reins-guitar/README.md` and `reins-ring/README.md`.

| Board | Gerber zip | BOM | CPL |
| --- | --- | --- | --- |
| Guitar | `reins-guitar/fab/jlc/reins-guitar/reins-guitar-gerbers.zip` | `reins-guitar-bom.csv` | `reins-guitar-cpl.csv` |
| LED ring | `reins-ring/fab/jlc/reins-ring/reins-ring-gerbers.zip` | `reins-ring-top-bom.csv` | `reins-ring-top-cpl.csv` |

## JLCPCB options

| Option | Guitar | LED ring |
| --- | --- | --- |
| Size | 68.2 x 30.5 mm | 44.4 x 44.4 mm |
| Layers, material | 2, FR-4 | 2, FR-4 |
| Thickness | 1.6 mm | **0.8 mm**. At 1.6 mm the ring does not fit the pickguard pod |
| Color, finish | Black, LF HASL | Black. If black is not offered at 0.8 mm, use green |
| Mark on PCB | Remove Mark | Remove Mark |
| PCB quantity | 5 (the minimum) | 5 |
| PCBA | Economic, top side, quantity 2 | Economic, top side, quantity 2 |
| Parts placement check before production | Yes | Yes |

One guitar needs one guitar board and one ring. Two assembled pieces give one spare.

## Placement preview checks

Guitar board:

1. U1 (SOT-23-6): the pin 1 dot is at the pin 1 mark on the silkscreen.
2. D1, D2: the cathode bar is on pad 1 (the LED_PWR side). Both point the same way.
3. M1A, M1B: each of the 14 staggered legs sits on a pad.
4. SW1: the four legs sit on the four pads, two on each side. If the part is turned 90 degrees, the legs miss the pads: turn it in the preview.
5. The BOM has 8 lines and 14 placed parts.

LED ring:

1. All 16 LEDs point the same way. Pin 1 (VDD) is on the outer edge, on the counter clockwise side. If all LEDs are wrong by the same angle, set that angle in `ROT['C52917433']` in `reins-ring/export_jlc.py` and export again.
2. There are 16 parts, all on the top.
3. Open the C52917433 data sheet and check the pin order: 1 VDD, 2 DOUT, 3 VSS, 4 DIN.

## Parts to buy elsewhere

| Item | Qty | Note |
| --- | --- | --- |
| 100 nF 0603 caps for the ring | 16 per ring | Soldered by hand on the ring back |
| JST PHR-2, PHR-3, PHR-4 housings and SPH-002T-P0.5S contacts | see `CABLES.md` | Cables to J1, J2, J3 |
| E-Switch EG1201 slide switch | 1 | Battery on and off, on J5 |
| 1S LiPo, 36 x 29 x 4.75 mm, JST PH 2.0 plug | 1 | Check the polarity with a meter before you plug it in |
| Bourns PEC12R encoder, 15 mm shaft | 1 | S1. For another shaft length, set `ENC_SHAFT` in the pickguard script |
| 1x4 female socket (Harwin M20-7820446) | 1 | J4, for the OLED module |
| Screws | see the pickguard README | "Bought parts" in `reins-guitar/pickguard/README.md` |

## Not checked

The JLCPCB quote and fees, the C52917433 data sheet and the real socket height are not checked yet. Nobody tested a board with power yet.
