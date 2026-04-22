# fast-photon

A Vulkan-based C++ game engine specialized for turn-based strategy games.

## Stack

- **C++20**, CMake ≥ 3.20
- **Vulkan 1.2** + [VulkanMemoryAllocator](lib/VulkanMemoryAllocator)
- **SDL2** for windowing and input
- **EnTT** for ECS
- **GLM** for math, **spdlog** for logging, **ImGui** for debug UI
- **stb**, **nlohmann/json**, **cgltf**

Python 3.9+ is required for the developer tooling (not for the runtime).

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

Each script is idempotent and can also be run individually. Rerun
`setup_venv.sh` any time a tool's `requirements.txt` changes; rerun
`install_hooks.sh` if a hook file in `hooks/` is updated.

---

## Building

Shaders (`.vert`/`.frag`) are compiled to SPIR-V automatically as part of
the CMake build via `glslc` (shipped with the Vulkan SDK). Assets are
copied next to the executable as a post-build step.

```sh
cmake -S . -B build
cmake --build build
```

---

## Developer workflow

### Adding / changing textures

1. Drop PNG files into `assets/textures/` (subdirectories become part of
   the sprite ID, e.g. `units/warrior.png` → sprite id `units/warrior`).
2. Either:
   - **Raw mode (fast iteration):** run `tools/asset_indexer/index.sh`.
     Each PNG becomes its own texture + full-image sprite.
   - **Baked mode (shipping):** run `tools/atlas_baker/bake.sh` to pack
     all PNGs into atlas pages, then `tools/asset_indexer/index.sh` to
     regenerate the `assets.json` entries to reference the atlas.

The pre-commit hook verifies that `assets.json` is up-to-date with the
asset folders; it will block a commit if the index is stale and tell you
which command to run.

### Shaders and materials

Edit `assets/shaders/*.vert`/`*.frag` (rebuild handles SPIR-V). Edit the
`shaders` and `materials` sections of `assets/assets.json` by hand — the
indexer leaves those sections untouched.

---

## Repository layout

```
src/            engine source
assets/         shaders, textures, atlases, assets.json manifest
lib/            third-party dependencies (submodules / vendored)
tools/          developer utilities (see tools/README.md)
scripts/        project bootstrap (setup_venv.sh, install_hooks.sh, ...)
hooks/          git hooks, installed via scripts/install_hooks.sh
```

See [`tools/README.md`](tools/README.md) for the list of available tools
and how to add new ones.
