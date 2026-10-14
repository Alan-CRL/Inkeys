# 用户截图反馈审查（2026-10-14）

## 审查范围
- Active task: .trellis/tasks/10-14-select-clickthrough-modes，feature/mode，基线3f3946cf。
- 已读取保存的完整native hook输出和本轮需求/实施记录；检查本轮全部产品、测试、三语说明及任务规范差异。
- 不改主会话任务/规范，不启动GUI或Computer Use，不commit/push/finish/archive。

## Findings (fixed)
- 本检查阶段未发现需要新增局部修复的源码问题，未修改产品代码。
- 核对实现代理修复：Pierce本来已固定保留在Load运行时列表，实际补丁将隐藏坐标锚定到Freeze；统一Recall视图覆盖非白板空页Selection显隐；设置三段单换行及工具名「」/去掉清空括注。

## Findings (not fixed)
- 无待决设计或已确认而未修的源码缺陷。
- 真实主栏显隐动画、HWND计时唤醒、鼠标/触摸穿透、PPT场景和实际设置窗口视觉仍需人工验收。遵守本轮无GUI约束，静态/离屏证据不能代替这些验收。

## 实际路径核对
- Load无本轮生产变更，mainPierceButton由shared_ptr持有，固定插在Freeze之前；hide/size变更不删除/重插运行时成员，不写回A2的Pierce配置。
- AnchorHiddenBarButton声明未export，属于Bar模块内部；生产和离屏测试共用此helper。生产回调沿原SetButtonPositionTar处理批次/镜像/换边关键帧，不另建曲线。BarButtonListClass::Get对负索引及越界返回nullptr，最近有效锚点搜索安全。只改变隐藏目标与lastDraw位置，折叠及可见按钮仍走原路径。
- Recall视觉入口、视图读取及ActivateRecallPopupCommand/EngageRecallCommand的迟到popup判定共享同一精简门禁。compactSelection只覆盖主栏和popup显隐，不清hold/pendingLayout或3秒steady_clock保护；idle到期重新投影仍保持隐藏。主栏悬停/点击扫描沿已有IsVisible门禁排除隐藏对象。
- IsCompactSelectionBar一次取完整StateMode快照，只读取现有原子内容/白板标识。新增recallChromeMutex到modeMutex的读取路径未发现反向持锁：工具setter在mode锁释放后才Sync/Refresh，Reconcile同样先释放mode锁再发布Bar内容/历史。runtime可用性快照在调用新mode快照前已返回，不跨越两个锁。
- 新离屏fixture在早期CLI内只规范化内存A2 Freeze尺寸，避免Load因默认TwoOne与注册TwoTwo差异触发既有迁移Write。IdtMain在配置读取、互斥体和任何产品HWND初始化前直接return该测试，修改进程内fixture不加载或保存用户配置，测试返回后进程退出无需恢复内存。
- 说明测量与绘制继续使用同一字符串、字体比例和换行宽度；单换行减少两个空白行，卡片高度仍随英文自然换行增加，主栏选择(清空)/Select (Clear)标签未变。

## Verification
- 静态diff检查：git diff --check --ignore-submodules=all通过，退出0。
- 编码/换行：21个修改的跟踪文件UTF-8解码及原BOM政策通过，工作区CRLF且未引入裸LF。Git blob可能按autocrlf归一化为LF，不能直接用blob裸LF计数与工作区作逐数比较。
- i18n check：再次执行Scripts/i18n.ps1 check退出0，en-US/zh-TW各349/349，100%。sync已由实现代理完成，头键名不变，基准快照同步。
- 设置回放：核对Build/SelectClickThroughValidation/settings-feedback.log，三语×0.6/1/1.5/2共12组全部failures=0，clipped/glyphClipped/missing/overlaps/spacingFailures均0；1.0中文文字45/旧75 DIP，英文75/旧105 DIP，恰少两行，实际卡片85/115 DIP。
- TypeCheck/完整原生编译：待主会话统一InkeysRepo.sln Debug|ARM64。本检查代理未独立启动构建。
- Headless/offscreen动态回归：待主会话编译后执行；新增测试代码已审查实际共享helper、动画推进、生产Recall刷新/到期路径和稳定对象指针序列，不能提前标为运行通过。

## 交接
生产源码保持冻结，可直接统一构建与无窗口测试；本检查阶段没有额外源码变更。

## 主会话最终动态验证
- 完整Debug|ARM64 solution exit 0，0 errors。
- Headless --no-window、Bar offscreen、StateMode production三个CLI均exit 0；EraserVisual/RecallAttribute failures=0。
- 本轮完整结果、日志及人工边界见 ../validation.md；产品源码冻结后未修改，无GUI/commit/push/task结束。
