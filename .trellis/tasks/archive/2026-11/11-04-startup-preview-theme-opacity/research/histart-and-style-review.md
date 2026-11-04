# HiStart 算法与本轮样式研究

## 外部只读源码

- `D:/Project/HiEasyX/HiEasyX/HiEasyX/HiStart.cpp` 为 GBK/GB18030；文件前部是巨大的内嵌 Icon 数据，读取时应跳过数组，保留原文件不写。
- `:221-227 IsEdgePoint` 名称叫边缘判断，实际接受所有非白像素；`:231-330 GetDstPoints` 在临时 IMAGE 画文字/Icon，收集每个目标点及颜色，居中映射到窗口。
- `:333-345 GetSrcPoints` 用一次 time seed，在整个给定区域随机分配源点。
- `:348-364 Blur` 对当前像素与上/下/左/右五邻域求平均，再给 RGB 加 speed 逐渐回到白色；这是不透明白底专用，不能直接复制到透明 ULW。
- `:388-418` 运动公式为 `source + (target-source)*min(256,i)/max(256,514-i)`，另有 brightness 由亮到正常；每帧先扩散旧帧，再直接绘制清晰目标颜色点。`:423` 固定 sleep 与末尾1s等待不适用于本项目异步启动。
- `HiStart.h` 注明开场动画源自“冰封的 EasyX”；这里只借鉴点集运动/残影扩散思想，不复制 Icon、文字、IMAGE/窗口管理或源代码资产。
- `HiWindow.cpp:1423-1425 / :1675` 是独立启动 scene 调用边界，无额外粒子物理算法。

## 本项目证据

- StartupPreview.cpp:398/519 建立共享 D2D device 的 target/mask/brush；:790 附近先背景/反光再进度，最后 GDI GetDC -> ULW。不是 GDI+ 渲染器。
- Bar.Rendering.cpp:1766/2371 已有 GaussianBlur 实例，BALANCED、SOFT、3 sigma padding，可复用同一兼容性与生命周期习惯。
- Setting.Pages.cpp:1073 当前 ToggleRow；邻近 :1087 的 ConditionalBlock 可用于下落选择行。Setting.Pages.h:277/309 的 draft/events，Setting.cpp:2555/3438/3573 的配置同步链需同时扩展。
- StartupPreview.State.cppm:33 起已有4s反光周期、300ms启动、无进度300ms/有进度1000ms交接；新像素拼装可在进入成功 handoff 前加入最短时长门。

## 用户本轮修正（必须覆盖旧假设）

1. 角落固定深色；效果选择仍保留三种。
2. 新增启动效果下拉框；新两款不是仅浅色专属，深浅均可用。
3. 像素透明背景、来源区域按指定几何，提前 ready 也播完拼装，再顺序渐隐/渐显。
4. 允许使用 D2D effects；原先“不要高级效果”已被用户后续授权覆盖。
5. 彩虹晕染必须是完整固定圆角底板内部动态材质，包含反光；不允许只剩漂浮色团。预览已补暖黄/青绿/蓝/紫/粉多层及内部反光。
6. 新样式必须本人确认后实施；后续问题使用对话问题工具。

## Style artifact and verification status

- `C:/Users/alan-/.codex/visualizations/2026/11/04/01a1a45b-c9bf-7683-8ea8-ae00465741f1/startup-effects-review.html`：两效果轮播，含设置下拉框、深浅、三位置、75/100/125/150%缩放、独立透明度、快/慢/失败时序、拖动时间与单次播放。
- 像素演示实际采用五邻域历史透明覆盖率衰减并青色着色；产品优先使用等价 D2D effect graph，演示并非原生渲染截图。
- Node VM 执行完整脚本并以 Canvas API 合同替身覆盖 36 组主题/位置/缩放及控制变化；32668 次绘制参数均有限，时序/折叠控制检查通过。该结果不能声称浏览器像素或产品 D2D 已验证。
- 本机 Edge headless 尝试未生成截图且无诊断输出；未启动交互浏览器，未用 Computer Use。未修改产品或编译产品。
- 2.4秒拼装、约3.7DIP粒子间距、配色与拖尾强度为待用户核对的初稿，不作为已批准产品参数。
