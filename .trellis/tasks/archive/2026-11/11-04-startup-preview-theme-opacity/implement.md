# 执行计划（第四版已批准，可实施）

## Approval gate

- [x] 用户允许创建本任务。
- [x] 确认角落固定深色；随后用户扩充三效果、选择框、像素算法、透明背景、D2D effects、缩放。
- [x] 已研究 HiStart.cpp/HiStart.h/HiWindow.cpp 调用边界及现有 StartupPreview/Setting。
- [x] 完成可交互样式稿及纯 JS/Canvas API 合同检查；样式稿不是产品运行验证。
- [x] 2026-11-04 用户明确回复“这版可以，开始实现”，批准第四版样式、名称与时长。
- [x] 已验证上下文并激活任务，按分工完成实现及完整检查。

## Ordered implementation

1. trellis-implement 子代理读取 implement.jsonl；所有权为本任务必要的配置、StartupPreview、IdtMain、Setting 和相关测试/国际化路径。说明其他工作并行存在，禁止回滚别人的修改；主会话负责规划/spec/收尾。
2. 新增效果 enum/normalize/default/Config schema 与 Mini snapshot；使用现有配置编解码入口。补主题、背景/内容、Effect 早期读取和回退测试。
3. 形成无设备策略：有效材质、透明度分层、源/目标/presentation bounds、粒子 seed/位置、最短播放成功门。保留真实进度 reducer 与 handoff 语义。
4. 实现三效果：旧掠光适配浅色；固定基底+内部彩虹色场+反光；仅非全透明底板像素渐显外散/回收、精确还原静止底板与青尾消退、接入掠光。D2D effect/bitmap 随尺寸/epoch 正确释放重建，设备恢复不重播。
5. 设置页 Toggle 下 ConditionalBlock + ComboRow；draft/events/模型快照/落盘贯通；沿 Scripts/i18n.ps1 既有流程新增名称/说明并同步生成键，避免无关生成 diff。
6. 扩展现有 Headless 的 Config、设置真实 ImGui 回放、StartupPreview 状态/几何和 WARP/D2D 像素测试。使用生产 helpers，不另写一份效果算法代替生产验收。保存两主题/三位置/三效果关键帧供静态看图。
7. trellis-check 子代理审查本轮完整 diff 与检查清单，自修本任务问题，报告测试结果和未验证项。
8. 主会话更新必要长期 spec（startup-preview、配置合同、设置条件块的新增规则如有），再次确认用户格式/无 GUI/不 commit/push 约束，再收尾。

## Validation

- 静态：git diff --check；核对原文件字节 BOM/CRLF；配置 key/字段 consumer 全链；三效果 switch 完整、没有 shader/device/thread 无关新增。
- 纯逻辑：深浅×位置×透明度、DPI/Zoom/负坐标/工作区裁剪、固定 seed、跨帧率进度连续、快速 success 最短播放、failure/exit bypass、设备重建保持时钟。
- 无 HWND：既有 InkeysHeadlessTests.exe --no-window，包含真实 D2D/WARP 离屏和 Settings UI 回放；不得调用 GUI smoke/manual-delay。
- 正式编译：先通过当前 VS 安装定位原生 ARM64 MSBuild.exe，同次 PowerShell `Remove-Item Env:PATH -ErrorAction SilentlyContinue`，`$env:MSBUILDDISABLENODEREUSE='1'`，再 `MSBuild.exe InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /nr:false`。总预算至少 5min、记录退出码/首个实质错误。环境问题不改代码/SDK绕过。
- 原生离屏关键图核对：彩虹固定完整底板、密集连续淡色场、深浅都低强度半透明、仅按形状裁剪无羽化、随机局部色块二维碰撞/融合和掠光；像素渐显后全域随机散布（目标区不留空）、内部点随机短程可见流动/所有点同时归位、透明角/半透明边缘、无缝静止底板、青尾消退后正常掠光；进度在上、独立 alpha、边界无黑白矩形或裁切亮线。

## Risky areas / rollback points

- IdtMain Mini 缺少效果/透明度读项会出现启动闪回默认；Config 与 Setting 必须同步。
- 粒子外扩 HWND 必须与正式目标坐标分开，不污染 CachedStartupBarWidthDip 或缩放；空白像素保持透明。
- 预乘 alpha、ColorMatrix alpha 行、blur padding/offset、历史缓存 target/input 切换和 source-over 次序是重点审查点。
- 成功最短播放仅作用于像素效果；失败/退出/设备损失恢复不能等待动画或误记进度已呈现。
- 未获 GUI 授权；本轮最终验收说明只涵盖无窗口证据。现有历史 PNG 的 AD 状态不属于本任务，不动。

## Completion

- [x] 配置/设置/二十语与三效果实现完成；第四版视觉合同已落实。
- [x] 完整 ARM64 solution 构建退出0；启动专项及全套 --no-window 最终退出0。
- [x] 已检查原生离屏关键帧；固定步进/模糊边界/逆缩放回归通过。
- [x] 完整静态检查、差异/编码/换行和依赖二进制校验通过。
- [x] 按用户规则不运行GUI、不commit/push；仅归档本任务。
