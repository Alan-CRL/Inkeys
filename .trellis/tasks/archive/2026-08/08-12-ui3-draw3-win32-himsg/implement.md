# UI3 / Draw3 接入准备实施计划

## 顺序

1. 在 `D:\Project\HiMsg` 实现 `EnqueueMessage` / `ClearMessages`，补测试和 README，运行全部测试并独立 commit。
2. 在 Inkeys 增加 `Inkeys/additional/HiMsg` 子模块和工程接入，先以现有窗口绑定验证消息 API。
3. 实现 `Inkeys.Graphics.Surface` 与资源测试，逐一替换 Draw2、Freeze、PPT、Setting 和 Magnifier 的 `IMAGE`。
4. 抽离 `IdtFloating` 中 UI3 复用的内置动作与 Hook；将 UI2 文件改为工程 `None`。
5. 实现 `Inkeys.Window` 的角色表、两个窗口 jthread、命令队列、HiMsg 生命周期、owner 链和退出顺序；迁移 `IdtWindow`、Magnifier、Setting 和 DisplayObserver 创建。
6. 删除 UI3 实验开关与所有 UI2/UI3 分支，旧 JSON 写回时清除 key。
7. 删除 HiEasyX/EasyX 文件、静态库和全部工程/生产引用。
8. 执行静态扫描、单测、GDI handle 压力、Headless Tests 和完整 ARM64 Solution 构建。
9. 更新 native-desktop spec 与开发日志，审查差异并提交 Inkeys。

## 阶段门禁

- HiMsg commit 前：测试、README、`git diff --check` 通过，工作区只含 HiMsg 变更。
- Surface 迁移后：生产 `IMAGE` 使用为 0，资源失败路径可回滚，不改变 premultiplied BGRA 语义。
- Window 迁移后：线程 ID、owner/style、退出顺序自动检查通过；不得出现循环逐窗口 Z 序维护。
- 删除依赖前：所有消费者已迁移；删除后静态扫描不得命中 HiEasyX/EasyX 符号或库。

## 验证命令

- HiMsg：使用上游项目声明的测试入口构建并运行全部测试。
- 静态：`rg -n "HiEasyX|hiex::|\\bIMAGE\\b|EasyX" Inkeys`，仅允许迁移说明或明确非生产历史文本。
- 差异：`git diff --check`、`git status --short`。
- 构建：ARM64 host `MSBuild.exe InkeysRepo.sln /m:1 /p:Configuration=Debug /p:Platform=ARM64`，超时不少于 5 分钟。
- 测试：运行 `InkeysHeadlessTests` 目标产物及新增 Surface/Window 测试。

## 高风险点

- `IdtDrawpad.cpp` 的 `IMAGE` 数量与撤销/PPT 页面状态较多，按 API 使用面逐批替换，禁止机械替换指针所有权。
- owned popup 的 owner 必须创建时一次性建立；不恢复 2025-02/03 的逐窗 SetWindowPos 修补。
- Setting 的 ImGui/DX11 资源仍在 Setting 生命周期内清理，普通窗口改造不能改变 device/swap-chain 所有权。
- 删除 HWND 前必须先 join 所有仍调用 ULW、D3D present 或命中检测的线程。

## 实施结果（2026-08-12）

- HiMsg 上游已提交 `363b9ca feat: add synthetic message queue controls`，主仓子模块固定到该 commit；合成触摸转单指消息通过 `Inkeys::Window::Enqueue` 原样入队。
- UI3 已成为唯一入口；UI2 与旧 `IdtWindow` 文件保留为工程 `None`，HiEasyX/EasyX 文件、库和工程引用已删除。
- `Inkeys.Graphics.Surface`、`Inkeys.Message`、`Inkeys.Window`、`Inkeys.Input.MouseHook`、`Inkeys.Business.ComponentActions` 已接入，Draw2 暂以 `DibSurface` 承载 HDC。
- ARM64 host MSBuild 完整 `Debug|ARM64 /m:1` 构建通过；`InkeysHeadlessTests.exe --no-window` 通过。
- 按用户限制未运行会创建 HWND 的 Window Tests，也未执行主程序、Setting 任务栏、Z 序或其他 GUI 手工回归；这些保留为允许 GUI 的后续验证门。
