# Playdate 本地 3D 复刻

复刻 play.date 首页的 `intro-canvas` 展示，不包含整站商城。

## 启动

需要 Node.js 18 或更新版本，无需安装依赖。

```powershell
cd "项目所在目录"
npm start
```

浏览器访问 http://127.0.0.1:4173 。请通过 HTTP 服务打开，直接双击 HTML 会因资源加载限制而失败。也可以双击 `start.cmd`。

## 操作

- 拖拽模型，或聚焦模型后使用方向键旋转。
- Home 键或“回到正面”复位。
- 页面仅显示居中的模型；默认轻微自动转动，循环播放屏幕动画。拖拽后停止自动转动。
- 系统开启减少动态效果时，默认暂停自动转动和屏幕动画。

## 按钮与摇柄

- 直接点击或按住模型上的 A/B、方向键、菜单键和顶部按键，实体按钮会下压，松开回弹。
- 点击收起的摇柄将其展开；拖动展开的摇柄可连续转动。单击转动 45 度，双击收起。鼠标悬停摇柄时也可用滚轮转动。
- 聚焦模型后，Z/X 对应 A/B，WASD 对应方向键，M 对应菜单键，C 展开/收起摇柄；长按 Q/E 可连续反向/正向摇动，松开后平滑减速。摇柄收起时按 Q/E 会自动展开。
- 拖动机身仍可旋转整机，操作按钮或摇柄时不会带动机身。
- 这些是模型交互效果；屏幕仍播放原站视频，未实现掌机游戏模拟器。

`interactions.js` 从机身的材质组拆出独立按钮网格，以实际三角面做命中检测，并控制按压及摇柄动画。`intro-canvas` 会派发 `playdate-input` 事件，可用于接入游戏逻辑；事件 `detail` 包含按钮按下/松开，或摇柄角度/增量。

## 结构

`app.js` 是模型初始化及控制逻辑；`styles.css` 是页面样式；`vendor/model-viewer.min.js` 是原版查看器（含 Three.js r90）；`assets/scenes/asheville.json` 是场景，`assets/scenes/data/asheville.bin` 是自定义二进制几何。材质贴图在 `assets/asheville/`，环境光在 `assets/environments/env/`，屏幕视频在 `assets/video/intro.mp4`。`asset-manifest.json` 记录原资源地址及文件大小。

加载顺序：环境光 → 几何数据 → 场景与贴图 → 初始化材质/交互 → WebGL 渲染循环。屏幕视频经 `THREE.VideoTexture` 赋给 `asheville_screen` 的 albedo uniform。

所有渲染资源都已本地化，运行时不需要访问原站。`server.cjs` 仅监听本机，并支持视频 Range 请求。设置 `PORT` 可修改端口。

## 素材来源

模型、贴图、视频和原查看器来自 https://play.date/ 和 https://assets.play.date/ 。保留查看器中的原许可证注释。品牌及素材权利属于原权利人；本项目用于本地学习和研究。发布或商业使用前请自行取得相应授权。
