# Hexen II for MC1

Hexen II (Hammer of Thyrion / uHexen2) ported to the [MC1](https://gitlab.com/mrisc32/mc1)
computer and its MRISC32-A1 CPU. Branch `mc1` holds the port; `master` is uHexen2.

Build for the MRISC32 simulator (needs the MRISC32 GNU toolchain):

    cd engine/hexen2 && mkdir out && cd out
    cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=../mc1-toolchain.cmake -DCMAKE_BUILD_TYPE=Release ..
    ninja

Run `mr32sim ./mc1hexen2` from a folder that has `data1/` with the game data (not
included; the August 1997 demo works). The PC build used for reference is
`make h2 DEMO=1 USE_SOUND=no USE_MIDI=no USE_CDAUDIO=no` in `engine/hexen2`.

What the port adds:

- `sys_mc1.c`, `vid_mc1.c` (320x240, two VRAM pages with a VCP each, z-buffer in
  VRAM), `in_mc1.c`, `net_mc1.c` (loopback only).
- MRISC32 speedups: span drawing from mc1-quake (`d_scan_mr32.s`), vector alias model
  spans and vertex setup, single-precision math where uHexen2 used double.
- Benchmarking: `timedemo` prints cycle-counter markers and quits with `-tdquit`,
  `vid_dumpframes N` dumps frames, `-extrapaks` lets the demo load extra paks.
- `mc1/mdllod.py` builds reduced-polygon models from your own pak files (the models are
  Raven's, so generate them locally rather than sharing them). `mc1/route.py` records a
  scripted benchmark demo on the PC build.

Licensed under the GNU GPL, as uHexen2.

## Hot function order

`hot_functions.txt` lists the functions that run most per byte; the build places them first
in `.text` so the per-frame code shares the instruction cache. To regenerate it after big
changes: run `prof/h2prof.sh NAME REF`, then
`mc1/hotlist.py perf_h2_NAME.log <(mrisc32-elf-nm -S mc1hexen2) 30000 > mc1/hot_functions.txt`.
