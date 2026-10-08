# Playdate（CrankLab）

基于 Unreal Engine 5 的 Android Playdate 兼容模拟器原型，以及最初的网页版 3D 掌机参考项目。

Android 应用名称为 **Playdate**，使用黄色掌机图标和配套启动画面。
图标／启动页原始素材及生成提示位于 `outputs/ue5-playdate/Unreal/Build/Branding/`。
内部 UE 项目仍为 CrankLab，包名保留 `com.cranklab.emulator`，便于覆盖安装与保留数据。

目前支持 Lua 与未加密 `pdex.bin` ARM 游戏代码、掌机 3D 视角、沉浸式游戏屏幕、触控按键和摇柄、屏幕滑动模拟摇柄、运动跟随与震动设置。PDGames 是默认启动器，游戏详情内提供下载／运行和中文／原文切换。

游戏介绍在本机通过 HY-MT2 1.8B 翻译，默认使用约 440 MiB 的 1.25Bit 模型，设置中可以切换 Q4。模型首次单独下载，不包含在仓库或 APK 中。系统界面由兼容层实现，没有捆绑原厂固件；完整游戏兼容性和 Android 性能仍需逐项验证。

两个翻译模型均从腾讯的 ModelScope 魔搭仓库及国内 CDN 下载，固定版本并校验 SHA-256，已有模型与译文缓存可继续使用。

## 项目结构

- `outputs/ue5-playdate/Unreal/`：UE5 C++ 项目、CrankRuntime 插件、内容资产、设计与交互约定。
- `outputs/ue5-playdate/runtime/`：Lua、C API 兼容层和基于 Unicorn 的 ARM 执行后端。
- `outputs/ue5-playdate/translation/`：本地翻译后端、锁定的模型／依赖版本及 STQ 补丁。
- `outputs/ue5-playdate/tools/`：构建、资产转换与验证脚本。
- `outputs/playdate-local/`：原始模型、材质与交互的网页版参考实现。

Unicorn 使用锁定提交的 Git 子模块。SDL、SDL_mixer、zlib 和兼容 Lua 源码保留在项目中；llama.cpp 由翻译构建脚本按锁定版本下载并应用补丁。来源记录见 `outputs/ue5-playdate/third-party.json` 和各依赖内的说明文件。

## 获取与构建

```powershell
git clone --recurse-submodules https://github.com/dodola/cranklab.git
cd cranklab/outputs/ue5-playdate
```

已验证的环境为 Windows、UE 5.8.3、Visual Studio 2022 C++ 工具、Android NDK 27.2.12479018、CMake 3.22.1 和 JDK 21。脚本中的默认安装路径来自开发机，需要按实际环境传入 `EngineDirectory`、`SdkDirectory`、`NdkDirectory`、`JdkDirectory` 等参数。部分 SDK API 生成和原生测试游戏编译需要本机 Playdate SDK。

构建 Windows 原生组件和 UE 编辑器：

```powershell
./tools/Build-Runtime.ps1
./tools/Build-Translation.ps1 -Platform Win64
./tools/Build-Unreal.ps1
```

构建 Android ARM64 APK：

```powershell
./tools/Build-AndroidRuntime.ps1 -Abi arm64-v8a
./tools/Build-Translation.ps1 -Platform Android
./tools/Build-Unreal.ps1 -Platform Android -Architecture arm64
./tools/Cook-Unreal.ps1 -Architecture arm64 -SkipBuild
```

Unreal 项目入口是 `outputs/ue5-playdate/Unreal/CrankLab.uproject`。仓库保留内容资产，因此普通源码构建无需重新执行资产转换。

网页版参考：

```powershell
cd outputs/playdate-local
npm start
```

## 当前验证与限制

已验证本地真实翻译、缓存隔离与离线恢复、模型下载校验／取消、PDGames 中文／原文切换及 Android 原生库打包。STQ 的旧类型编号通过针对已校验文件的读取适配处理；详见 `outputs/ue5-playdate/translation/STQ-NOTES.md`。

1.25Bit 的个别译法弱于 Q4。Windows 通用 STQ 路径较慢，Android 使用专门的 ARM 内核；真机翻译速度、内存和持续运行表现尚未测量。后续手机触控验收由用户进行。

构建产物、模型文件、私人游戏存档、编译缓存和测试工作目录不纳入 Git。开发交付与历史验证说明保留在源码目录中。
