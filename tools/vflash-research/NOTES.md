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

## Drive model in vf.cpp (works up to the TOC and pause)

- Pickup radius is the master state: sled moves ($22 out / $23 in, 4 mm/s), track jumps ($28/$2C
  single kicks; $4 auto sequence 1/10/2N/M jumps with the $7 count = (cmd >> 4) & 0xFFFF) and normal
  play (one frame further out per 1/75 s). LBA from radius with CD geometry: 1.6 um pitch, 1.3 m/s,
  LBA -150 at 25 mm, lead-in 23..25 mm, inner limit switch at 22.5 mm.
- Subcode Q at 75 Hz while CLV runs and tracking is on ($24/$25): 0x64 bit 5 on IRQ 14, bytes in
  0x54 (bit 16 = CRC ok, ctrl/adr, track), 0x58 (index, min, sec, frame), 0x5C (0, amin, asec,
  aframe). Lead-in carries the TOC pointers A0/A1/A2/01.
- 0x14 bit 1 asks for a completion flag (0x64 bit 1); bit 2 means "select SENS only" (the command
  is not executed). Readback value in 0x10. $39xx is a monitor readout: $391F must read -103..-89 in
  its low byte for the offset sweep at 0x1005B540 to finish.
- SENS: $3x = inner limit switch, $38 = auto gain done, $Ex while braking ($EA) drops after 0.3 s.
- 0x40 reads 0x65 when spinning (bits 0, 2, 5, 6 set; 3-4 clear): the ISR's "servo good" test.
- 0x4C = data header (bit 31 valid, MSF in 23..0). 0x64 bit 15 (monitor, enabled through 0x60's top
  byte = 0xC8 and mask 0x8000) is an error/abort event: raising it makes the state machine abort and
  restart the spin-up, so a healthy drive never raises it.
- Read setup routines: 0x10062608 (target MSF in 0x1C, count in 0x20 or bit 24 = endless, buffer
  0x24, ring 0x28/0x2C and 0x34/0x38) and 0x10062BE0 (0x1C, 0x20 = 4, 0x24 = 0x10355780).
- Idle task 0x10010994 is a counting loop; the harness halts the CPU there (10x faster runs).

### Boot flow with GPIOD = 0 (retail)

- VTech logo, then the second display layer (0xB8000164 = 0x10BC8000, 352x288 RGB555) shows the
  V.Smile Pro splash (boot-splash.png). The main task (0x10011040) waits for 0x101E7F40 == 1 and
  then calls the loader 0x10012788(7).
- The disc-check task (0x100145B0) registers callback 0x10013670 (job 0x202) and starts detection
  (job 0x201 -> 0x10068198 -> 0x100704BC posts handler 0x1007095C to the device manager queue).
  The callback needs event 0x10 (sets 0x1024C580), which 0x1007095C posts via 0x10067AB8 at its end.
- Media detection: 0x64 bit 14 (monitor enabled by 0x10062AC0: 0x60 bits 20-23 and mask 0x4000)
  is "disc found". Raise it once per insertion: its handler 0x10067754 posts a media message to the
  state machine at 0x1006E400 (state 0x103556C0, substate 0x10355760). A second media message while
  the substate is non-zero counts as "disc changed again" (substate 7, retry counter 0x10354500,
  gives up after 3 and the callback receives event 0x0F). Bit 15 is the matching "lost" event.
- The device manager (task 0x1006F858, current op 0x103561A0, busy flag 0x10354620) runs one op at
  a time and waits in 0x10356140 for its completion; completions come from the job thread
  0x1006DA40, which runs a job's work function and then its completion wrapper (0x1006E130..
  0x1006E208, posting a type-2 message handled at 0x1006FE6C).
- After the media event the state machine re-inits the drive (substate 2 job, work function
  0x1006D020). That job reads the TOC (0x10060C10 / 0x10060D88) and then calls 0x1006145C, which seeks
  to MSF 00:02:08 (LBA 8) with 0x10055CBC(0, 2, 8, 1) and only then programs the first sector read
  (0x10062BE0(0xA6)). The seek never converges in the model: the pickup hovers around LBA 3..25 with
  1-track kicks. Next: work out 0x10055CBC's acceptance test (compare near 0x10055DC4 with 0x21,
  current Q from 0x10355640 vs target 0x10354E60) and fix the jump/kick model so it lands.
- 0x1005908C waits for a valid subcode Q (0x10354648 = 0 after parsing; 1 = CRC error, 2 = bad).

### Sector reads (vf.cpp, DISC=<image.bin>)

- Fine seek 0x10055CBC accepts a landing 0..37 frames before target-4; corrections are
  floor(d/12.37)+1 one-track kicks ($49), with ~190 ms (10 x 10 ms $A0 GFS debounce) of play
  between a kick and the next position check. The model uses VEL = 0.952 m/s so a track holds the
  firmware's 12.37 frames at the start of the disc (table 0x100A0EE8, per 10 minutes), and
  single-track kicks cover 1.6 tracks so the pause loop converges.
- 0x40: bits 0 (decoder on) and 2 (transfer on) are control; bits 3-6 read as status (3-4 = 0,
  5-6 = locked) and the driver writes them back. Bit 7 set = header-slot mode, clear = ring mode.
- Header slots: 4 x 1 KB at 0x3C. Word 0 = status (1 = decoded OK), word 1 = address in the ring
  where that sector's user data went, +0x08 sync and header (mode byte at +0x17), +0x18 the
  sector's EDC and P/Q parity (raw bytes 2064..2351) for the software ECC 0x10072128 (GF(256)
  tables at 0x101A2018/0x101A2118; returns 0 when the syndromes are clean). In slot mode (header
  checks) the slot holds the raw sector start instead. 0x70 = count << 2 | read slot; 0x64 bit
  12 per entry (+ bit 4 in slot mode); 0x74 bit 0 or 1 releases one; a new ring read (write to
  0x24) empties the buffer and re-arms the address match. The ISR posts event 6 per entry.
- Ring mode (0x10062608): 0x1C = start MSF (bit 24 = no match), 0x20 bit 24 = until stopped,
  0x24 = write pointer, 0x28/0x2C = ring (20 x 2048 bytes), 0x80 = mark: bit 16 (event 9) when the
  pointer reaches it; with 0x84 bit 0 the sector starting at the mark is the last one and the
  transfer ends with bits 16 + 4 (event 8). 2048 user bytes per sector.
- CD events (category 2 in the device manager queue) go through 0x100633C0 to the handler table
  at 0x101A1FC4 (event n -> entry n). Event 6 -> 0x10063D1C (headers, 0x10065BFC), 7 -> 0x10063D40
  (read finished: continues only if decoder state 0x10358380 is 3 = ended), 8 -> 0x100640B8
  (transfer end), 0x0B -> 0x10064334 (ECC result for one sector, from task 0x100689CC).
- Boot result: disc accepted, loader 0x10012788(7) opens 0SYSTEM\BOOT.BIN;1 (LBA 50), the file
  system reads LBA 16..35 with per-sector ECC passing, re-reads from LBA 16 into the next ring
  position, gets 2 sectors, posts event 7 while the decoder state is still <= 2, stops the decoder
  and goes quiet. The loader's request (0x10358BC0, state at +4: 2 = busy, 4 = done) is completed
  by 0x1009A26C via callback 0x1009AC88 (registered with the read at 0x10098EB0), which never runs.
  Next: work out how the read request struct 0x10355260 (+0x14 start, +0x18 position, +0x34
  count) is advanced per sector (0x10065FF4 maps a ring address to a sector index) and why the
  first 20-sector read is redone.

### Read request 0x10355260 (findings so far)

- +0x08 buffer, +0x10 address of the newest sector, +0x14 start frame, +0x18 next frame,
  +0x1C frame of the newest sector (0x10065FF4 maps a ring address to a frame), +0x34 sector count
  (0x0E for the loader's first read), +0x40 mode (1 here). 0x10355960 = header limit (-1 = none),
  0x10356240 = headers seen. The per-sector path (event 6 -> 0x10065D74 -> ECC -> event 0x0B) advances
  +0x18 correctly from 0xA7 to 0xB9.
- The read is open-ended and fills all 20 ring slots because nothing consumes the ring. At the
  20th sector the transfer ends at the stop mark, event 7 takes the "buffer full" branch
  (0x103550A0 set, 0x10355240 = -1, mode 1, 0x10354640 = 0 -> 0x10063F78) and restarts the read at
  +0x1C, which by then maps back to the start frame 0xA6. Data consumers are notified through
  0x10066568(code) -> callback pointer 0x101A2010 (the job layer, 0x10069708), which only ever
  received code 5 (started). Next: find what should call 0x10066568 while data arrives (or why
  it does not), so the file system copies the 14 sectors out and completes job 0x206.

### BOOT.BIN loads and runs (vf.cpp, DISC=<image.bin>, GPIOD=0)

- Ring consumer: each sector is copied out of the ring by the DMA engine at 0xBC000000 (channel 7:
  0x170 control with the count of 32-bit words in bits 0-9, 0x174 source, 0x178 destination;
  0x10 bit n starts channel n, 0x0C = busy, 0x1C = done (write 1 to clear), 0x04 = IRQ 12 enable;
  the handler 0x10096C88 calls the channel's callback). Its completion posts event 0x0E, which lets
  the CD layer deliver the next sector (flow flag bit 0 of 0x103561F8; bit 2 = paused until the
  file system asks for more with event 1).
- Event 1 for data outside the ring stops the transfer (0x40 bit 2 cleared) and waits for the
  decoder to confirm with "transfer ended" (0x64 bit 4); the event-8 handler then re-issues the
  pending request saved at 0x10355300.
- BOOT.BIN (LBA 50..1030, header "BOOT", size 0x1EA2CC, load 0x10C00000, entry 0x10C0011C) is read
  into 0x10C00000 and started. It jumps through [0x10C00020] = 0x1880, the ROM's suspend routine
  (0x900A0F04 = 0x16, 0x8FFF0004 = 0xCD, 0x900B0000 = 3, 0x900B0004 = 2, 0x900B0014 = 1,
  0x900A0008 = 1). The chip resets; the ROM reset path reads the wake reason in 0x900A000C
  (bit 1 = woke from sleep) and resumes through the RAM vector 0x1000FFC8, which brings up the
  game's own MORE OS (second UART banner) running at 0x109xxxxx.
- The game drives the graphics engine at 0xA8000000 (IRQ 8): 0x28 = command list address,
  0x00 = 1 runs it (3 = reset; status code in bits 16-31, 3 = busy), 0x10 = IRQ enable, 0x14 =
  interrupt status (write 1 to clear): bit 3 with ID 0xFD in bits 8-15 is the list-end interrupt
  raised by the list's closing 0x1400FD01 packet. 0x90/0x94 = 0x00F00200 and 0x98 = 0x10048000
  look like the render target. Command lists are register-write packets (0x20RRNNNN = write N
  registers from R), opcodes 0x84/0x88/0x8A/0x8B/0x8C and 0x1400xx01 interrupts.
- With a stub that finishes every list at once, the game runs its main loop (about 1.8 million
  lists in 15 s). Nothing appears on screen because all drawing goes through this engine.
  Next: decode the graphics command stream and render it; then input (GPIO), sound and the
  game's own CD reads.

### Graphics engine command packets (from the game's builders at 0x109E44D8..0x109E6300)

Each queued list is `00000001`, packets, then `1400FD01` (interrupt ID 0xFD = list done). The
engine works in one 12-bit (x, y) coordinate space; positions are packed as (y << 12) | x.

| Packet | Meaning (evidence) |
|---|---|
| `20RRNNNN` + N words | write N registers starting at R (e.g. `20740001 0`, `20780002 a b`) |
| `81yyyxxx` | origin / centre inside the target (0x109E44D8) |
| `82yyyxxx`, `83yyyxxx` | clip rectangle top-left and bottom-right (x+w-1, y+h-1) (0x109E4520) |
| `D3000003 pos size colour` + `96000000` | rectangle fill: pos (y<<16)\|x, size (h<<16)\|w; 96 = go |
| `84000xxx` | render flags (bit 5, bit 1 from settings) (0x109E5F38) |
| `85yyyxxx`, `87yyyxxx`, `8Eyyyxxx`, `8Fyyyxxx` | positions in the coordinate space (texture source etc.) |
| `86` | (a << 15) \| (b << 11) \| c (0x109E5E9C) |
| `88`, `8A`, `8B`, `8C` (16-bit arg), `8D` (24-bit colour, e.g. FCFCFC) | state |
| `1C006760`, `1E00E230` | mode words (blend / texture modes?) |
| `09000000 addr` | draw the primitive record at addr |

Primitive records (28 bytes): `C8050005`, word, position as two signed 16-bit values, size
(h << 16) \| w, 0, colour 0x00RRGGBB, `04000000`.

Three 512x240 framebuffers sit at y = 0, 240 and 480 (clears at (0,0), (512,240) etc.); textures
use x >= 512 (e.g. 0x8F2E6220 = (544, 742)). Where this coordinate space lives in memory is not
known yet: RAM from 0x10048000 (0x98, the render target register) at 2048 bytes per row does not
show textures there, and the only bitmap found in RAM (0x10230000, the upside-down V.Smile Pro logo)
is left over from the boot ROM splash. The game has not read anything from the disc yet after
starting, so its textures may simply not be loaded.

The math unit at 0xA4000000: writing a float to 0x00 and reading it back gives the integer
(modelled); 0x18 is used similarly with fixed-point values (still a pass-through).

## Status

The boot reaches the MORE OS scheduler, shows the VTech logo and the V.Smile Pro splash, spins the
disc up, accepts it, loads BOOT.BIN, suspends and resumes into it, and the game's main loop runs.
Nothing is drawn yet: the graphics engine is a stub.

## Next steps

1. Model the CD controller: answer servo commands, report disc present / TOC, then sector reads by
   DMA into the 0x3C buffer with IRQ 14. Alternative: hook the boot ROM's CD driver API
   (high-level) with ARM926::hle_hook and serve sectors from the image directly.
2. Once BOOT.BIN loads, find what it needs (graphics, ZSP400 DSP for sound / MJPEG).

## Long seeks (game CD driver)

The game's own driver seeks far with: brake `$EA000200`, sled `$22`/`$23`
(forward/back), traverse count `$Bx` (track count in bits 19..4 of the
payload, e.g. `$B1175080` = 4469 tracks), then polls sense `$B0`.
SENS for `$B` is **active low**: it stays high while tracks are still being
crossed and drops when the count is reached; the driver then stops the sled
with `$20`, re-locks CLV and fine-seeks (`$Bx` again with a smaller count).
With that, the game reaches AMAIN/MAIN.VFF (LBA 52231) and streams
52231..55254 (~6 MB). The game's driver also does not set 0x60 bit 31, so the
decoder must not be gated on it.

Screen state after the load: framebuffer clears only; 09 primitive records
(sprites/textured quads) are not drawn yet.

## Textures (in progress)

- MAIN.VFF is a "vfD0" executable, not an asset pack: header at 0 = magic,
  then 3 segments {load address, size}: 0x104B8000/0x18E000,
  0x107F1000/0x1A4000, 0x101B8000/0x2AC000 (entry probably 0x104BC890).
- Texture objects (OS, 0x2C bytes): +4 x,y (u16, 2D space, x in 16-bit
  units), +8 w,h, +0xC bpp (4/8/16), +0x10 unit, +0x14 palette x,y,
  +0x1C palette source, +0x20 size, +0x24 bytes, +0x28 pixel source.
- Bind (0x10a2be64 / 0x10a2bff4): 8F/90/91 = (y << 12) | x of the palette
  (8bpp) for texture unit 0/1/2 (builder 0x109e61c0), 87 = texture position,
  86 = format. Upload when not yet loaded: 0x10a36030 -> 0x109e4a3c(rect,
  src, bpp), which writes list chunks:
  `D4000002, (y<<16)|x, (h<<16)|w16, 0E000000|words, src address, 95000000`.
  Never called in the current run (all textures already flagged loaded).
- OS textures do sit in RAM at 0x10048000 + y*2048 (rows 720+, 8bpp indices
  seen at 0x101B0000), but MAIN.VFF segment 3 (0x101B8000 = row 736) loads
  over them. Segment 3 holds the game's texture art (bricks, temple carvings,
  menu buttons "Music/Sound/Player/Level", copyright text), packed per texture
  (best partial view: 64x16 tiles, 16 per band) - i.e. source blobs, not the
  chip's layout. Game palettes (8F rows 2305-2317) land in segment 1 code if
  read as RAM, so the game's 2D texture space is probably NOT plain RAM:
  open question where it lives and how the game fills it.
- 09 + address = call a sublist (records start C8.., 40.., 6F.., 0A..);
  3D records carry half-float vertices and 0x7FFF colours.
- No CPU or DMA writes go to unmodelled regions besides small register blocks
  at 0xA1000000 and 0xB0000000 (OS boot).

## Texture memory layout (solved) and where the game stands

- The 2D space is 2048 bytes wide but **block-linear**: blocks of 64 bytes x
  8 rows (32 x 8 pixels at 16 bpp), 32 blocks per block row.
  `addr = 0x10048000 + (y/8)*16384 + (xb/64)*512 + (y%8)*64 + xb%64`, with
  `xb = x*2` (descriptor x is in 16-bit pixels, also for 8 bpp textures).
  Found from statistics: byte autocorrelation peaks at lag 64, a seam every 8
  strips, block below = +32 blocks, block right = +1.
  (Whether the framebuffers use the same blocking is untested: only fills so far.)
- MAIN.VFF ("vfD0") header at 0x104B8000: +0x10 bss start 0x10646000, +0x14
  bss size, +0x20 entry 0x104BC890, +0x2C segment count, then
  {address, size, 0, 0} per segment. Segment 3 (0x101B8000 = row 736) is a
  ready-made texture page in the layout above; the engine (BOOT.BIN, at
  0x109xxxxx-0x10Axxxxx) creates ~190 texture objects from a 0x2C-byte
  descriptor table in segment 1 (0x105418CC...) via 0x10AA1EA8 ->
  0x10A35ED4, with no pixel source = "already in place", so nothing is uploaded.
  With the layout above all 8 bpp game textures decode (menu buttons, level
  icons, title text).
- Texture format word (0x10A361F8): log2 size codes for w (bits 0-2) and h
  (bits 3-5), 0x200 = 8 bpp, 0x400 = 4 bpp, +0x100|0x40000 when descriptor
  +0x10 is set.
- **Open: palettes.** Game palettes sit at rows 2288-2342 (x = 0/256/512/768),
  i.e. 0x104C0000-0x104DC000, which still holds the module's start-up code;
  rows 2104-2272 (dynamic textures) are still empty too, and the module's bss is
  untouched. So the module has not really started: palettes/dynamic textures
  are presumably written once it runs.
- What the engine is doing instead: it loops on the intro video. After
  MAIN.VFF it reads MAIN01.MJP sectors 0-19, then 0-1, about every 1.4 s.
- MJP container: "MIAV", u32 header size 0x40, u32 payload size, 0, u32 frame
  count (300), u16 fps (20), u16 1, u16 width (320), u16 height (160); then
  chunks {tag, u32 0, u32 size}: `00dc` video (JPEG, bytes swapped in pairs;
  only the first frame carries the DQT/SOF/DHT headers, and its scan data does
  not decode with a stock JPEG decoder yet), `01wb`/`02wb` audio
  (612 bytes per frame each).
- Sound chip at 0xB0000000: 64 voices x 0x40 bytes: +0 start, +4 end, +8 loop
  address, +0xC control (bit 4 tested), +0x10 pitch (13 bits), +0x14.. volume/
  envelope words, +0x34 status (bits 16-18 non-zero = voice busy).
  0x1008/0x100C status pair, 0x1010/0x1014 key-on masks (voices 0-31 / 32-63),
  0x1018/0x101C key-off or acknowledge. Not modelled yet (reads return 0).
- 0x90020000 / 0x90030000: two 16550 UARTs set up at t=207 s (divisor 0xF4,
  8N1, FIFO on, receive interrupt on) - the controller ports.
- 0xDC000000 interrupt mask in the game: 0x00365146.
- The engine submits ~60,000 tiny command lists per second.

## Intro video and sound (in progress)

- MJP video frames decode offline: swap bytes in pairs, take the JPEG headers
  (DQT, SOF 320x160 4:2:0, DHT, SOS) from frame 0; later `00dc` chunks start
  directly at their SOS. The scan data has **no 0xFF00 byte stuffing** and no
  EOI: add both and any JPEG decoder accepts it. (A few frames still fail,
  probably odd-length chunk alignment.)
- Why the engine loops on MAIN01.MJP: the stream fills the 20-sector ring
  (mark moved two sectors ahead each time, 0x84 = 0), nothing consumes it, so
  the driver stops the transfer (0x40 bit 2 off), seeks back to the first
  unconsumed sector (target 12:18:35 = LBA 55235), reads two sectors and tries
  again. The CD side is behaving; the MJP player is not pulling data yet.
- Sound is not the blocker. The engine queues commands in a ring at
  0x10AF96D0 (read index 0x10AF96CC, write index 0x10AF96C8) and handles one
  per tick (0x109DFA54, command 5 = key-on). Voices 1/2 are re-keyed every
  ~171 ms and 16-21 / 45-48 in groups: a note sequencer playing samples from
  VFF segment 2. Harness model: key-on 0x1010/0x1014, key-off 0x1018/0x101C,
  +0x34 bit 16 while a voice is busy (looping when control & 0x1E0), length
  from end-start at 44100 bytes/s * pitch/0x1000 (a guess). VFSND logs it.
- Display base (0xB8000160) is rewritten 60 times a second with 0x1004A000,
  0x100C0000 or 0x100C2000, but the loader screen is *rendered* ~1077 times a
  second (each state change or primitive is its own tiny list): frame pacing
  is not tied to the display yet - worth checking against the vblank status.

## The game is in its 3D hub (t ~ 190 s onward)

- The engine is not stuck on the video. From ~190 s it renders a full 3D scene
  about 125 times a second (~96 meshes, ~2250 triangles per frame): the temple
  room with the puzzle box from the intro. The MAIN01.MJP reads are a
  read-ahead that nobody consumes (the MJP player code at
  0x109A2900-0x109A4900 is never executed in that window; its state block is
  at 0x10AD9AF0, default name "\AANY_AVI.mjp;1").
  What is missing is the 3D renderer and controller input, not the disc.
- Chip registers written directly: 0xA8000098 = base of the 2D space
  (**ROM OS: 0x10800000, game OS: 0x10048000**), 0x90/0x94 = 0x00F00200
  (240 x 512). The harness fill code now follows 0x98 and the block layout.
- Buffers in the game: six 512x240 regions at x = 0/512, y = 0/240/480.
  (0,0) is cleared with 0x199 every frame (depth); the display base cycles
  through (512,0), (0,240), (512,240) = 0x1004A000, 0x100C0000, 0x100C2000.
  82/83 = clip corners as (y << 12) | x.
- Transform registers (list packet 20RRNNNN: N counts 16-bit values, packed two
  per word, low half first): 0x3C scale x3 (4.12), 0x40 rotation 3x3 (4.12),
  0x4C translation x3 (s16), 0x50 second 3x3 (lighting?), 0x5C x3, 0x60 x3,
  0x6C x1 (0x77BD), 0x74/0x75, 0x78 x2 (0x0172, 0x020F: screen offsets?).
- Mesh sublist: `0A000000, vertex buffer address, 4478NNNN` then N 16-bit
  indices (word offsets into the buffer: vertex i = index / 5), triangle list.
  Vertex = 5 words: 8 half floats (x, y, z, three more, u, v) + colour 0x7FFF.
  Other record heads seen: 4038xxxx, 4078xxxx, 6FB8xxxx.
- Texture state per mesh: 8F = palette (y << 12) | x, 87 = texture position,
  86 = format (size = 8 << code for w and h).
- py/scene.py + py/wire.py draw a wireframe of one dumped frame (VFFRAME=ms
  dumps the lists); py/render.py is a rough textured attempt. Projection used
  there is a guess: camera = R * (v * scale * 4096) + T, visible z negative,
  screen = xy / -z, autoscaled.
- Still open: palettes (rows 2288-2342 would overlap module code at base
  0x10048000), exact projection (0x6C, 0x78), lighting, the 40/6F record types.
- New harness switches: VFFLOW=lo,hi (with VFTRACE window) logs every
  non-sequential PC in a range; VFBASE logs writes to 0xA800008C-A4; VFFRAME.

## Palettes (solved) and draw records

- **Palette rows are biased by 240**: the palette for a texture is the 256
  16-bit entries at (cx, cy - 240) in the same block-linear space. Game
  palettes (cy 2288-2342) are therefore rows 2048-2102 = the tail of VFF
  segment 3 (0x10448000 onward), not module code. With that, the 8 bpp game
  textures decode in full colour. (Why 240: unknown, it equals the buffer
  height in registers 0x90/0x94; the texture position itself has no bias.
  A handful of textures still look wrong: probably 4 bpp or shared palettes.)
- Sublist commands: `0A000000 addr` selects a vertex buffer (it stays selected
  across sublists); primitive header `TTFFNNNN`: TT = 0x44 triangle list,
  0x40 and 0x6F other topologies (strip/fan, not confirmed), FF bit 6 set =
  N 16-bit indices follow (padded to a word), clear = N inline 5-word
  vertices follow; `04000000` ends the sublist.
- Packet `10000000 addr` appears once per frame (address 0x10AFB34C, zeros in
  the dump): purpose unknown.
- 21 of the 96 meshes per frame are drawn with a zero translation: probably
  the 2D/overlay layer, with a different transform set-up.

## Vertex number format, output buffers, first menu render

- Vertex components are **not IEEE half floats**: 16-bit floats with 1 sign
  bit, 6 exponent bits (bias 31) and 9 mantissa bits; 0x3E00 = 1.0, exponent 0
  = zero. Check: the three "extra" components per vertex are unit normals
  with this decoding. Vertex = position xyz, normal xyz, u, v (0..1, v points
  up: sample row = (1 - v) * height), then a 16-bit colour word.
- With positions decoded this way, camera = R * (pos * scale) * 4096 + T
  (T in the same 4.12 units), depth = -z, and a guessed perspective
  (x' = 256 + f*x/-z, y' = 120 + 0.47*f*y/-z, f ~ 420), py/render.py draws the
  hub/menu room in colour: "Score" banner, score digits, stars, paw prints,
  pillars and the three menu buttons. Palette entry 0 is treated as
  transparent. Button faces are still smeared and the true projection
  constants (registers 0x6C, 0x78) are not derived.
- Packet `0C000000 addr` precedes some meshes: the chip is expected to WRITE
  the transformed vertices of the following mesh to that address (the buffer
  at 0x10DE8DF0 still holds the heap fill pattern 0xB2B2 because the harness
  does not do it). Later sublists select that buffer with 0A and draw indexed
  strips from it with registers 0x3C/0x40 set to 0x3E00 (1.0 in the vertex
  float format) and zero translation: that is the "zero translation" group
  (animated models and probably the button faces).

## High rows, output buffers in practice

- **Rows 2048 and up sit 240 rows lower in memory**, for textures and palettes
  alike (y >= 2048 -> y - 240). So the "palette bias" above is really this:
  e.g. texture (0,2064) 256x128 with palette (0,2308) is the "Game Zone"
  button at rows 1824.. with its palette at row 2068. Dynamic-looking
  positions (2176, 2240) are ordinary rows of VFF segment 3 as well.
- `0A000000 addr` also appears in main lists, not only inside sublists.
- Output buffers: captures are laid end to end from 0x10DE8DF0 (20 bytes per
  vertex) and later draws index into the whole run from that base. Storing
  camera-space positions there and drawing them with an identity transform
  puts the menu buttons and other pieces in the right places (strips, not
  fans). Their texture V axis is still wrong (button text upside down).
- py/render.py now draws the room with walls, ceiling, pictures, pillars,
  score board and the four buttons (New Game / Load Saved Game / Options /
  Game Zone).

## Open points on the menu buttons (observations, not solved)

- The shared button mesh (sublist 0x105C926C, 118 vertices) has its front face
  at local z = +0.022 with local +y = top and v = 0.08 (top) .. 0.49 (bottom);
  the back/side faces use v 0.63 .. 0.92. Elsewhere in the scene camera-space
  +y is down the screen and sampling row = (1 - v) * height reads correctly
  ("Score", digits), so captured pieces need something extra: at draw time
  register 0x50 is diag(1, -1, -1) (4.12) while 0x3C/0x40 are 1.0 in the
  vertex float format.
- In memory the button art starts 16 rows below the descriptor position
  ((128,1408) 256x128: face at rows 1422-1492, state strips below, and the
  strips run 6 rows into the next texture), while the "High/Score" texture
  (576,1728) is exact. So either v is offset by 0.125 for these draws or the
  format word matters (buttons 0x60265, banner 0x40264).
- Texture descriptors are packed tightly (x in 16-bit units even for 8 bpp:
  an 8 bpp texture W wide occupies W/2 units), e.g. (128,1392,512x16),
  (128,1408,256x128), (128,1536,...), (128,1664,...), (0,2064,...).

## Live rendering in the harness

- `VFRENDER=ms` turns on an experimental software renderer inside vf.cpp
  (rn_* functions): it tracks list state all the time, rasterises from the
  given time, performs the 0C write-back into RAM, and at exit writes fb.ppm
  (shown buffer) and vram.ppm (the 1024x720 buffer area). At 240 s the three
  render targets hold the menu room, same as the offline script.
  Cost: a run to 240 s takes ~25 minutes instead of ~12.
- Packet 81 = projection centre, (y << 12) | x: (768,600), (256,600), (768,360)
  for the targets (512,480), (0,480), (512,240). The renderer uses it.
- Render targets are (512,240), (0,480), (512,480) (82/83 clip), but the
  display base register cycles (512,0), (0,240), (512,240): each is exactly
  240 rows (0x78000 bytes) above a target. The harness therefore shows
  "display base + 240 rows"; why is not understood (an LCD start-line setting?).
- D3 fills: (512,480)-style positions clear a colour target with 0; the fill at
  (0,0) with 0x199 is separate (depth clear, shows as orange if viewed as
  colour).

## Controller ports

- Two 16550 UARTs: port 0 = 0x90020000 (IRQ 1), port 1 = 0x90030000 (IRQ 2);
  divisor 0xF4, 8N1, receive interrupt. Port 0 also carries the OS banner text
  at start-up. Driver code 0x10A1BF00-0x10A1F260 (receive handlers 0x10A1E4EC
  and 0x10A1EBD0, same logic per port).
- Packet both ways: `FF, 01, L`, then L raw bytes packed two-into-three
  (`0x40 | r0 & 0xF`, `0x40 | r0 >> 4 | (r1 & 3) << 4`, `0x40 | r1 >> 2`),
  then the 16-bit sum of (1 + L + raw bytes) packed the same way.
- Console -> controller every ~15.3 s on each port: raw `40 03` (probe /
  keep-alive; bytes on the wire FF 01 02 40 74 40 46 44 40).
- Controller -> console, by L:
  - 2: raw[0] = 1..4 sets the device type (0x10B3ADA8) and "present"
    (0x10B91A30); raw[0] = 0x20 sets a flag at 0x10B95C30.
  - 4: 12-bit X = raw[0] | (raw[1] & 0xF) << 8, 12-bit Y = raw[1] >> 4 |
    raw[2] << 4, raw[3] bit 0 = a flag; min/max are tracked (a pointing or
    drawing device).
  - 6: state record, queued (up to 9) at 0x10B3A248 and unpacked at
    0x10A1C43C: bits 4-11 and 13-20 = two 8-bit axes, bits 22-37 = 16
    buttons (compared with the previous value to find changes).
- Harness: VFPAD=n answers the probe with type n (accepted: type and present
  are set); VFPADKEY="ms:mask,..." then sends state records with that button
  mask (axes 0x80). Which bit is which button, and the polarity, are being
  tested.

## Save states (harness)

- `VFSAVE=file:ms` writes the whole machine (CPU incl. banked registers and
  CP15, RAM, register map, drive/decoder/interrupt/timer/sound/pad state,
  renderer state) at that emulated time; `VFLOAD=file` starts from one.
  Step counts on the command line stay absolute.
  Checked: a run loaded from a state ends with byte-identical RAM to an
  uninterrupted run.
- Working state: menu.state at t = 212000 ms (step 6946265), made with
  `VFPAD=1 VFPADKEY=0:0 VFRENDER=999999999` so the controller is present and
  the renderer state is tracked. ~64 MB, kept outside the repo.
  `VFLOAD=menu.state VFRENDER=212050 ./vf rom 6951000` renders menu frames in
  about a second instead of a 25-minute boot.
- The renderer no longer transforms vertices while it is only tracking state.

## Controller input reaches the game

- State records are accepted: after a record with button mask 0x0008 the
  driver's "last buttons" word (0x10B7A310) reads 0x0008. Buttons are active
  high.
- The driver regroups the 16 button bits into bytes of an output struct
  (0x10A1C668..): out[3] <- bits 0x0001, 0x0002, 0x0008; out[4] <- 0x8000;
  out[0] <- 0x0004, 0x0400, 0x0100, 0x0200; out[2] <- 0x0800, 0x1000, 0x2000,
  0x4000; out[1] <- 0x0010, 0x0020, 0x0040, 0x0080; out[5]/out[6] = the two
  8-bit axes.
- Tested from the menu save state, one bit at a time (300 ms press, frame
  compared with a no-input run): bits 2 (0x0004) and 12 (0x1000) act as
  confirm - the menu board flips to a second screen; bit 8 only changes the
  animated model; no other single bit changes the menu picture.
- Not found yet: what moves the menu cursor. Record axes (centre 0x80 or 0),
  the L=4 analog packet (VFPADSTICK="ms:x:y") and single button bits did not
  visibly move it. Next step is to read the code that consumes the output
  struct.
- py/padtest.sh runs one scripted-input test from menu.state and saves the
  frame.

## Controller mapping and menu fixes

- The pad object's update (0x10AF5298...) reads the unpacked struct: out[5]
  and out[6] are **signed** axes with centre 0 (|v|*2, capped at 0xFF,
  compared with a dead-zone at object +0x1C/+0x20) and become direction flags
  0x10000000/0x20000000 (out[5] -/+) and 0x40000000/0x80000000 (out[6] +/-).
  In the record: bits 4-11 = out[6] = vertical (+ = down in the menu),
  bits 13-20 = out[5] = horizontal.
- Confirmed from the menu state: stick down then confirm opens a different
  screen than confirm alone, so the cursor moves. Confirm = button bit 2
  (0x0004) or bit 12 (0x1000).
- Driver timing: a record must arrive at least every 1.5 s or the pad is
  dropped (1 s triggers a re-request). 0x10A1DB9C can also poll with raw
  `00 03`, but the game relies on unsolicited records.
- VFPADKEY mask: bits 0-15 buttons, 16-23 vertical axis, 24-31 horizontal
  axis (signed bytes, 0 = centre).
- Renderer: captured (0C) pieces are mirrored in y about their own origin
  (consistent with register 0x50 = diag(1,-1,-1) at draw time); with that the
  button text is upright. 256x128 textures below row 2048 are sampled 16 rows
  lower (empirical, NOTEX16 turns it off). 4-bit textures (format bit 0x400)
  are sampled low nibble first (untested). The four main-menu buttons now read
  correctly; items on the two sub-screens are still garbled.

## The screen behind "New Game" (stuck here)

- Confirm with the cursor on the first menu item opens a board with a music
  icon, an X icon, a "Save Game" plate, a row of six gold pieces of growing
  size and a purple plate; confirm on any of the other three items opens a
  different board. The textures and meshes for these decode correctly (the
  gold pieces are real 3D objects, texture (704,2192) 64x64).
- On that board nothing reacts: every button bit, both stick axes and repeated
  confirms leave the picture unchanged, even several seconds after the
  transition sounds (voices 56-59) have ended. The pad is still polled
  (0x10A1C348 keeps being called) and frames keep animating.
- Ruled out: the MJP video player (0x109A2900-0x109A4900 never runs), texture
  uploads (no D4 packets), new hardware accesses (only the periodic timer,
  RTC and GPIO reads at 0x90000018/58/98/D8, which the harness answers with 0).
- PC samples with stack words (VFSAMPLE + VFSTACK, now fixed and deeper) show
  no return addresses inside the VFF module's code on the main thread: the
  menu flow is driven by engine code, with module data (display lists) as
  arguments.
- Leads: the texture set has "Memory card error. No saved game.", "inserted",
  "Overwrite?", "Saving" strings, so this is probably the save-slot step and
  it may be waiting on a memory-card check; GPIO inputs are the first thing to
  try.
- Harness additions: VFRNLOG (one line per drawn mesh with texture, format,
  palette, screen box), second state sg.state at 215000 ms on this board,
  py/sgtest.sh.
