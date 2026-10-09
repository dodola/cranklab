# CrankLab libretro 核心

在 RetroArch 里运行 Playdate 游戏的 libretro 核心。它复用 CrankLab 的兼容运行时（Lua 游戏和未加密的 `pdex.bin` ARM 设备代码），不含任何官方固件或 SDK CoreLibs。

目前只做了 **Linux**：桌面（x86_64）和掌机系统（ArkOS、EmuELEC、ROCKNIX 一类的 aarch64 / armhf）。Windows、macOS、Android 的 RetroArch 核心还没做。

> 状态：x86_64 上有 57 项自动化测试全部通过。aarch64 和 armhf 能编出来，但**没有在真机或真正的 RetroArch 上运行过**。详见[详细文档](docs/GUIDE.md)的"验证范围"一节。

## 能做什么

- 运行 Lua 游戏，以及带 ARM 设备代码的 `pdex.bin` 游戏（含 C 与 Lua 混合）。
- 画面 400×240，三种调色板（Playdate 液晶灰绿、纯黑白、反色）。
- 44.1 kHz 立体声，音画随 RetroArch 的快进、慢放、暂停同步。
- 十字键、A/B、**摇柄**（L/R 键、右摇杆画圈、L3 收起/拉出）。
- **Playdate 系统菜单**（游戏自定义的按钮、勾选、多选项）画在画面上，Start 打开。
- 游戏自己的存档放进 RetroArch 的存档目录。

不支持：即时存档、倒带、runahead（运行时没有可序列化的状态）；联网；PDGames 启动器。

## 快速开始

构建并跑一遍测试（需要 cmake 3.20+、ninja、gcc、python3）：

```sh
git submodule update --init          # 拉取 Unicorn，只需第一次
./build.sh native                    # 产物：dist/native/cranklab_libretro.so
tests/run-tests.sh dist/native/cranklab_libretro.so
```

用 RetroArch 打开测试游戏：

```sh
mkdir -p ~/.config/retroarch/system/cranklab/fonts
cp dist/native/system/cranklab/fonts/cranklab-zh.pft ~/.config/retroarch/system/cranklab/fonts/
retroarch -L dist/native/cranklab_libretro.so ../native-test-games/Life.zip
```

掌机用的交叉构建：

```sh
pip install ziglang
ZIG="python3 -m ziglang" ./build.sh aarch64     # 也可以是 x86_64、armhf
```

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
├── CMakeLists.txt         独立的 Linux 构建，静态链接 SDL、mixer、zlib、Lua、Unicorn
├── link.T                 版本脚本：只导出 retro_* 符号
├── cmake/                 zig 交叉工具链包装
├── cranklab_libretro.info 核心信息文件
├── include/libretro.h     libretro API 头文件（MIT）
├── src/                   核心源码：入口、内容加载、系统菜单
└── tests/                 测试前端和回归测试
```

## 许可

核心静态链接了 Unicorn，Unicorn 采用 GPLv2，所以分发核心的二进制时需要按 GPLv2 兼容的方式发布。菜单使用的中文字体的许可没有核实，见[详细文档](docs/GUIDE.md)的"许可与第三方"一节。
