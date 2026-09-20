# Implementation Plan

1. 读取 native desktop 规范并审计现有 `IdtD2DPreparation`、`RenderScheduler`、Bar/PPT/Setting、ImGui backend、RC、vcxproj 和 headless tests 的所有权与引用。
2. 新建 `Inkeys.UI.RenderPipeline`，迁入共享图形资产、backend/generation 恢复、客户端注册/请求、唯一线程与 60 FPS 调度；扩展无窗口调度测试。
3. 将 Bar 与五个 PPT 客户端迁移到 `FrameContext`，移除 Bar 对调度长循环的线程承载，验证按位唤醒、固定顺序、局部 Retry 和 device lost 恢复。
4. 将 ImGui HLSL/CSO 移至 RenderPipeline 资产目录，更新 RC/vcxproj/filter/backend 引用并静态确认不存在运行时 `D3DCompile`。
5. 将 Setting 拆为窗口线程接口、渲染线程持久 session/单帧回调和 FIFO 业务 worker；用共享 WARP device 创建独立 discard swap chain/RTV，替换 `test.select` 显隐入口。
6. 调整初始化/退出次序并同步注销全部客户端；删除 `IdtD2DPreparation.*`、旧 RenderScheduler 工程项和旧图形全局引用。
7. 增加 Setting 纯状态测试和无 HWND WARP 资产检查，完成静态兼容审计。
8. 使用 ARM64 host MSBuild 构建完整 `InkeysRepo.sln` 的 `Debug | ARM64`；仅运行 `InkeysHeadlessTests.exe --no-window`、`git diff --check` 与静态引用审计。
9. 运行 `trellis-check`，修复范围内问题；按需更新 native desktop spec，记录 Win7 仅静态审计且未做 GUI 验收。

## Risk And Rollback Points

- RenderPipeline 线程/唤醒修改后先通过调度单元测试，再迁移客户端。
- Setting 接入前保留旧实现可对照；确认单帧路径覆盖退出与隐藏后再删除长循环。
- RC/Shader 路径迁移后立即做资源 ID 和工程引用静态审计。
- 设备恢复必须只由渲染线程发布 generation，发现其他发布者时停止迁移并统一入口。

## Verification Commands

```powershell
# 使用本机 ARM64 Host 的 MSBuild.exe，路径在执行前只读定位
MSBuild.exe InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64
.\ARM64\Debug\InkeysHeadlessTests.exe --no-window
git diff --check
rg -n "IdtD2DPreparation|d3dDevice_UI3|d2dDevice_UI3|Inkeys.UI.RenderScheduler" Inkeys
rg -n "D3DCompile|D3DCompileFromFile" Inkeys
```
