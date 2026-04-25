# Manifest Baker

Consolidates per-faction `assets/data/factions/<faction>/assets.json` files
on top of the common `assets/assets.json` into a single
`assets/assets.baked.json` — the only manifest the engine reads at
startup.

---

## Pipeline position

```
asset_indexer  →  per-faction assets.json  →  manifest_baker  →  assets.baked.json  →  engine
                  (auto + hand-edited)                          (gitignored, fully resolved)
```

The baker doesn't scan the filesystem — it consolidates manifests that
`asset_indexer` (for textures/sprites/sounds/fonts) and
`animation_editor` (for animations/animation_sets) have already
prepared.

---

## Namespacing

- **Common** (`assets/assets.json`) — names live in the global namespace
  as-is (`tile`, `ui_24`, `tile_grass`).
- **Per-faction** files — every locally-defined `name` is rewritten to
  `<faction>/<name>` in the output. Local references (sprite → texture,
  animation → frames, animation_set state → animation, transition →
  target, etc.) are rewritten the same way. So a faction file written
  in terms of `warrior` and `warrior_idle` becomes
  `<faction>/warrior` and `<faction>/warrior_idle` after bake.
- **Cross-faction references** — a name containing `/` is treated as
  already-qualified and left untouched. So a faction file may
  reference `common/ui_font` or `player/idle_anim` directly. The bake
  validates that the qualified name resolves in the consolidated
  namespace.

Example: a `player/assets.json` entry like
```json
{
  "name": "warrior_idle",
  "frames": ["warrior_00", "warrior_01"],
  "fps": 8.0
}
```
ends up in `assets.baked.json` as
```json
{
  "name": "player/warrior_idle",
  "frames": ["player/warrior_00", "player/warrior_01"],
  "fps": 8.0
}
```

---

## Strict validation

The baker fails (non-zero exit) on any dangling reference:

- sprite → unknown texture
- material → unknown texture/shader
- animation → unknown sprite frame
- animation_set state → unknown animation
- animation_set `default` not in own states
- transition `target` / `next` not in own states

The engine's loader assumes everything is resolvable, so it's better to
surface errors here.

---

## Usage

```sh
# Linux / macOS / Git Bash:
tools/manifest_baker/run.sh

# Windows cmd / PowerShell:
tools\manifest_baker\run.bat
```

Both wrappers just invoke `bake.py <repo>`. The script uses only the
Python standard library — no venv setup is needed — but the run scripts
go through the shared `<repo>/venv` for consistency with other tools.

CMake invokes the bake automatically before each build via
`add_custom_command(... PRE_BUILD ...)`, so manual runs are usually
unnecessary.
