# PR #219 合并 dev 冲突与橡皮指针回归（2026-09-27）

## 分支与冲突

PR `Alan-CRL/Inkeys#219` 的 head 为 `bugfix/eraser@aea346c8972003b280fcfa1737003b468c369dd6`，base 为 `dev@5780f6165373d8a8cfdba79b3bee63c576afd746`；本地两分支与 PR OID 一致，merge base 为 `94e07b2599adab9de4286aa5fb7526e2c1c681f6`。从干净工作区执行 `git merge --no-commit --no-ff dev`，未产生提交。真实三方合并只有 `.trellis/workspace/codex/index.md` 的会话索引文本冲突，其余源码自动合并。

索引冲突按两侧并集解决：保留 bugfix/eraser 的会话 #17/#18/#20 和 dev 的 #16/#19/#21–#26；总会话号为 26、最近日期 2026-09-27。自动合并的 `journal-1.md` 为 709 行；索引所列 24 个会话 ID 与 journal 标题逐项一致、无重复，历史原有 #11/#12 空号保持不造记录。冲突标记已清除，索引已暂存，`git ls-files -u` 为空。

## dev 指针修复保留证据

dev 在分叉后有 `5fdd4a67`、`428860b5`、`31c48f4e`、`f0560ecd` 四次相关修复。合并后 `Draw3.WindowControl.cpp/.cppm`、`Draw3.PenCursor.cpp/.cppm` 和 `InkeysHeadlessTests/draw3_contact_tests.cpp` 的暂存 Git blob 均与 dev 完全相同。它们保留：Touch 橡皮期间抑制旧主光标；来源为 `IMDT_UNAVAILABLE + IMO_SYSTEM` 的系统 MouseMove 不能抢回 Touch 光标；真正 Mouse/Pen 可重新接管；`WM_MOUSELEAVE` 检查持久 `cursorOwner_`，不误用临时视觉 `CursorOwner()`。详见 [研究记录](research/dev-eraser-pointer-merge-20260927.md)。

两边都修改过的 `Draw3.DrawingController.cpp` 自动合并后，bugfix/eraser 的增量集中在尺寸/面积诊断快照；dev 的主光标和每个 Touch 接触独立光标路径保留。独立检查发现一个合并后的诊断归属边界：主光标被抑制、列表仅有后追加的 Touch 圆环时，不能把该圆环记为主 Mouse/Pen 光标。仅把非 Touch 诊断条件改为 `primaryCursorCount != 0`，不动实际渲染或输入判据。合并后的 `input-and-ink.md` 同时保留 dev 指针归属合同与本分支速度/面积合同。本轮未对 `WindowControl`、`PenCursor` 或其接触测试做源码手工覆盖。

## 验证与限制

- 当前 Visual Studio 安装定位 ARM64 原生 MSBuild；在同一 PowerShell 调用中规范化重复 Path/PATH、保留原 Path 内容，设置 `MSBUILDDISABLENODEREUSE=1`，仅本次调用追加 `/FS`、`/Z7` 避免编译 PDB 争用。加入主光标诊断修正后，`MSBuild.exe InkeysRepo.sln /m:1 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /v:minimal` 退出 0；日志 `%TEMP%/inkeys-pr219-merge-final-build-stdout.log`。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` 最终退出 0，整体 `PASS animation correctness`、`SpeedEraser failures=0`。其中包含 dev 的指针来源/归属序列用例和本分支的短快划、保持/回缩、面积恢复回归。日志 `%TEMP%/inkeys-pr219-merge-final-headless-stdout.log`。
- `Build/ARM64/Debug/Inkeys.exe --draw3-eraser-hidden-test` 以隐藏窗口运行，最终退出 0。DComp/ULW 的恢复探针均经历面积下限释放到约 16 DIP、真实拖动后恢复到首次 50 DIP 上界，且光标/真实几何检查通过；日志 `%TEMP%/inkeys-pr219-merge-final-hidden-stderr.log`。
- 另执行更广的 `Build/ARM64/Debug/Inkeys.exe --draw3-hidden-test`，退出 1，出现 22 条 PPT 结束页、SlideID 重排和选择页墨迹相关断言。第一条为 `one UInk file contains distinct last-slide and marked end-page content`；这组测试中的 PPT 产品源码及相关测试段与 dev 一致，不能据此把失败直接归为本轮橡皮合并，也未独立构建 dev 基线来证明其预存或环境原因。日志 `%TEMP%/inkeys-pr219-merge-full-hidden-stderr.log`；没有删除、跳过或改写断言。

完整构建改写的受跟踪 `Inkeys/PptCOM.dll` 已从本次合并的暂存 blob 恢复，未把生成产物混入额外工作区 diff。用户随后明确授权为本次合并创建 commit 并推送到 `bugfix/eraser`，使 PR 在线冲突状态更新。任务继续保持 `in_progress`，不 finish/归档；真实 Touch/Mouse/Pen 指针手感仍待设备验证。
