# LiteRT-LM 翻译实验（2026-10-09）

Pixel 4 的 LiteRT-LM GPU 路径已跑通，但本轮 Qwen3-0.6B 候选没有通过翻译质量验收。正式 Playdate App 保留 HY-MT2／llama.cpp CPU 后端；本实验没有接入 NPU，也没有改变 UE 渲染、模型下载或游戏存档。

## 设备、模型与配置

- Google Pixel 4，SM8150／Adreno 640，Android 13，约 5.6 GiB 系统内存。
- LiteRT-LM 0.18.0。独立包名 `com.cranklab.litertprobe`，ARM64，CPU 明确设置 4 个线程，GPU 明确指定 `Backend.GPU`。失败直接记录，不自动切换 CPU。
- `Qwen3-0.6B_dynamic_wi4b32_afp32.litertlm`：344,671,744 字节，约 329 MiB，动态 INT4。初步实验出现操作说明错误和输出异常。
- `qwen3_0_6b_mixed_int4.litertlm`：497,516,544 字节，约 475 MiB。最终 CPU／GPU 对照使用同一文件、相同输入与采样参数。
- 2048 上下文；每段最多约 1600 UTF-8 字节，优先按段落／空格切分。短文和操作说明输出上限 512 token，长介绍每段 1100 token。达到上限会记录为不完整，不按成功译文处理。
- `topK=20, topP=0.8, temperature=0.2, seed=3407`。同时设置 `ThinkingConfig(false, 0)` 和模板参数 `enable_thinking=false`。
- 最终配置将翻译要求与原文放在 user 消息中。完整提示词保存在结果 JSON；初始 system 提示词／英文严格提示词的探索记录不混入最终对照数据。
- GPU 与 CPU 使用各自默认激活精度，GPU 默认 FP16，CPU 默认 FP32；虽然权重、输入、种子一致，输出仍会不同。须同时核对 token 数量及内容，不能把全部时长差异归因于硬件。

模型来自固定版本的 ModelScope 镜像，SHA-256 与 LiteRT 社区上游文件的 LFS 哈希交叉核对。锁定信息在 `tools/android-litert-probe/model*.json`。依赖从阿里云 Maven 镜像下载，并与 Google Maven／Maven Central 原始校验信息核对；实际 SHA-256 和字节数保存在 `dependencies.json`。模型、AAR、JAR、APK、编译缓存与原始报告全部放在 `D:/PlaydateAI/litert-experiment/`，不进入 Git。

## 结果与限制

校准后的混合 INT4 短文为 323 UTF-8 字节，同一引擎内执行两次，每次创建独立 conversation：

| 后端 | 初始化 | 第一次请求 | 第二次请求 | 第二次首段可见文字 | 第二次生成速率 | 请求后 PSS |
|---|---:|---:|---:|---:|---:|---:|
| CPU | 1.54 s | 7.84 s | 5.97 s | 2.57 s | 16.44 token/s | 约 2.20 GiB |
| GPU | 6.76 s | 4.43 s | 3.42 s | 1.26 s | 21.94 token/s | 约 1.37 GiB |

请求时长不包含独立列出的初始化。测试没有清除操作系统页缓存或已有 GPU 编译缓存，因此“第一次请求”不等于完全冷启动。第二次请求复用已初始化的引擎，未复用上一段原文或译文的 conversation。短文各后端两次结果一致，但 CPU／GPU 译文不同：CPU 把“B 返回”译成“B 前进”；GPU 的此处正确，但遗漏“益智”类别，把转动摇柄说成按摇柄。

操作说明样本包含长按 B、顺逆时针摇柄、禁止校准时拔出摇柄、双击退出全屏、12 个关卡、3 种难度以及联网范围。混合 INT4 GPU 的生成约 4.96 s，但把摇柄变成方向键、遗漏长按与双击，并把“不要拔出”改成“不要松开”。

PDGames 的真实长介绍取自已缓存的游戏 2524（Vet’s Quest I: The Lump），原文 3395 UTF-8 字节，切分为三段。混合 INT4 GPU 请求约 16.40 s，首段可见文字约 3.36 s，输出共 252 token。虽然未达到 token 上限，仍大量遗漏病患、小游戏与操作说明，重复安装步骤，并把安装时应解压的目录说成未解压文件。自然结束不能等同于全文翻译完成。

同一配置的 CPU 长文约 105.45 s，首段可见文字约 7.41 s，输出 684 token。它既有错误译法，也有大段英文未翻译。CPU 与 GPU 的输出长度和完整性差异很大，因此长文总时长不能用来宣称 GPU 有约六倍的有效翻译收益。

HY-MT2 轻量模型的同一短文实际译文完整，包含“益智游戏”“转动摇柄”“按 B 返回”。既有原生性能报告记录了短文预热后约 10.75 s、长文约 172.78 s／833 输出 token。本轮质量采集中的 HY-MT2 时长更高，不能用来宣称固定倍率收益：设备状态、目标包进程优先级及前台条件会影响结果。HY-MT2 与 Qwen 的权重、模板、tokenizer 和译文长度不同，本轮主要对比内容准确性；硬件对照采用同一 Qwen 文件。

HY-MT2 1.25Bit 的固定操作说明也不是质量通过的基线：它把“退出全屏”译成“进入全屏”，把“校准时不要拔出摇柄”译成“不要松开摇柄”，长按 B 的描述也不准确。保留现有后端只是避免引入本轮 Qwen 的漏译回归；后续 HY-MT2 加速实验应先用 Q4 建立内容基线，并同时评估轻量量化与提示词的影响。

这是少量固定文本的探索实验，不是完整质量评估或正式 App 内与 UE 同时渲染的性能验收。模型已提前校验，Qwen 请求时长也不包含正式模型下载／校验流程。短文与 GPU 长文测试的 Android thermal status 为 0；CPU 长文从 0 上升到 1，持续负载的温度变化也限制了直接时长对比。完整温度日志在 D 盘。

验收结论：GPU 后端兼容性通过；Qwen3-0.6B 翻译质量未通过，暂停正式接入该候选。下一轮应优先验证保留 HY-MT2 质量的 GPU 路径或更适合翻译的转换模型，再在 UE 前台验证内存、首字、长文完整性、后台释放与取消。NPU 需要另外验证支持的 SoC、转换模型和运行库，不能由 GPU 实验推定可用。

## HY-MT2 的 GPU／NPU 路线

当前 App 的原生包装器显式使用 `n_gpu_layers=0`，轻量 STQ1_0 由所锁定补丁的 CPU／NEON／DotProd 实现运行。440 MiB 文件的特殊量化内核不能因为有 GPU 或 NPU 就自动迁移；本轮检查的上游 OpenCL 与 Hexagon 类型支持中没有 STQ1_0。

保留 HY-MT2 时，优先验证标准 Q4_K_M 模型与 llama.cpp GPU 后端。最新上游 OpenCL 支持 Q4_K、Q6_K 等常用量化类型，但其官方文档明确提醒手机上的旧 A6xx 驱动可能不支持该后端。Pixel 4 的 LiteRT GPU 成功不能证明 llama.cpp OpenCL 也成功；仍需检查实际 GPU 层数、错误回退、完整输出和驱动兼容性。

NPU 有两类路线：

- 最新 llama.cpp 的实验性 Hexagon 后端，可将兼容量化的 GGUF 算子卸载到 Qualcomm HTP。本轮读取的主线源码已有 Q4_K、Q6_K 等 K 类型支持，因此不应笼统认为所有 GGUF 都必须先转 ONNX／LiteRT 才能使用 NPU。需要单独锁定验证过的运行库、Hexagon SDK 和目标架构；模型架构、算子覆盖及 HTP 上实际执行层数仍须实测。当前 App 的 CPU 构建没有开启此后端，HY-MT2 的手机 NPU 运行尚未验证。
- LiteRT／QNN 等运行库的模型转换路线，需要重新导出模型，验证 tokenizer、特殊 token、RoPE、KV cache、量化与目标 SoC；本轮未确认现成的 HY-MT2 Android NPU 包。现有 `.gguf` 不能直接作为 LiteRT `.litertlm` 使用。

上游参考（本轮核对日期 2026-10-09）：[OpenCL](https://github.com/ggml-org/llama.cpp/blob/master/docs/backend/OPENCL.md)、[Hexagon 构建与运行](https://github.com/ggml-org/llama.cpp/blob/master/docs/backend/snapdragon/README.md)、[Hexagon 类型支持源码](https://github.com/ggml-org/llama.cpp/blob/master/ggml/src/ggml-hexagon/ggml-hexagon.cpp)、[STQ CPU 内核 PR](https://github.com/ggml-org/llama.cpp/pull/22836)、[LiteRT-LM NPU](https://developers.google.com/edge/litert/next/litert_lm_npu)。

## 排查所得

1. Pixel 4 公共库列表提供的是 `libOpenCL-pixel.so`。仅声明 `libOpenCL.so` 时，引擎可初始化，首条推理仍会报 `Can not find OpenCL library`。加入 Pixel 库声明后推理成功。只检查磁盘是否有驱动文件不足以判断 App 能否加载。
2. LiteRT-LM 必须在创建 engine 前开启 `ExperimentalFlags.enableBenchmark`，否则 `getBenchmarkInfo()` 报错。报告分别记录墙钟首段文字与原生 TTFT，不混用两个指标。
3. 混合 INT4 的内嵌旧模板不接受新版消息 content 列表。实验用官方 Qwen3 模板覆盖，来源提交 `a8178d7dca6ac4d472476c9d76e482d8c8cbee3c`，通过 `ConversationConfig.chatTemplate` 传入；未修改模型权重。模板从上游 Apache-2.0 源文件提取并保留来源：
   <https://github.com/google-ai-edge/LiteRT-LM/blob/a8178d7dca6ac4d472476c9d76e482d8c8cbee3c/models/qwen3/LlmMetadataProto.pbtext>。
4. SDK 36.1.0 自带 D8 无法正确处理 Kotlin 2.4 元数据。独立工具锁定 Google Maven 的 R8／D8 9.5.23，并把它放在 D 盘；UE 的 NDK 与主工程构建配置保持原样。
5. 独立原生 JNI 库的 ELF LOAD 对齐为 0x4000，APK 经过 16 KiB zipalign 和签名验证。

## 复现

从仓库根目录执行，默认缓存目录为 D 盘；新机器按需传入 SDK、JDK、签名文件与缓存路径。签名密码按现有开发机 Android debug keystore 的默认值处理。

```powershell
python outputs/ue5-playdate/tools/Fetch-LiteRtProbe.py --model mixed-int4
./outputs/ue5-playdate/tools/Build-LiteRtProbe.ps1
python outputs/ue5-playdate/tools/Run-LiteRtProbe.py --serial DEVICE_SERIAL --model mixed-int4 --backend CPU --case short --report cpu-short --repeats 2
python outputs/ue5-playdate/tools/Run-LiteRtProbe.py --serial DEVICE_SERIAL --model mixed-int4 --backend GPU --case short --report gpu-short --repeats 2
```

`--case controls` 测量固定操作说明；`--case long --repeats 1` 从当前 debug Playdate 的已缓存游戏 2524 中只读原文。正式 App 需已缓存该介绍。脚本验证电脑与手机模型哈希，保存原始结果、温度与指定进程的 native 日志。工具需要显式指定设备 serial，不自动挑选其他设备。它会覆盖安装自己的测试包，保留主 App 数据。

`runtimeSuccess=true` 仅表示没有异常、空输出或输出上限截断；仍须人工核对原文。首条请求包括 conversation 创建等工作，`firstVisibleSeconds` 指首个非空文字回调，并不要求第一个字符已为中文。输出原文／复制英文同样不能视为质量通过。

需要采集 HY-MT2 对照译文时，原有 `Build-TranslationSpeedProbe.ps1` 支持 `-BuildDirectory D:/PlaydateAI/litert-experiment/hy-probe-build`。`SpeedProbe` 读取正式 App 现有模型，原始 metrics 中返回完整 UTF-8 译文；可传 `-e case controls` 或 `-e sourceId 2524`。它不写入译文缓存或游戏存档。

测试后仅卸载 `com.cranklab.litertprobe` 与 `com.cranklab.speedprobe`，再打开 `com.cranklab.emulator`。测试包自有的手机模型和 GPU 缓存随卸载清除；正式 App 模型、原文、译文缓存及游戏存档保留。电脑 D 盘保留可复现模型和报告。

官方运行库参考：<https://developers.google.com/edge/litert-lm/android>。
