# Claude-style pixel creature: style brief

A self-contained brief for recreating the pixel creature from 5DChess's Pixel theme with any image tool or artist. It is an original design, not Anthropic's logo.

## Concept
A tiny, friendly blocky creature drawn as 16×16 pixel art. Every variant uses **the same body**. Only **what sits on top of it** changes: a hat, crown, tool or accessory. That's how you tell variants apart.

The mood is cute, calm and a little mischievous. It has no mouth; all the expression is in two vertical slit eyes.

## Canvas
- **Grid:** 16×16 pixels, transparent background, one creature centred.
- **Hard pixels only:** no anti-aliasing, gradients, blur, glow, drop shadows or dithering.
- **Export:** scale up with nearest-neighbour only, ×8 for 128×128 or ×16 for 256×256, so pixels stay crisp squares.
- **Colours:** no text, letters or logos on the creature, and nothing beyond the six palette colours below.

## The body (never changes)
Shown on the grid below (`.` = transparent):

```
row 6   ...HBBBBBBBBB...   top edge, highlight pixel at the top-left corner
row 7   ...BBBBBBBBB....
row 8   ..BBBEBBBEBBB...   one-pixel side nubs stick out left and right
row 9   ...BBEBBBEBB....   eyes are 1 wide × 2 tall, three body pixels apart
row 10  ...BBBBBBBBB....
row 11  ...O.O...O.O....   four stubby legs, 1 pixel wide
row 12  ...O.O...O.O....
row 13  ...O.O...O.O....
```

Legend: `B` body, `S` shade, `H` highlight, `E` eye, `A` accent, `O` outline.

- **Body:** a wide, flat rectangle about 9–10 px wide and 5 px tall, sitting on four thin legs with gaps between them. The bottom of the body may carry one row of shade.
- **Eyes:** two vertical slits, 1×2 px, level with the side nubs.
- **Highlight:** a single pixel at the top-left corner of the body. Light comes from the top-left.
- **Outline:** after drawing, add a 1 px outline around the whole silhouette, in every gap and on every edge.
- **Headgear zone:** rows 0–5. Anything identifying goes here, and may overhang the body by 1 px.
- **Blink frame (optional):** turn each eye into a closed slit. The top eye pixel becomes body colour, the bottom one becomes shade.

## Palette (exactly six colours per creature)

| Role | Main (terracotta) | Dark variant (charcoal) |
|---|---|---|
| Outline | `#5A2A18` | `#121110` |
| Body | `#D97757` | `#3D3A36` |
| Shade | `#B85C3D` | `#2A2825` |
| Highlight | `#F4B89C` | `#6B6660` |
| Eyes | `#1F1E1D` | `#F0A27E` (warm, glowing) |
| Accent (headgear) | `#F2C14E` gold | `#C9A227` |

Use the terracotta set by default. The charcoal set is the "night" or opposing twin.

## Making a new variant
1. Keep the body, eyes, legs and palette identical.
2. Describe the subject only through headgear or a held item in rows 0–5, built from the accent colour plus outline. Use 1–3 shapes at most, and make sure the silhouette still reads at 16 px.
   - A chef: a tall white-ish toque (use the highlight colour).
   - A wizard: a pointed hat.
   - A coder: tiny headphones.
   - A gardener: a sprout.
   - Chess pieces, as in the game: king = cross on a crown; queen = five-point tiara; rook = battlements; bishop = mitre with a diagonal slit; knight = snout pushed forward with ears; pawn = a single antenna.
3. If the subject needs a pose change, add at most 1–2 px. For example, the knight's head leans 2 px forward. Never redraw the body shape.
4. Check the silhouette alone, filled solid black. You should still recognise the variant.

## Example: the king, full 16×16 grid

```
................
.......A........
......AAA.......
.......A........
....A..A..A.....
....AAAAAAA.....
...HBBBBBBBBB...
...BBBBBBBBB....
..BBBEBBBEBBB...
...BBEBBBEBB....
...BBBBBBBBB....
...O.O...O.O....
...O.O...O.O....
...O.O...O.O....
................
................
```

(Then add the 1 px outline around every filled pixel.)

## Prompt for AI image generators
> 16x16 pixel art sprite of a small cute blocky creature, wide flat rectangular terracotta body (#D97757) on four thin stubby legs, two tall narrow black slit eyes, no mouth, tiny one-pixel nubs on each side, single light highlight pixel at the top-left corner, 1-pixel dark brown outline (#5A2A18), wearing a gold (#F2C14E) [YOUR HEADGEAR HERE] on top, limited 6-colour palette, crisp hard pixels, no anti-aliasing, no gradients, transparent background, centered, upscaled with nearest-neighbour, game sprite, flat lighting from top-left

Replace `[YOUR HEADGEAR HERE]` with the subject, e.g. "tiny chef's hat" or "pointed wizard hat". For the dark twin, swap in "charcoal body (#3D3A36) with warm glowing orange eyes (#F0A27E)".

## Don'ts
- Don't add a mouth, arms, fingers or a face outline. The eyes do all the work.
- Don't use the Anthropic or Claude logo, the asterisk mark, or the wordmark. This creature is an original design inspired by the colours only.
- Don't present results as official Claude or Anthropic artwork.
