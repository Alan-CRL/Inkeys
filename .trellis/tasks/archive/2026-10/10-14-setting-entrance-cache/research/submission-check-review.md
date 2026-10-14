# 第二轮提交优化独立审查

2026-10-14，trellis-check。依据 submission-revision.md（优先）、check.jsonl 所列资源、prd/design/implement，以及 rendering-and-ui.md 的 Setting、主页入场和共享调度合同。审查期间产品和测试由各 ownership 代理修改，本代理只读代码并向对应代理反馈；没有并发修改产品、构建、运行测试、GUI/Computer Use、commit/push。

## Findings (fixed)

以下五项包含开发和候选验证期间的修复记录；最终省 Clear 逻辑撤出产品，RGB边界回归留在测试局部实验。

1. `Setting.Pages.cpp::PrepareHomeEntryTargetClear` 最初允许任意有限 0..1 RGB。Clear 的 float→UNORM 与 ImGui 的 float→U32 在半字节舍入点可能不同，不能据此省 Clear。已反馈 core 并复核其修复：仅精确 U32→float roundtrip 的 RGB 允许候选；bench 新增 2.5/255 非 byte-grid 材质回退的整帧精确 RGBA 对照。
2. `Setting.cpp::SettingEntryProfiler` backend 失败原先用 backendEnd 当成 Present 返回时间并 Record，生成无真实 Present 的间隔样本。已反馈 core 并复核其修复：失败单列 backend-failures，只 Flush 已有真实样本；skip 只增加 skipped-frames，不生成 backend/Present 样本。
3. `Setting.cpp` 新的资源前置门禁最初对 FAILED(GetLastResult()) 直接 continue。主会话发现并交 core 修复，本代理沿 vendor 复核：NewFrame 在资源齐全时不清 LastResult，RenderDrawData 才清上一帧 Map/OOM 错误。当前只 device reason 失败提前走恢复，旧 backend 失败强制真实 Render 重试；bench 的实际 IndexMap 故障→相同 DrawData 再提交覆盖这个合同。
4. 首轮无窗口执行暴露 `setting_design_tests.cpp::CheckFrameSubmission` 的手工 ImDrawCmd 未初始化：ImVector::resize 不调用构造函数，callback/offset 未定义字节被正确门禁拒绝。core 已通过 `cmd=ImDrawCmd{}` 初始化，本代理复核未改判等条件。
5. 同轮执行暴露 `setting_home_warp_tests.cpp::SelectOverlaySubset` 在裁剪输出缓冲后未更新 ImGui 内部写游标，后续 AddDrawList sanity assertion 失败。bench 已同步 `_VtxWritePtr`、`_IdxWritePtr` 和 `_VtxCurrentIdx`；已有 VtxOffset==0 门禁和原断言保留，本代理复核只改测试夹具。

## Findings (not fixed)

候选版静态审查及实际无窗口测试已通过。省 Clear 候选无稳定收益（hold +0.69%、dock -0.80%、settle +0.81%），主会话依 revision 决定撤出产品，仅保留局部测试实验。回撤后的静态接口核查已通过：产品旧 helper、分支、clearOmitted 键和可选背景参数无残留；实际提交无条件 Clear。bench 局部实验通过 AddDrawList + rotate 保持正常背景顺序和标准计数，CPU 键回归改为 geometry 变化。最终版本完整构建、全套无窗口测试及静态检查均通过，最终执行的日志与状态已独立读取核对；没有保留未修复产品问题。

Windows 7 实机、原生 HWND caption、真实 Present/遮挡和共享客户端的现场行为没有运行；本轮明确禁止 GUI，本报告仅以生产分支和无窗口夹具检查这些边界。新增固定组件消融目前仅测生产 WARP；独立 hardware 保留原阶段 full/cached 诊断，不把 WARP 组件结果推广到 hardware。

## 行为与资源核查

- CPU NewFrame、实际 UI 构建、延迟 tooltip/menu、caret、导航/入场时钟、下载文字轮询继续执行；精确 duplicate 返回 Continue，不改全局 FrameInterval 或 Present(1,0)。没有在 skip 调用 Complete(false)，也不虚增准备/捕获/恢复提交计数。
- FrameSubmissionCache 保存单帧值字节，固定上限 1MiB，热相同帧不复制/扩容。ImDrawVert 布局用 static_assert 证明无 padding；顶点/索引精确字节以及命令 ClipRect、实际 GetTexID、VtxOffset、IdxOffset、ElemCount 逐字段精确比对；不靠 hash，不持有跨帧 DrawList、ImFontBaked、纹理数据或 callback 指针。
- 非 OK texture lifecycle、任何 callback、输入、backend/removed reason 失败、Home entry 准备/捕获/恢复均强制提交。取间接纹理 ID 前先查 Status 和有效 TexID。快照只有 backend 成功、真实 Present==S_OK 后发布，且保存 atlas 最终身份和 page cache revision；失败、遮挡、Hide、resize、fonts、epoch、主题和 session teardown 撤销。
- 省 copy 仅在 style alpha=1、覆盖 opacity=1 且客户矩形四边通过本帧 DisplayPos/FramebufferScale 映射为整数像素时生效；分数边界保留 copy。页面截图属于 DrawData 外的 GPU copy，revision 纳入提交键。单张同设备 copy-only texture 不持有 swap-chain backbuffer；Validate 与失败/Hide/epoch 等生命周期撤销。新代码没有 product device/thread/shader，也没有 ClearView 或对 discard 保留像素的依赖。
- 最终产品每次实际提交保留原 ClearRenderTargetView；省 Clear 的生产 helper、分支、可选背景列表和快照键已由 core 回撤。测试局部实验继续要求 Solid、style/window alpha 精确1、规范字节 RGB、完整整数客户区覆盖，并用 poisoned target 精确RGBA验证。实验结果无稳定收益，不推广为产品优化。

## 测试与测量核查

- 真实 Frame + 字体 + DX11 backend 测静态第一次提交后12次 skip、hold十次 skip 继续约200ms、dock首变帧提交、终态真实恢复；真实 Fluent hover/tooltip、InputText 字符与1.2s caret、caption/轮询文本变化；真实 callback、texture create/update/destroy 和 Map失败重试。
- 七材质/边界×四时刻×浅深精确全帧 RGBA；候选之前污染整个 target，确认不依赖离屏/交换链旧像素。writing .76 覆盖用预先真实 capture 后回拨，只验证候选安全；writing 吞吐 mode4 不提前捕获。
- 消融使用同固定 hold/dock/settle、面积、字体、标题、cache。仅切原 foreground 的矩形与字标索引范围，完整混合与分次 cover→ink 绘制必须逐像素相同。无 cover 组仍保留 Clear 和强制 copy，目标完整初始化。
- 真实提交的 EVENT+Flush+有界 GetData 与 staging Copy/Map 正确性读回分离；CPU/backend/wait/total 都是 wall time。skip 不发 EVENT，报告 CPU callback-only 和 actual-submit/skip/copy/clear 计数，不报显示或GPU FPS。total percentiles 取同帧样本，不相加各项 median。

## Verification

- 静态格式检查：限定 Setting/tests 路径并 `--ignore-submodules=all` 的 git diff --check 通过。默认全仓命令触发 HiMsg 子模块 git 元数据 Permission denied；属于 sandbox 权限，不修改 Git/SSH/权限绕过。
- 编码/换行：本次七个源码/头当前均 UTF-8 无 BOM、CRLF、无 bare LF；五个既有文件 HEAD 原始字节同样无 BOM；core.autocrlf=true、git ls-files --eol 显示 i/lf w/crlf，与工作树 CRLF 格式一致；两个新头无尾空白。
- Lint：本包没有独立 C++ lint 命令；diff/严格UTF-8解码/编码换行检查通过。主会话最终 i18n check exit 0，en-US 与 zh-TW 均 384/384。
- TypeCheck/Build：候选版本完整 InkeysRepo.sln Debug|ARM64 原生 ARM64 MSBuild 通过，exit 0，用时 18.677s。省 Clear 回撤后最终完整构建 exit 0，用时 22.3753978s；PATH 重复键仅通过本次构建进程 canonical 环境修正，没有修改产品或全局环境。
- Tests：最终回撤版本全套 InkeysHeadlessTests.exe --no-window exit 0，用时 278.4643381s，pid 18240，stderr 0；stdout 末尾 PASS animation correctness。EXE SHA256 5EA497E71DF5E8AD5321F521F308EF20CAAFD5BD258E70154241FC51CAD48EBD。最终60组完成（21新增WARP、39原WARP/hardware），静止/hold skip100 actual0，delay tooltip、caret/文字变化、callback/纹理事务及实际Map失败重试均通过。候选版 238.3326s PASS 单独保留，不混用旧状态。
- Spec sync：主会话已同步 rendering-and-ui.md；本代理定向复核新增签名、五参Filter、Continue/S_OK/1MiB/事务门禁、旧backend错误重试、整数不透明省copy、Clear只留局部实验、错误/测试矩阵和真实Present诊断计数，均与最终代码一致。

## 最终报告复核

已读 submission-validation.md、最终 build.status.json/headless.status.json、stderr长度0、stdout末尾和相关回归行，以及七文件 format-audit.json，结果一致。最终完整MSBuild使用已安装VS的arm64 host，exit0/22.3753978s。七文件格式审计均valid，Setting.Pages.h/.cpp相对本轮baseline逐字节相同。定向git diff --check含同步spec再次通过。

审查发现报告生成器遗漏旧39组 unlimited-fps 字段，已反馈主会话并复核其修复：旧表static WARP32.1951、dock-prepared WARP64.6224、dock-cached hardware631.393，与完成total对应；所有60组和skip无GPU/显示FPS标签一致。最终hold/static skip total为1.0238/1.7279ms，比较0.1537/0.0944ms，实际提交0/跳过100；这些是CPU回调成本。省Clear最终median变化+0.92%/+0.04%/-0.43%，未声称稳定收益，仍只局部实验。EVENT为wall-time completion，组件归因保守，caret计数窗口、WARP/hardware范围、未GUI/Win7/原生Present限制均明确。无夸大验证范围。

本代理最终只写此审查记录，未改产品、未build/run/GUI/commit/push；记录完成后停止写入。
