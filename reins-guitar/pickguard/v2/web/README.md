# REINS pickguard v2: web export

`build_pickguard_v2.py` (two folders up) writes this folder, except `body_outline.json`, which `photo_ref_v2.py` writes. Run the scripts again after a change and the files are replaced. The part names do not change.

## Files

| File | Content |
| --- | --- |
| `parts.json` | Every part: name, STL file, colour, material, assembly order, explode offset. Also the board placements and the plate box. |
| `<Name>.stl` | One STL per part, in place, in mm, in the frame below. |
| `Plate_PlyBottom.stl`, `Plate_PlyMiddle.stl`, `Plate_PlyTop.stl` | The plate cut into its three colour plies (charcoal, orange, charcoal). Use them instead of `Plate.stl` to show the orange line in the bevel and the chamfers. |
| `boards/reins-guitar.glb`, `boards/reins-ring.glb` | The two boards from `kicad-cli pcb export glb` with pads, silkscreen and solder mask. |
| `body_outline.json` | The guitar body around the pickguard, as a polyline in the same frame. See "Body silhouette". |

Printed parts: Plate, ControlFrame, Knob, ButtonCap, Pod, Diffuser, BatteryCradle.

Bought or stock parts, as simple models: ToF_Board, Encoder_PEC12R, Button_B3SN, Switch_EG1201, LiPo, Toggle, Pickups.

Boards (GLB): Board_Guitar, Board_Ring.

## The frame

All STLs share one frame, the guitar frame of the pickguard scripts:

- X points to the neck (mm).
- Y points to the bass side (mm).
- Z points up. Z = 0 is the body top, which is also the underside of the plate. The plate top is Z = 3.0.

Seen from above (looking down -Z), the neck is on the right and the bass side at the top. The plate spans about X -6.7 to 190.3 and Y 2.0 to 189.7. `parts.json` gives the exact box and its centre in `plate_bbox`. Centre the scene there.

In three.js (Y up), put every STL in one group with `group.rotation.x = -Math.PI / 2`. Then three.js (X, Y, Z) = (frame X, frame Z, -frame Y).

## Placing the KiCad boards

A point of a KiCad board is (x, y, z) in mm. The point (x, y) is on the KiCad sheet, where y points down. z is the height above the bottom face of the board.

### Guitar board, `reins-guitar.kicad_pcb`

1. Flip the sheet: p = (x, -y).
2. Turn p by THETA = 167.5 degrees about Z (counter clockwise seen from above).
3. Add (TX, TY) = (234.1938, -121.3741).
4. Z = z - 9.61. In the kicad-cli STEP and GLB, the PCB top is at z = 1.51. It goes to Z = -8.10. That is the stack for an XIAO on 8.5 mm sockets (see `XIAO_STACK`).

As a matrix from KiCad mm to the frame (row major, `matrix_kicad_mm_to_frame_rowmajor` in `parts.json`):

```
[[ cos T,  sin T, 0, TX ],      cos T = -0.976296
 [ sin T, -cos T, 0, TY ],      sin T =  0.216440
 [ 0,      0,     1, -9.61 ],
 [ 0,      0,     0, 1 ]]
```

### LED ring, `reins-ring.kicad_pcb`

The ring centre is KiCad (100, 100). It sits on the pod, centred on (81.312, 117.374), with its board bottom at Z = -3.00.

1. p = (x - 100, -(y - 100)).
2. Turn p by the angle in `parts.json` (`kicad_to_frame` of Board_Ring, about -80.8 degrees). This puts the three wire pads over the wire slot of the pod, which points at the body channel.
3. Add (81.312, 117.374).
4. Z = z - 3.00.

`parts.json` has the full 4 x 4 matrix.

### GLB files

`kicad-cli pcb export glb` writes metres with Y up. A GLB point (gx, gy, gz) is KiCad x = 1000 gx, height z = 1000 gy and KiCad y = 1000 gz.

1. Scale the GLB point by 1000.
2. Swap it to (x, y, z) = (1000 gx, 1000 gz, 1000 gy).
3. Apply the matrix above.

After you place a board, check one known point. The XIAO USB end is KiCad (189.7, 127.87). In the frame it must land at (76.67, 44.52).

### Module heights

The KiCad models put the XIAO and the OLED module on 6.6 mm sockets. The real board has 8.5 mm sockets. For a true picture, raise GLB node `M1` (XIAO) by 1.97 mm and `J4` (OLED) by 1.92 mm (`module_lift_mm` in `parts.json`). The real encoder is a separate STL (Encoder_PEC12R) with the shaft length of the build: if both show, hide the GLB node `S1`.

The GLB solder mask comes out green. The real boards are black: recolour the board body to `#111214`.

## Exploded view

Each part in `parts.json` has:

- `order`: the order of the real assembly, 1 first.
- `explode`: the offset in mm, in the frame (Z up), that moves the part to its exploded place.

The plate stays. The control frame and the knob go up. The board, the pod, the ring, the ToF board, the battery and its cradle go down, in that order. Pickups has order 0: it is the stock part and does not move.

## Body silhouette

`body_outline.json` holds the guitar body outline on the body top (Z = 0), in mm, in the same frame.

- `closed_polyline`: a closed outline for a ghost silhouette.
- `segments`: the same outline in four parts, each with its source and accuracy:
  - `treble_edge_and_cutaway`: from the owner's photo IMG_2407 (background removed), flattened with the nine screw holes. About 3 mm. The photo sees the rounded body edge from an angle, so this is the outer edge of the roundover.
  - `bass_horn_lower_edge`: from IMG_2408. About 4 mm.
  - `bass_edge`: an estimate. Neither photo shows it: IMG_2407 is cut at the pickguard there, and IMG_2408 shows the body up to its frame at Y 198. About 10 mm.
  - `butt_end`: an estimate from the overall length of a Sonic Mustang. About 20 mm.

The neck covers the body from X 166 between Y 93.5 and 146.5. The polyline jumps straight across there.
