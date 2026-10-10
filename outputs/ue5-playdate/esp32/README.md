# Playdate on ESP32

ESP32 原生 Playdate Lua 运行时，首块开发板支持 **M5Stack M5Dial / StampS3**。
游戏在开发板上执行；电脑只用于编译、烧录和串口调试。

复用项目已有的 Lua 解释器、1 位图形、字体、精灵、几何、输入、文件和 JSON / datastore 实现。
游戏画面仍是 400×240，按比例缩放到圆屏中央的 200×120 区域，完整保留画面。

## 操作

- 游戏库：转动外圈选择游戏，短按屏幕实体按钮启动。
- 游戏中：外圈旋钮对应摇柄，每圈 360°；屏幕下方触控区域提供方向键和 A/B。
- 实体按钮也映射为 A；长按超过 600 ms 后松开，返回游戏库。
- 存档放在 `/saves/<bundleID>`，不同游戏独立保存。

## 当前边界

这是 MCU 端的早期移植。支持普通 Lua PDX，包括 SDK 编译的 `main.pdz`；
带非空 `pdex.bin` 的原生 ARM/C 游戏会明确拒绝。
M5Dial 的运行时实际检测到 **PSRAM 为 0**，可加载游戏的大小受内部 RAM 限制。
Flash 中的 PDZ 按索引加载单个条目，Lua 编译后立即释放源缓冲；
内存不足会显示错误并可返回游戏库，不会把整个游戏包预先解压到 RAM。

目前提供单音蜂鸣器 synth；PCM sampleplayer / fileplayer、网络、系统菜单和翻译尚未移植。
`sample` 类构造器会返回明确的未支持错误，不能把能加载一个游戏等同于所有 SDK API 已支持。

## 构建与烧录

依赖 Python、PlatformIO 6.2 和本地 Playdate SDK。ESP32 平台及 M5Stack 库由 PlatformIO 安装。
Windows 包装脚本默认把工具与构建缓存放在 D 盘，串口默认 COM9，均可通过参数覆盖。

```powershell
python -m venv D:/PlaydateAI/toolchains/esp32-pio
D:/PlaydateAI/toolchains/esp32-pio/Scripts/python.exe -m pip install platformio==6.2.0 pyserial

# 在 esp32 目录执行
./tools/build.ps1 -Action build
```

游戏来源是本地 `.pdx` 目录。先编译附带的普通 Playdate 游戏或准备自己的游戏，
再复制到忽略 Git 的 `data/games/<id>` 目录。已有目标目录不会被自动覆盖。

```powershell
$sdk = 'C:/Users/dodol/Documents/PlaydateSDK'
& "$sdk/bin/pdc.exe" -sdkpath $sdk examples/crank-paddle D:/PlaydateAI/esp32-games/crank-paddle.pdx
python tools/stage_game.py D:/PlaydateAI/esp32-games/crank-paddle.pdx --id crank-paddle
python tools/prepare_corelibs.py --sdk $sdk

./tools/build.ps1 -Action filesystem
./tools/build.ps1 -Action flash -Port COM9
./tools/build.ps1 -Action flash-filesystem -Port COM9
```

`$sdk` 指向你安装的 Playdate SDK。共享 CoreLibs 从本地 SDK 编译，生成的游戏包、
SDK 资源、工具链、固件和存档不会提交到仓库。

首次烧录文件系统会替换该分区，后续再次上传完整文件系统也会替换板上的存档。
固件更新只上传应用程序即可；不需要每次重新上传文件系统。
挂载失败时程序显示错误，**不会自动格式化**。

## 板级扩展

- `boards/m5dial.json`：SoC、CPU / Flash 时钟、Flash 大小、Arduino variant 和上传参数。
- `include/boards/m5dial.h`：屏幕布局、编码器每圈脉冲数及硬件引脚说明。
- `include/board.h`：板级显示、输入和音频接口；共享模拟器不直接访问 M5Stack 类。
- `src/board_m5dial.cpp`：使用官方 M5Dial / M5Unified / M5GFX 驱动的实现。
- `platformio.ini`：选择板卡环境、驱动文件、依赖和 Flash 分区。

增加开发板时，新建 board JSON、配置头和驱动 `.cpp`，再添加 PlatformIO 环境及
对应的 `build_src_filter`。沿用 `micro_runtime` 和共享 runtime；不要在 Lua / 图形核心中写板卡引脚。
带 PSRAM 的板卡需要在自己的环境中启用对应的内存配置；当前没有将其他板卡标记为已验证。

## 验证与调试

串口使用 115200 baud。命令：`list`、`load <id>`、`menu`、`status`、
`buttons <mask>`、`crank <degrees>`、`frame`、`reboot`。
按钮位与 Playdate 一致：左 1、右 2、上 4、下 8、B 16、A 32。
`frame` 回传原始 400×240 1 位帧；`status` 返回实际 Lua / heap 用量、绘制耗时和帧 CRC。

```powershell
D:/PlaydateAI/toolchains/esp32-pio/Scripts/python.exe tools/verify_board.py --port COM9 --music --asteroids
```

验证脚本需要预先烧录 `sdk-crank`、`crank-paddle`，以及可选的 `music-box` / `sdk-asheteroids`。
前两项分别使用官方 SDK 的 `Examples/Single File Examples/crank.lua` 和附带的普通 PDX 源码。
测试将运行真实游戏、核对输入导致的画面变化，并检查十次退出 / 重启后的 heap 回收。
`--asteroids` 还会默认执行 30 秒的持续移动 / 射击检查，可通过 `--asteroids-seconds` 调整时长。
它会使用这些测试游戏的独立存档，不替代实体旋钮、触控和扬声器的人工验收。

当前 COM9 设备已通过 SDK 摇柄示例、Crank Paddle 按键 / 摇柄响应、Asheteroids
30 秒持续移动 / 射击测试，以及十次游戏切换后的内存回收检查。
Music Box 已尝试加载，但当前无 PSRAM 的 M5Dial 在加载 CoreLibs UI 时 RAM 不足，尚不能运行。
