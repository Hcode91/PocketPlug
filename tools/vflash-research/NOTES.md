# V.Flash (V.Smile Pro) research

Work-in-progress notes for a future PocketPlug core. Nothing here is built into the app.

## Files

- `vf.cpp`: harness that runs the boot ROM (`70004.bin`, 2 MB) on PocketPlug's ARM926
  interpreter (`cores/gpl32/arm926.cpp`) and logs every hardware access.
  Build from `app/src/main/cpp`: `g++ -O2 -std=c++17 -fexceptions -I. -o vf ../../../../tools/vflash-research/vf.cpp cores/gpl32/arm926.cpp`.
  Run: `./vf 70004.bin <steps>` (one step is about 30 us of machine time). Env: `VFALL=1` logs every access,
  `DUMP=1` writes the first 8 MB of SDRAM to `ram.bin`, `GPIOD=hex` sets GPIO D input.
- `dis.py`: ARM disassembler with literal-pool values (`python3 dis.py ram.bin 10000000 <pc> <count>`).
- `callhw.py`: follows `bl` calls from a function and lists the hardware addresses each one uses.

## What the boot ROM is

"V.Flash Boot Rom" (VTech, 5/14/07), ACCESS "MORE v4.0 SDK ARM9T". It copies itself to SDRAM at
0x10000000 and runs there; its job is to mount the disc and load `\0SYSTEM\BOOT.BIN`
(2 MB ARM program, "c:/game_engine/vboot", also MORE-based). Game data: `.VFF` modules
("vfD0" container), `.MJP` Motion-JPEG video, `.WAV`, `.PTX` textures.

The SoC (LSI Zevio 1020) shares its peripheral map with the classic TI-Nspire (also a
Zevio), so Firebird (GPL-3, github.com/nspire-emus/firebird, core/misc.c, interrupt.c) is a
good reference. Differences found so far are marked V.Flash below.

## Memory map (so far)

| Address | What | Status |
|---|---|---|
| 0x00000000 | boot ROM 2 MB | |
| 0x10000000 | SDRAM (64 MB assumed) | |
| 0x8FFF0000 | SDRAM controller (init writes only) | |
| 0x90000000 | GPIO (4 ports x 0x40, like Nspire) | stub |
| 0x90010000 | timer pair 0 (Nspire layout) | |
| 0x90020000 / 0x90030000 | 16550 UARTs; boot console on 0x90020000 | modelled |
| 0x90080000 | ? (6 words written at init) | stub |
| 0x90090000 | RTC: 0x00 seconds (read), 0x08 set; 0x88/0x8C boot magic (C01DB007 / 1230FFAB) | modelled |
| 0x900A0000 | misc. V.Flash: timer pair 0 status/mask at 0x2C/0x34, pair 1 at 0x30/0x38 (Nspire uses 0x10..0x24). 0x0C bit 0/1 = warm-boot flags, 0x08=2 powers off | modelled |
| 0x900B0000 | PMU / PLL; 0x14 bit 0 = PLL locked, bit 5 checked at boot | stub |
| 0x900C0000 | timer pair 1 (Nspire layout). V.Flash: OS tick = timer 0, reload 0xEA6, divider 9; a 3.75 MHz clock gives 100 Hz | modelled |
| 0x900D0000 | GPIO bank 2 (4 ports). 0x18 input: bits 0 and 2 must be 1 or the boot powers off (power switch?) | stub = 5 |
| 0xA0000000 | ? 0xC0/0xC4 read at boot | stub |
| 0xA1000000 | serial bus (video encoder setup from a table); 0x10 bit 0 = done | stub = done |
| 0xA8000000 | ? cleared at boot (0x000..0x1FF) | stub |
| 0xB0000000 | 64-channel block, 0x40 per channel (DMA?) | stub |
| 0xB8000000 | display / 2D engine registers (0x0C = 0x00EF013F: 320x240); SRAM from 0xB8000800 | partly |
| 0xBC000000 | ? (0x100.. cleared) | stub |
| 0xC0000000 | video out? | stub |
| 0xC4000000 | CD controller, IRQ 14 (below) | to do |
| 0xD1000000..0xD6000000 | ? | |
| 0xDC000000 | interrupt controller, classic Nspire layout (Firebird interrupt.c) | modelled |

## Interrupts (V.Flash numbering)

- 5: timer pair 0; 6: timer pair 1 (OS tick). The Nspire uses 17/18.
- 14: CD controller.
- Enabled at boot (DC000008): 0x00165346 = bits 1, 2, 6, 8, 9, 12, 14, 16, 18, 20.
- IRQ dispatch: vector 0x18 -> RAM 0x1000FF98 -> 0x100873D0, which walks a list of
  {check, handler} pairs (list at 0x100ACA18). Checks read DC000000 bits.

## CD controller (0xC4000000), from the driver at 0x10061000..0x10063800 (boot ROM copy in RAM)

- 0x64 interrupt status (write 1 to clear), 0x68 interrupt mask. Handler 0x10061768 checks bits
  2, 4, 5, 8, 11, 12, 14, 15, 16 and posts events to the CD task (0x100676dc(event, ...)).
- 0x40 servo/drive status (bits 0, 2, 3-4, 5, 6 checked together).
- 0x08 / 0x0C servo command serial port (0x10061d1c sends a command, e.g. 0x340018);
  0x14 / 0x18 status (0x18 bit 1 read after a command).
- 0x4C subcode Q bytes (>>16, >>8, ...).
- 0x3C DMA buffer address (0x10357000), 0x44 / 0x48 buffer setup, 0x78 = 0x81000000 at init.
- The drive is likely a Sony CXD3059AR (CD DSP + servo), driven over its serial interface.

### Servo command port (0x10061d1c)

- 0x00 = number of bits, 0x08 / 0x0C = command bits (64-bit, e.g. 0x340018 with 8 or more bits),
  0x14 bit 0 = start (bit 2 / bit 3 options, upper half timing), then poll 0x64 bit 0 = sent,
  acknowledge by writing 1 to 0x64. A 0x20-bit command with top bits 0x80000000 is also copied to 0x78.
- After a command the driver reads 0x18 bit 1: most likely the drive chip's SENS output, whose
  meaning depends on the last command (focus OK, busy, frame sync...). 0x18 bit 0 is read elsewhere.

### Layers above (boot ROM copy)

- File loader: 0x10011dac(?, path); BOOT loader at 0x10012788 tries "\0SYSTEM\BOOT.BIN;1",
  then "\VBOOT.BIN;1", then jumps via 0x10053814.
- ISO 9660: 0x10099ef0 checks "CD001"; sector cache 0x10098454 / 0x10098560.
- CD stream layer: 0x10069000..0x1006b000 (buffer state at 0x10354580 / 0x10355840),
  CD task with the TOC state machine at 0x1003b4f8 ("Get TOC ...", "Wake up CD task stream at %d for %d LBAs").
- BOOT.BIN carries its own MORE OS and drivers, so hooks at this level would not cover games:
  emulating the CD hardware (servo chip + 0xC4000000 block) is the robust route.

### Drive (Sony CXD3059AR) behaviour found so far

- Command words are sent MSB-first; width in 0x00 (8, 24, 32 or 36 bits), bits in 0x08 (low) / 0x0C (high).
  The address nibble is the top 4 bits. Start = rising edge of 0x14 bit 0; 0x64 bit 0 = sent.
- Startup: $34xxxx x 80 (servo coefficient RAM), $10, $30, $9B.., $81.., $A0.., $B0.., $C5.., $D0..,
  $E0.., $3Axxxx and $A5EA.. (same as the datasheet's test settings), then the sled is homed:
  loop {$AE0000, $A2004000, $30, read SENS}; SENS after $3X = SSTOP = sled at inner limit.
  $22 moves the sled out, $23 moves it in, $20 stops. The harness models a sled that takes
  about a second to reach home.
- Then a successive-approximation calibration ($AD8008, $ADC008, ... $ADF788), more $A5EA..
  settings, $A48339, and the drive's job queue empties.
- SENS by command group (Sony CD DSP family, CXD2500 and later datasheets; psx-spx documents the
  PS1's CXD2510): $0X FZC, $1X A.S, $2X TZC, $3X SSTOP, $4X XBUSY (high when the auto sequence
  ends), $5X FOK, $6X SEIN, $AX GFS (frame sync), $BX COMP, $CX COUT, $EX OV64.
- FOK, GFS, SCOR and SQSO/SQCK (subcode Q) are separate chip pins; the Zevio probably exposes
  them through 0x40 (status) and 0x4C (subcode bytes) and the 0x64 interrupt bits.
- The TOC request is queued as job 0x106 on queue 0x10355840 (0x1006a708). Everything is then
  idle: some task waits on an event that is not raised yet. Next: find that consumer and what it
  waits for (likely FOK / GFS / subcode Q through 0x40, 0x4C, 0x64).

- CD interrupt mask helper: 0x10061adc(enable, bits) (enable: mask |= bits; disable: mask &= bits).
  Nothing has enabled CD interrupts yet when the system goes idle. Bits the driver uses:
  0x4, 0x10, 0x20, 0x100, 0x200, 0x400, 0x1800, 0x2000, 0x4000, 0x8000, 0x10000.
- CD events: the ISR posts events 8, 0x10, 6, 9 (bits 4 / 5 / 11-12 / 16) through 0x100676dc into a
  message queue (0x1006f9b4); event 0xE comes from 0x10068734 after 0x10096444 reads the 0xBC000000
  device. The TOC job consumer 0x10069708 is registered with 0x10067a98 as the CD event callback,
  so the TOC read only advances on CD events: the next step is finding which event must come
  first (probably a periodic subcode / frame event once the spindle runs).

### Interrupt map (handler chain at 0x100ACA18, check -> handler)

| IRQ | Device (from the handler's registers) |
|---|---|
| 1, 2 | UART 0 / 1 |
| 5, 6 | timer pairs (6 = OS tick) |
| 8 | 0xA8000000 device (0x14, 0x28) |
| 9 | DMA (0xB0001004) |
| 12, 20 | 0xBC000000 device (0x0C, 0x1C, 0x100..) |
| 14 | CD controller |
| 17, 18 | ? (0x900A0104 / 0x900A0110) |

0xF8000000: 8 KB RAM cleared at boot with a flag at 0xF8001FFE (settings memory?).

## Status

The boot reaches the MORE OS scheduler with a working 100 Hz tick, sets up the display (320x240),
DMA and CD controller, homes the drive's sled and calibrates, then queues the TOC read and goes idle.

## Next steps

1. Model the CD controller: answer servo commands, report disc present / TOC, then sector reads by
   DMA into the 0x3C buffer with IRQ 14. Alternative: hook the boot ROM's CD driver API
   (high-level) with ARM926::hle_hook and serve sectors from the image directly.
2. Once BOOT.BIN loads, find what it needs (graphics, ZSP400 DSP for sound / MJPEG).
