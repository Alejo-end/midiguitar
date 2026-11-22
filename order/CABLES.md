# REINS guitar: cables

Four cables join the guitar board to the parts under the pickguard. The lengths come from the version 2 model (`reins-guitar/pickguard/v2/checks.txt`, "Cable routes"). The route goes out of the pod and across the pickup cavity. Then it follows the body channel under the plate groove into the board cavity, and runs under the board to the connector. The channel depth is not measured, so add the slack given below. Details and pictures: `reins-guitar/pickguard/README.md`, "Wiring and cables".

Parts for all four cables:

| Part | Qty | Note |
| --- | --- | --- |
| JST PHR-2, PHR-3, PHR-4 housing | 1 each. The PHR-2 is for the battery extension only | |
| JST SPH-002T-P0.5S crimp contact | 10 | Takes 30 to 24 AWG, insulation 0.9 to 1.5 mm across |
| 26 AWG silicone wire, red, black, green | about 0.5 m each | Insulation about 1.3 mm across |
| SparkFun Flexible Qwiic Cable 200 mm, PRT-17258 | 1 | For the ToF cable |
| 2.5 mm cable tie or waxed thread | 1 | Strain relief at the pod tie bar |
| Heat shrink 1.5 mm | 10 cm | Over the ring and switch joints |

## (a) Ring cable: J3 to the ring pads

| Item | Value |
| --- | --- |
| Board end | PHR-3 with 3 x SPH-002T-P0.5S, into J3 (B3B-PH-K-S) |
| Ring end | soldered to the three round pads on the ring back |
| Wire | 3 x 26 AWG silicone: red, green, black |
| Route in the model | 89 mm |
| Length to make | 110 mm from the pad to the back of the housing (cut 120 mm, strip 2 mm at each end) |
| Ready-made | none. Make it |

| J3 pin | Net | Wire | Ring pad |
| --- | --- | --- | --- |
| 1 | LED_PWR | red | 5 |
| 2 | LED_DIN | green | D |
| 3 | GND | black | G |

Solder each wire straight down onto its pad (the wire stands at a right angle to the ring). Pass the three wires down through the pod slot. Then bend them outward under the tie bar. They lie side by side, G nearest the ToF board, then D, then 5. Tie them to the bar.

## (b) ToF cable: J1 to the SparkFun VL53L1X board

| Item | Value |
| --- | --- |
| Board end | PHR-4 with 4 x SPH-002T-P0.5S, into J1 (B4B-PH-K-S) |
| ToF end | the Qwiic plug (JST SH 1.0 mm) of the bought cable, in the Qwiic socket on the neck side of the ToF board |
| Wire | the Qwiic cable's own wire (silicone, about 28 AWG) |
| Route in the model | 137 mm from the Qwiic plug |
| Length to make | cut the 200 mm cable 165 mm from the plug you keep |
| Ready-made | a plain Qwiic cable does not fit J1: J1 is JST PH 2.0 mm, Qwiic is JST SH 1.0 mm. Use one Qwiic end and crimp the other |

| J1 pin | Net | Qwiic wire colour | Qwiic pin |
| --- | --- | --- | --- |
| 1 | GND | black | 1 |
| 2 | 3V3 | red | 2 |
| 3 | SCL | yellow | 4 |
| 4 | SDA | blue | 3 |

The warning is true: Qwiic has pin 3 SDA and pin 4 SCL. J1 has pin 3 SCL and pin 4 SDA. So do not crimp the wires in their Qwiic order. Put yellow in J1 pin 3 and blue in J1 pin 4.

Do not use the 0.1 inch holes. They sit on the slot edge of the ToF board. The battery cradle is only 0.8 mm under the board. The solder tails touch the cradle.

## (c) Battery: LiPo to J2

| Item | Value |
| --- | --- |
| Board end | the LiPo's own PHR-2 plug, into J2 (B2B-PH-K-S) |
| Battery end | the cell |
| Route in the model | 91 mm from the cradle opening |
| Lead of an Adafruit 1578 class cell | about 80 mm (retailer figure). Too short |
| Fix | buy a cell with a lead of 110 mm or more, or make a 50 mm extension: cut an Adafruit 1131 JST PH extension (500 mm) and crimp a PHR-2 on the socket half |

| J2 pin | Net | Wire |
| --- | --- | --- |
| 1 | GND | black |
| 2 | VBAT | red |

Check with a meter before you plug in: red must reach J2 pin 2. Cell makers do not agree on this. If it is wrong, lift the two latches of the PHR-2 with a needle and swap the contacts.

## (d) Battery switch: EG1201 to J5

| Item | Value |
| --- | --- |
| Board end | soldered into the J5 wire pads (1.0 mm holes), wire in from below |
| Switch end | soldered to the EG1201 pins |
| Wire | 2 x 26 AWG silicone: red and orange (or red and white) |
| Route in the model | 76 mm |
| Length to make | 100 mm |
| Ready-made | none |

| J5 pad | Net | Switch pin |
| --- | --- | --- |
| 1 | VBAT | middle pin (common) |
| 2 | VBAT_SW | the end pin that connects to the middle pin with the slide towards the neck. Find it with a meter |

The battery current goes through this switch. The XIAO charges the battery through TP1, J5 and the switch. With the switch OFF, the battery does not charge.
