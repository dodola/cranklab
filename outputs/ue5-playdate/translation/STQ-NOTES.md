# Hy-MT2 1.25Bit integration

The default model is Tencent's official `Hy-MT2-1.8B-1.25Bit.gguf` at
revision `9df5c824a00a744fb0512a29c640466f4d97dfb0`, 461,860,800 bytes
(440.46 MiB). SHA-256 and all dependencies are in `dependency-lock.json`.
Q4_K_M remains independently selectable in Settings and is never deleted by
switching profiles. Both profiles run locally with the same inline PDGames UI.

## Model downloads

Both profiles download from Tencent-Hunyuan's **ModelScope** repositories,
redirecting to `cdn-lfs-cn-1.modelscope.cn`, without a Hugging Face request or
login. The download URLs/revisions are recorded separately from the original
artifact provenance in `dependency-lock.json`:

- [1.25Bit](https://modelscope.cn/models/Tencent-Hunyuan/Hy-MT2-1.8B-1.25Bit-GGUF):
  revision `a1ba5a8101d7b257d42c0980e0d6415d0bd9e1cf`.
- [Q4_K_M](https://modelscope.cn/models/Tencent-Hunyuan/Hy-MT2-1.8B-GGUF):
  revision `ef1d40b8b315575d30d1eb6579996130b8fc0bb2`.

ModelScope file sizes and SHA-256 match the existing pinned artifacts. The
transfer limits, native SHA-256 verification, atomic installation, model paths
and translation-cache keys remain content-based. Existing downloaded models
and saved translations are reused when upgrading.

## Backend compatibility

The model requires the STQ1_0 kernel from llama.cpp PR #22836, still unmerged
when inspected. `stq1-pinned.diff` is that PR's diff at head
`1e411d8f5a1e23525fa3265dfb4bd76265465397`, adapted only in one context
comment to apply to our existing pinned llama.cpp revision. It includes a
generic CPU dot product and both baseline ARMv8 NEON and DotProd paths.
Build-Translation verifies patch hashes and applies them reproducibly.

The official file's header uses type **42** for 224 ternary tensors; the current
PR assigns STQ1_0 type **43**, while 42 is ordinary Q2_0. Its metadata name is
`HyMT2 1.8B 2bit Stride16` and `general.file_type=41`. A plain PR build cannot
load it correctly. `stq1-legacy-reader.diff` adds a thread-local read-only
remapping scope, enabled by our native wrapper only after verifying the exact
official model hash. It resets immediately after model loading, including
exceptions. Ordinary Q2_0 semantics remain intact. The file is never rewritten
and no second full-sized converted model is required.

## Storage and behavior

New installations default to STQ1. Profile selection persists in private
`Translation/model.json`. Transfers are bounded by the selected model's size,
SHA-256 verified, and atomically installed. Switching is disabled while a
model transfer or its verification is active. Switching profiles cancels
inference, clears stale displayed translations and loads only that profile's
cache. Existing Q4 caches keep their original keys. Models/context/sampler
objects are freed after each completed or cancelled inference job.

The APK carries native kernels and the CJK font; the model downloads separately.
Desktop inference and Android builds do not establish phone throughput, memory
or translation quality across all games. Those remain device acceptance work.

The real Dino-Run sample completed and retained its paragraph structure. Its
first line translated as `Dino-Run 的 Playdate 功能！`, less precise than a
Playdate edition/title rendering. Keeping Q4 selectable is therefore useful.
The long functional probe completed all three segments and abort checks, but
the Windows generic STQ backend took 459.21 seconds versus 16.91 seconds for
the Q4 probe. These were workstation functionality runs alongside other work,
not a controlled benchmark; Android uses the dedicated NEON/DotProd kernels.
No Android speedup or RAM figure is claimed from those measurements.
