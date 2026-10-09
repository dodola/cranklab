# 本地游戏介绍翻译（Hy-MT2 Q4_0，骁龙 HTP）

2026-10-09。在线翻译保留；Android 上增加本机推理，作为“自动选择”的首选。参考实现为 `ai_hymt2`（llama.cpp `de7fa0a` + STQ1_0／线程池补丁）。

## 行为

| 设置 | 顺序 |
|---|---|
| 自动选择 | 模型已下载 → 本地；否则微软 → 腾讯。本地出错、模型校验失败或 30 秒无首个输出时改用在线 |
| 本地翻译 | 只在本机推理，不调用在线翻译接口，失败即报错并保留原文；下载模型和刷新游戏介绍仍可联网 |
| 微软／腾讯 | 与此前相同 |

非 Android 平台不提供本地选项。缓存键包含模型身份（`hy-mt2-q4_0-5458acf2`），与在线译文互不覆盖；自动模式先读本地缓存，再读微软、腾讯缓存。

## 后端选择（`crank_translation_configure`）

`auto`：有 Hexagon HTP 设备用 HTP，其次 Adreno OpenCL，否则 CPU。只使用一个加速后端，不拆层。
DSP 无法从应用 `nativeLibraryDir` 读取 `libggml-htp-v*.so`，应用启动时把它们复制到私有目录 `Translation/htp` 并设置 `ADSP_LIBRARY_PATH`。
prefill 线程数按较快的 CPU 核数量估计，并为 prime 核减少线程池规模；不绑定 CPU 亲和性。
调试（非 Shipping）：`-CrankTranslationDevice=cpu|opencl|htp`、`-CrankTranslationModel=<path>`、`-CrankTranslationModelUrl=<url>`。

## 模型

`Hy-MT2-1.8B-Q4_0.gguf`，1,079,997,408 字节，SHA-256 `5458acf22435287fb9aa7a5bf6cc041806df15d54cea020cbad25ac95e2d8a21`。
来源为 ModelScope `unsloth/Hy-MT2-1.8B-GGUF`，固定提交 `eaa43c0c98c505ffe6759528221a123dfe70383a`（大小与 SHA-256 已与 ModelScope 文件列表核对）。
仅 Wi-Fi 下载，由 Java `CrankModelDownload` 把连接绑定到选定的 Wi-Fi 网络；取消或 Wi-Fi 断开会清理临时文件，重试从头下载，当前不支持断点续传。下载后原生校验 SHA-256，再原子替换。校验结果以大小和修改时间记在 `<模型>.verified`，之后启动不再重复约 9 秒的哈希；文件被替换或改动即作废。

官方 1.25-bit 文件把 STQ1_0 记为类型 42，新的 llama.cpp 基线需要先把头部改成 43，目前不提供，原生库遇到它会返回 `stq1_requires_type_remap`。

## 构建与打包

```bash
# Linux 或 WSL；路径均使用 Linux 格式
export ANDROID_NDK_ROOT=/path/to/android-ndk-r29
export HEXAGON_SDK_ROOT=/path/to/hexagon/6.6.0.0
export HEXAGON_TOOLS_ROOT=$HEXAGON_SDK_ROOT/tools/HEXAGON_Tools/19.0.07
# 镜像提供的 NDK 已包含 OpenCL；独立 SDK 可用此变量覆盖
export OPENCL_SDK_ROOT=$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr
OUT=/path/to/build STAGE=/path/to/android-libraries translation/build-snapdragon.sh

# 可选容器构建，直接构建不依赖 Docker
translation/build-snapdragon.sh --docker
```

脚本克隆固定版本的 llama.cpp（`dependency-lock.json`），校验并应用 `patches/hymt2/*.patch`，编译 `libcrank-translate.so`、ggml 的 CPU／OpenCL／Hexagon 插件和 v73／v75／v79／v81 DSP 骨架，并复制到
`Unreal/Plugins/CrankRuntime/Binaries/ThirdParty/Translation/Android/arm64-v8a`，`CrankRuntime_APL.xml` 再把该目录放进 APK。
宿主库按 Android ARM64 基线 `armv8-a` 编译，CPU 后端使用 `GGML_CPU_ALL_VARIANTS`。DotProd／FP16／i8mm 插件仅在运行时评分确认设备支持时加载，保留旧设备的 CPU 回退；不再对整个原生库强制使用 `armv8.7a+i8mm`。Android 最低 API 与应用一致，保持 26。
`tools/verify-translation-package.py` 检查 APK 含这些库、DSP 骨架、下载器、不含 GGUF，且 16 KiB 对齐。

`tools/Build-Translation.ps1`（Windows）仍指向旧的 STQ 补丁和旧 llama.cpp 版本，与本方案不兼容，不用于 Android 本地推理。

## 分支原作者实测（SM8750 HONOR PPG-AN00，独立测试程序，未走 UE 应用）

| 内容 | 后端 | 结果 |
|---|---|---|
| 79 token 短文（首次，含 SHA 校验 9.4 s、加载 2.9 s） | HTP | 13.3 s |
| 同一段，模型已加载 | HTP | 0.72 s（decode 约 42 t/s） |
| 同一段，模型已加载 | CPU | 1.08 s |
| 3.4k 字符长介绍，27 个保护标记 | HTP | 首字 0.19 s，全文 27 s（prefill 约 2200 t/s，decode 约 37 t/s），标记全部保留 |

后续在同一台手机上用原生测试程序补测（`crank_translation_trust_model` 之后的版本）：

| 项目 | 结果 |
|---|---|
| 冷启动，已信任模型（跳过 SHA） | HTP 4.8 s（加载 3.7 s），此前含校验为 13.3 s |
| OpenCL | 短文热态 2.3 s、冷启动 13.3 s，明显慢于 HTP，仅作后备 |
| HTP 骨架目录无效，`auto` | 加载失败后自动改用 CPU（首次约 4.2 s，之后复用模型 1.1 s）；显式 `htp` 则报 `model_load_failed` 不回退 |
| 取消 | 加载中约 1.9 s 内返回，解码中途 85 ms 内返回；之后完整翻译正常 |
| 损坏的模型（翻转 1 字节） | 9.3 s 后报 `model_invalid`；大小不符 40 ms 内拒绝 |
| 原版 1.25-bit 文件 | `stq1_requires_type_remap` |
| 持续负载：24 次 1.5k 字符（HTP，约 4.5 分钟） | 每次 10.9–11.0 s，无降频；CPU 温度 39→56 °C；进程 RSS 260→267 MB |

以上数据来自分支原作者的单台设备记录。本次合并验证未复测 HTP、OpenCL 实际加速，也未覆盖所有 DSP 版本、峰值内存和持续负载。长介绍的本地总耗时可能长于在线服务，但支持流式输出。

## 本次合并验证（Pixel 4，Android 13）

| 项目 | 结果 |
|---|---|
| WSL 直接构建 | Linux NDK r29、Hexagon SDK 6.6／Tools 19.0.07；CPU 全部变体、OpenCL、Hexagon 及 v73／v75／v79／v81 骨架编译成功，无需运行 Docker |
| UE Win64／Android | 编译通过；新 APK 覆盖安装成功；推理库代码匹配构建，ARM64 加载段 16 KiB 对齐，不包含模型 |
| 377 字符操作说明，CPU 独立探针 | 首次 26.5 s（校验 9.8 s、首个输出 18.1 s）；同进程第二次 10.8 s（首个输出 2.6 s），使用 `android_armv8.2_2` 后端；流式 UTF-8 和完整输出一致 |
| 自动后端选择，独立探针 | 加速器无法加载后回退 CPU，24.4 s 完成；统计 `cpu*` 表示这次回退 |
| UE 内部 PDGames 详情页 | 本地中文直接显示、持久缓存、介绍接口离线时读取缓存、切回原文通过；显式本地模式未调用在线翻译接口 |
| 竖屏翻译设置 | 实际显示截图确认本地选项、服务说明和模型状态可见；沿用统一操作按钮和滚动设置布局 |
| 下载器，独立探针 | 手机模式 17 项通过：真实 Wi-Fi 绑定、重定向、写入字节数与 SHA 一致、短文件／超长响应／HTTP 错误拒绝、取消清理、重试；不改变手机网络设置，未模拟真实 Wi-Fi 断开 |
| 在线回归 | 12 项通过，包含微软、腾讯、备用服务、缓存、标记恢复、错误回复、取消请求、介绍加载期间取消以及本地路由策略 |

修复了原分支的全局高阶 ARM 指令要求、Android 最低 API 偏高、Android 12+ 可选厂商驱动声明遗漏、补丁重复应用失败、同大小 DSP 骨架升级不刷新、取消下载按钮文字及取消后迟到介绍仍启动翻译的问题。

Pixel 4 只证明旧设备兼容与 CPU 回退，不能证明较新骁龙设备的 HTP 性能。证据保存在本机 `D:/PlaydateAI/htp-merge-20261009/`，APK、模型和原始设备日志不提交 Git。

复现入口：`tools/verify-online-translation.py`、`tools/verify-translation-package.py`、`tools/verify-phone-local-translation.py`、`tools/Build-TranslationSpeedProbe.ps1`。下载器可用 `tools/Build-ModelDownloadProbe.ps1` 编译；手机保持已验证的 Wi-Fi，通过 `adb reverse tcp:18783 tcp:18783` 连接 `tools/model-download-fixture-server.py`，以 `am instrument -w -r -e safePhone true com.cranklab.modelprobe/com.cranklab.ModelProbe` 验证；只有默认的隔离模拟器模式才会切换网络设置。
