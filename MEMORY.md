# Memory and build status

## Known design sizes

| Item | Size / status |
| --- | ---: |
| One active tile map (`44 × 15` bytes) | 660 bytes |
| Twenty room maps, including terminators in C strings | about 13.5 KB read-only data |
| Player state | 18 bytes before compiler padding |
| Save record | about 50–52 bytes depending on ABI padding (fixed fields, per-coin flags, checksum) |
| Full-screen game-owned framebuffer | none |
| GraphX drawing buffers | GraphX uses calculator video memory; it is not a game-owned RAM framebuffer |
| Linker-measured code/data/BSS and stack margin | **pending a successful CEdev build** |

The CEdev documentation describes roughly 4 KiB of stack and a 60 KiB shared BSS/heap region. These source-level counts do not establish compliance with the requested total active-RAM ceiling. Check the generated `bin/ABYSS.map` after building; this session could not obtain linker measurements.

## Build attempt

Official CEdev v15.0 was downloaded and unpacked in session scratch. Windows Application Control blocked the bundled `make.exe` and `ez80-clang.exe` before execution (system DLL relocation / application-control errors). WSL is not installed in this environment. No executable or linker report has been fabricated. The checked-in workflow installs the official v15.0 Linux release, runs map tests, builds, and uploads the `.8xp` plus map file.
