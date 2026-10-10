# 开发与构建

玩家介绍与下载入口见 [项目首页](../README.md)。本页保留开发环境、源码结构和构建入口；各平台的详细操作与验证范围仍以对应文档为准。

## 源码导航

| 目录 | 内容 |
|---|---|
| [`Unreal`](../outputs/ue5-playdate/Unreal) | Android UE 项目、CrankRuntime 插件、内容资产、界面与交互 |
| [`runtime`](../outputs/ue5-playdate/runtime) | Lua / C API 兼容层、ARM 执行后端与共享宿主接口 |
| [`translation`](../outputs/ue5-playdate/translation) | 本地推理、在线翻译、中文字体与历史实验资料 |
| [`switch`](../outputs/ue5-playdate/switch/README.md) | Switch 原生前端、PDGames 适配、NRO 构建与验证 |
| [`libretro`](../outputs/ue5-playdate/libretro/README.md) | Linux RetroArch 核心、构建与回归测试 |
| [`tools`](../outputs/ue5-playdate/tools) | Android / Windows 构建、资产转换及验证脚本 |
| [`playdate-local`](../outputs/playdate-local/README.md) | 最初的网页版模型与交互参考 |

## Android / Windows

```powershell
git clone --recurse-submodules https://github.com/dodola/cranklab.git
cd cranklab/outputs/ue5-playdate
```

已使用 Windows、UE 5.8.3、Visual Studio 2022 C++ 工具、Android NDK 27.2.12479018、CMake 3.22.1 和 JDK 21 构建。脚本默认路径来自开发环境，请按实际安装位置传入 `EngineDirectory`、`SdkDirectory`、`NdkDirectory` 与 `JdkDirectory`。部分 API 生成与原生测试游戏编译需要本地 Playdate SDK。

Windows 原生组件与 UE 编辑器：

```powershell
./tools/Build-Runtime.ps1
./tools/Build-Unreal.ps1
```

Android ARM64：

```powershell
./tools/Build-AndroidRuntime.ps1 -Abi arm64-v8a
./tools/Build-Unreal.ps1 -Platform Android -Architecture arm64
./tools/Cook-Unreal.ps1 -Architecture arm64 -SkipBuild
```

当前 Android APK 还需要本地翻译库和后端插件。先按 [本地翻译构建说明](../outputs/ue5-playdate/translation/LOCAL-TRANSLATION.md#构建与打包) 使用 `translation/build-snapdragon.sh` 构建并放入插件的 Android 翻译库目录，再进行 UE 打包。历史 `Build-Translation.ps1` 不用于现行 Android HTP 方案。

项目入口为 `Unreal/CrankLab.uproject`。仓库保留已转换的内容资产，普通源码构建无需重新执行资产转换。APK 验证入口为 `tools/verify-translation-package.py`；发布附件中的验证报告记录对应构建的来源提交、签名与对齐检查。

## Switch 与 Linux

- [Switch 构建、按键与验证范围](../outputs/ue5-playdate/switch/README.md)：NRO 原生前端，首版仅支持 Lua，不依赖 UE。
- [Linux 核心快速开始](../outputs/ue5-playdate/libretro/README.md) 与 [完整指南](../outputs/ue5-playdate/libretro/docs/GUIDE.md)：默认构建 Lua 核心；实验性 ARM 后端单独启用。

Android 的 ARM C 支持不受这两个平台的 Lua 构建选项影响。各平台完整游戏兼容性仍需逐个验证；构建通过与主机回归不等于设备回归通过。

## 翻译与依赖

- [本地翻译](../outputs/ue5-playdate/translation/LOCAL-TRANSLATION.md)：模型来源、下载行为、CPU / OpenCL / HTP、构建及设备记录。
- [在线翻译](../outputs/ue5-playdate/translation/ONLINE-TRANSLATION.md)：微软与腾讯服务、缓存、备用服务及验证记录。
- [来源记录](../outputs/ue5-playdate/third-party.json)：兼容运行时、原始模型、素材与依赖版本。

Unicorn 使用锁定提交的 Git 子模块；SDL、SDL_mixer、zlib 和兼容 Lua 源码保留在仓库中。Linux 默认 Lua 核心不需要 Unicorn 子模块。

构建产物、翻译模型、私人游戏存档、编译缓存和测试工作目录不纳入 Git。网页参考可在 `outputs/playdate-local` 运行 `npm start`。
