# 游戏介绍在线翻译

> 之后在 Android 上加入了可选的本地翻译（自动选择时优先本地、在线兜底），见 [LOCAL-TRANSLATION.md](LOCAL-TRANSLATION.md)。下文中“应用不再加载本地翻译模型／不提供模型下载”描述的是 2026-10-09 当时的状态，现已被取代；微软／腾讯的接口、保护标记、缓存和取消机制仍然适用。

2026-10-09。正式应用改为在线翻译，保留 PDGames 详情内的中文／原文切换，不弹出翻译对话框。应用不再加载本地翻译模型、启动本地推理或提供模型下载。

## 接口与选择

参考 [kiss-translator 的接口适配代码](https://github.com/fishjar/kiss-translator/blob/e7ccd36ac2261928e0e52f56a41531a7b30e0902/src/apis/trans.js)，固定参考提交为 `e7ccd36ac2261928e0e52f56a41531a7b30e0902`，未引入浏览器扩展本身。

| 服务 | 入口 | 应用行为 |
|---|---|---|
| 微软 | `edge.microsoft.com/translate/translatetext` | 默认首选；英语 → 简体中文；POST 字符串数组 |
| 腾讯 | `transmart.qq.com/api/imt` | 自动模式中的备用；也可单独选择 |
| 火山 | 扩展中的 `translate.volcengine.com/crx/translate/v1` | 此次测试均返回 HTTP 307，不接入应用 |

这些是网页／浏览器使用的无密钥入口，不等同于有服务保障的付费开发者 API。首次翻译需要联网，服务可能调整或限流。自动模式最多尝试两个服务，每次请求超时 8 秒；失败后保留原文并提供重试。设置可固定服务，并保存选择。

只发送待翻译的公开介绍文本，不发送存档、账户、设备识别码或购买信息。

## 性能与译文质量

Pixel 4，Android 13，真实网络请求，独立 Java 测试包：

| 服务 | 次数 | 请求耗时 |
|---|---:|---:|
| 微软 | 6 | 0.214–0.873 秒 |
| 腾讯，修正标记空格后的复测 | 3 | 0.569–3.921 秒 |

语料包含 323 字符的简介、377 字符的操作说明、3315 字符的长介绍。这里计时从发出翻译请求到接收译文，不包含打开详情、下载图片或应用冷启动。少量样本不能保证所有网络环境都有相同速度。相比此前用户反馈的本地轻量模型约一分钟，在线方案适合当前用途。

操作说明中的顺逆时针、长按、12 个关卡、3 个难度和自动存档意思正确。长文本可完整返回，但普通机翻仍会误解语境，例如将 board 译成“滑板”、将故事中的 “Five more” 理解为“再来五杯”。因此保留原文对照，不把测试通过等同于翻译完全准确。

生产代码保护 URL、PDX 文件名、Playdate、方向键名称、展开／收起摇柄操作和按钮符号，回复后恢复原值或固定术语。腾讯有时给保护标记插入空格，恢复逻辑兼容这种情况；标记缺失或重复则拒绝结果并尝试备用。通用语义误译仍可能发生。

## 缓存、取消与升级

缓存使用游戏 ID、规范化原文、服务、目标语言和术语版本生成键。不同服务缓存独立；原文变化会产生新键。只保存完整、校验通过的译文，已保存译文可离线读取。

请求在离开详情、返回原文、切换服务或应用进入后台时取消。请求版本检查拒绝迟到响应，避免旧介绍覆盖新介绍。接口响应限制为 1 MiB，原文限制为 24000 UTF-16 单元；长介绍按实际保护后的长度分段，每段最多 3500 单元，保持代理对、按钮符号和分段空白。

升级仅删除应用自己的 `Translation/Models/Hy-MT2-1.8B-1.25Bit.gguf`、`Hy-MT2-1.8B-Q4_K_M.gguf`、对应 `.part-*` 和旧 `model.json`。游戏存档、原文和译文缓存保留；不删除外部用户文件。历史 CPU／LiteRT／GPU 实验源代码与开发目录不参与正式应用构建。

## 验证证据

UE 实际异步 HTTP 服务通过 11 项检查：真实微软操作说明、真实微软长介绍、真实腾讯说明、密集符号多段输入、微软失败转腾讯、腾讯标记带空格、两端失败、取消后拒绝迟到结果、空数组响应、多余响应条目、保护标记丢失。另验证服务选择持久化、缓存键变化、服务缓存隔离、持久缓存及离线读取。

实际 Lua／核心桥通过中文显示、滚动、恢复原文、翻译取消、错误重试、游戏 ID 校验、过期会话拒绝、原有截图浏览和下载／运行入口检查。桌面 Slate 横屏 1280×720、竖屏 600×1000 的服务选择界面已截图检查；设计 lint 和 premium 严格审查均为 0 错误、0 警告。

最终 APK 为 `Playdate-online-arm64.apk`，166077378 字节，SHA-256：

```text
2fc9b20dc212bb70e4cfd15cd90bd8b8daabfce9ba43bc5924556debc7a0fa90
```

已确认 APK 不含 GGUF／LiteRT 模型、llama／ggml／crank-translate 推理库及模型下载器类。打包的核心 ELF 代码与此次构建匹配，所有 arm64 原生库的加载段符合 16 KiB 对齐。

完整本地证据位于 `D:/PlaydateAI/online-translation/`；不将 APK、设备存档或原始设备日志加入 Git。

最终 APK 已覆盖安装到 Pixel 4，未清空应用数据。升级前后的 SHA-256 校验确认 9 个原文缓存、译文缓存和非启动器存档文件保持不变，证据为本地 `upgrade-preservation.json`。手机当前锁屏，GameActivity 启动后立刻进入后台，实际 UE 详情页显示／原文切换与首次启动模型清理的真机验收等待解锁；旧私有模型文件目前仍在。上述独立接口测速已完成，但不能代替这几项应用内验收；尚未将它们记为通过。

## 复现

从仓库根目录执行，SDK／UE 路径可通过各脚本参数调整：

```powershell
python outputs/ue5-playdate/tools/probe-online-translation.py --providers microsoft,tencent --output D:/PlaydateAI/online-translation/pc-results.json
./outputs/ue5-playdate/tools/Build-OnlineTranslationProbe.ps1
# 在专用测试设备安装生成的独立测试 APK 后执行：
python outputs/ue5-playdate/tools/Run-OnlineTranslationProbe.py --serial DEVICE_SERIAL --provider microsoft
python outputs/ue5-playdate/tools/Run-OnlineTranslationProbe.py --serial DEVICE_SERIAL --provider tencent --repeats 1
./outputs/ue5-playdate/tools/Build-Unreal.ps1 -Platform Win64
python outputs/ue5-playdate/tools/verify-online-translation.py
./outputs/ue5-playdate/tools/Build-AndroidRuntime.ps1 -Abi arm64-v8a -BuildDirectory D:/PlaydateAI/online-translation/runtime-android
./outputs/ue5-playdate/tools/Build-Unreal.ps1 -Platform Android -Architecture arm64
./outputs/ue5-playdate/tools/Cook-Unreal.ps1 -Architecture arm64 -SkipBuild -OutputDirectory D:/PlaydateAI/online-translation/package
python outputs/ue5-playdate/tools/verify-translation-package.py --apk APK_PATH --output D:/PlaydateAI/online-translation/package-check.json
```

手机上的实际 UE 详情页验证使用 `verify-phone-online-translation.py`，在新的应用私有目录内复制固定的测试启动器状态，不覆盖用户存档。`verify-online-upgrade.py snapshot/verify` 用于升级前后检查缓存与非启动器游戏存档的散列，启动器自己的可刷新缓存不作为不可变存档。

本次设计变化沿用共享 `SCrankAction`、CrankTheme 字体、黄色选中态与现有设置滚动容器；移除旧模型进度和下载入口。DESIGN.md、UX-CONTRACT.md 已同步，未改变其他 HUD 控件的视觉规则。
