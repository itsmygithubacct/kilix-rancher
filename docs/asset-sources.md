# Asset provenance and generation record

All project-specific runtime art and audio is original to Kilix Rancher and is
distributed under the repository's MIT License to the extent applicable. No
assets from *Monster Rancher* or another commercial game are included.

## Runtime visual assets

| Runtime file | Source | Processing |
|---|---|---|
| `assets/kilix.ppm` | Original Kilix identity plate generated with OpenAI's built-in image-generation tool | Flat green source was chroma-keyed with the installed OpenAI image skill helper, visually inspected, trimmed, resized to a fixed 384×384 ground-anchor canvas, and flattened over runtime magenta chroma |
| `assets/backgrounds/ranch.ppm` | Original ranch environment generated with OpenAI's built-in image-generation tool | Center-cropped and normalized to binary 640×360 PPM |
| `assets/backgrounds/arena.ppm` | Original arena environment generated with OpenAI's built-in image-generation tool | Center-cropped and normalized to binary 640×360 PPM |
| `assets/opponents/*.ppm` | Six original rival monsters (Mossnub, Dewdrop, Mistwing, Stonecalf, Moonmoth, Duskcub) generated with the local Gemini image tool (`gemini-3-pro-image`) | Each rendered on a flat chroma-key background, keyed to alpha, trimmed, ground-anchored and scaled into a 320×320 canvas, then flattened over runtime magenta into binary P6 PPM |

This repository ships **only runtime assets**. The lossless generation sources,
intermediates, and the `process_sprite.py` conversion script are kept out of the
game tree under `<workspace>/kilix-rancher/` (see its `README.md`). The full
recipe — model, prompts, chroma-key choice, and renderer contract — is at
`<workspace>/image_generation.md`.

### Rival monster generation

The six rivals share a structured prompt (species, epithet, palette, "cozy
monster-raising" style, three-quarter pose facing slightly left, flat
chroma-key background, no shadow/text/props). Non-green creatures are generated
on green `#00ff00`; the green creature (Mossnub) is generated on magenta
`#ff00ff` instead, because a subject sharing the key colour keys out with the
background. `process_sprite.py` auto-detects the border key colour, so the same
command processes either. The renderer (`draw_opponent` in `src/render.c`) keys
magenta at draw time and ground-anchors each plate like the Kilix. The
validated transparent Kilix intermediate lives with the other sources at
`<workspace>/kilix-rancher/asset-sources/kilix.png`. Runtime PPM dimensions are
enforced by `make validate-assets`.

### Kilix identity prompt

```text
Use case: stylized-concept
Asset type: game character sprite plate
Primary request: an original fire-kitten monster named Kilix for a cozy monster-raising game; cute but battle-capable, clearly feline, compact athletic body, oversized expressive amber eyes, ember-orange fur, cream muzzle and chest, dark charcoal ear tips and paws, flame-shaped tuft at the end of the tail, tiny glowing ember markings on cheeks and shoulders
Scene/backdrop: perfectly flat solid #00ff00 chroma-key background for background removal
Subject: one single full-body Kilix, fully visible from ear tips to paws and tail, standing in a confident friendly three-quarter hero pose facing slightly right
Style/medium: polished hand-painted 2D game sprite with chunky PS1-era pre-rendered charm, crisp cel-shaded silhouette, simplified fur clumps rather than fine individual hairs, original design
Composition/framing: centered, generous even padding on all sides, character fills about 72 percent of the canvas, no crop
Lighting/mood: warm internal fire glow, cheerful and adventurous, strong readable silhouette
Color palette: ember orange, cream, charcoal, golden yellow, small hot-coral highlights; do not use green anywhere in the subject
Constraints: background must be one perfectly uniform #00ff00 color with no shadows, gradients, texture, reflections, floor plane, or lighting variation; crisp hard outer edges suitable for chroma-key removal; no cast shadow; no contact shadow; no reflection; exactly one character; no props; no text; no logo; no watermark; no border
Avoid: realistic delicate fur strands, photorealism, existing game characters, copyrighted creature designs, extra limbs, duplicate tails, accessories, scenery
```

### Ranch environment prompt

```text
Use case: stylized-concept
Asset type: game environment background
Primary request: an original cozy monster-training ranch built for a fire-kitten creature-raising game
Scene/backdrop: a sunlit highland ranch inside a distant volcanic valley; small round-roof stone barn with an open doorway, low split-rail fences, a straw training circle, water trough, a few ember-red wildflowers, hazy mountains and a peaceful dormant volcano far away
Subject: inviting empty ranch yard with a broad open low-contrast grassy-dirt center where a game character will be composited later
Style/medium: hand-painted 2D game background with late-1990s pre-rendered console charm, chunky shapes, warm painterly pixels, original setting, polished but not photorealistic
Composition/framing: 16:9 wide establishing view, slightly elevated camera, foreground path leading into the broad center, environmental detail concentrated around the edges, no central character or prop blocking the play area
Lighting/mood: soft golden morning light, cheerful, safe, pastoral, faint ember motes near the barn chimney
Color palette: warm sage green, honey gold, terracotta, dusty blue mountains, ember orange accents
Constraints: environment only; broad clear center; no creature; no people; no UI; no text; no signs with writing; no logo; no watermark; no border; no recognizable copyrighted setting or design
Avoid: checkerboard, game board, isometric grid, dark horror mood, busy central detail, modern objects, photorealism
```

### Arena environment prompt

```text
Use case: stylized-concept
Asset type: game battle arena background
Primary request: an original small tournament arena for a fire-kitten monster-raising game
Scene/backdrop: a circular sunken stone arena built into a friendly volcanic highland town; worn sandstone fighting floor, low curved walls, colorful pennants without writing, distant cheering silhouettes, mountain peaks and a glowing caldera on the horizon
Subject: empty battle stage with a very broad low-contrast central floor for two creatures and combat effects to be composited later
Style/medium: hand-painted 2D game background with late-1990s pre-rendered console charm, chunky shapes, warm painterly pixels, original setting, polished but not photorealistic
Composition/framing: 16:9 wide side-on battle view, slightly elevated camera, arena floor spans most of the lower two-thirds, left and right combat positions clear, detail concentrated around edges and upper background
Lighting/mood: late-afternoon festival light, exciting and welcoming rather than brutal, warm rim light
Color palette: sandstone gold, burnt orange, deep teal shadows, faded crimson and cream pennants
Constraints: environment only; empty stage; no creature; no people on the arena floor; no UI; no text; no logos; no watermark; no border; no recognizable copyrighted setting or design
Avoid: colosseum realism, gore, weapons, dark horror mood, busy central detail, modern sports signage, checkerboard, grid, photorealism
```

## Runtime audio assets

`assets/sfx/*.wav` contains six original effects synthesized locally by
`tools/generate_audio.sh` with deterministic SoX oscillators, filtering, and
envelopes. No recordings, sample libraries, or third-party compositions are
used. The effects are mono, 44.1 kHz, signed 16-bit PCM.

## Code provenance

The Kitty graphics transport and asynchronous presenter in `src/term.c` are
adapted from the MIT-licensed `~/chess-bash` project. Its artwork, chess logic,
music, sound effects, and game-specific renderer were not copied. Kilix
Rancher's game rules, characters, renderer, generated assets, procedural
opponents, effects, UI, and writing are project-specific.
