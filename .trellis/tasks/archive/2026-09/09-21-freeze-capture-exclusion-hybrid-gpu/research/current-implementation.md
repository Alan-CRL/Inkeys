# 当前定格实现调查

## 调用链

1. 用户切换定格状态后，`FreezeFrameThread()` 负责显示 `MagnifierHost`/`MagnifierChild`。
2. 第一次激活时该线程直接调用 `UpdateMagWindow()`、重绘 child，并把 host alpha 设为 255。
3. 同一分支随后设置 `RequestUpdateMagWindow=1`。
4. `MagnifierThread()` 观察到请求后，在 `MagTransparency==0` 时再次调用 `UpdateMagWindow()`。
5. `UpdateMagWindow()` 使用主屏 `{0, 0, width - 1, height - 1}` 调用 `MagSetWindowSource` 并 invalidates child；关键返回值没有转化为捕获成功/失败状态。

直接风险：第一次定格存在两个生产者和两个捕获时刻。host 已由第一个路径变为不透明，但普通全局 `MagTransparency` 没有同步更新，因此第二次捕获可能把承载窗口或当时的 Inkeys 内容再次纳入画面。

## 排除列表

`MagnifierThread()` 等待窗口创建后，一次性组装并提交以下句柄：

- Bar/floating window；
- 四个 PPT/PageControl 角色；
- Drawpad；
- DrawpadPresentation；
- Freeze；
- Setting。

存在的问题：

- 没有 `IsWindow` 和当前进程校验；
- 没有去重；
- 没有排除 host 本身；API 文档只保证 magnification window 自动排除，不能把该语义扩展到不透明 owner host；
- 不调用 `MagGetWindowFilterList` 回读模式/数量/句柄；
- 捕获前不刷新动态窗口句柄；
- 把“API 返回 TRUE”直接当作所有后续捕获都可用。

## 线程与生命周期

- Magnification 初始化和窗口 callback 位于 Window Service overlay owner 线程。
- `FreezeFrameThread` 与 `MagnifierThread` 均可触发画面更新。
- `magnificationReady`、`MagTransparency`、`RequestUpdateMagWindow` 是跨线程普通全局变量，没有 atomic/mutex/message contract，构成 C++ data race。
- 当前 Window Service 在初始化 Draw3 DComp fallback 时可能重建一次，但发生在 MagnifierThread 启动前，且随后会刷新句柄；未发现常规运行中相同的重启路径，因此它不是目前最强的 stale-HWND 解释。

## 画面承载与 owner 链

当前基础链为：

`MagnifierHost -> Freeze -> DrawpadPresentation -> Drawpad -> Bar/PPT`

`Freeze` 本身仍承担恢复画布/PPT 通知等透明叠层，不能简单改为不透明桌面截图窗口。若移除 Magnification control，应让新的 `FreezeCapture` 替代链根，并保留独立的 `Freeze` 提示层。

## 历史变化

- 排除列表与 Magnification 结构源自旧实现，之后逐步加入 PPT、DrawpadPresentation 和 Setting 句柄，但没有形成动态验证/事务。
- `cec656e0 fix: restore freeze magnifier display` 为恢复立即显示，在 FreezeFrame 路径增加了直接捕获/揭示；旧 MagnifierThread 的请求捕获仍保留，由此形成双捕获。
- 当前仓库没有 Desktop Duplication 或 Windows.Graphics.Capture 后端。

## 结论强度

- **已确认代码缺陷**：双捕获、跨线程 data race、返回值/帧发布缺少合同、一次性且未验证的排除列表。
- **与现场截图高度一致的推断**：一个主栏被捕获进静态帧，实时主栏仍在上层，因此看到两个主栏。
- **平台风险而非当前设备复现**：Win7 排除异常、混合显卡错误 adapter/黑屏，需专门矩阵验证。
