# 第三轮圆头接缝与明显横幅波纹验收记录

2026-10-11；当前 fluent 工作区，继续 10-10-setting-home-design，父任务保持原关联。本轮依据 ripple-revision.md；前两轮记录保留为历史证据。本轮用户授权覆盖上一轮仅笔画轻微提亮/背景不变的限制。

## 实现与审查

- 字标继续使用网站曲线构建的本地缓存透明矢量网格，经 ImDrawList 与现有 DX11 管线绘制；没有 PNG 遮缝、产品新增 shader、离屏纹理、设备、线程或定时器。
- 真端头改为定向半圆，弧端复用笔画实体/AA 截面顶点。k 首尾重合的回折笔画在约一个笔宽的弧长内平滑统一宽度，首末共享同一几何截面方向，避免独立圆盘 AA 环和极短末段突扩产生内部接缝。
- 开场同时提亮字标笔画与整个圆角渐变横幅，青蓝柔光与宽亮波峰从字标中心向外推进，约 900ms 一次；正文与按钮随后绘制，不参与运动。透明字标 AA alpha 保持原值；结束/关闭完全恢复静态像素。
- 背景缓存固定 66×43（2,838 顶点 / 16,380 索引）圆角网格，径向距离与圆角边缘预计算；只在开场提交，批次数保持不变。字标径向数据也预计算，稳定状态复用已计算结果。
- 整个 hero 的真实非空 ClipRect 可见性推进开场时钟；字标离屏停止其几何和姿态更新，但 hero 下半部分可见时背景仍推进；整个 hero 离屏暂停。真实进入/ShowSerial、导航、动画开关/速率、隐藏恢复与鼠标视差门禁继续复用。
- trellis-implement 实施；trellis-check 独立审查，真实像素诊断与修正记录见 ripple-check-review.md。新动效不改变文字、Fluent 命中、链接动作或后续区域。

## 编译与回归

- 完整 InkeysRepo.sln Debug|ARM64 最终构建 ExitCode=0，95.665 秒。由 vswhere 定位 ARM64 原生 MSBuild，同一 PowerShell invocation 应用 PATH workaround、禁用 node reuse，1800 秒上限。已有依赖 warning 未越界修改。
- 最终 InkeysHeadlessTests.exe --no-window ExitCode=0，223.722 秒，stderr 为空。真实三语言/字体、七倍率、浅深主题、导航展开/收起、三窗口及窄宽场景、原滚动/输入/链接/键盘焦点与动效生命周期回归均通过。
- WARP 明确请求 FL11.0，使用生产 RenderHome DrawData、现有 DX11 backend/产品 CSO/字体纹理、RGBA8 离屏目标，不创建 HWND 或 swap chain。浅深分别验证 ink 的真实 RGB 强变化/透明 alpha 不变、背景明显变化/圆角外像素不变、后期扩散到字标下方、结束及禁用精确复原；视差仍仅影响字标区域。
- k 接合在 1 / 1.875 / 4 倍测试跨圆头与 shaft 的内部接合带，内部 alpha≥250 的严格断言保留，浅深六组 gaps=0。测试采样保留 0.4 物理像素侧边余量，真实外周 AA 允许透明。
- i18n 本轮无源文案变化，check 仍为简体基准下繁/英各 384/384 PASS；无需重写生成头。主会话最终格式审计通过：20 个已跟踪文件保持 checkout 的 UTF-8/BOM/CRLF/末尾换行，19 个新增文本保持 CRLF，git diff --check 通过。

## 失败定位与修正

首轮完整构建通过；首轮 --no-window ExitCode=1（158.847秒），仅四项高倍率 cap 像素断言失败。4倍真实 RGBA 的接合侧边存在 alpha 200→109→19→0→60 的透明洞，不能归为普通外周 AA 或放宽测试。

原因是原首末端宽约 15.5 / 14.0869、最后离散段仅约 0.0038 源单位，只统一末点宽会形成急剧扩宽，同时首末截面 normal 不共线。修正为短弧长内平滑宽度及共同截面；保留内部 alpha 和多倍率断言，全部通过。

修正 cap 后中间版本完整 --no-window 已通过（ExitCode=0、210.221秒），结果保留为 ripple-headless-cap-verified*。主会话实际图像复核仍见后期斜侧窄波峰的网格齿纹，追加两处相同参数调整：背景/笔画峰宽从 .025 经 .06 增宽到 .12，保留峰值强度、band .12 与拓扑预算，改善平滑；不声称彻底消除有限三角插值痕迹。最终再构建并完整回归，结果以本记录最终日志为准。

审查还修复深色颜色通道峰值溢出，并强化真正隔离笔画 RGB/alpha 的像素回归；性能入口预热与 footer 滚动单位问题均在首次源码冻结前修正，未在共享构建期间改源。未执行旧 cap 实现的 mutation 对照，不声称新增测试已经在历史版本运行失败。

## 同机性能与像素证据

Windows11 ARM64 / Debug，同一 1000×1100 RGBA8 WARP FL11.0 目标；每模式20帧预热、100帧采样。单位 ms。CPU为真实主页 DrawData 构建；完成提交包含执行及 CopyResource/Map 等待和读回，不称 GPU-only。returning 重算包含计时外建立偏转的准备帧。

| 模式 | CPU median / p95 | 完成提交及读回 median / p95 | 顶点 / 索引 / 批次 | 缓存重算 |
| --- | --- | --- | --- | --- |
| static | 2.099 / 3.609 | 24.258 / 70.087 | 13807 / 57942 / 6 | 0 |
| idle | 2.456 / 3.484 | 22.022 / 41.136 | 13807 / 57942 / 6 | 0 |
| moving | 3.566 / 5.986 | 21.683 / 40.048 | 13807 / 57942 / 6 | 100 |
| returning | 3.067 / 4.145 | 19.360 / 26.685 | 13807 / 57942 / 6 | 200 |
| entrance | 3.559 / 4.615 | 10.919 / 16.173 | 16645 / 74322 / 6 | 100 |

所有采样模式 ImGui 分配及字标/波纹缓存扩容为0；idle无动态重算。仅开场比静态增加2,838顶点/16,380索引，批次仍为6，结束后取消波纹提交。计数不覆盖页面已有 std::string/vector 或驱动分配，不能扩大为整页零堆分配；Debug数值仅作同机对照，不代表Win7实机性能保证。

本轮完成提交/读回在模式之间有明显波动，开场读回 median 低于静态不解释为波纹提高性能，也不把此表外推为可见窗口帧率。此测量包含 CPU WARP 执行与强制读回；可靠预算证据为固定几何增量、批次不增、预热后缓存/ImGui分配为0与静止不重算。未进一步重复测试选择有利结果。

```text
[HomeWARP] cold-entry clipped=1 visible=11 entries=1 elapsed=0.183333
[HomeWARP] stable-theme=light frames=21 identical-readbacks=20
[HomeWARP] k-cap scale=1 solid-samples=153 gaps=0
[HomeWARP] k-cap scale=1.875 solid-samples=546 gaps=0
[HomeWARP] k-cap scale=4 solid-samples=2501 gaps=0
[HomeWARP] transparent-ink solid=23207 AA=2403 lower=1314 outside=0
[HomeWARP] pixels mode=ripple changed=123854 outside=0 logo=75557 background=48297 strong-background=38775 radius=258.78
[HomeWARP] ink-ripple changed=15537 strong=12281 alpha-changes=0
[HomeWARP] stable-theme=dark frames=23 identical-readbacks=3
[HomeWARP] k-cap scale=1 solid-samples=153 gaps=0
[HomeWARP] k-cap scale=1.875 solid-samples=546 gaps=0
[HomeWARP] k-cap scale=4 solid-samples=2501 gaps=0
[HomeWARP] transparent-ink solid=23207 AA=2403 lower=1314 outside=0
[HomeWARP] pixels mode=ripple changed=122687 outside=0 logo=74791 background=47896 strong-background=38824 radius=258.457
[HomeWARP] ink-ripple changed=15466 strong=11690 alpha-changes=0
[HomeMotionCull] scale=1 viewport=800 scroll=331 max=331 brandBottom=52.2297 clipTop=16
[HomeMotionCull] scale=1 viewport=300 scroll=889 max=889 brandBottom=-505.77 clipTop=16
[HomeMotion] scale=1 entries=3 cacheBuilds=1237 allocations=1
[HomeMotionCull] scale=1.875 viewport=1500 scroll=617 max=617 brandBottom=101.556 clipTop=30
[HomeMotionCull] scale=1.875 viewport=562.5 scroll=1200 max=1657 brandBottom=-481.444 clipTop=30
[HomeMotion] scale=1.875 entries=3 cacheBuilds=1237 allocations=1
```

## 兼容、范围与可复查证据

产品未新增 FL11.1 专属接口，兼容目标保持 Win7 SP1+KB2670838 / FL11.0；本轮源码边界审计与 Windows11 无窗口 WARP 证据分开记录。Win7 实机、原生窗口最终观感及实机输入未运行，保留人工验收。

未启动交互式 GUI、设置窗口或浏览器，未用 Computer Use，未 commit/push；当前任务继续 in_progress 供人工审阅，不自动归档。

- Inkeys/Cache/home-design-20261010/ripple-build-final-result.json / .log
- Inkeys/Cache/home-design-20261010/ripple-headless-final-result.json / .stdout / .stderr
- Inkeys/Cache/home-design-20261010/ripple-*.rgba 与仅解码生成的 .png（真实生产 WARP 像素）
- Inkeys/Cache/home-design-20261010/encoding-audit-final.json
- rendering-and-ui.md 本任务主页合同与 research/ripple-check-review.md
