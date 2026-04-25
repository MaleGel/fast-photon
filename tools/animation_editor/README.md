# Animation Editor

GUI tool for authoring sprite animations and animation state machines.
Reads and writes `assets/assets.json` (common) or
`assets/data/factions/<faction>/assets.json` — one manifest at a time.

---

## Run

From the repository root, launch the wrapper script. It uses the shared
`<repo>/venv` (creating it on first run) and installs this tool's
requirements on top — siblings under `tools/` share the same venv.

```sh
# Linux / macOS / Git Bash:
tools/animation_editor/run.sh

# Windows cmd / PowerShell:
tools\animation_editor\run.bat
```

Pass `--manifest /custom/path.json` to override the default
(`assets/assets.json`).

## Manual setup

```sh
python -m venv venv
# Windows:
venv\Scripts\activate
# Linux/macOS:
source venv/bin/activate

pip install -r tools/animation_editor/requirements.txt
python tools/animation_editor/main.py
```

---

## Tabs

### Sprites

Slice a sprite sheet (PNG) into named sub-rect frames:

- **Auto grid** — set rows / cols / outer padding / inner spacing → the
  tool generates one rectangle per cell.
- **Manual draw** — drag rectangles directly on the canvas.

Frames are named `<prefix>_NN`. Saving upserts a single texture entry +
one sprite per frame. The asset_indexer preserves sub-rect sprites on
subsequent runs, so you can rename the source PNG without losing your
work.

### Animations

Compose named tracks from existing sprites:

1. Pick frames in the left-side picker (multi-select supported).
2. **→ Add to timeline** — appends them as new frames.
3. Right-click a timeline thumbnail to reorder or remove it.
4. Set name + FPS, hit **Save animation**.

The preview pane plays the timeline in real time using the configured
FPS, so what you see in the editor is exactly what the engine will play
at runtime.

**Cross-faction frames:** **+ Add cross-ref frame…** lets you reference
a sprite that lives in another faction's manifest (e.g. `common/foo`).
Cross-ref frames have no thumbnail — they'll be resolved by the bake
step against the consolidated manifest.

### Sets

Author state machines as a node graph:

- Right-click empty canvas → **Add state…** (name + animation + loop).
- Right-click a state → Edit / Set as default / **Set next →** /
  **Add transition…** / Delete.
- Right-click an edge → Edit / Delete.

Two edge kinds:
- **`next`** (solid line) — auto-transition fired when a one-shot
  animation completes.
- **trigger** (dashed line, labeled) — fired by gameplay via
  `AnimationSystem::trigger(entity, "name")`.

The state's animation accepts cross-faction references typed inline
(`common/idle`); the dropdown shows local animations, but the field is
editable.

### Preview

Full state-machine playback. Pick a set; the canvas plays its default
state. Click trigger buttons to fire them; one-shots auto-advance to
their `next`. The **Force state (debug)** dropdown jumps to any state
unconditionally.

---

## Saving and dirty tracking

- Each tab's Save button (Save animation / Save set / Add to manifest)
  writes the in-memory manifest to disk **immediately**.
- The window title shows `* ` when there are unsaved changes (e.g. you
  dragged nodes around in Sets but haven't hit Save).
- Closing the window or reloading prompts to save unsaved changes.
- File → **Open manifest…** (Ctrl+O) switches to a different file
  (e.g. between common and faction manifests). Same dirty prompt.
- File → **Reload manifest** (Ctrl+R) re-reads the current file from
  disk — useful after a CLI tool (asset_indexer, manifest_baker)
  rewrites the file behind the editor's back.

---

## Where things end up

| Tab        | Manifest section it edits |
|------------|---------------------------|
| Sprites    | `textures`, `sprites`     |
| Animations | `animations`              |
| Sets       | `animation_sets` (with `_layout` UI metadata the engine ignores) |
| Preview    | none — read-only          |

Unknown sections (shaders, materials, sounds, fonts, anything else) are
preserved verbatim with their original key order.
