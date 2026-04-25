# Particle Editor

GUI tool for authoring `particle_systems` entries in a fast-photon asset
manifest, with a real-time CPU-emulated preview.

## Run

From the repository root:

```sh
# Linux / macOS / Git Bash:
tools/particle_editor/run.sh

# Windows cmd / PowerShell:
tools\particle_editor\run.bat
```

The wrapper uses the shared `<repo>/venv` (creating it on first run) and
installs this tool's requirements on top.

Pass `--manifest /custom/path.json` to override the default
(`assets/assets.json`).

## What it edits

A particle system is one entry under the `"particle_systems"` array of
a faction or common manifest. Fields:

| Field           | Meaning                                                        |
|-----------------|----------------------------------------------------------------|
| `name`          | Local id; bake prefixes it as `<faction>/<name>`               |
| `max_particles` | Pool size. Capped at 4096 by the engine                        |
| `emit_rate`     | Particles per second for persistent emitters                   |
| `burst_count`   | Optional one-shot burst on emitter start                       |
| `lifetime_min/max` | Per-particle lifetime in seconds (uniform random in range)  |
| `velocity_min/max` | Initial velocity in world units/sec, sampled per-axis       |
| `gravity`       | Constant world-space acceleration                              |
| `size_start/end` | Lerped over the particle's lifetime                           |
| `color_start/end` | RGBA, lerped over lifetime                                   |
| `sprite`        | Sprite id (cross-ref like `common/foo` allowed)                |

## Preview

The preview canvas is a Python emulation of the engine's compute
simulate pass — close enough for visual feedback, not byte-identical to
GPU output. Sprites aren't sampled; particles render as solid alpha
discs in the system's current colour.

## Saving

Save buttons write straight to disk. The window title shows `*` while
there are unsaved changes; closing prompts to save / discard / cancel.
File menu (Ctrl+O / Ctrl+S / Ctrl+R) provides explicit open / save /
reload.

The editor preserves any unknown sections of the manifest verbatim.
