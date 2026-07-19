# RTS 多接触绘制执行计划

## 1. 输入核心

- [x] 新增 `draw3.contact_input` 模块和实现，定义枚举、值类型、`IdtAtomic<T>`、不可复制的 `ContactRecord` 与 generation handle。
- [x] 实现 32-slot 稳定地址分块池、无锁热路径扫描、奇偶 sequence 一致快照及 Down/Move/Up/Cancelled 状态机。
- [x] 接入 concurrentqueue 1.0.4 `BlockingConcurrentQueue`，配置 `MAX_SEMA_SPINS=0`、256 项预分配、Down 可靠入队和合并 `ControlWake`。
- [x] 为消费方提供排空/阻塞、快照读取、slot 回收和生产者批量关闭接口。

## 2. RTS 适配

- [x] 新增 `draw3.realtime_stylus` RAII 模块，完成 MTA COM、RTS 多点启用、同步插件注册和严格关闭顺序。
- [x] 仅请求 X/Y，缓存每个 tcid 的 packet 属性索引、诊断比例和设备类型；坐标统一沿用首 context scale，Down/Up 解析完整 packet，Packets 只解析批次最后一个 packet。
- [x] 将 Touch/Pen/MouseLeft/MouseRight 映射为 0–3，回调热路径不分配、不查询 COM、不做逐包日志。
- [x] 初始化或多点接口失败时明确报告并退出，不启用旧输入回退。

## 3. 窗口控制唤醒

- [x] 将 coordinator/wake 接口安全连接到 `WindowController`。
- [x] resize、clear、full-present、DWM 和 exit 在发布已有原子请求后投递合并控制唤醒。
- [x] 保留未使用的旧鼠标 poll API，避免无关清理。

## 4. 多 contact 模型与渲染

- [x] 把单笔 `ActiveMouseStroke` 抽象为可 Reset、保留容量的 `ActiveStroke`，预热 16 个模型对象。
- [x] 实现 `DrawingController::Run()`：可靠消费初始 Down，读取一致快照，按真实序列/QPC 计算速度，Move 覆盖，Up 作为最终 `kUp`。
- [x] 调整 L0 helper：每帧共享 L0 只清一次，然后绘制所有活动 contact 的尾部和预测。
- [x] 无 Up 时把所有稳定增量合入共享 L1；有 Up 时把同帧完整笔画合并为一次 L2 resolve。
- [x] L2 提交后从 CPU 状态重建剩余 contact 的 L1/L0，批量 dirty rect，并保证每帧一次 backbuffer 合成和一次 Present。
- [x] resize 后保留 L2 并重建活动临时层；clear 延迟到全部 contact 结束。
- [x] L2 成功提交后才回收 ended slot/model；Cancelled 也能安全结束。
- [x] 接入空闲时 1/2/3 工具选择；首个 Down 锁定整批工具，普通笔 5px 模拟压感，荧光笔/橡皮 50px 固定宽度。
- [x] 修复 Up 帧末端折返/双束：直接烘干最后可见 L0，不再用 `kUp` 平滑结果重连终态尾部。

## 5. 主程序、计时和工程注册

- [x] 在 `main.cpp` 构造 coordinator、窗口唤醒关联、RTS RAII 与新绘制入口，并处理初始化失败和关闭次序。
- [x] 活动期按 `target_fps` 连续绘制；完全空闲时二次排空后 `wait_dequeue`。
- [x] 首/末活动 contact 配对 `timeBeginPeriod(1)` / `timeEndPeriod(1)`；Release 禁用逐帧日志，Debug 限频。
- [x] 在 `.vcxproj` 和 `.filters` 注册新模块/实现；不改 vcpkg manifest、triplet、install 路径及未跟踪 `Vcpkg/`。
- [x] 保持既有 UTF-8 BOM、CRLF，并为状态发布、提交/重建等关键步骤添加简短中文注释。

## 6. Quality gates

- [x] 检查 `git diff --check`、源码编码/换行、vcpkg 配置未变和 `Vcpkg/` 未被触碰。
- [x] 使用 ARM64 MSBuild 对完整解决方案执行：

  ```powershell
  & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' `
    '.\inkStrokeModelerTest.sln' /t:Rebuild /m `
    /p:Configuration=Release /p:Platform=ARM64 /verbosity:minimal
  ```

- [x] 确认 C++ modules、两个 Shader、资源嵌入与最终链接均成功；超时不少于 10 分钟。
- [x] 由 `trellis-check` 进行全范围规格、并发状态机、生命周期、渲染分层和构建复核，修复后重复质量门。
- [ ] 人工验证 PRD 中鼠标/单指/多指、快速 Up、同帧多 Up、resize/clear、空闲阻塞与控制唤醒场景；无可用触摸硬件的项目必须明确标记为待实机验证，不能伪报通过。

## Rollback points

- 输入核心和 RTS 模块在接入 `main.cpp` 前可独立编译修正。
- 渲染批处理若出现视觉回退，先恢复严格的 CPU 几何重建和一次性提交，不通过把活动笔画提前写入 L2 来绕过。
- 任何失败均不得修改/重置用户的未跟踪 `Vcpkg/` 或其他无关工作树内容。
