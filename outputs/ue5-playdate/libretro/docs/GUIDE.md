# CrankLab libretro 核心：详细文档

本文说明核心怎么工作、怎么构建和安装、各项行为的细节、怎么测试，以及出问题时怎么排查。快速上手请看 [README](../README.md)。

目录

1. [总览](#1-总览)
2. [架构](#2-架构)
3. [一帧是怎么跑的：同步模型](#3-一帧是怎么跑的同步模型)
4. [构建](#4-构建)
5. [安装](#5-安装)
6. [加载内容](#6-加载内容)
7. [控制：按键、摇柄、菜单](#7-控制按键摇柄菜单)
8. [核心选项](#8-核心选项)
9. [存档与文件布局](#9-存档与文件布局)
10. [兼容性与限制](#10-兼容性与限制)
11. [测试](#11-测试)
12. [排错](#12-排错)
13. [对运行时的改动](#13-对运行时的改动)
14. [许可与第三方](#14-许可与第三方)
15. [后续工作](#15-后续工作)

---

## 1. 总览

CrankLab 是一个 Playdate 兼容运行时：用自己实现的 Lua 虚拟机和 SDK API 运行 Lua 游戏，用 Unicorn 模拟 Cortex-M7 运行 `pdex.bin` 里的 ARM 设备代码。它原本被嵌进 Unreal Engine 5 的 Android 应用里，对外只有一组 C 接口（`runtime/include/crank_host.h`）。

这个核心做的事很简单：**写一个 libretro 前端，调用同一组 C 接口**。运行时的 Lua、图形、音频、ARM 执行代码都没有重写，核心只负责把 RetroArch 的输入、视频、音频、时间和文件系统接上去。

| 项目 | 值 |
|---|---|
| 核心名 / 版本 | `CrankLab` / `0.1.0` |
| 支持的内容扩展名 | `pdx`、`pdz`、`pdxl`、`zip` |
| 是否需要完整路径 | 是（`need_fullpath`），且禁止 RetroArch 自己解压（`block_extract`） |
| 无内容启动 | 不支持 |
| 视频 | 400×240，XRGB8888，宽高比 5:3，30 fps（游戏可改） |
| 音频 | 44100 Hz，16 位，立体声 |
| 即时存档 / 倒带 | 不支持（`retro_serialize_size` 返回 0） |
| 目标平台 | Linux：x86_64、aarch64、armhf |

## 2. 架构

```
RetroArch
   │  retro_run / retro_load_game / 输入、视频、音频回调
   ▼
cranklab_libretro.so
 ├─ cranklab_libretro.c   核心入口：输入、视频、音频、暂停检测、会话管理
 ├─ cranklab_content.c    内容解析：zip 安全解压、.pdxl、文件夹
 ├─ cranklab_menu.c       系统菜单：JSON 解析与画面叠加
 │
 │  crank_host.h（C 接口）
 ▼
CrankLab 运行时（runtime/src，静态链接）
 ├─ 游戏线程   Lua 虚拟机 / Unicorn 执行 ARM 代码，按帧运行游戏
 ├─ 音频线程   SDL_mixer 经 dummy 设备混音（按"额度"产出，见第 3 节）
 └─ 图形、声音、文件、精灵、菜单等 SDK 实现
```

线程一共四个：

| 线程 | 谁创建 | 作用 |
|---|---|---|
| 主线程 | RetroArch | 调用 `retro_run`，每次推进游戏一帧 |
| 游戏线程 | 运行时（`crank_start`） | 跑游戏的主循环，每帧结束后等待主线程的下一次步进 |
| 音频线程 | SDL | 把混音结果送进运行时的音频队列，受额度控制 |
| 卡顿检测线程 | 核心 | 每 25 ms 检查一次 `retro_run` 是否停了，用来触发暂停回调 |

### 与 RetroArch 隔离

运行时依赖 SDL2 和 SDL_mixer，而 RetroArch 自己也可能用 SDL 做视频、音频或输入。运行时在退出时会调用 `SDL_Quit()`，如果和 RetroArch 共用同一份 SDL，会把前端的子系统一起关掉。为此：

- SDL、SDL_mixer、zlib、Lua、Unicorn 全部**静态链接**进核心。
- 用版本脚本（`link.T`）只导出 `retro_*`，其余符号都设为本地，再加 `-Bsymbolic`。
- 运行时只使用 SDL 的计时、线程和 dummy 音频，不创建窗口，也没有真正的音频设备。

构建后可以这样检查：

```sh
nm -D --defined-only dist/x86_64/cranklab_libretro.so | grep -v ' retro_'   # 应该没有输出
```

### 源码文件

| 文件 | 内容 |
|---|---|
| `src/cranklab_libretro.c` | libretro 入口函数、选项、输入、摇柄、画面与音频输出、卡顿检测、菜单的状态机 |
| `src/cranklab_content.c/.h` | 内容路径解析、`mkdir -p`、ZIP 解压与安全检查、`.pdxl` 解析 |
| `src/cranklab_menu.c/.h` | 解析运行时给出的菜单 JSON，绘制菜单（ASCII 用内置 5×7 字体放大两倍，其余字符用可选的中文字体） |
| `include/libretro.h` | libretro API 头文件，MIT 许可，来自 RetroArch 项目 |
| `link.T` | 链接器版本脚本 |
| `build.sh`、`CMakeLists.txt`、`cmake/` | 构建 |
| `tests/harness.c`、`tests/run-tests.sh` | 测试前端与回归测试 |

## 3. 一帧是怎么跑的：同步模型

### 问题

运行时原本的游戏线程按真实时间自己跑：循环末尾 `SDL_Delay` 补足 33 ms，音频线程也按真实时间"假装播放"。放进 RetroArch 之后，这会带来三个问题：RetroArch 暂停或打开菜单时游戏照常往前跑；快进时游戏不变快；画面、声音、游戏逻辑三者的节奏互相漂移。

### 做法：锁步（lockstep）

核心打开运行时的锁步模式（`crank_set_lockstep(1)`，必须在 `crank_start` 之前）。之后：

1. **每次 `retro_run` 对应游戏的一帧。** 游戏线程画完一帧、发布画面后，停下等待。`retro_run` 里调用 `crank_step()` 放行一帧，并等游戏线程报告这一帧完成（最长等 2 秒）。
2. **游戏时间按帧累计。** 运行时所有的时间查询（`playdate.getCurrentTimeMilliseconds`、`playdate.wait`、定时器、声音播放位置等）都经过 `pd_time_milliseconds()`。锁步模式下它返回一个虚拟时钟，每步前进 `1 / 刷新率` 秒。快进时游戏时间跟着变快，暂停时不动。
3. **音频按额度产出。** 核心改动的 SDL 里，dummy 音频设备只在拿到额度时才混音。每步给 `44100 / 刷新率` 个采样帧的额度（带小数累加，长期精确）；开头预给 2048 个采样帧，避免前端欠载。
4. **核心每次 `retro_run` 向前端交出恰好一帧的音频。** 核心里有个 16384 帧的环形缓冲，运行时产出多了就丢旧的，不够就补静音。

结果：RetroArch 的快进、慢放、暂停、逐帧都会让游戏按同样比例变化，不需要额外处理。

### 刷新率

游戏可以通过 `playdate.display.setRefreshRate` 改帧率。核心每次 `retro_run` 都读一次 `crank_refresh_rate()`，变化超过 0.25 fps（并且在 1 到 60 之间）时，调用 `RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO` 通知前端，音频的每帧采样数也随之变化。刷新率为 0（手动更新模式）时保持上一个帧率。

### 卡顿检测与暂停回调

锁步已经让游戏在 RetroArch 暂停时自然停住，但 Playdate 游戏常常依赖 `gameWillPause` / `gameWillResume` 这两个回调来暂停音乐或保存状态。所以核心保留了一个检测线程：如果 `retro_run` 超过 150 ms 没被调用，就让运行时进入"暂停"（触发 `gameWillPause`）；下次 `retro_run` 到来时恢复（触发 `gameWillResume`）。

副作用：RetroArch 以很慢的慢放速度运行（每帧间隔超过 150 ms）时，会误触发暂停回调。

### 会话启动与失败

`retro_load_game` 调用 `crank_start` 后最多等 30 秒，直到运行时状态变为"运行中"。如果游戏初始化失败（比如 `pdex.bin` 调用了没实现的 C API），状态变为"失败"，核心把运行时的错误信息写进日志，并让加载失败。游戏运行中途失败时，`retro_run` 会记录错误并请求 RetroArch 关闭内容。

## 4. 构建

### 依赖

| 用途 | 需要 |
|---|---|
| 所有构建 | cmake 3.20+、ninja、python3、git |
| 本机构建（`native`） | gcc / g++ 或 clang |
| 交叉构建 | `pip install ziglang`（zig 作为 C/C++ 编译器），不需要各架构的 binutils |

首次构建前要拉取 Unicorn 子模块：`git submodule update --init`。SDL2 2.30.12、SDL_mixer 2.8.1、zlib、Lua 的源码已经在仓库里（`runtime/deps/`、`runtime/vendor/`），不依赖系统里安装的 SDL。

Windows / WSL 构建需要注意换行：主仓库已通过 `.gitattributes` 为 shell 脚本和无扩展名的 `cmake/zig-*` 包装器指定 LF；Unicorn 子模块不会继承主仓库的属性。建议在 WSL 中克隆。若从 Windows 导出子模块源码，使用 `git -c core.autocrlf=false -c core.eol=lf archive ...` 保留 LF。遇到 `/bin/sh^M`、`configure: not found` 或配置文件缺失时，先检查依赖脚本的换行。CMake 会在 Unicorn 没有生成有效的 ARM 配置时立即停止。

### 命令

```sh
./build.sh native                              # 用本机编译器，产物在 dist/native/
ZIG="python3 -m ziglang" ./build.sh x86_64     # glibc 2.17 兼容的 x86_64
ZIG="python3 -m ziglang" ./build.sh aarch64    # ArkOS / EmuELEC 等
ZIG="python3 -m ziglang" ./build.sh armhf      # 32 位 ARM
```

环境变量：

| 变量 | 作用 | 默认 |
|---|---|---|
| `ZIG` | zig 的调用方式 | `zig` |
| `BUILD_ROOT` | 中间文件目录 | `/tmp/cranklab-libretro` |
| `JOBS` | 并行编译数 | CPU 核数 |

`dist/<目标>/` 里会有：

```
cranklab_libretro.so
cranklab_libretro.info
system/cranklab/fonts/cranklab-zh.pft      菜单用的中文字体（可选）
system/cranklab/fonts/PROVENANCE.txt       字体来源说明
```

产物大小约 3.3–3.9 MB，在链接时就已经去掉了调试信息。

### 为什么用 zig 交叉编译

直接用发行版的交叉编译器（例如 Ubuntu 22.04 的 `aarch64-linux-gnu-gcc`）编出来的 `.so` 会引用 glibc 2.33、2.34 的符号（`dlopen`、`pthread_*`、`stat` 在 2.34 前后换了版本），在 glibc 较旧的掌机系统上加载失败。zig 的目标写成 `aarch64-linux-gnu.2.17` 就把符号版本固定在 2.17。可以用下面的命令确认：

```sh
objdump -T dist/aarch64/cranklab_libretro.so | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1   # 应为 GLIBC_2.17
```

仓库里还有 `cmake/aarch64-linux-gnu.cmake`，是用发行版交叉编译器的工具链文件，只适合你清楚目标系统 glibc 版本时使用。

### 构建的关键点

- Unicorn 只编 ARM 客户端（`UNICORN_ARCH=arm`）。它的 CRC 函数通过 `runtime/compat/unicorn_crc.h` 改名，避免和 zlib 的 `crc32` 冲突。
- SDL 关掉了所有真实的视频、音频后端（X11、Wayland、KMS/DRM、ALSA、PulseAudio 等），只保留 dummy 驱动，所以不会因缺少这些开发库而失败。
- `CRANK_LOCKSTEP_AUDIO` 同时定义给 SDL 和核心，打开 SDL 里的额度控制代码。
- 链接选项：`-Wl,--version-script=link.T`、`-Wl,-Bsymbolic`、`-Wl,--gc-sections`、Release 下 `-Wl,--strip-all`。
- zig 的链接器不支持 `--exclude-libs`，也不接受 `-E` 配合带 glibc 版本的目标。`cmake/zig-cc` 和 `cmake/zig-c++` 对后者做了处理（预处理时去掉版本后缀），前者靠版本脚本达到同样的隐藏效果。

## 5. 安装

### 桌面 RetroArch

```sh
cp dist/native/cranklab_libretro.so ~/.config/retroarch/cores/
cp dist/native/cranklab_libretro.info ~/.config/retroarch/cores/    # 或你的 info 目录
mkdir -p ~/.config/retroarch/system/cranklab/fonts
cp dist/native/system/cranklab/fonts/cranklab-zh.pft ~/.config/retroarch/system/cranklab/fonts/
```

命令行直接加载时不需要 `.info` 文件：`retroarch -L cranklab_libretro.so 游戏.zip`。

### 部署到 EmuELEC / ArkOS 一类掌机系统

> 这一节**没有在真机上验证过**。目录和启动方式随系统与版本而异，下面凡是拿不准的位置，都给了在设备上查证的命令，请以设备上查到的为准。

#### 一条命令：`deploy.sh`

```sh
ZIG="python3 -m ziglang" ./deploy.sh root@<设备IP>
```

脚本通过 SSH 完成这些事：

1. 检测设备架构：看 `uname -m`，64 位 ARM 上再读 `retroarch` 的 ELF 位数（1 为 32 位，2 为 64 位），所以 64 位内核加 32 位用户空间的系统也能选对（得到 `armhf`）。
2. 缺少对应的产物就调用 `build.sh` 构建。
3. 读设备上的 `retroarch.cfg`，取出 `libretro_directory`、`libretro_info_path`、`system_directory`、`savefile_directory`。以 `:` 开头或为 `default` 的值是相对路径，会被忽略。
4. 把核心和 info 文件放到 `--dest`（默认 `/storage/cranklab`），把中文字体放进 system 目录的 `cranklab/fonts/`。拷贝走 `ssh ... cat >`，设备上不需要 `scp` 或 sftp，先写 `.part` 再改名，最后核对 sha256。
5. 打印手动启动的命令。

常用参数：

| 参数 | 作用 |
|---|---|
| `--diagnose` | 只检查设备，什么都不拷。出问题时把输出发给开发者 |
| `--dry-run` | 打印计划，不触碰设备 |
| `--arch aarch64\|armhf\|x86_64` | 不自动检测，直接指定 |
| `--dest DIR` | 核心放哪里（要在可写的位置） |
| `--system-dir DIR` / `--cfg FILE` | 自动读取失败时手动指定 |
| `--game FILE` `--rom-dir DIR` | 顺便把一个游戏传到设备上 |
| `--install-core` | 另外把核心放进 RetroArch 自己的 cores 目录（仅当它可写） |
| `--no-build` | 不构建，缺产物就报错 |

环境变量 `DEPLOY_SSH` 可以换掉 ssh 命令，比如 `DEPLOY_SSH="ssh -p 2222 -i key"`。脚本在本机用一个假的 ssh 做过完整测试（检测、解析目录、拷贝、校验、错误处理），但没有连过真实设备。

#### 手动步骤

不想用脚本，或者要排查问题时，按下面做。

**1. 确认架构**

```sh
uname -m                      # aarch64 → 构建 aarch64；armv7l / armv8l → 构建 armhf
file /usr/bin/retroarch       # 没有 file 命令就用 deploy.sh --diagnose
```

核心必须和 `retroarch` 同一位数。

**2. 构建**：`ZIG="python3 -m ziglang" ./build.sh aarch64`（或 `armhf`）。

**3. 摸清设备上的目录**

```sh
CFG=/storage/.config/retroarch/retroarch.cfg     # 不存在就 find / -name retroarch.cfg
grep -E '^(libretro_directory|libretro_info_path|system_directory|savefile_directory)' $CFG
```

如果 `libretro_directory` 在只读的系统分区上，就不要往里放，改用完整路径指定核心。

**4. 拷贝**：核心和 info 放到 `/storage/cranklab/` 之类的可写目录；`cranklab-zh.pft` 放进 `<system_directory>/cranklab/fonts/`；再放一个测试游戏（比如 `native-test-games/Life.zip`）。

**5. 先绕过 EmulationStation 手动启动**，确认核心本身能跑：

```sh
systemctl list-units | grep -i emu          # 找到 EmulationStation 的服务名
systemctl stop <服务名>                      # 释放屏幕
/usr/bin/retroarch -L /storage/cranklab/cranklab_libretro.so /storage/roms/playdate/Life.zip --verbose 2>&1 | tee /tmp/cranklab.log
grep CrankLab /tmp/cranklab.log
systemctl start <服务名>                     # 验完恢复
```

RetroArch 起不来是因为配置、手柄或显示参数时，对照系统自带的启动脚本（比如 `/usr/bin/emuelecRunEmu.sh`）看它是怎么调用 RetroArch 的，补上相同的参数。再按第 11 节的手测清单检查画面、声音、按键、摇柄、Start 菜单、快进、暂停。

**6. 接入 EmulationStation**：手动启动成功后再做。

1. 找系统定义文件：`find / -name 'es_systems*.cfg' 2>/dev/null`。
2. 复制一个现成的 libretro 系统（比如 GB 或 NES）的条目，改成 Playdate：ROM 目录用 `playdate/`，扩展名填 `.pdxl .pdz .zip`（大小写都写），启动命令照抄，核心换成 `cranklab_libretro`。
3. 系统自带的启动脚本通常按核心名在固定目录里找 `.so`。核心放不进那个目录时，在启动命令里改用完整路径，或为每个游戏写一个只含第 5 步那条 `retroarch -L ...` 命令的 `.sh` 脚本，放进能显示脚本的系统目录（比如 Ports）。后一种粗糙，但最不依赖前端内部约定。
4. 游戏是文件夹时，为每个游戏放一个 `.pdxl` 指针文件，或直接用 `.zip`。

#### 常见问题

| 现象 | 处理 |
|---|---|
| 提示找不到核心或加载失败 | 位数不一致（32 / 64 位）或路径不对。核对 `.so` 与 `retroarch` 是否同架构 |
| 核心目录是只读 | 用完整路径指定核心，不要往只读目录里放 |
| 没有声音或严重爆音 | 先在 RetroArch 里加大音频延迟，再看设备算力 |
| 带 ARM 代码（`pdex.bin`）的游戏卡 | 低端 SoC 上模拟 CPU 吃力，Lua 游戏通常轻得多 |
| 菜单里中文是方框 | 字体没放进 `<system_directory>/cranklab/fonts/` |
| 按 Start 反而退出 | 有些配置把 Start+Select 当退出热键。把核心选项里的菜单键改成 Select，或调整热键 |
| 通过 ES 启动时看日志 | EmuELEC 的日志通常在 `/emuelec/logs/` 一类位置，以实际镜像为准 |

向开发者反馈时请附上：`./deploy.sh --diagnose root@<设备IP>` 的输出、RetroArch 版本、第 5 步的日志、`es_systems.cfg` 里一个现成 libretro 系统的条目。

## 6. 加载内容

`cranklab_resolve_content` 按下面的顺序判断 RetroArch 传来的路径：

1. **是目录**：只要里面有 `pdxinfo`、`main.pdz`、`pdex.bin` 或 `main.lua` 就当作游戏文件夹。
2. **扩展名 `.zip`**：解压后使用。
3. **扩展名 `.pdxl`**：读取第一行作为游戏文件夹路径。
4. **其他文件**：看它所在的文件夹是不是游戏文件夹（`main.pdz`、`pdxinfo`、`pdex.bin` 都属于这一类）。

### ZIP

- 解压到 `<系统目录>/cranklab/cache/<文件名>-<8 位哈希>/`。哈希由路径、文件大小和修改时间算出，所以压缩包变了会重新解压，没变就复用。目录里有 `.cranklab_extracted` 标记文件。
- 压缩包里可以直接放游戏文件，也可以放在一层文件夹里（通常是 `游戏.pdx/`），核心最多往下找两层，找到含有 `pdxinfo` 等文件的目录。
- 安全检查与限制：

| 项目 | 处理 |
|---|---|
| 含 `..`、以 `/` 开头、含盘符（`C:`）的条目 | 跳过，不写出 |
| `__MACOSX/` 下的条目 | 跳过 |
| 加密条目 | 整个压缩包拒绝 |
| ZIP64、条目数超过 20000 | 拒绝 |
| 压缩包超过 1 GiB，或单个条目超过 512 MiB | 拒绝 |
| CRC 或大小校验不一致 | 拒绝 |
| 压缩方式 | 只支持存储（0）和 deflate（8） |

### `.pdxl` 指针文件

一个纯文本文件，第一行是游戏文件夹的路径：

```
/roms/playdate/Hello-World.pdx
```

相对路径从 `.pdxl` 所在目录算起：

```
Hello-World.pdx
```

目标必须是含有 `pdxinfo`、`main.pdz`、`pdex.bin` 或 `main.lua` 的目录，否则加载失败。

## 7. 控制：按键、摇柄、菜单

### 按键

核心每次 `retro_run` 轮询一次手柄 0，转成 Playdate 的 6 位按键掩码交给 `crank_set_buttons`。

| RetroPad | Playdate |
|---|---|
| 十字键 左 / 右 / 上 / 下 | 左 / 右 / 上 / 下 |
| B（下方键） | B |
| A（右方键） | A |
| Start | 系统菜单 |
| Select | 默认不用；菜单键选项设为 Select 时用作系统菜单 |
| L / R | 摇柄逆时针 / 顺时针 |
| L3 | 摇柄收起 / 拉出 |
| 右摇杆 | 摇柄（按角度） |
| X、Y、L2、R2、R3 | 未使用 |

Playdate 的 B 在左、A 在右，核心直接对应 RetroPad 的 B 和 A。掌机的按键布局不一样时，可以用"互换 A 和 B"选项。

### 摇柄

运行时只关心两个量：**每帧的转动增量（度）**和**摇柄是否收起**。核心每帧调用一次 `crank_add_crank(增量, 是否收起)`，三个来源的增量相加：

- **L / R**：按住时每帧加减一个固定角度，默认 6°，30 fps 时约每秒 180°。
- **右摇杆**：
  - 摇杆偏离中心的距离超过量程的约一半（偏移向量长度大于 16000，满量程 32767）才算有效。
  - 有效时用 `atan2(y, x)` 求当前角度。屏幕坐标 Y 向下，所以角度顺时针增加，对应顺时针转摇柄。
  - 增量是这一帧与上一帧的角度差，规整到 -180° 到 180°，越过 ±180° 时不会跳变。
  - 刚进入有效区的那一帧只记录起点，增量为 0；松开后再抓，也从新的起点算，不会产生一次大的跳变。
- **收起状态**：
  - 游戏启动时摇柄是收起的，和真机一致。很多游戏据此提示"请拉出摇柄"。
  - 收起时不传增量。
  - 只要有 L、R 或摇杆产生转动，就自动拉出。
  - L3 在收起和拉出之间切换。因此，单独按一次 L3 会拉出摇柄但不转动；转动后再按 L3 会重新收起。

游戏里 `getCrankPosition()` 返回的是累计角度（从 0 开始，对 360 取模），不对应摇杆的绝对方向。

菜单打开期间，所有摇柄输入对游戏无效。

### 系统菜单

Playdate 的系统菜单由游戏自己添加菜单项（`playdate.getSystemMenu():addMenuItem` 等），有三类：按钮、勾选、多选项。游戏还可以用 `playdate.setMenuImage` 设置菜单背景图。核心把这些画成叠加层。

打开：按 Start（或选项里设的菜单键）。核心会同时让运行时进入暂停（`gameWillPause`）并打开菜单，等运行时确认进入暂停状态（最多 500 ms）。

画面：

- 左半边显示游戏设置的菜单背景图（按它给的横向偏移左移，0 到 200 像素）。没有设置就显示冻结的最后一帧。
- 右半边（x 从 200 开始）是面板：标题 `MENU`，游戏的菜单项，最后固定一行 `Resume`。选中行反白。
- 勾选项右侧显示 `[ ]` / `[x]`，多选项显示 `< 当前值 >`。标题太长会用 `..` 截断。
- 菜单最多解析 8 项（Playdate 本身限制为 3 项）。

操作：

| 按键 | 作用 |
|---|---|
| 上 / 下 | 移动选择（首尾循环） |
| 左 / 右 | 勾选项取反；多选项切到上一个 / 下一个值（循环） |
| A | 按钮项：执行其回调并关闭菜单；勾选项取反；多选项下一个值；Resume：关闭 |
| B 或 菜单键 | 关闭菜单 |

勾选项和多选项的改动在**关闭菜单时**、`gameWillResume` 之前才交给游戏（由运行时在游戏线程上依次调用回调），和真机顺序一致。按钮项的回调则在你按下 A 后马上执行，然后菜单关闭。菜单打开期间 RetroArch 的卡顿不会重复触发暂停回调。

字体：ASCII 字符用内置 5×7 点阵字体放大两倍绘制。其他字符（比如中文）依赖可选的字体文件 `<系统目录>/cranklab/fonts/cranklab-zh.pft`；没有这个文件时，那些字符显示成空方框。

## 8. 核心选项

| 选项键 | 说明 | 取值（默认在前） |
|---|---|---|
| `cranklab_palette` | 显示调色板。`lcd` 约为黑 `#312F28`、白 `#B1AFA8`（Playdate 液晶的灰绿色） | `lcd` / `black_white` / `inverted` |
| `cranklab_crank_speed` | L / R 键转摇柄的速度，单位度每帧 | `6` / `3` / `9` / `12` / `18` / `24` |
| `cranklab_analog_crank` | 右摇杆是否转摇柄 | `enabled` / `disabled` |
| `cranklab_swap_ab` | 互换 A 和 B | `disabled` / `enabled` |
| `cranklab_menu_button` | 打开系统菜单的键 | `start` / `select` |

选项用 libretro 最基础的变量接口（`SET_VARIABLES`）注册，老版本的 RetroArch 也能识别。选项改动在下一次 `retro_run` 生效。

## 9. 存档与文件布局

| 位置 | 内容 |
|---|---|
| `<存档目录>/cranklab/<bundleID>/` | 游戏自己写的文件（`playdate.datastore`、`playdate.file`）。`bundleID` 取自游戏的 `pdxinfo`；没有时取游戏文件夹名，其中空格和斜杠换成下划线 |
| `<系统目录>/cranklab/cache/` | ZIP 的解压缓存，可以随时删除，下次加载会重新解压 |
| `<系统目录>/cranklab/fonts/cranklab-zh.pft` | 菜单的中文字体（可选） |

存档目录来自 `RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY`，拿不到时退回系统目录，再退回 `/tmp`。

**关闭内容时**，核心调用 `crank_stop`，运行时会触发 `gameWillTerminate`，游戏可以在回调里写存档。所以请通过正常的关闭内容或退出来结束，不要直接杀进程，否则只在退出时存档的游戏会丢进度。

即时存档、倒带、runahead 都不可用：Lua 虚拟机和 Unicorn 的状态没有可序列化的实现，`retro_serialize_size` 返回 0。

## 10. 兼容性与限制

- **Lua 游戏**：由运行时的 Lua 5.4（打了 Playdate 字节码补丁）和自己实现的 SDK 运行。完整的游戏兼容性需要逐个验证，项目本身也这样声明。
- **ARM 设备代码（`pdex.bin`）**：用 Unicorn 模拟 Cortex-M7。SDK 的 C API 表有 590 个槽位，只实现了其中一部分；调用没实现的 API 时，游戏会以带完整 API 名的错误终止，核心让加载失败并写日志。失控的 C 循环有看门狗，会被终止而不会卡住前端。
- **没有联网**：`playdate.network` 在这个核心里总是返回连接失败。PDGames 启动器没有接入，选游戏用 RetroArch 的播放列表或 EmulationStation 就行。
- **加速度计**没有映射。
- **`system.delay`**（设备代码里的阻塞延时）仍按真实时间等待，不随快进变快。
- **刷新率为 0（手动更新）**的游戏：步进时钟按 30 fps 走。
- **卡顿检测**在 RetroArch 极慢的慢放下可能误触发暂停回调（见第 3 节）。
- **只做了 Linux**。Windows、macOS 和 Android 的 RetroArch 需要另外的构建（Android 已有现成的运行时构建，但没有接 libretro 入口）。
- **Playdate 的 Wi-Fi、同步、系统设置、官方 CoreLibs** 都不在范围内；运行时不带官方固件。

## 11. 测试

### 自动化回归测试

```sh
tests/run-tests.sh dist/native/cranklab_libretro.so
```

脚本会：

1. 编译测试前端 `tests/harness.c`（一个最小的 libretro 前端，不需要 RetroArch）。
2. 生成测试夹具：Lua 小游戏、生命周期游戏、系统菜单游戏、摇柄游戏、时钟游戏，以及几个恶意或损坏的压缩包。
3. 逐项运行并打印 `PASS` / `FAIL`，最后输出 `ALL PASSED` 或失败数量；退出码等于失败数量。

目前共 57 项，覆盖：

| 分组 | 检查的内容 |
|---|---|
| Lua 游戏 | 能加载；每次 `retro_run` 一帧画面和 1470 个音频采样帧；音频有真实声音；A 键和摇柄让画面变化；调色板选项被接受 |
| 压缩包与 ARM 代码 | 7 个测试游戏能运行；C 代码的音频到达前端；失控的 C 循环、未实现的 C API 干净地失败，且日志里有 API 名 |
| 生命周期与存档 | 前端卡顿触发暂停与恢复回调；退出时 `gameWillTerminate` 写出了存档；同一进程里 5 次加载卸载加 reset |
| 系统菜单 | 勾选与多选的改动按顺序、在暂停与恢复之间生效；按钮项执行回调后恢复；菜单打开时卡顿不重复暂停；Start 关闭菜单；叠加层确实画出来；没有菜单项的游戏也能开关菜单；菜单打开时 reset；关闭后输入恢复；Select 可作菜单键 |
| 摇柄 | 初始收起；L/R 方向与速度；速度选项；L3 的各种切换；右摇杆顺时针、逆时针、越过 ±180°、死区、关闭选项、松开再抓不跳变、与按键叠加；菜单期间无效 |
| 锁步 | 600 帧不限速跑完只用约 0.3 秒；游戏更新次数等于步数；游戏时钟按帧而不是墙钟；1.5 秒卡顿不让游戏多跑帧；刷新率变化通知前端并改变时钟；正常速度下持续音没有欠载；不限速运行不会被音频卡住 |
| 压缩包安全 | 路径穿越条目被跳过；没有文件逃出缓存目录；非压缩包、不存在的文件被拒绝 |

### 测试前端

`tests/harness.c` 可以单独使用：

```sh
gcc -O1 -Iinclude tests/harness.c -o harness -ldl -lm
./harness CORE.so 内容路径 [选项]
```

| 选项 | 作用 |
|---|---|
| `--frames N` | 运行 N 次 `retro_run`（默认 60） |
| `--realtime` | 按核心报告的帧率真实休眠，用于测试音频与卡顿 |
| `--out DIR` | 系统目录与存档目录的根（默认 `/tmp/cranklab-harness`） |
| `--opt key=value` | 设置核心选项，可重复 |
| `--hold 帧:数量:按键` | 从某一帧起按住按键若干帧。按键名：left right up down b a l r l3 start select |
| `--spin 帧:数量:幅度:每帧度数[:起始角]` | 右摇杆以匀速转动，正数为顺时针 |
| `--stall 帧:毫秒` | 在某一帧前休眠，模拟前端卡顿 |
| `--cycles N` / `--reset-at 帧` | 反复加载卸载 N 次 / 在某一帧调用 `retro_reset` |
| `--ppm 文件` | 把最后一帧保存为 PPM 图片 |

最后打印一行 `RESULT ...`，包含视频帧数、画面变化次数、音频采样帧数与峰值、静音批次、最终帧率等。

### 桌面 RetroArch 手测

自动化测试用的是简化前端，真实 RetroArch 的视频驱动、音频同步、热键等它覆盖不到，所以还需要手测：

| 项目 | 看什么 |
|---|---|
| 画面 | 灰绿色液晶画面，比例 5:3，没有拉伸或花屏 |
| 声音 | Audio-C 有持续音，无爆音、断续 |
| 按键 | 十字键、A/B 有响应 |
| 摇柄 | L/R、右摇杆、L3 都生效 |
| 系统菜单 | Start 打开，上下选择，B 或 Start 关闭 |
| 快进 | 游戏和音乐一起变快 |
| 暂停 | 游戏冻结，恢复后接着走 |
| 重置与换游戏 | 不卡死、不崩溃 |
| 存档 | 退出再进入，进度还在 |

### 验证范围

| 已验证 | 未验证 |
|---|---|
| x86_64 Linux 上用测试前端的全部项目 | 真正的 RetroArch |
| zig 交叉工具链编出的 x86_64 产物同样通过 | aarch64、armhf 产物的运行（只确认了能编、glibc 2.17、无多余导出） |
| 摇柄逻辑与方向 | 摇杆死区、转速的实际手感 |
| 2026-10-09：Windows 运行时重新编译，输入/音频、生命周期、SDK 菜单回归通过 | 本次构建没有新增 Android 真机操作回归 |
| 2026-10-09：Android ARM64 运行时与 UE 应用重新构建，APK 签名、在线翻译打包、原生库 16 KiB 对齐检查通过 | |
| | ArkOS、EmuELEC 上的目录、EmulationStation 配置 |
| | 菜单在小屏幕上的可读性 |

## 12. 排错

核心的日志带 `[CrankLab]` 前缀。运行时自己的输出写到标准错误。

| 现象 | 可能原因与处理 |
|---|---|
| 加载失败，日志有 `No Playdate game files next to ...` | 内容路径不是游戏文件夹里的文件。改用 `.zip`、`.pdxl`，或指向游戏文件夹里的 `main.pdz` / `pdxinfo` |
| `Not a ZIP archive` / `Corrupt or unsupported ZIP entry` | 压缩包损坏、加密、ZIP64，或用了 deflate 以外的压缩方式。用 `unzip -t` 检查 |
| `Game failed to start: Unimplemented C API: xxx` | 这个 `pdex.bin` 用到了还没实现的 SDK C API，这是运行时的限制 |
| `Game failed to start: Native execution exceeded watchdog time budget` | C 代码里有长时间不返回的循环，被看门狗终止 |
| 加载超时（30 秒）无报错 | 游戏的初始化很重。看运行时的标准错误输出有没有卡在哪一步 |
| 动态库加载失败，提示 `GLIBC_2.xx not found` | 用了本机编译器而不是 zig 交叉构建，或者目标系统的 glibc 太旧（低于 2.17） |
| 动态库加载失败，提示架构不对 | `.so` 的架构和设备不符，用 `file cranklab_libretro.so` 检查 |
| 菜单里的中文显示成方框 | 没有放字体文件，或放错了目录。检查 `<系统目录>/cranklab/fonts/cranklab-zh.pft` |
| 声音爆音或断续 | 设备算力不够，或前端音频缓冲太小。先在 RetroArch 里加大音频延迟 |
| 快进时没声音 | 正常现象，前端在快进时会丢弃音频 |
| 菜单一直打不开 | 看菜单键选项（Start 或 Select）；掌机上 Start+Select 常被前端当作退出热键 |
| 退出后存档没写 | 游戏只在 `gameWillTerminate` 里存档，而你强制结束了进程。请正常关闭内容 |
| 游戏在 RetroArch 慢放下突然收到暂停回调 | 帧间隔超过 150 ms 触发了卡顿检测，属于已知限制 |

## 13. 对运行时的改动

核心本身都在 `libretro/` 里，但为了接入，`runtime/` 里有一些改动。它们都有开关或只增加接口，默认行为不变：

| 文件 | 改动 |
|---|---|
| `runtime/include/crank_host.h` | 新增 `crank_set_lockstep`、`crank_step`、`crank_refresh_rate` |
| `runtime/src/pd_host.c` | 锁步的信号量、虚拟时钟、步进接口；音频额度的接入 |
| `runtime/src/pd_main.c` | 游戏循环末尾：锁步模式下等待步进，否则照旧 `SDL_Delay` |
| `runtime/src/pd_graphics.c`、`pd_runtime.h` | 新增 `pd_font_glyph_bitmap`、`pd_font_release`、`pd_load_pft_detached`，让核心能用 PFT 字体画菜单 |
| `runtime/src/pd_http_stub.c` | 新增，没有网络传输层的宿主用它（总是返回连接失败） |
| `runtime/CMakeLists.txt` | 把 `pd_http_stub.c` 排除出 Windows / Android 构建，避免重复定义 |
| `runtime/deps/sdl-source/src/audio/SDL_audio.c` | `CRANK_LOCKSTEP_AUDIO` 宏保护的额度控制代码，只在 libretro 构建里打开 |
| `third-party.json` | 记录了上面的 SDL 改动 |

2026-10-09 已重新编译 Windows 和 Android ARM64 运行时，并完成 Android UE 应用构建和 APK 校验。Windows 的输入/音频、生命周期及 SDK 菜单探针通过。Android 保持默认的实时模式，锁步音频只在 libretro 构建中启用；本次构建没有新增 Android 真机操作回归。

## 14. 许可与第三方

| 组件 | 许可 / 说明 |
|---|---|
| Unicorn 2.1.4 | GPLv2。核心静态链接了它，所以分发核心的二进制时需要按 GPLv2 兼容的方式发布。`cranklab_libretro.info` 里的 `license` 也据此写了 GPLv2 |
| SDL 2.30.12、SDL_mixer 2.8.1、zlib | zlib 许可 |
| Lua | MIT |
| `libretro.h` | MIT |
| CrankLab 运行时 | 来源 pd4rk（MIT），详见 `third-party.json` |
| 菜单中文字体 `cranklab-zh.pft` | 从 pocket-reader 转手的 Source Han Sans 像素图集编译而来，来源见 `PROVENANCE.txt`。**条款没有核实**，分发前请自己确认 |

核心不包含 Playdate 官方固件、系统字体或 CoreLibs。游戏本身有各自的版权，请只运行你有权使用的游戏。

## 15. 后续工作

- 真机验证：aarch64 / armhf 产物、ArkOS 与 EmuELEC 的安装与启动配置、音频缓冲、性能。
- 摇杆死区做成核心选项；加鼠标滚轮、L2/R2 模拟量转摇柄。
- 把测试前端改成可以在开发机上交叉编译后拷到设备运行，掌机上不需要 gcc。
- 加速度计映射到 `RETRO_SENSOR_ACCELEROMETER`。
- 联网：接入 libretro-common 的 HTTP 或 libcurl，让 `playdate.network` 可用。
- 其他平台：Windows、Android 的 RetroArch 核心。
- 在 libretro 的构建系统（buildbot）里发布前，确认 GPLv2 与字体许可。
