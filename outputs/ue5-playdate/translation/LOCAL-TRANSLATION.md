# 本地游戏介绍翻译（Hy-MT2 Q4_0，骁龙 HTP）

2026-10-09。在线翻译保留；Android 上增加本机推理，作为“自动选择”的首选。参考实现为 `ai_hymt2`（llama.cpp `de7fa0a` + STQ1_0／线程池补丁）。

## 行为

| 设置 | 顺序 |
|---|---|
| 自动选择 | 模型已下载 → 本地；否则微软 → 腾讯。本地出错、模型校验失败或 30 秒无首个输出时改用在线 |
| 本地翻译 | 只在本机运行，不联网（下载模型除外），失败即报错并保留原文 |
| 微软／腾讯 | 与此前相同 |

非 Android 平台不提供本地选项。缓存键包含模型身份（`hy-mt2-q4_0-5458acf2`），与在线译文互不覆盖；自动模式先读本地缓存，再读微软、腾讯缓存。

## 后端选择（`crank_translation_configure`）

`auto`：有 Hexagon HTP 设备用 HTP，其次 Adreno OpenCL，否则 CPU。只使用一个加速后端，不拆层。
DSP 无法从应用 `nativeLibraryDir` 读取 `libggml-htp-v*.so`，应用启动时把它们复制到私有目录 `Translation/htp` 并设置 `ADSP_LIBRARY_PATH`。
骁龙的 prime 核不参与 prefill 线程，避免 ggml 自旋线程池卡死（ai_hymt2 记录的现象）。
调试（非 Shipping）：`-CrankTranslationDevice=cpu|opencl|htp`、`-CrankTranslationModel=<path>`、`-CrankTranslationModelUrl=<url>`。

## 模型

`Hy-MT2-1.8B-Q4_0.gguf`，1,079,997,408 字节，SHA-256 `5458acf22435287fb9aa7a5bf6cc041806df15d54cea020cbad25ac95e2d8a21`。
来源为 ModelScope `unsloth/Hy-MT2-1.8B-GGUF`，固定提交 `eaa43c0c98c505ffe6759528221a123dfe70383a`（大小与 SHA-256 已与 ModelScope 文件列表核对）。
仅 Wi-Fi 下载，断点续传由 Java `CrankModelDownload` 负责，下载后原生校验 SHA-256，再原子替换。校验结果以大小和修改时间记在 `<模型>.verified`，之后启动不再重复约 9 秒的哈希；文件被替换或改动即作废。

官方 1.25-bit 文件把 STQ1_0 记为类型 42，新的 llama.cpp 基线需要先把头部改成 43，目前不提供，原生库遇到它会返回 `stq1_requires_type_remap`。

## 构建与打包

```bash
translation/build-snapdragon.sh   # Docker：ghcr.io/snapdragon-toolchain/arm64-android:v0.7
```

脚本克隆固定版本的 llama.cpp（`dependency-lock.json`），校验并应用 `patches/hymt2/*.patch`，编译 `libcrank-translate.so`、ggml 的 CPU／OpenCL／Hexagon 插件和 v73／v75／v79／v81 DSP 骨架，并复制到
`Unreal/Plugins/CrankRuntime/Binaries/ThirdParty/Translation/Android/arm64-v8a`，`CrankRuntime_APL.xml` 再把该目录放进 APK。
CPU 后端按 `armv8.7a+i8mm` 编译，只面向 Snapdragon 8 Gen 1 及更新的设备；更旧的 CPU 会非法指令，需要另做 `GGML_CPU_ALL_VARIANTS` 构建。
`tools/verify-translation-package.py` 检查 APK 含这些库、DSP 骨架、下载器、不含 GGUF，且 16 KiB 对齐。

`tools/Build-Translation.ps1`（Windows）仍指向旧的 STQ 补丁和旧 llama.cpp 版本，与本方案不兼容，不用于 Android 本地推理。

## 实测（SM8750 HONOR PPG-AN00，独立测试程序，未走 UE 应用）

| 内容 | 后端 | 结果 |
|---|---|---|
| 79 token 短文（首次，含 SHA 校验 9.4 s、加载 2.9 s） | HTP | 13.3 s |
| 同一段，模型已加载 | HTP | 0.72 s（decode 约 42 t/s） |
| 同一段，模型已加载 | CPU | 1.08 s |
| 3.4k 字符长介绍，27 个保护标记 | HTP | 首字 0.19 s，全文 27 s（prefill 约 2200 t/s，decode 约 37 t/s），标记全部保留 |

只测过这一台手机。v73／v75／v81、Adreno OpenCL、热降频、峰值内存、应用内端到端尚未测。长介绍的本地总耗时明显长于在线服务，但是流式输出。
