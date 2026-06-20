# BitonicPixelSorter

Realtime GPU pixel-sort post-process effect (bitonic sort), implemented as an `ISceneViewExtension` + RDG compute passes. UE port of ruccho/BitonicPixelSorter (Unity, MIT).

Git-managed (independent repo), branch `ue` (the UE-only tree; `master` tracks the Unity upstream for reference). Use `git`, not `p4`. MIT licensed. Never commit without explicit user approval.

## Notes
- Single runtime module `BitonicPixelSorter`; hooks `EPostProcessingPass::Tonemap` (after tonemap, before UI).
- Driven by a Post Process Volume blendable (`UBitonicPixelSorterBlendable`) or `r.BitonicPixelSorter.*` CVars; the volume takes precedence.
- The sort is an exact permutation (no resampling, no blur). Sorted axis >= 2048 px switches to a global-scratch "wide" path bounded by VRAM (`r.BitonicPixelSorter.MaxScratchMB`).

## Documentation
Read [`Docs/INDEX.md`](Docs/INDEX.md) before non-trivial work. Decisions (immutable): `Docs/decisions/`. This repo follows the project doc-system convention.
