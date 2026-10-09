# Playdate for Switch — Lua-only NRO 0.1.0

适用于可运行自制程序的初代 Switch / Lite / OLED。原生 SDL2 + libnx 前端，
复用项目的 Playdate Lua 运行时；不依赖 UE5、不执行 C/混合游戏，不提供翻译。
当前只交付 NRO，无需 prod.keys。游戏 API 兼容性仍需逐个游戏验证。

## 安装和操作

将 `Playdate.nro` 放到 SD 卡的 `/switch/playdate/Playdate.nro`，从 Homebrew Menu 启动。
PDGames、图标、公开来源目录和 HTTPS CA 已嵌入 NRO，不用额外复制 RomFS。
首次启动会创建 `/switch/playdate/games`、`saves` 和设置文件。

| 按键 | 功能 |
|---|---|
| 方向键、A / B | 游戏操作；系统界面选择 / 返回 |
| 右摇杆转圈 | 模拟摇柄，中心死区避免抖动 |
| 按住 ZR / ZL | 持续顺时针 / 逆时针摇动 |
| 按下右摇杆 | 摇柄收纳 / 展开 |
| + | 游戏 SDK 菜单；选项有高亮；关闭时应用勾选回调 |
| − | 返回内置 PDGames |
| Y | 本地游戏库 |
| X | 设置：整数倍缩放、震动、音量、摇柄速度 |

本地导入：将完整 Lua 游戏文件夹复制到 `/switch/playdate/games/游戏名.pdx/`，
该文件夹下直接包含 `pdxinfo` 和 `main.pdz` 或 `main.lua`。按 Y 刷新游戏库。
含非空 `pdex.bin` 的游戏会显示不支持，运行前和运行时均会检查。
存档按 bundleID 派生稳定目录，更新/重命名游戏不会因安装目录变化而丢失存档。

PDGames 详情页有 Details 与 Download & Play 入口；已安装的显示 Run game / Run demo。
下载仅使用发布者公开暴露的游戏包，付费游戏可能只有试玩包或发布者页面入口。
网络失败可重试，也可手动复制 PDX；离开原详情或切到其他游戏后，完成的下载不会自动抢占游戏。
ZIP 以流式方式处理：压缩包最多 512 MiB、展开最多 1 GiB、单文件最多 256 MiB。
加密/分卷/ZIP64、目录穿越、链接、重复路径、损坏 CRC、多游戏包及 C 游戏包均被拒绝。

内置 PDGames 是用户提供的 build 18 固定版本。仅该版本省略可选 C 二维码加速模块，
使用其自带 Lua QR 回退；已实际生成并解码二维码。普通游戏不会删除 C 模块后强行运行。

## 构建

Linux / WSL Ubuntu 可直接构建，不运行 Docker。工具链：devkitA64、libnx、Switch
portlibs 的 SDL2、SDL_mixer、SDL_ttf、libcurl、zlib；主机需要 CMake、Python3。
本次验证用官方 `devkitpro/devkita64` SDK 文件快照，摘要见 `tools/sdk-lock.json`。

```sh
export DEVKITPRO=/opt/devkitpro
OUT=/path/to/build-switch bash build.sh switch
python3 tools/verify_artifact.py --build /path/to/build-switch --output /path/to/dist
```

开发用桌面构建：

```sh
# Ubuntu 24.04 dependencies:
# cmake pkg-config g++ libsdl2-dev libsdl2-mixer-dev libsdl2-ttf-dev
# libcurl4-openssl-dev zlib1g-dev openssl fonts-noto-cjk
bash build.sh host
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy \
PLAYDATE_UI_FONT=/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc \
build-host/playdate-switch --root /tmp/playdate-test --library --frames 10 \
  --screenshot /tmp/playdate-library.bmp
```

桌面调试按键：方向键、A/Enter、S(B)、M(菜单)、H(PDGames)、Y(游戏库)、X(设置)、
Q/E(持续摇柄)、D(收纳)，系统返回为 Escape。

`tests/probe_pdgames.py` 用公开详情缓存和桌面 PD_EVAL 导航钩子验证真实详情、图片与 QR 屏幕，
不替换其 bytecode 或 QR 实现；没有把当前网络失败伪装成在线列表测试通过。

## 验证边界

已验证：主机 Lua 帧、C/混合包拒绝、连续游戏切换、SDL 窗口生命周期、暂停恢复、
真实混音 PCM 与总音量静音、datastore 跨重启、SDK 菜单回调、摇柄连续性、显示比例、
UTC 日期换算；本地 TLS 下载/安装/取消/失败/旧会话/离线索引回归；真实 PDGames Lua QR。
已完成 Switch AArch64 交叉编译和 NRO/内嵌资产结构检查。

尚未在 Switch 真机运行：需要验证手柄映射、震动、音频设备、Home/休眠、掌机/底座显示
以及真实网络下载。当前测试网络访问 PDGamez 列表超时，发布者网页解析也可能随网站变更。
主机回归与编译成功不代表所有 Lua 游戏兼容或真机测试通过。

共享运行时的 Switch 适配均由 `__SWITCH__` / `CRANK_EXTERNAL_SDL` 条件控制；
Android/Windows 的 SDL 所有权和原有启动器脚本默认路径保持其原来的构建方式。
