# fast-photon

A Vulkan-based C++ game engine specialized for turn-based strategy games.

## Stack

- **C++20**, CMake ≥ 3.20
- **Vulkan 1.2** + [VulkanMemoryAllocator](lib/VulkanMemoryAllocator)
- **SDL2** for windowing and input
- **EnTT** for ECS
- **GLM** for math, **spdlog** for logging, **ImGui** for debug UI
- **stb**, **nlohmann/json**, **cgltf**, **miniaudio**

Python 3.10+ is required for the developer tooling (not for the runtime).

---

## First-time setup

From the repository root:

```sh
scripts/prepare_all.sh
```

This runs every project-bootstrap script in `scripts/` in the correct order:

1. `setup_venv.sh` — creates `./venv` and installs dependencies for every
   tool under `tools/`.
2. `install_hooks.sh` — copies `hooks/*` into `.git/hooks/`.

Each script is idempotent and can also be run individually.

---

## Building

```sh
cmake -S . -B build
cmake --build build
```

Two CMake-driven steps run automatically before/after every build:

- **Manifest bake** (PRE_BUILD): runs `tools/manifest_baker/bake.py` to
  consolidate the common `assets/assets.json` plus every
  `assets/data/factions/<faction>/assets.json` into a single
  `assets/assets.baked.json` — the file the engine actually reads.
  Build fails on any dangling cross-reference (missing texture, missing
  animation, etc.).
- **Asset copy** (POST_BUILD): copies the `assets/` tree next to the
  executable so the runtime finds shaders, textures, fonts, audio,
  and the baked manifest at the expected relative paths.

Shaders (`*.vert`/`*.frag`/`*.comp`) are **not** baked at build time —
`ShaderCache` invokes `glslc` at startup using `$VULKAN_SDK`, and the
runtime file watcher recompiles + rebuilds affected pipelines whenever a
shader file changes on disk.

---

## Asset workflow

Assets live under `assets/` and are organised per-**faction**: a faction
is just a folder name. Two faction names are special:

- `common` — global, faction-agnostic assets (UI fonts, the tile sprite,
  shared materials)
- everything else — gameplay-side factions (`player`, `enemy`, future
  additions like `orcs`, `neutral`, …)

Adding a new faction needs zero code changes — drop the appropriate
folders and the tools pick them up.

### Filesystem layout

```
assets/
  assets.json                              # common manifest
  assets.baked.json                        # generated, gitignored
  shaders/                                 # *.vert/*.frag/*.comp (common)
  fonts/                                   # *.ttf + *.ttf.meta (common)
  textures/
    common/<name>.png                      # → common manifest
    <faction>/<name>.png                   # → faction manifest
  audio/
    <faction>/<group>/<name>.{wav,ogg,mp3} # group = sfx/music/ui/...
  data/
    audio_config.json
    camera_config.json
    input_bindings.json
    scenes/main.json
    factions/
      <faction>/
        assets.json                        # auto-edited by indexer + animation_editor
        units/<name>.json                  # prefab definitions
```

### Naming + namespacing

Inside a manifest, names are written **unqualified** (e.g. `warrior`,
`tile`). The bake step rewrites every locally-defined name to
`<faction>/<name>` in the consolidated output. Cross-faction references
inside a manifest are written with an explicit `/` (e.g.
`common/ui_font`); the bake leaves those untouched and validates that
they resolve.

### Editing assets

| You did this | Run this | Effect |
|---|---|---|
| Added/renamed/removed a PNG, OGG, or .ttf.meta | `tools/asset_indexer/index.sh` | Refreshes `textures`/`sprites`/`sounds`/`fonts` in the relevant manifests. Sub-rect sprites authored in animation_editor are preserved. |
| Want to author animations or state machines | `tools/animation_editor/run.sh` | GUI: slice sprite sheets, build animation tracks, design state machines, preview. |
| Edited shaders, materials, prefabs, scene | nothing | The engine reads them directly (shaders) or after the next build (everything else). |
| Need a fresh consolidated manifest | nothing — CMake bakes it before every build | If you want it explicitly, `tools/manifest_baker/run.sh`. |

See [`tools/README.md`](tools/README.md) for the per-tool documentation.

---

## Repository layout

```
src/            engine + game source (engine/ is generic, game/ is project-specific)
assets/         shaders, textures, fonts, audio, prefabs, scenes
lib/            third-party dependencies (submodules / vendored)
tools/          developer utilities (see tools/README.md)
scripts/        project bootstrap (setup_venv.sh, install_hooks.sh, prepare_all.sh)
hooks/          git hooks, installed via scripts/install_hooks.sh
```
