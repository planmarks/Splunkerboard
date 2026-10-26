# Manufacturing & assembly

← [Hardware overview](README.md) · [Main README](../../README.md)

## Fabrication files

All gerbers were exported from **KiCad 10.0.5** (Pcbnew) in Gerber X2 format, with a `.gbrjob` job file and Excellon drill files.

| Board | Gerber folder | Layers | Size (mm) | Exported |
|---|---|---|---|---|
| Splunkerboard 1 (V1C) | [`Splunker 1/Splunkerboard V1 Gerber/`](../../Splunker%201/Splunkerboard%20V1%20Gerber) | 4 | 47.07 × 79.39 | 2026-09-21 |
| Expansion board | [`Splunker 1 Prototyping Board/Prototyping Board/`](../../Splunker%201%20Prototyping%20Board/Prototyping%20Board) + `.zip` | 4 | 47.07 × 79.39 | 2026-09-20 |
| Splunkerboard Dev (V1B) | [`Splunkerboard DIY/Spunkleboard Gerber 2208/`](../../Splunkerboard%20DIY/Spunkleboard%20Gerber%202208) + `.zip` | 2 | 47.07 × 79.39 | 2026-08-22 |

### File set (4-layer example)

| File | Layer |
|---|---|
| `*-F_Cu.gbr` | L1 top copper |
| `*-In1_Cu.gbr` | L2 inner copper |
| `*-In2_Cu.gbr` | L3 inner copper |
| `*-B_Cu.gbr` | L4 bottom copper |
| `*-F_Mask.gbr` / `*-B_Mask.gbr` | Solder mask |
| `*-F_Paste.gbr` / `*-B_Paste.gbr` | Stencil / paste |
| `*-F_Silkscreen.gbr` / `*-B_Silkscreen.gbr` | Legend |
| `*-Edge_Cuts.gbr` | Board outline |
| `*-PTH.drl` / `*-NPTH.drl` | Plated / non-plated drills |
| `*-job.gbrjob` | Job file (stack-up, rules) |

### Fab parameters

| Parameter | Value |
|---|---|
| Thickness | 1.6 mm FR-4 |
| Min track / clearance | 0.2 mm / 0.2 mm (0.5 mm copper-region spacing) |
| Smallest plated drill | 0.3 mm (vias). Other PTH sizes: 0.8, 0.9, 1.0, 1.5 mm |
| NPTH | 1.4 mm, 2.1 mm (M2 mounting) |
| Surface finish | Not specified (the job file says `None`). **ENIG** is recommended for the QFN/TDFN parts and the XIAO castellations. |

All of these values are within the standard capabilities of JLCPCB and PCBWay. To order, zip the gerber folder and upload it. The DIY and expansion boards already have ready-made `.zip` files.

> The job files carry project revision **"V1B"** for all three boards. If you track revisions on the fab order, set the revision in KiCad (*File → Board Setup → Title block*) before you export.

## Assembly

### Tools

- Fine-tip soldering iron, flux, 0.5 mm solder
- **Splunkerboard 1:** a hot plate or hot-air station, plus solder paste (a stencil is optional), for the MCP23017 (QFN-28) and MAX17048 (TDFN-8)
- Tweezers, a magnifier, isopropyl alcohol

### Recommended order (Splunkerboard 1)

1. **Paste and reflow the fine-pitch parts first:** U1 MCP23017, U3 MAX17048, the microSD socket J26 and all 0805 resistors and capacitors. Check the QFN pin-1 orientation.
2. **OLED panel (DS1):** align the FPC and solder the 30 pads. Keep the iron off the glass. Fold the panel over its adhesive pad.
3. **Tactile switches SW2–SW7.**
4. **XIAO module:** tack two opposite corner pads, check alignment against **one** of the two overlaid footprints, then solder all castellations. The XIAO's bottom battery pads must line up with the board's BAT pads.
5. **Through-hole parts:** slide switch SW1, the battery header BT1 and the 2.54 mm headers (optional).
6. **Clean** off flux residue, especially around the QFN and the OLED.

### Recommended order (Dev / DIY)

1. Tactile switches SW1–SW6
2. XIAO module (as above)
3. OLED module (4-pin header)
4. Slide switch and battery header
5. Optional: u.FL + SMA, 2.0 mm breakout headers

### Bring-up checklist

| Step | Check | Expected |
|---|---|---|
| 1 | Before the first power-up, measure 3V3 to GND and VBAT to GND | No short |
| 2 | Plug in USB-C, switch OFF | XIAO enumerates as a serial/JTAG port |
| 3 | Measure 3V3 at TP `3V3` | 3.3 V ± 0.1 V |
| 4 | Run an I²C scanner sketch ([dev guide](../firmware/development-guide.md#i2c-scanner)) | V1C: `0x20 0x36 0x3C`; Dev: `0x3C` |
| 5 | Run a button test sketch | All six buttons report correctly |
| 6 | Insert a FAT32 microSD card (V1C) | `SD.begin()` succeeds and card-detect changes state |
| 7 | Connect the LiPo and switch ON | Board runs from battery and the MAX17048 reports a plausible voltage and % |

## 3D models and enclosure

| File | Content |
|---|---|
| [`Splunker 1/3D/Splunkerboard.stl`](../../Splunker%201/3D/Splunkerboard.stl) | Splunkerboard 1 board model (GitHub shows STL files in an interactive viewer) |
| [`Splunker 1/3D/Splunkerboard Dev.stl`](../../Splunker%201/3D/Splunkerboard%20Dev.stl) | Dev board model |
| `Splunker 1/3D/*.glb` | Same models in glTF binary (for web viewers, Blender, three.js) |
| `Splunker 1/3D/Spunkleboard 1 3D.blend` | Blender scene |
| `Splunker 1/Case/Splunkerboard Case.blend` | **Enclosure design** (Blender). Export it to STL for 3D printing. |
| `Splunker 1/Render-V1C-*.png` | Product renders (white / transparent backgrounds). Despite the "V1C" name, they show the **Dev board**; there is no Splunkerboard 1 render yet. |

> **Repo hygiene:** the [`.gitignore`](../../.gitignore) leaves local-only and superseded files out of the public repo. That covers KiCad `.history/` snapshots (which contain nested git repos), `*.kicad_prl` UI state, backups and stale `*.net` netlists, Blender `.blend1` backups, the 25 MB `Spunkleboard-test-render.png`, the older `Spunkleboard.stl`/`.glb` exports, and the render and STL copies duplicated into the DIY and expansion-board folders. The largest tracked files are the STLs in `Splunker 1/3D/` (~36 MB), under GitHub's 50 MB warning threshold. If they grow, move `*.stl`/`*.glb` to Git LFS.
