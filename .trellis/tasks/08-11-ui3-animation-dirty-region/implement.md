# Implementation Plan

1. 新增纯头文件 dirty tracker 和 headless 单元测试，覆盖旧/新并集、显隐、多键合并、裁剪、首帧、commit/retain/full fallback。
2. 在渲染状态中接入 tracker；为标准控件、父布局和自绘功能组建立稳定键与变化标记。
3. 为主光、鼠标光、FPS 文字和调试红框登记显式影响范围，保持业务 damage 与调试 damage 分离。
4. 用 tracker 输出替换现有逐帧 `predicted ∪ LastPresentedBounds` 脏区选择；保留并注释旧用途，维持相同 D2D clip/ULW 矩形。
5. 将 tracker commit/retain 与现有呈现完成状态绑定，并覆盖 device generation 与失败全脏恢复。
6. 更新 native-desktop 渲染规范，记录变化脏区和事务约束。
7. 运行 headless 测试、`git diff --check`，使用 ARM64 Host MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`。
8. 拆分主光/鼠标光变化信号，将光源 damage 裁剪到实际 PointLight 边框影响带；补充无边框交集、单光源变化和面积上界测试。
9. 将 tracker 改为稳定快照记录并停用普通帧的未来缩窗边界收集，验证普通提交不再进行逐帧哈希节点重建或全表复制。

## Risk and Rollback Points

- 最大风险是自绘或继承布局未登记变化；所有未分类请求必须全窗口回退，功能组优先保守覆盖。
- 调试红框存在自引用风险；必须先冻结业务 damage，再追加红框旧/新边界。
- 呈现失败前不得提交 tracker 快照，否则重试会遗失旧像素范围。
- 如运行验证发现漏刷，先将对应内容升级到更粗功能组；无需恢复全局每帧可见内容脏区。

## Validation Commands

- `ARM64\\Debug\\InkeysHeadlessTests.exe`
- ARM64 Host `MSBuild.exe InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64`
- `git diff --check`

