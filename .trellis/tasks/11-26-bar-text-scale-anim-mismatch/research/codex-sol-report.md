# Codex 独立调查报告（原文）

模型：gpt-6.2-sol，effort high，service_tier fast，read-only 沙箱，codex-cli 0.171.0，exit 0，工作区无改动。沙箱内 shell 启动被拒（CreateProcessAsUserW 5），Codex 改为直接读文件。

## 事实

- **实栏绘制属性窗**：笔型、粗细及档位数字的 `size.SetTar` 与面板宽高共用展开 `EaseOutBack`、收起 `EaseInBack`，默认 400 ms，加入主栏批次时用剩余时长。换边时两者共用 `EaseInCubic → EaseOutBack`。菜单及锁定提示逐帧 `SetDirect(基准字号 × 当前宽度 / 370)`，取整目标用 `w.tar`。组件页预览点击画笔只切换工具模式，绘制入口只有主栏、More 和悬停提示。`Bar.RenderLoop.cpp:3730–3766,4887–5108,5799,6412–6413,6447–6448,6530–6534` `Bar.ContentInstance.cpp:946–975,1432–1441`
- **两宿主的 More**：同一进度以 Back 曲线开合，默认 400 ms。标签每帧 `SetDirect(基准字号 × s)`，`s` 由 `progress.val` 从 0.16 插到 1，`fontTargetSize` 用 `progress.tar`。外框宽高也取同一进度，但从 60×30 插到网格尺寸，外框与标签的缩放比例可以不同。`Bar.MainContent.cpp:509–520` `Bar.ContentInstance.cpp:88–99,133–147,248–256` `UI3.Button.cpp:563–567,615–617`
- **最终字号**＝`size.val × frameZoom × contentScale × labelFit × 校正系数 × DC 像素倍率`。实栏用当前栏缩放绘制，取整用目标栏缩放，主体 DC 为刚性平移。预览用相机当前缩放绘制，取整用包络算出的 `zoomTarget`，DC 为 96 DPI、单位矩阵。两宿主显式设目标变换，按钮按压额外用 `val` 绘制、`tar` 取整。固定布局下，`targetPixels` 在每段开合内稳定。关闭小数字号时，系数向 `floor(targetPixels)/targetPixels` 做 `EaseInOutCubic`，时长优先取字号剩余时长，再取按压作用域时长，最后默认 400 ms。`Bar.RenderLoop.cpp:7936–7944,7970,9144–9157,13523–13531` `Bar.ContentInstance.cpp:1224–1254,1432–1435` `UI3.Button.cpp:312–327,449–450` `UI3.Renderer.cpp:192–229,2344–2369` `UI3.Animation.cppm:24–29,605,833–859`
- **提交**：`3d9d04d5` 引入校正及属性窗最终目标，`eff9011c` 给 More 增加最终倍率。`0557a861` 把实栏 More 定位和绘制抽给两宿主，并给预览传 `targetScale` 和目标变换 1。`016a86ec` 共享 More 原有曲线，将预览改为先定位后推进、用固定包络 `zoomTarget`，实栏属性窗改从共享批次取剩余时长。`85331018` 将预览高度改为半视口、最低 80，可能压低目标缩放。近期三提交沿用校正时长和曲线。`0557a861:Bar.RenderLoop.cpp:5513,9564` `0557a861:Bar.ContentInstance.cpp:94,251,945–946` `016a86ec:Bar.RenderLoop.cpp:3737–3743,5137` `016a86ec:Bar.ContentInstance.cpp:1224–1254,1301–1313` `85331018:Setting.ComponentEditor.cpp:271`

## 推测

**高置信**：已有校正状态时，即使目标像素稳定，系数仍会追赶新端点。以 1×、12 DIP、未拟合的 More 标签为例，收起端系数约 0.52，展开端为 1。展开时 Back 先越过终点，系数仍在上升。收起初段外框仍大，系数已下行。实际方向取决于两端取整值。**较低置信**：More 主标签每帧按目标槽拟合并缓存，图形标签还测当前和待切换文字。拟合结果变化也会重定向校正，常规路径没有落定后才测量的分支。`UI3.Renderer.cpp:2355–2369,2434–2465` `UI3.Button.cpp:588–617`

## 待测

扩展 `--ui3-trace-dump` 的 `draw-attribute`、`more-open-reverse`，给预览记同样字段。逐帧记面板宽高、进度、`size.val/tar/dur`、当前和目标 zoom、DC Y 倍率、`targetPixels`、`labelFit`、系数当前值和目标值、最终像素字号。对照开启小数字号，并覆盖完整收起后重开。以上为静态核对。`Bar.Golden.Test.cpp:570–600,681–711`

## 修复方向

若确认是校正尾巴，优先让这两窗在开合及隐藏期间沿用展开态系数，保留静止展开时的整数像素字号。若要保留两个端点的校正值，让系数随面板进度到达端点。若开启小数字号后 More 仍显得偏小，再对齐外框和内容的紧凑倍率。