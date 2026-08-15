# 实施计划

## Phase 0：基线与任务

- [x] 确认前置显示任务进入 HEAD `47acefc5`，Headless 测试和完整 ARM64 Debug Solution 构建通过。
- [x] 创建子任务并记录需求、设计、研究与上下文。
- [x] `task.py validate` 通过后启动本任务。

## Phase 1：纯逻辑与测试

- [x] 新增 `Bar.BottomDock.h`，实现 dock 线、边框对齐、启动居中、x 夹取、迟滞状态机、纵向映射和二阶弹簧。
- [x] 新增 `bar_bottom_dock_tests.cpp`，先覆盖 workArea、zoom、上下吸附/脱离、快速跨阈值、x 反向、弹簧收敛和刚性/形变映射。
- [x] 在 Headless 工程与 filters 中登记新测试，确认纯测试编译运行。

## Phase 2：显示、状态与启动布局

- [x] 将 workArea 四边与 bounds/DPI 一起发布到 Bar，并在显示 serial 上原子消费。
- [x] 增加底栏稳态、拖动快照和渲染弹簧状态；默认 docked、主栏展开。
- [x] 在完整主栏布局后按可见主体居中并精确对齐可见 stroke 底端；折叠保持 dock，底栏固定右展和上弹层。

## Phase 3：拖动与渲染接入

- [x] 将鼠标/触摸统一为连续绝对屏幕采样；把 `Seek` 改为结构化结果并实现无跳变捕获、脱离和重基准。
- [x] 保持 x 轴独立直移、主按钮可见夹取和 ULW desired/presented translation 协议。
- [x] 对主体应用纵向 group transform；主按钮背景和主图标共同形变，次级面板单独刚性绘制，并同步命中、边框光效与阴影。
- [x] 为弹性阶段加入一次性 capacity 包络和局部 dirty union；收敛后停止续帧。

## Phase 4：验证

- [x] 运行底栏专项及全部 `InkeysHeadlessTests.exe --no-window`。
- [x] 运行 `git diff --check`，检查新旧文件编码、CRLF、工程与 filters 一致性。
- [x] 使用 ARM64 host MSBuild 构建完整 `InkeysRepo.sln /m:1 /p:Configuration=Debug /p:Platform=ARM64`，超时至少 5 分钟。
- [x] 审查最终 diff，不包含现有 `Inkeys/PptCOM.dll`，不启动 GUI、不提交、不推送。

## 验证结果（2026-08-14）

- `python ./.trellis/scripts/task.py validate 08-14-ui3-bar-bottom-dock-elastic`：通过；大型规范文件仅有注入截断警告。
- `InkeysHeadlessTests.exe --no-window`：通过，输出 `PASS animation correctness`。
- ARM64 host MSBuild 完整构建 `InkeysRepo.sln` `Debug | ARM64`：通过；仅保留仓库既有转换警告。
- 未启动可见窗口；真实任务栏组合与果冻手感仍需维护者手工验收。

## 回滚点

- 纯逻辑测试失败时仅回滚 `Bar.BottomDock.h` 与专项测试，不触及当前拖动路径。
- 输入接入失败时保留纯 helper，恢复 `Seek` 调用合同与现有直移吸收协议。
- 渲染形变导致 dirty/capacity 回归时先关闭视觉 transform 接入，底栏稳态定位仍可独立验证。
