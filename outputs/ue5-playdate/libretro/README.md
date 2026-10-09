# CrankLab libretro 核心

在 RetroArch 里运行 Playdate Lua 游戏的 libretro 核心。第一版默认构建 `0.1.1-lua`，不链接 Unicorn，不支持 C 或 Lua+C 游戏，不含任何官方固件或 SDK CoreLibs。本目录的构建选择不会改变 Android UE 应用的 C 游戏支持。

目前只做了 **Linux**：桌面（x86_64）和掌机系统（ArkOS、EmuELEC、ROCKNIX 一类的 aarch64 / armhf）。Windows、macOS、Android 的 RetroArch 核心还没做。

> 兼容性按游戏验证。Lua 游戏仍可能使用尚未实现的 SDK API。测试范围见[详细文档](docs/GUIDE.md)的"验证范围"一节。
>
> 2026-10-10：默认 Lua 核心在 x86_64 和 RK3326/AArch64 的 ArkOS 设备上各通过 53 项回归测试。Music Box 与 Pooh Mini Mahjong 已在设备 RetroArch 1.22.2 上验证画面和正常退出。ARM64 核心约 2 MB，未链接 Unicorn。

## 能做什么

- 运行 Lua 游戏；包含非空 `pdex.bin` 的 C / Lua+C 游戏在启动前明确拒绝，空文件占位不会阻止 Lua 游戏。
- 画面 400×240，三种调色板（Playdate 液晶灰绿、纯黑白、反色）。
- 44.1 kHz 立体声，音画随 RetroArch 的快进、慢放、暂停同步。
- 十字键、A/B、**摇柄**（L/R 键、右摇杆画圈、L3 收起/拉出）。
- **Playdate 系统菜单**（游戏自定义的按钮、勾选、多选项）画在画面上，Start 打开。
- 游戏自己的存档放进 RetroArch 的存档目录。

不支持：即时存档、倒带、runahead（运行时没有可序列化的状态）；联网；PDGames 启动器。

## 快速开始

构建并跑一遍测试（需要 cmake 3.20+、ninja、gcc、python3）：

```sh
./build.sh native                    # 产物：dist/native/cranklab_libretro.so
tests/run-tests.sh dist/native/cranklab_libretro.so
```

用 RetroArch 打开测试游戏：

```sh
mkdir -p ~/.config/retroarch/system/cranklab/fonts
cp dist/native/system/cranklab/fonts/cranklab-zh.pft ~/.config/retroarch/system/cranklab/fonts/
retroarch -L dist/native/cranklab_libretro.so /path/to/lua-game.pdx.zip
```

掌机用的交叉构建：

```sh
pip install ziglang
ZIG="python3 -m ziglang" ./build.sh aarch64     # 也可以是 x86_64、armhf
```

从 Windows / WSL 构建时，shell 脚本必须使用 LF 换行，建议在 WSL 中克隆源码。实验性 ARM 构建还需检查 Unicorn 子模块的换行，详见[构建说明](docs/GUIDE.md#4-构建)。

默认构建不需要 Unicorn 子模块。实验性 ARM 后端仍可用 `LUA_ONLY=OFF ./build.sh native` 构建，但不属于第一版掌机支持范围；开启它才需要拉取 Unicorn。

部署到 EmuELEC、ArkOS 一类掌机系统（自动检测架构、读取设备上的目录、拷贝并校验）：

```sh
./deploy.sh --diagnose root@<设备IP>            # 先只检查设备
ZIG="python3 -m ziglang" ./deploy.sh root@<设备IP>
```

手动步骤、EmulationStation 接入和常见问题见[详细文档](docs/GUIDE.md)第 5 节。

交叉构建用 zig 把 glibc 符号版本钉在 2.17，所以同一个 `.so` 在旧系统镜像和新系统上都能加载。

## 加载什么内容

RetroArch 只能加载文件，而 Playdate 游戏是一个文件夹，所以核心接受：

| 内容 | 说明 |
|---|---|
| `游戏.zip`（`.pdx.zip`） | 首次加载时解压到系统目录的缓存里 |
| `main.pdz`、`pdxinfo`、`pdex.bin` | 游戏文件夹里的任意一个，所在文件夹就是游戏 |
| `游戏.pdxl` | 文本文件，第一行写游戏文件夹的路径（绝对或相对本文件），适合在 EmulationStation 里一个游戏一个条目 |

## 按键

| RetroPad | Playdate |
|---|---|
| 十字键 | 十字键 |
| B / A | B / A（核心选项里可互换） |
| L / R | 摇柄逆时针 / 顺时针 |
| 右摇杆 | 按摇杆角度转摇柄（沿着摇杆画圈） |
| L3 | 摇柄收起 / 拉出 |
| Start | 系统菜单（也可在选项里改成 Select） |

菜单里：上下移动，左右改勾选或多选项，A 确认，B 或 Start 关闭。

## 核心选项

| 选项 | 取值（默认在前） |
|---|---|
| 显示调色板 | `lcd` / `black_white` / `inverted` |
| 按键转摇柄速度（度/帧） | `6` / 3 / 9 / 12 / 18 / 24 |
| 右摇杆转摇柄 | `enabled` / `disabled` |
| 互换 A 和 B | `disabled` / `enabled` |
| 菜单键 | `start` / `select` |

## 目录

```
libretro/
├── README.md              本文件
├── docs/GUIDE.md          详细文档：架构、同步模型、构建、安装、排错
├── build.sh               构建脚本（native / x86_64 / aarch64 / armhf）
├── deploy.sh              通过 SSH 部署到掌机系统
├── CMakeLists.txt         独立的 Linux 构建，静态链接 SDL、mixer、zlib、Lua
├── link.T                 版本脚本：只导出 retro_* 符号
├── cmake/                 zig 交叉工具链包装
├── cranklab_libretro.info 核心信息文件
├── include/libretro.h     libretro API 头文件（MIT）
├── src/                   核心源码：入口、内容加载、系统菜单
└── tests/                 测试前端和回归测试
```

## 许可

默认 Lua 构建不包含 Unicorn。实验性 ARM 构建静态链接 GPLv2 的 Unicorn。其他组件和中文字体的来源见[详细文档](docs/GUIDE.md)的"许可与第三方"一节。
