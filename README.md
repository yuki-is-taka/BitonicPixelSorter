# BitonicPixelSorter (Unreal Engine port)

Realtime GPU **pixel-sort** post-process effect using bitonic sorting, ported to Unreal Engine
from [ruccho/BitonicPixelSorter](https://github.com/ruccho/BitonicPixelSorter) (Unity, MIT).

This is the `ue` branch — a clean, UE-only tree. The `master` branch tracks the original Unity
project upstream for reference. Original algorithm and shaders © 2020 ruccho; UE port © 2026
yuki-is-taka. MIT licensed (see `LICENSE`).

![status](https://img.shields.io/badge/status-working-brightgreen)

## Features

- Realtime per-pixel **value sort** as a scene post-process (no render-target plumbing needed).
- **Selectable sort key** — order by **luma, hue, saturation, value, or a single R/G/B channel**.
  The same key drives the threshold, so e.g. picking *Hue* selects a colour range and sorts it by hue.
- **Arbitrary angle** — sort along any direction, not just horizontal/vertical. The sort is an
  **exact permutation** of the original pixels (no resampling), so the source image quality is
  preserved; only which pixels move changes.
- **Strength** cross-fade (0..1) to dial the effect in and out.
- **Threshold window** (on the selected key) to choose which pixels participate.
- Two ways to drive it: a **Post Process Volume** (artist / Blueprint / Sequencer friendly) or
  **console variables** (quick global control / tuning).
- **Firm bypass** — when the effect would do nothing, no GPU pass is scheduled at all.
- **No hard resolution cap** — large views (sorted axis ≥ 2048 px) use a seamless global-memory path
  bounded only by VRAM, so 4K / 8K and beyond work (see *Notes & limitations*).

## Requirements

- Unreal Engine 5 (developed and tested on a **UE 5.8** source build; the module is not
  version-pinned). Deferred renderer.
- Verified on **macOS / Metal**. Windows / D3D12 should work (same RDG + cross-compiled HLSL) but
  is currently untested.

## Installation

1. Copy or clone this repo into your project's `Plugins/` folder as
   `Plugins/BitonicPixelSorter/` (use the `ue` branch).
2. Regenerate project files and build (the plugin is a C++ Runtime module).
3. Enable **Bitonic Pixel Sorter** in *Edit → Plugins* if it isn't already, and restart the editor.

## Usage

The effect is applied to the scene **after tone mapping** (and after any TSR/TAA/DLSS upscale),
**before** UI — so HUD/widgets are never sorted. There are two ways to control it.

### A) Post Process Volume (recommended)

This is the idiomatic path: spatial blending, per-camera control, Blueprint, and **Sequencer
keyframing** (most properties are `interp`).

1. **Create the asset:** Content Browser → right-click → *Miscellaneous → Data Asset* → pick
   **Bitonic Pixel Sorter Blendable**. Name it (e.g. `DA_BitonicSort`).
2. **Add a Post Process Volume** to your level (or use a camera / `UPostProcessComponent`). For a
   quick test, enable **Infinite Extent (Unbound)** so it affects every view.
3. **Attach the blendable:** with the volume selected, go to *Details → Rendering Features →
   Post Process Materials*, add an array entry, switch it to **Asset reference**, and select your
   `DA_BitonicSort` asset.
4. **Tune** the Data Asset's properties (below). Overlapping volumes blend by weight/falloff.

When a volume's blendable affects a view, it **takes precedence** over the console variables.
A ready-made sample asset is included at `Content/DA_BitonicPixelSorterBlendable.uasset`.

### B) Console variables

Useful for quick global tuning. These apply when **no** Post Process Volume blendable affects the
view. Type them in the editor console (`` ` `` / `~`):

```
r.BitonicPixelSorter.Enable 1
r.BitonicPixelSorter.Angle 45
r.BitonicPixelSorter.SortKey 0
r.BitonicPixelSorter.ThresholdMin 0
r.BitonicPixelSorter.ThresholdMax 1
r.BitonicPixelSorter.Strength 1
r.BitonicPixelSorter.Ascending 1
```

## Parameters

| Parameter | CVar | Data Asset | Default | Range | Meaning |
|---|---|---|---|---|---|
| Enable | `r.BitonicPixelSorter.Enable` | (presence of the volume) | `0` | 0 / 1 | Turn the effect on (CVar path only). |
| Angle | `r.BitonicPixelSorter.Angle` | `Angle` | `0` | `[0, 180)` deg | Sort direction. **0 = horizontal**, **90 = vertical**, **45 = diagonal**. |
| Sort key | `r.BitonicPixelSorter.SortKey` | `SortKey` | `0` (Luma) | 0..6 | Pixel value the threshold + sort use: 0 Luma, 1 Hue, 2 Saturation, 3 Value, 4 R, 5 G, 6 B. |
| Threshold Min | `r.BitonicPixelSorter.ThresholdMin` | `ThresholdMin` | `0.4` | `0..1` | Only pixels with brightness ≥ this are sorted. |
| Threshold Max | `r.BitonicPixelSorter.ThresholdMax` | `ThresholdMax` | `0.6` | `0..1` | Only pixels with brightness ≤ this are sorted. |
| Strength | `r.BitonicPixelSorter.Strength` | `Strength` | `1` | `0..1` | Cross-fade between the original (0) and the fully sorted (1) image. |
| Ascending | `r.BitonicPixelSorter.Ascending` | `bAscending` | `1` | 0 / 1 | Sort order: 1 = dark→bright along the line, 0 = reversed. |
| Max scratch | `r.BitonicPixelSorter.MaxScratchMB` | — | `1536` | MB, `0` = ∞ | Wide-path (sorted axis ≥ 2048) VRAM budget; a view needing more scratch is skipped. |

**The threshold window is the "amount" control:** a narrow `[Min, Max]` sorts fewer pixels (subtle);
`Min 0 / Max 1` sorts everything (strongest, and heaviest). Position the window to sort only
highlights (e.g. `0.6 .. 0.9`) or only shadows (e.g. `0.05 .. 0.35`).

## How it works

- An `ISceneViewExtension` hooks `EPostProcessingPass::Tonemap` (after pass) and runs two **RDG
  compute** passes: **MetaPass** marks the in-threshold spans per line; **SortPass** bitonic-sorts
  them in group-shared memory and gathers the result.
- The sort is index-based — each output pixel is an exact copy of an input pixel — so at full
  strength there is **no blur**; untouched pixels are left exactly as they were.
- **Arbitrary angle** is a shear parameterization: each sort line is the digital line
  `pixel = (coord, round(L + coord·slope))` along the major axis. Because `round(n + f) == n +
  round(f)` for integer `n`, this is an exact tiling of the pixel grid — a true permutation.
- The passes are confined to the view's `ViewRect` (scene-color render targets are often padded
  larger; their padding texels are uninitialized).
- **Large views** (sorted axis ≥ 2048 px) keep the exact same algorithm and result, but move the
  per-line working set out of group-shared memory into global scratch buffers, synchronized with
  device-memory barriers between bitonic levels. The line is never split into tiles, so the output
  is identical and seamless; the only added bound is scratch VRAM.

## Notes & limitations

- **No hard resolution cap; sorted axis ≥ 2048 px is heavier.** Below 2048 px the sorted axis
  (the view width for near-horizontal angles, the height for near-vertical — **not** the diagonal,
  even at an angle) uses the fast group-shared path. At or above 2048 it automatically switches to
  the global-memory "wide" path described in *How it works* — still one seamless sort per whole line,
  bounded only by scratch VRAM rather than a fixed pixel cap. A view whose predicted scratch
  (≈ `10 × lines × axis` bytes; roughly 0.1–0.2 GB at 4K, 0.3–0.8 GB at 8K, 1–3 GB at 16K) exceeds
  `r.BitonicPixelSorter.MaxScratchMB` (default `1536`, `0` = unlimited) is skipped untouched — raise
  it for very large LED-wall resolutions. The wide path is more bandwidth-heavy, so it suits
  moments/transitions rather than always-on at extreme resolutions.
- **Edges are stair-stepped at non-axis angles.** Because pixels are kept exact (no anti-aliasing),
  diagonal sort streaks have a digital-line staircase. That is the cost of preserving pixel quality;
  smoothing them would require blending (blur).
- **Bypass:** no compute pass is scheduled at all when the effect is disabled, `Strength <= 0`, or
  the threshold window is empty/inverted (`Min >= Max`).

## Layout

```
BitonicPixelSorter.uplugin
Source/BitonicPixelSorter/Public/    Renderer + Blendable (UBitonicPixelSorterBlendable) headers
Source/BitonicPixelSorter/Private/   Module, SceneViewExtension, Renderer, Blendable
Shaders/Private/                     BitonicPixelSorter.usf (MetaPass / SortPass)
Content/                             Sample blendable Data Asset
```
