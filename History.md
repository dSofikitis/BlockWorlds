# Changelog

## ToDo

### Gameplay

- Survival mode: health, hunger, damage, armor
- New "block types": items, like torch and tools
- Block-break timing + tool tiers (wood / stone / iron / diamond)
- Dropped items and pickup
- Crafting table + forge (craft & reapir tools and armor) + furnace (shape-match recipes) + anvil (just to upgrade tools and armor)
- Mobs: passive + hostile, simple-state-machine AI, spawn rules, spawn rate in per-world settings (add to .bw)
- Combat (PvP & PvE with realistic HP reductions)
- Particle effects (block break, footstep dust, water splash)
- Fix the hideous sounds that walking and water dip / get out actions have

### Multiplayer

- LAN multiplayer
- Per-user permission to restrict commands ("cheats") (saved to per-world settings)

---

## 0.0.1 (Alpha) — Audio, packs, CoreSkin codec, the .blockworlds store, clouds, weather, and per-pixel light

- Hand-written CoreAudio / AudioUnit backend: a `pthread`-guarded 16-voice mixer (stereo float32 @ 48 kHz); dropped the vendored miniaudio
- Audio packs: 19 effects via a hand-rolled RIFF/PCM reader (16-bit / float, resampled to 48 kHz), per-sound fallback to the procedural synth; footsteps vary by block underfoot, plus jump / land / swim / pickup / UI ticks
- All state under a hidden `.blockworlds/` dir — `saves/` (one dir per world), `skins/`, `audio/`, and a sparse `settings` file
- Custom **CoreSkin** PNG decoder (`coreskin.c`): from-scratch inflate + PNG parse + unfilters → RGBA8, pixel-exact vs. libpng; replaced stb_image
- Title-screen texture- and audio-pack pickers: validate, preview, hot-swap the live pack, and persist the choice
- Cross-platform port (macOS / Linux / Windows·MinGW): `glcompat` GL 4.1 loader, a portable audio core with per-OS backends (CoreAudio / ALSA / WinMM), one host-detecting Makefile
- Volumetric clouds: a world-space ray-marched cloud slab in `sky.frag` (3D-noise density, Beer–Lambert with a sun-ward light march), camera parallax, sun tint, and night / overcast response — replaced the flat fbm layer
- Weather: camera-wrapped rain (`GL_LINES`) / snow (`GL_POINTS`) particles with surface + roof culling, an auto biome-aware cycle and `/weather` command, and an `overcast` ramp that greys the sky, dims the sun, and thickens fog
- Per-pixel block light: a 128³ `GL_R8` 3D light texture flood-filled from glowstone and sampled trilinearly in `cube.frag`, replacing the per-vertex glow; added `glTexImage3D` + point-sprite GL bindings

---

## 0.0.0.9 — Real-time renderer, atmosphere, and survival inventory

- Off-screen rendering foundation: new `framebuffer` module (depth / raw-depth / color targets) + `mat4_ortho`; per-frame pass chain: sun shadow depth → water reflection → SSAO (depth pre-pass + blur) → gradient sky → opaque/glass → reflective water. MSAA 4×
- Dynamic sun shadow maps replace the baked `sun_occluded`: 4096 orthographic depth pass, 5×5 PCF, normal-offset + slope-scaled bias, `GL_DEPTH_CLAMP`, light-space texel snapping (no edge crawl or acne); track the sun smoothly and survive edits. Vertex format 14 → 13 floats; dropped the periodic re-bake
- Reflective water: planar reflection (camera mirrored about the true surface y=22.875, below-water geometry clipped) via a dedicated shader — Fresnel reflect/refract blend, animated ripple normals, sun specular
- SSAO: full-res depth pre-pass → 16-sample hemisphere kernel (tiled rotation) → 5×5 blur, modulating the ambient term on top of the per-vertex corner AO
- Gradient sky (`sky.frag` fullscreen pass): horizon→zenith gradient, terrain-occluded sun disk + halo, moon, twinkling stars, animated clouds; biome-driven sky/fog hue (desert / snow / forest); dynamic distance fog (thinner by day, mistier at night, brightens toward the sun, on terrain + water)
- Cave / ambient lighting: skylight-independent, AO-aware ambient floor so deep caves aren't pitch black; glowstone reach 6 → 10; smooth per-vertex block light (4-cell vertex averaging)
- Render-class system (`block_class`: SOLID / CUTOUT / TRANSLUCENT) driving light passage, AO, and face culling per block; see-through leaves (CUTOUT) with alpha-tested gaps, interior canopy faces once per boundary, light filtering through, no hard AO
- Survival inventory: E opens an 8×4 backpack; breaking collects blocks (hotbar then backpack, stacking to 64), placing consumes, click a slot to equip. Save format v6 (v5 still loads, with an empty inventory)
- Day/night cycle lengthened 48 → 96 minutes; random seeds drawn from a nanosecond clock (no repeats on rapid relaunch)
- UI polish: 1px transparent font-atlas gutter (crisp glyphs), centered `+` glyph, no hotbar backing panels, softer selected-slot highlight; debug `G` cycles a fullscreen view of the reflection / SSAO buffers

---

## 0.0.0.8 — Console, commands, and new blocks

- In-game console / chat (T to chat, "/" for a command): scrollback history, blinking input line, movement + mouse-look suppressed while typing; new `console` module + player input gate (`player_set_input_enabled`)
- Slash commands (/help, /time, /tp, /gamemode, /give, /seed, /fly, /clear) with Tab autocomplete + a live suggestion list; per-world "Allow commands" toggle (save format v5), commands rejected when disabled; chat / command output flashes bottom-left then fades even with the console closed
- Renamed the game to "BlockWorlds" (window title, title screen, HUD)
- New blocks: wood planks, quartz, gray concrete, see-through glass (own transparent draw pass — glass-to-glass faces cull, blocks behind stay visible, no shadow or AO from glass)
- Glowstone light reworked to an occlusion-aware flood-fill (no longer a radius sphere; doesn't leak through walls, so a sealed-room lantern stays lit inside only)
- Hotbar grown to 10 slots (keys 1–9, 0); creative block chooser / inventory (E): left-click a block to fill the slot, click a slot to select, right-click to clear

---

## 0.0.0.7 — Threaded generation + a real interface layer

- Background chunk generation on a 3-thread gen pool (pure terrain fill off-thread; main thread finalizes veins/deltas/overlay/water/trees under the write lock)
- New `text` module: embedded 8×8 bitmap font baked to a GL texture, immediate-mode `text_draw` in aspect-correct UI space; new `ui` rect/icon/blur primitives
- Hotbar HUD (atlas icons, selection highlight, slot numbers, held-block name); loading screen with a real gen+mesh progress bar (`world_work_remaining`); pause menu over a frozen, blurred back-buffer capture
- Title-screen world picker + New World / Settings / Quit; multiple save slots (`saves/<name>.bw`, world created on select, destroyed on quit-to-title), save format v4 (adds name + gamemode); world creation with typed name, editable per-world seed, Creative / Survival (Survival disables flight)
- Settings (FOV, sensitivity, volume, render distance, FPS overlay) saved to `saves/settings.cfg`, applied live; `main` runs a TITLE / CREATE / SETTINGS / LOADING / PLAYING / PAUSED state machine

---

## 0.0.0.6 — Threaded meshing, soft shadows, ambient occlusion, sound

- Background mesh threading: worker pool builds meshes (read-locked world), main thread uploads via `world_pump_meshing`; generation stays main-thread
- Per-face ambient occlusion (0..3 corner darkening, AO-aware greedy-merge split + diagonal flip, new per-vertex `ao`); soft sun shadows via per-corner jittered ray bundles → fractional penumbra (was 1-bit per quad)
- Procedural sound effects via vendored miniaudio (break / place / step / splash, 16-voice mixer, no asset files); half master volume, exit-water sound on surfacing, distinct wet footsteps, softer splash (dropped the tonal beep)
- Tighter frustum culling (per-chunk content AABB); chopped trees no longer regrow on reload; O(1) delta hash-index + collapsed-overlay storage past 50% edits; save format v3; vertex format 13 → 14 floats
- Snappier auto step-up outside water; reworked in-water movement (less floaty, faster horizontal, stronger sink, quicker swim-up); underwater overlay fades in over 0.25 s
- Added a flat `docs/` directory documenting every subsystem; dropped background music from the roadmap (out of scope)

---

## 0.0.0.5 — Day/night, sun lighting, depth shading, falling solids

- Day/night cycle (`world_time`, 48-min day, persisted, T skips ~1.2h, shown as HH:MM in the title) with dynamic sky colour (sunset glow near the horizon)
- Sun-direction lighting (per-vertex normals + `u_sun_dir` / `u_sun_strength` / `u_ambient` diffuse over an ambient floor); depth shading via per-vertex `skylight` (exponential falloff below terrain); baked sun shadows (one DDA ray per quad toward the sun, budgeted re-bake as it drifts)
- Glowstone (ID 14): emissive block seeding a 28³ block-light grid (radius 6) baked to per-vertex `blocklight`, combined with skylight via `max()` + warm tint
- Animated falling sand (`falling` module + shader); queued water flood-fill (per-frame spread, infinite sources, resumes after reload); underwater darkening / blue tint / fog / fullscreen overlay
- Hot chunk cache: out-of-radius chunks go dormant (GPU freed, cells kept), LRU-evict above `CHUNK_CAP = 2400`; sneak edge-protection, smooth physics-based auto step-up, in-water "moon-like" physics
- Atlas 4×4 → 5×5; vertex format `shade` → normals + skylight + blocklight (32 → 48 bytes); hotbar 7 → 8 slots; save format v2 (adds `world_time`, v1 rejected)

---

## 0.0.0.4 — Interaction, streaming, and save/load

- Block break (LMB) / place (RMB) via DDA voxel raycast; crosshair + targeted-block wireframe; hotbar (keys 1–7) selects the placement block, title bar shows mode / xyz / held block
- Caves (3D trilinear-noise carving below Y=55), clustered ore veins, multi-sample biome height blending; unbreakable deepstone at Y=0; redrawn ore textures (coal / iron / diamond)
- Infinite world via chunk streaming (4096-bucket hash map, radius 5, load/unload on chunk-cross); world height extended to Y=255 (16 vertical chunks)
- Save / load: bareiron-style delta array in `world.bw` (auto, same-seed resume, different-seed rejected, gitignored); player position / look / mode / selected block travel with it. `world_t` made opaque; `world_set_block_gen` separates procedural writes from player deltas
- Two-pass water rendering (opaque, then depth-write-off, see-through for opaque face culling) + recessed top + underwater tint overlay; aspect-corrected crosshair (`u_aspect`)
- Sprint (Left Ctrl, 1.43×), half-block auto step-up, cross-chunk tree-leaf stitching (3×3 neighbourhood); GitHub Actions release workflow (macOS build → zipped artifact on tag push)

---

## 0.0.0.3 — Procedural content

- Four biomes (plains, forest, desert, snow) driven by 96-block-cell temperature × humidity noise
- Trees stamped across chunks (4-block trunk + 3-layer leaf canopy)
- Sea level (Y=22) with water filling sub-surface terrain; sand on shores
- Sprinkled ores (coal, iron, diamond) with depth-dependent rates
- Vertical chunk stacking (multi-chunk world height)
- Frustum culling per chunk via 6-plane test on `P·V`
- New blocks: sand, snow, wood, leaves, coal / iron / diamond ore

---

## 0.0.0.2 — Voxel chunks: textures, greedy meshing, physics

- `chunk` module: 16³ flat block array + indexing helpers; cube-per-voxel meshing with hidden-face culling; new `mesh` (dynamic `realloc`-grown buffer) + `chunk_mesher` modules
- Procedural 4×4 atlas of 16×16 tiles with UV-per-vertex per-face tile lookup (grass top vs side vs bottom); optional PNG atlas via vendored `stb_image` (empty path → procedural); per-face shade multipliers (top 1.0, bottom 0.5, sides ~0.7–0.8)
- Greedy meshing: coplanar same-block faces merged via per-axis mask sweep, atlas tiling across merged quads via the `fract()` UV trick; vertex format gains `tile`
- Player physics: AABB-vs-voxel collision (per-axis sweep + edge snap), gravity / terminal velocity / jump, walk + double-tap-Space fly, crouch (Shift) with head-bonk protection; player module owns position / velocity / yaw / pitch / mode; ESC releases the cursor without quitting
- `world` module: grid of chunks indexed by chunk coords, bilinear-noise heightmap from hashed chunk corners (bareiron-style, no Perlin), cross-chunk neighbour queries for correct edge culling, one draw call per chunk with a per-chunk model translation
- Window-title FPS counter (F toggles), 120 fps soft cap via `nanosleep`

---

## 0.0.0.1 — Foundation & first-person 3D

- GLFW window scaffold with OpenGL 4.1 core on macOS; Makefile build (clang, strict warnings, pkg-config GLFW)
- `vec3` + `mat4` math libraries with perspective / `lookAt` derived from first principles (`vec3` in its own translation unit); first textured triangle through the full `P·V·M` pipeline
- Indexed cube drawing (EBO), time-driven model rotation, depth test; 3×3×3 cube grid viewed through `lookAt`; shaders moved out of C string literals into `.vert` / `.frag` files
- First-person controls: WASD movement + mouse-look (yaw / pitch), cursor capture (`GLFW_CURSOR_DISABLED`), pitch clamp at ±89°, frame-rate-independent motion via `dt`
