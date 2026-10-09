# Playdate（CrankLab）

基于 Unreal Engine 5 的 Android Playdate 兼容模拟器原型，以及最初的网页版 3D 掌机参考项目。

Android 应用名称为 **Playdate**，使用黄色掌机图标和配套启动画面。
图标／启动页原始素材及生成提示位于 `outputs/ue5-playdate/Unreal/Build/Branding/`。
内部 UE 项目仍为 CrankLab，包名保留 `com.cranklab.emulator`，便于覆盖安装与保留数据。

目前支持 Lua 与未加密 `pdex.bin` ARM 游戏代码、掌机 3D 视角、沉浸式游戏屏幕、触控按键和摇柄、屏幕滑动模拟摇柄、运动跟随与震动设置。PDGames 是默认启动器，游戏详情内提供下载／运行和中文／原文切换。

游戏介绍改为在线翻译，参考 kiss-translator 的接口适配：默认优先微软、失败时尝试腾讯，设置里也可固定选择。中文仍直接显示在 PDGames 详情中；翻译结果保存在本机，可离线阅读。首次翻译需要网络，不需要密钥或下载模型。Android 上另可在设置中下载约 1 GiB 的 Hy-MT2 Q4_0 模型，在本机翻译（骁龙设备走 Hexagon NPU，其他设备走 Adreno／CPU）；“自动选择”在模型就绪时优先本地，失败或 30 秒无输出时改用在线。升级时保留游戏存档和译文缓存。详见 [本地翻译](outputs/ue5-playdate/translation/LOCAL-TRANSLATION.md)。

系统界面由兼容层实现，没有捆绑原厂固件；完整游戏兼容性仍需逐项验证。

## 项目结构

- `outputs/ue5-playdate/Unreal/`：UE5 C++ 项目、CrankRuntime 插件、内容资产、设计与交互约定。
- `outputs/ue5-playdate/runtime/`：Lua、C API 兼容层和基于 Unicorn 的 ARM 执行后端。
- `outputs/ue5-playdate/translation/`：本地 Hy-MT2 推理库（`crank_translate.cpp`、`build-snapdragon.sh`）、中文字体，以及历史 LiteRT／STQ 实验资料。
- `outputs/ue5-playdate/tools/`：构建、资产转换与验证脚本。
- `outputs/playdate-local/`：原始模型、材质与交互的网页版参考实现。

Unicorn 使用锁定提交的 Git 子模块。SDL、SDL_mixer、zlib 和兼容 Lua 源码保留在项目中。来源记录见 `outputs/ue5-playdate/third-party.json` 和各依赖内的说明文件。

## 获取与构建

```powershell
git clone --recurse-submodules https://github.com/dodola/cranklab.git
cd cranklab/outputs/ue5-playdate
```

已验证的环境为 Windows、UE 5.8.3、Visual Studio 2022 C++ 工具、Android NDK 27.2.12479018、CMake 3.22.1 和 JDK 21。脚本中的默认安装路径来自开发机，需要按实际环境传入 `EngineDirectory`、`SdkDirectory`、`NdkDirectory`、`JdkDirectory` 等参数。部分 SDK API 生成和原生测试游戏编译需要本机 Playdate SDK。

构建 Windows 原生组件和 UE 编辑器：

```powershell
./tools/Build-Runtime.ps1
./tools/Build-Unreal.ps1
```

构建 Android ARM64 APK：

```powershell
./tools/Build-AndroidRuntime.ps1 -Abi arm64-v8a
./tools/Build-Unreal.ps1 -Platform Android -Architecture arm64
./tools/Cook-Unreal.ps1 -Architecture arm64 -SkipBuild
```

Unreal 项目入口是 `outputs/ue5-playdate/Unreal/CrankLab.uproject`。仓库保留内容资产，因此普通源码构建无需重新执行资产转换。

## RetroArch（libretro）核心

`outputs/ue5-playdate/libretro/` 把同一个运行时做成 libretro 核心，可以在 RetroArch 里运行，目前只支持 Linux（x86_64，以及 ArkOS、EmuELEC 一类掌机系统的 aarch64 / armhf）。支持 Lua 与 ARM `pdex.bin` 游戏、摇柄（L/R 键、右摇杆、L3）、画在画面上的 Playdate 系统菜单，并随 RetroArch 的快进、暂停同步。不支持即时存档和联网。

```sh
cd outputs/ue5-playdate/libretro
git submodule update --init
./build.sh native
tests/run-tests.sh dist/native/cranklab_libretro.so
```

x86_64 上 57 项自动化测试通过；aarch64 / armhf 产物只确认能编译，尚未在真机运行。详见 [libretro/README.md](outputs/ue5-playdate/libretro/README.md) 和 [详细文档](outputs/ue5-playdate/libretro/docs/GUIDE.md)。

网页版参考：

```powershell
cd outputs/playdate-local
npm start
```

## 当前验证与限制

已完成 Pixel 4 在线接口实测：微软六次请求为 0.21–0.87 秒，腾讯最新三次为 0.57–3.92 秒。这些数据是请求耗时，不包含打开详情的网络加载。具体译法仍可能有误，保留原文供对照。

无密钥接口来自服务的网页／浏览器入口，可能变化或限流；应用提供备用服务、明确重试和本地译文缓存。源码与复现脚本见 `outputs/ue5-playdate/tools/probe-online-translation.py`，项目集成报告见 [在线翻译](outputs/ue5-playdate/translation/ONLINE-TRANSLATION.md)。历史本地 CPU／LiteRT 实验保留供参考，正式应用无需运行其构建脚本。后续手机触控验收由用户进行。

构建产物、模型文件、私人游戏存档、编译缓存和测试工作目录不纳入 Git。开发交付与历史验证说明保留在源码目录中。
