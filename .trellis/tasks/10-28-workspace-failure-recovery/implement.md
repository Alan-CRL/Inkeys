# 实施计划

## 1. 基线与接缝

- [x] 保存正常三态、DComp 恢复及故障现场证据，确认不改正常路径。
- [x] 为源补偿、等待进度、退休等待和 legacy 重建选择现有生产 helper/CLI 接缝，不新增平行状态机。

## 2. 协调器

- [x] 实现 Rejected 后 Whiteboard 源预览补偿及真实背景 ACK。
- [x] 实现各类非原生等待的失败预算、实际进度重置和显式重试。
- [x] 扩展 `--state-mode-production-test`：恢复屏障、源预览、无进展、进度、取消和重试。

## 3. DComp

- [x] 实现有界退休及隔离/迟到清理，覆盖成功、失败、阻塞和第二 worker 拒绝。
- [x] 实现样式可清除时的原 ULW 回退，以及不可清除时的完整 legacy 重建。
- [x] 覆盖新旧 generation、文档/页身份、新首帧、dirty/保存状态和旧 completion 拒绝。

## 4. 收尾

- [x] 同步 whiteboard、canvas-freeze、Draw3 和 rendering 规范，删除过时描述。
- [x] 独立静态复审正常路径、故障写集、线程/资源所有权和 Win7 回退。
- [x] 完整 `InkeysRepo.sln Debug|ARM64`、相关生产 CLI、`InkeysHeadlessTests.exe --no-window` 和 `git diff --check`。
- [ ] 人工复核正常三态/焦点/任务栏；真实 DComp 故障与 Win7–Win11 分平台验证单列。
- [ ] 不提交或 push，除非用户另行明确授权；不因验证结束自动归档。
