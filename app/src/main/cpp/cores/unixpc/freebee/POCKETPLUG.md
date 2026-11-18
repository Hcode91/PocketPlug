# FreeBee in PocketPlug

Taken from https://github.com/philpem/freebee at commit
058aef25084ec68f4c3e464cbfdcfa3fb1da2628 (Musashi submodule at
2e6f563b79db739c8c082bc77ad7ebee069ba447). FreeBee's LICENSE file is the GPL
version 3 (`../../../../../../../LICENSES/GPL-3.0.txt`); its `main.c` banner
mentions the Apache License 2.0, but `main.c` isn't used here and the files that
are carry the repository's GPL-3 licence. Musashi is MIT (`musashi/readme.txt`).

Changes for PocketPlug (Android, no SDL):

- `keyboard.c`: the SDL key map is gone; `keyboard_key()` takes the 3B1's own scan codes.
- `wd2010.c`: SDL timers replaced by a millisecond countdown, `wd2010_tick()`.
- `dialer.c`: the SDL audio callback became `dialer_render()`, pulled once a frame.
- `state.c`: ROMs are handed over in memory (`state_set_roms()`).
- `fbconfig.h`: fixed settings instead of the TOML file.
- `i8274.c`: no serial PTY (`POCKETPLUG` defined).
- `musashi/m68kfpu.c` renamed `m68kfpu.inc` (it is #included, not built alone).
- Not used: `main.c`, `fbconfig.c`, `toml.c`, `lightbar.c`, the tools.
