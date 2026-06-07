# BitonicPixelSorter (Unreal Engine port)

Realtime GPU **pixel-sort** post-process effect using bitonic sorting, ported to Unreal Engine
from [ruccho/BitonicPixelSorter](https://github.com/ruccho/BitonicPixelSorter) (Unity, MIT).

This is the `ue` branch — a clean, UE-only tree. The `master` branch tracks the original Unity
project upstream for reference. Original algorithm and shaders © 2020 ruccho; UE port © 2026
yuki-is-taka. MIT licensed (see `LICENSE`).

## Status

Skeleton. Plugin module, shader directory mapping (`/Plugin/BitonicPixelSorter`), and a
`SceneViewExtension` host are in place but inert. The compute kernels (MetaPass / SortPass) and
the planned **arbitrary-rotation** feature are not yet ported.

## Design

- Effect runs as **RDG compute passes** injected via `ISceneViewExtension`
  (`SubscribeToPostProcessingPass`, planned at `EPostProcessingPass::Tonemap`).
- **Cross-platform by design**: targets Windows + macOS today, with headroom for further
  platforms. No platform-specific module deps; shaders avoid platform-only intrinsics so they
  cross-compile to DXIL / SPIR-V / Metal. (Metal path needs verification — not covered upstream.)

## Layout

```
BitonicPixelSorter.uplugin
Source/BitonicPixelSorter/        Runtime module (SceneViewExtension host + module)
Shaders/Private/                  Compute shaders (.usf)
```
