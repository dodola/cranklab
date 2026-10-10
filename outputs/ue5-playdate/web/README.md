# Playdate Web

在浏览器里转动摇柄，玩 Pooh Mini Mahjong 和 Music Box。

- 原有网页的 3D 掌机、材质、按键和可操作摇柄。
- 沉浸式大屏，手机横竖屏布局，大号方向键和 A/B。
- 游戏画面、声音和逻辑由现有 Lua 模拟器核心实际执行。
- 游戏内普通存档自动保存在当前浏览器；支持切换游戏和刷新后恢复。
- 游戏菜单、暂停、静音和浏览器全屏。

## 本地启动

已打包的目录直接运行 `start.cmd`，或在目录里执行：

```sh
node server.cjs
```

然后打开 **http://127.0.0.1:4180/**。需要 Node.js 18 及以上，不能直接双击 HTML 文件。

从源码首次使用时，需要先编译核心和准备自己的两款游戏，见下面的构建步骤。
游戏资源、WebAssembly 编译产物和本地存档不提交到 Git。

## 操作

| 动作 | 操作方式 |
|---|---|
| 方向键 | 方向键 / WASD，或屏幕上的十字键 |
| A / B | Z / X，或点击真实 3D 按钮 / 沉浸模式大按钮 |
| 转动摇柄 | 拖动 3D 摇柄、拖动转盘、滚轮、长按 Q / E |
| 沉浸模式摇柄 | 在游戏屏幕上上下滑动 |
| 收起 / 展开摇柄 | C，或转盘下面的按钮 |
| 调整视角 | 在 3D 掌机背景/机身拖动 |
| 切换视图 | 工具栏按钮，或双击显示屏 |
| 游戏菜单 | M，或工具栏/掌机菜单按钮 |

切换视图会保留正在运行的游戏。游戏库与菜单打开时暂停游戏，返回后继续。
首次启动游戏的点击会启用声音；网页失焦或退到后台会暂停并释放按键。
存档在当前站点的 IndexedDB 中，清理浏览器站点数据会删除它们。

## 从源码构建

先安装并激活 **Emscripten 4.0.23**，另需 CMake、Ninja 和 Python 3。
Windows 示例（在本目录运行）：

```powershell
.\tools\build.ps1 -EmsdkRoot D:/path/to/emsdk -BuildDirectory D:/path/to/web-build
python tools/prepare-games.py --mahjong C:/path/to/PoohMiniMahjong.pdx --music C:/path/to/music_box_1.1.4.pdx
npm start
```

其他支持 Emscripten 的构建环境可使用：

```sh
emcmake cmake -S . -B /path/to/build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build /path/to/build --parallel 8
```

输出位于 `public/core`。`npm run package` 生成独立 `dist` 目录，包含静态页面、原有模型、核心和本地准备的游戏。
只发布静态站点时使用 `dist/public` 为站点根目录，并把 `dist/model` 映射到 `/model/`。

部署服务必须为页面和资源配置：

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Resource-Policy: same-origin
```

`.wasm` 使用 `application/wasm`，`.mjs` 使用 JavaScript MIME。远程部署使用 HTTPS。
随附本地服务已配置这些响应头。

## 验证与当前范围

```sh
npm test
npm run check
node tests/browser.cjs
```

浏览器测试需要 Playwright 和本地 Chrome，执行前运行本地服务。
可以设置 `PLAYWRIGHT_MODULE`、`CHROME_EXECUTABLE`、`WEB_TEST_URL` 和 `WEB_TEST_OUTPUT` 指定环境。
测试实际运行两款游戏，检查画面、真实输入、Music Box PCM、模式切换、触摸摇柄、菜单、存档和失败恢复。

此版本支持这两款准备好的 **Lua 游戏**，不包含 C/ARM 游戏执行、PDGames 在线下载、翻译或完整 Playdate 系统固件。
桌面 Chrome/Edge 与浏览器模拟的横竖屏触控是本阶段的验证范围；其他浏览器和手机真机性能需要后续验收。

核心、输入和存档在 `public/runtime-worker.mjs`；WebGL/DOM/Web Audio 在 `public/app.mjs`。
原始 3D 查看器和资产保持在 `../../playdate-local`，不会维护第二套副本。
Web 专用异步停止接口仅在 `__EMSCRIPTEN__` 下编译，其他平台继续使用原来的入口。
