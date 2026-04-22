# fast-photon tools

Developer utilities that run **outside** the game executable — asset
pipelines, indexers, validators. All Python, all share the repo-root venv
created by `scripts/setup_venv.sh`.

---

## Layout

Every tool is a self-contained directory under `tools/`:

```
tools/
  <tool_name>/
    <tool>.py          # the implementation
    <tool>.sh          # thin wrapper: activates venv, forwards args
    requirements.txt   # pip dependencies (auto-picked by setup_venv.sh)
```

`scripts/setup_venv.sh` walks `tools/` and installs every `requirements.txt`
it finds, so adding a new tool never requires updating the setup script.

---

## Available tools

### atlas_baker — pack source PNGs into texture atlas pages

Reads `assets/textures/` recursively, packs every PNG into one or more
fixed-size atlas pages, writes the result to `assets/atlases/`:

- `atlas_0.png`, `atlas_1.png`, ... — one image per page
- `atlas.json` — manifest with page list and per-sprite UV rects

Multi-page: new pages are spawned automatically if everything doesn't fit
into a single page. Sprite IDs are derived from file paths relative to
`assets/textures/` (no extension, forward slashes).

**Run:**
```sh
tools/atlas_baker/bake.sh
tools/atlas_baker/bake.sh --page-size 512    # smaller page
tools/atlas_baker/bake.sh --max-pages 4      # lower safety ceiling
```

**Notes:**
- Every sprite must individually fit into one page (not larger than `--page-size`).
- Rotation is disabled so UVs remain intuitive.
- Output is sorted → rerunning with unchanged inputs produces byte-identical JSON.

### asset_indexer — regenerate `assets.json` textures/sprites sections

Scans asset folders and regenerates the **textures** and **sprites**
sections of `assets/assets.json`. The **shaders** and **materials**
sections are human-authored and preserved verbatim.

Two modes, auto-detected:

- **baked** — if `assets/atlases/atlas.json` exists: textures reference
  atlas pages, sprites reference atlas rects.
- **raw** — no atlas: every PNG in `assets/textures/` becomes its own
  texture plus a full-image sprite.

**Run:**
```sh
tools/asset_indexer/index.sh           # regenerate assets.json
tools/asset_indexer/index.sh --check   # exit 1 if stale (full scan)
```

The indexer is idempotent — calling it repeatedly with unchanged inputs
leaves `assets.json` untouched on disk.

A companion module, `check_staged.py`, provides a fast path used by the
pre-commit hook: it verifies only staged asset changes (PNGs, atlas.json,
assets.json) for internal consistency and skips the full disk scan.

---

## Adding a new tool

1. Create `tools/<your_tool>/` with `<tool>.py`, `<tool>.sh`, `requirements.txt`.
2. Make `<tool>.sh` executable and follow the venv-detect pattern used by
   existing wrappers (see `tools/atlas_baker/bake.sh`).
3. Rerun `scripts/setup_venv.sh` — new dependencies are installed automatically.
