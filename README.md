# Artwork

The protagonist is a hand-authored 16 × 18 pixel sprite encoded in `src/main.c`; the renderer recolors its cloak for each world and animates it with run, jump, dash, and rune effects. Room tiles, four palettes, the crawler, and four guardian silhouettes are drawn from compact GraphX primitives. These are native C assets, so no bitmap conversion dependency is needed.

The bitmap reference in `player/wayfarer.pgm` is provided for editing and review; the compiled game uses the matching C sprite data.
