# ABYSS

ABYSS is an original, C-based TI-84 Plus CE adventure built with the CEdev v15.0 toolchain. It has a side-scrolling 44 × 15 tile room engine, four themed worlds, twenty authored room maps, accelerating keypad movement, buffered jumps with coyote time and variable height, hazards, treasure, rune interactions, four guardian rune puzzles, a Waystation, three coin upgrades, save data, and an ending scene.

## Build

Install the official [CEdev v15.0 release](https://github.com/CE-Programming/toolchain/releases/tag/v15.0), then put its `bin` directory on `PATH`. From this directory run:

```sh
make clean all
```

The toolchain writes `bin/ABYSS.8xp` and `bin/ABYSS.map`. On calculator OS 5.5.0 and later, native C programs need an arTIfiCE setup. The toolchain documentation warns that OS 5.5.0+ no longer runs these programs natively.

## Controls

- Left / Right: move
- 2nd: jump
- Up: climb a ladder tile (the current maps do not yet place ladders)
- Alpha: pulse nearby runes
- Mode: use the highest unlocked ability
- Enter: confirm / interact
- Clear: pause or return

Mode activates the highest ability unlocked so far: Heavy Landing, Phase Dash, Protective Parry, Ground Wave, Whirlwind, and Abyssal Resonance unlock as the corresponding levels are cleared. Coins, individually collected room coins, and permanent upgrades are stored in an archived, checksummed `ABYSSSV` AppVar. Each guardian room requires three rune seals before its door opens.

## Current implementation limits

This is a playable campaign foundation, not yet the complete game in the supplied acceptance list. Sprite motion currently covers idle, run, jump/fall, dash, and rune effects; climbing, crouching, variable jump height, coyote time, moving platforms, collapsing floors, and changing gravity are not implemented. The maps have distinct platform and hazard arrangements, but optional secret chambers and level-specific puzzles are still sparse. Creatures and guardians use compact drawn silhouettes; they do not yet have the requested individual animation sets. Sound, relic gameplay, and a full guardian phase system are also unfinished.

The project includes a structural map test and a GitHub Actions build workflow. No CEmu or calculator run has been performed. The `.8xp` is intentionally absent until a successful CEdev build produces it.
