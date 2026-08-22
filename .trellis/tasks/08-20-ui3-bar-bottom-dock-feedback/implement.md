# Implementation Plan

## Preconditions

- [x] 确认依赖任务 `08-14-ui3-bar-bottom-dock-elastic` 的状态机与现有 Headless 测试基线可用。
- [x] 读取 `native-desktop`、共享指南及任务引用的渲染规范。
- [x] 在写代码前确认工作区差异，保留用户已有修改。

## Ordered Work

1. [x] 在窗口几何/呈现纯逻辑层加入 local-dirty 与 full-replacement 决策及成功 tuple 合同。
2. [x] 修改 Bar 呈现循环：变化帧完整清除、`prcDirty=nullptr`，仅成功后发布 tuple；补充失败重试测试。
3. [x] 增加前景交互指示器几何、文本测量、自适应宽度和显示生命周期，并在主栏内容之后绘制。
4. [x] 扩展成功呈现快照，并在全部 Bar 输入阶段之前实现指示器消息消费及 hover/pressed 清理。
5. [x] 添加 `UI.Bar.BottomDock.Mode`、`UI.Bar.BottomDock.Centered` 翻译，运行 i18n sync 并检查生成差异。
6. [x] 移除半透明背景板及其 capture generation、动画、缓存 target、dirty、viewport 和资源重置链路；指示器直接按主按钮/主栏可见描边联合外框定位。
7. [x] 扩充 Headless 测试，覆盖呈现决策、指示器几何、DPI、动画反向、语言宽度和命中阻断。
8. [x] 更新 `native-desktop` 渲染规范，保留 source/size 变化时的整窗替换合同并删除废弃装饰层合同。
9. [x] 将指示器几何改为 30 DIP 高、DWrite 实际宽高驱动的四边等距动态宽度，并让竖直中心跟随主栏实际可见上边框。
10. [x] 复用主栏 Surface、SurfaceFrame、TextPrimary、13 DIP 按钮字体及第一/第三 PointLight，删除蓝色不透明专用绘制。
11. [x] 为显示/隐藏加入与绘制属性提示浮窗一致的中心等比 Back 回弹，并增加主栏折叠门禁；成功快照继续发布实际缩放命中边界。
12. [x] 扩充 Headless 与静态覆盖，验证动态 metrics、异常输入、DPI、中心缩放、折叠门禁、光源 dirty 和输入遮挡保持不变。
13. [x] 将指示器文字改为主栏按钮的粗体 `TextPrimary`，并让基础边框、第一/第三 PointLight 的透明度、颜色和强度与主栏本体完全一致。
14. [x] 将指示器显示/隐藏时长改为问号提示浮窗使用的默认操作时长，保留中心等比 Back 显示与缩小隐藏，并验证快速反向连续。
15. [x] 修正指示器 dirty/viewport 包络，覆盖旧新边界、Back 顶端极值、缩放描边、固定 Gaussian 外扩和抗锯齿余量；补充回归测试。
16. [x] 重新运行完整质量门且不启动 GUI。
17. [x] 增加水平居中 mode/tracker、捕获带、联合外框中心和二维映射纯逻辑，并补齐快速跨带、折叠及 DPI 测试。
18. [x] 扩展成功呈现快照和交互发布 tuple，把横纵模式、阶段、形变量与直接窗口位移放入同一 transition serial；补齐失败回滚和显示重基准。
19. [x] 在渲染循环实现居中锚定、水平捕获/恢复弹簧、两轴主体映射、扩展面板刚性跟随、光源逆映射及完整 dirty/viewport 包络。
20. [x] 增加启动稳定居中、折叠退出和展开阈值内无提示自动居中的生命周期。
21. [x] 将指示器门禁改为仅浮动起始手势，并增加“底栏模式 / 底栏模式 · 居中”内容切换和无裁切动态宽度。
22. [x] 更新 native-desktop 两轴呈现规范，运行完整质量门且不启动 GUI、不提交 commit。
23. [x] 将水平果冻改为按主按钮展开侧固定远端、抓取侧跟手，并覆盖捕获/脱离首帧连续性及左右镜像。
24. [x] 允许底栏起始手势在本次 `Free → Centered` 后显示居中提示，离开居中带或松手后淡出。
25. [x] 将指示器内容切换拆成“先扩框后换字 / 先换字后缩框”的串行时间线，并重新运行完整质量门。
26. [x] 将水平绘制拆为主按钮/Logo 刚性抓手、主栏主体仿射形变和扩展面板锚点刚性跟随，补齐独立 dirty、光源及命中逆映射。
27. [x] 从最后成功呈现远端按 HWND 位移重基准捕获/脱离弹簧，并禁止形态 barrier 把视觉中心回灌未形变 tracker 基准。
28. [x] 将底栏起始手势的居中提示资格改为首次捕获后锁存，覆盖 `Free → Centered → Free → Centered` 的连续矩形和双向文案切换。
29. [x] 运行本轮全部质量门并静态复核单 target/单 ULW 合同，不启动 GUI、不提交 commit。
30. [x] 将提示资格改为“竖向进入底栏或任意水平模式切换”手势锁存，并覆盖居中起始脱离、普通起始捕获、未切换底栏拖动和离开/折叠清除。
31. [x] 新增独立 40 DIP 水平阈值，保持竖向 20 DIP；同步 tracker、快速跨带、自动居中和 Headless 边界断言。
32. [x] 为非拖动居中态加入逐帧联合外框中心补偿，并只在成功呈现且布局稳定后吸收到基础锚点。
33. [x] 首次激活提示时立即把 Back 峰值、描边、第一/第三光源、Gaussian 和抗锯齿包络加入业务 damage，失败提交继续保留。
34. [x] 运行本轮全部质量门并静态复核成功快照、单 target/单 ULW 合同，不启动 GUI、不提交 commit。

## Validation

- [x] `pwsh -NoProfile -File Scripts/i18n.ps1 check`
- [x] 运行仓库中全部 `InkeysHeadlessTests.exe --no-window`。
- [x] `git diff --check`
- [x] 使用 ARM64 `MSBuild.exe` 构建完整 `InkeysRepo.sln`：`Debug | ARM64`，超时至少 5 分钟。
- [x] 静态复核只有一次最终 GDI/ULW 提交，且未启动可见 GUI。
- [x] 重新运行上述全部验证，确认新指示器样式未引入第二 target 或 GUI 启动。
- [x] 新增水平居中状态机后重新运行以上全部验证。
- [x] 完成本轮刚性抓手、远端连续性和提示锁存修复后重新运行以上全部验证。
- [x] 完成本轮提示生命周期、首次显现脏区、40 DIP 水平阈值和动态布局居中后重新运行以上全部验证。

验证结果：ARM64-host `MSBuild.exe` 完整构建 `InkeysRepo.sln` 的 `Debug|ARM64` 通过；仓库中四个现存 `InkeysHeadlessTests.exe --no-window` 均输出 `PASS animation correctness`；i18n 英文与繁中均为 `274/274 translated`；水平 40 DIP/竖向 20 DIP 阈值、刚性主按钮、主栏远端重基准、提示手势锁存、首次显现完整光影包络、非拖动布局逐帧严格居中、成功快照实际联合中心及两轴 dirty/光源/命中均有 Headless 或静态覆盖；`git diff --check` 通过；静态确认保持单 target、单次最终 GDI/ULW 提交，未启动 GUI。

## Risk And Rollback Points

- 呈现快照推进：失败路径若误更新会使重试漏掉整窗替换；先用纯逻辑测试锁定，再改 ULW 调用。
- 输入遮挡：必须使用成功呈现快照，避免不可见区域抢占输入；保留现有阶段顺序，仅在最前方增加 blocker。
- i18n 生成物：只通过脚本更新；生成异常时回退源翻译差异并重新 sync。

## Manual Follow-Up

维护者手工验证自由主栏从上下方进入捕获区、快速跨阈值、100%/150% DPI、动画开关及脏区调试开关下均无重复上半栏、残影或点击穿透。本轮不自动启动 GUI。
