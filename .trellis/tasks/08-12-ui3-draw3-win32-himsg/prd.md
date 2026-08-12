# UI3 / Draw3 接入准备

## Goal

以 UI3 作为唯一产品入口，去除 HiEasyX/EasyX，并建立可被未来 Draw3 直接复用的 Win32 窗口、消息和 HDC 兼容图像基础设施。Draw2 在本阶段继续负责画板，但不得再依赖 EasyX `IMAGE` 或自行维护旧窗口框架。

## Background

- 当前 UI2 与 UI3 由 `Experimental.Inkeys3.UI3` 分流，UI3 仍复用 `IdtFloating` 中的内置组件动作和鼠标 Hook。
- Mag、Freeze、Drawpad、Setting、PPT 与 Bar 由 HiEasyX 创建并以 owner 链组织；历史提交 `01d55874` 与 `649de99d` 已确认周期逐窗口重排会造成绘制卡顿或闪烁。
- Setting 当前错误地处于覆盖层 owner 链并带 `WS_EX_NOACTIVATE`；目标是有任务栏图标、可激活和可调整大小的普通窗口。
- Draw2 的 GDI+ 算法依赖 `IMAGE` 提供 HDC/像素存储，本阶段以项目自有 DIB Surface 承载，不重写绘制算法。

## Requirements

- R1：删除旧配置 `setlist.Experimental.Inkeys3.UI3` 的字段、读写和设置开关；升级写配置时清除遗留 JSON key；启动、设置同步、交互和退出固定走 UI3。
- R2：UI2 源码保留但退出编译；将 UI3 仍需的内置组件动作和低级鼠标 Hook 迁到非 UI2 模块，生产代码不再包含 UI2 头文件。
- R3：在 HiMsg 上游提供线程安全 `EnqueueMessage` 与 `ClearMessages`，保持成功读取即消费语义，补足过滤、容量、丢弃、并发和 shutdown 测试及 README，并形成独立 commit。
- R4：将 HiMsg 作为 `Inkeys/additional/HiMsg` Git submodule 固定到 R3 commit，复用主仓 vcpkg 的 `concurrentqueue` 与 `libcuckoo`。
- R5：新增 `Inkeys.Graphics.Surface`，以 top-down 32-bit BGRA DIB Section 提供 HDC、像素 span、深拷贝、noexcept 移动、强异常安全 resize、透明合成、缩放、比较、加载和 PNG 保存。
- R6：Draw2、撤销栈、Freeze、PPT 控件、Setting 图片加载和 Magnifier 背景迁离 EasyX `IMAGE`；完整删除 HiEasyX/EasyX 源码、头文件、库和工程引用。
- R7：新增 `Inkeys.Window` C++20 module，统一窗口角色、创建、线程归属、消息绑定、style、owner、显隐、边界、穿透、置顶刷新和停止生命周期；迁入并删除旧 `IdtWindow.cpp/.h` 编译入口。
- R8：一个 `std::jthread` 同线程拥有 Mag、Freeze、Drawpad、PPT、Bar、DisplayObserver；Setting 由独立 `std::jthread` 拥有；低级 Hook 使用受管 `std::jthread`。创建结果用 promise/future，stop callback 必须可靠唤醒消息泵。
- R9：窗口线程内执行 Create/Destroy、HiMsg bind/unbind、style/owner/显隐等状态操作；逐帧 ULW/D3D present 与明确需要 HWND 的外部 API 可作为跨线程例外。
- R10：覆盖层创建时建立 `Mag -> Freeze -> Drawpad -> PPT Controls -> UI3 Bar` owner 链，Mag 不可用时 Freeze 为根；周期置顶只操作链根，禁止逐窗口重排。
- R11：Setting owner 为 null，使用 `WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN` 与 `WS_EX_APPWINDOW`；禁止 topmost/layered/noactivate/toolwindow，设置大小图标和正常光标，保留关闭按钮隐藏复用语义。
- R12：所有渲染/交互线程必须在其使用的 HWND 销毁前停止并 join；覆盖层按 Bar、PPT、Drawpad、Freeze、Mag child/host 的逆序销毁。

## Acceptance Criteria

- [ ] 产品启动和所有交互路径仅使用 UI3，旧 UI3 开关及遗留 JSON key 不再存在。
- [ ] UI2 文件只作为工程 `None` 项展示，生产编译单元不 include UI2 头文件。
- [ ] HiMsg 新 API、README 与自动化测试在上游通过并有独立 commit；Inkeys 子模块固定该 commit。
- [ ] `DibSurface` 的创建、复制、移动、resize、合成、加载/保存和失败路径测试通过，GDI 压力循环后 handle 数不持续增长。
- [ ] 工程和生产代码不存在 HiEasyX/EasyX 编译项、include、`hiex::`、`IMAGE` 或 EasyX 链接。
- [ ] 各 HWND 的创建线程、style/ex-style 与 owner 链符合 R8-R11，停止后无遗留 HWND 或 jthread。
- [ ] Setting 有任务栏按钮和应用图标，可正常激活、聚焦、Alt+Tab、最小化和还原，关闭按钮仍隐藏复用。
- [ ] 完整 `InkeysRepo.sln` 使用 ARM64 host MSBuild 以 `Debug | ARM64`、`/m:1` 构建通过，并运行 Headless Tests。
- [ ] 手工回归 Draw2、PPT、Freeze、Mag、穿透、多显示器/DPI 与 Z 序压力，不出现闪烁、错误置顶或绘制停顿，HiMsg dropped count 为 0。

## Out of Scope

- 不接入 Draw3，不删除 Draw2；后续 Draw3 绑定 `WindowRole::Drawpad` 的既有 HWND。
- 不实施 Setting、Bar、PPT 的共享 D3D device 或统一串行渲染调度。
- 不把 PPT 控件整体迁入 UI3，也不改变 Bar dirty-present、动画和设备 epoch 合同。
- 不修改 `D:\Project\Inkeys\inkStrokeModelerTest\Inkeys3-Draw3` 原型仓库。
