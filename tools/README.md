# fast-photon tools

Developer utilities that run **outside** the game executable — asset
indexers, manifest consolidation, GUIs for content authoring. All Python,
all share the repo-root `venv` created by `scripts/setup_venv.sh`.

---

## Layout

Every tool is a self-contained directory under `tools/`:

```
tools/
  <tool_name>/
    <tool>.py          # implementation
    run.sh / run.bat   # thin wrappers: locate venv, install deps, exec the tool
    requirements.txt   # pip dependencies (auto-installed by setup_venv.sh)
    README.md          # tool-specific docs (optional)
```

`scripts/setup_venv.sh` walks `tools/` and installs every `requirements.txt`
it finds, so adding a new tool never requires touching the setup script.

---

## Available tools

### asset_indexer — auto-generate textures / sprites / sounds / fonts

Walks `assets/textures/<faction>/`, `assets/audio/<faction>/<group>/`, and
`assets/fonts/`, and rewrites the auto-generated sections of every
faction's `assets.json`. Anything human-authored (shaders, materials,
animations, animation_sets, manual sub-rect sprites) is left intact.

Faction folders are discovered automatically — the literal name `common`
maps to `assets/assets.json`, every other folder under
`assets/textures/` or `assets/audio/` maps to
`assets/data/factions/<name>/assets.json`. Adding a new faction is just
`mkdir`.

**Sub-rect sprite preservation:** animation_editor writes sprite entries
with `x/y/w/h` covering only part of a texture (animation frames). The
indexer recognises those by name + non-default rect and keeps them; only
full-image entries it produces itself are eligible for overwrite.

**Sound naming:** sound ids are flat (the file stem; group goes in a
separate field). This avoids collisions with the bake step's
cross-reference syntax, which uses `/`.

**Run:**
```sh
tools/asset_indexer/index.sh           # regenerate
tools/asset_indexer/index.sh --check   # exit 1 if stale (used by CI)
```

### manifest_baker — consolidate per-faction manifests for the engine

Reads the common manifest plus every faction manifest, applies
namespacing rules, validates every cross-reference, and writes
`assets/assets.baked.json` — the only manifest the engine reads at
startup.

CMake invokes the baker automatically before each build, so manual runs
are usually unnecessary. See
[manifest_baker/README.md](manifest_baker/README.md) for cross-reference
syntax and validation rules.

**Run:**
```sh
tools/manifest_baker/run.sh
```

### animation_editor — GUI for animations + state machines

PySide6-based editor with four tabs:

- **Sprites** — slice a sprite sheet into named sub-rect frames (auto
  grid or manual rectangle drag).
- **Animations** — build named tracks (`frames` + `fps`) from existing
  sprites, with real-time preview. Cross-faction frames are supported.
- **Sets** — author state machines on a node-graph canvas: states have
  an animation + loop flag + optional `next` (for one-shots) + a list
  of named transitions fired by `AnimationSystem::trigger()`.
- **Preview** — full state-machine playback: select a set, fire
  triggers, watch transitions resolve in real time.

The editor opens a single `assets.json` at a time. File → Open switches
between common and faction manifests; the window title shows an unsaved
indicator and prompts on close. Save buttons in each tab write to disk
immediately (in addition to Ctrl+S).

See [animation_editor/README.md](animation_editor/README.md).

**Run:**
```sh
tools/animation_editor/run.sh
```

### atlas_baker — pack source PNGs into texture atlas pages

(Not currently part of the live pipeline — superseded by per-faction
manifests for the turn-based-strategy use case. The tool still works
for projects that prefer a baked-atlas pipeline.) Packs every PNG under
`assets/textures/` into atlas pages, writes `assets/atlases/`:

- `atlas_0.png`, `atlas_1.png`, ... — one image per page
- `atlas.json` — manifest with page list and per-sprite UV rects

**Run:**
```sh
tools/atlas_baker/bake.sh
tools/atlas_baker/bake.sh --page-size 512    # smaller page
tools/atlas_baker/bake.sh --max-pages 4      # lower safety ceiling
```

---

## Adding a new tool

1. Create `tools/<your_tool>/` with `<tool>.py`, `run.sh` (and `run.bat`
   if Windows users will use it), `requirements.txt`.
2. Make `run.sh` executable and follow the venv-detect pattern used by
   existing wrappers (see `tools/manifest_baker/run.sh` for the
   minimal version, `tools/animation_editor/run.sh` for the
   bootstrap-on-first-run version).
3. Rerun `scripts/setup_venv.sh` — new dependencies are installed
   automatically.
