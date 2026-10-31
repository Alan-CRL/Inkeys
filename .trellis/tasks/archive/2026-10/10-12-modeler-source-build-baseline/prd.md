# 阶段1：vcpkg Abseil 与 Modeler 源码基线

## Goal
将 Ink Stroke Modeler 从合并预编译库迁移为源码参与现有 MSBuild 编译，使用 vcpkg 固定 Abseil 20250512.1#0；以旧库独立对照验证行为与性能，为后续库内尾段重构提供源码基础。

## Background and authorization
父任务：10-12-draw3-curved-pen-up-research。2026-10-12 用户批准完整阶段1实施，随后明确授权：若 vcpkg 20250512.1#0 可行且维护更方便，停止手工 lib 导入并改用 vcpkg。源码/ABI约束与验收门仍生效。
历史大曲率即时 Up 末钩留给后续算法子任务，阶段1不宣称解决末钩。
原三架构 merge 和四配置旧程序/回放已保存；手工20250512.0构建仅作为历史证据，生产路线不继续采用。

## Requirements
R1. 根 manifest 精确固定 Abseil 20250512.1#0，保持原 builtin-baseline 和其他依赖版本。静态 /MT、NDEBUG、C++20、iterator0、lts_20250512、std string_view/ordering ABI一致。
R2. 复用现有 vcpkg/MSBuild 和三个 static-v143 triplet；仅 Abseil 指定 C++20。三消费者使用同一 vcpkg 包的头与 Release 静态库，不能混用保留的 .0 vendor头。
R3. 导入原13个 runtime cc，三个消费者共享清单与 canonical Modeler头；公共API、算法、参数及产品几何接纳逻辑不改变。numbers.h兼容处理保留，排除GMock。
R4. 仅13个Modeler TU保持旧MinSizeRel /O1 /Ob1及浮点/EH/RTTI条件，避免继承Debug /Od；业务优化与Headless /MTd不改。Abseil允许vcpkg标准Release优化，但必须经过数值和性能验证。
R5. 保留旧库独立对照，同输入与参数比较状态、计数、所有输出字段、Predict非修改性及Save/Restore。仅依赖版本元数据 .0→.1 是声明的预期差异；其余必须精确一致，不能放宽容差。
R6. 30轮交替成对无窗口性能测试；超出噪声且可重复的退化不验收。构建/链接/来源/ABI/导入表证据持久化。
R7. 最小diff，保留编码/换行、中文关键注释；不运行GUI，不commit/push。不升级SDK/工具集或其他依赖。

## Acceptance Criteria
- [x] AC1: 三架构 vcpkg 20250512.1#0 来源、实际flags、headers/options、hash/machine/CRT/namespace核验，无Modeler定义。
- [x] AC2: 仅13个runtime cc导入，三目标共享且无旧merge混链；active Abseil头与库来自同一包，旧头/库保留回退。
- [x] AC3: 旧/新回放全部语义字段精确一致，边界覆盖完整；依赖版本差异明示，其他差异/失败不能隐藏。
- [x] AC4: 30轮交替Move/Predict/Up/Reset测量，无可重复且超噪声的退化。
- [x] AC5: InkeysRepo Debug ARM64完整Rebuild、demo/test solution、ARM64 Release、x64/Win32 Release及无窗口回放；Headless --no-window和diff检查完成。环境失败单独记录。
- [x] AC6: provenance/规范/父子依赖同步，旧库hash不变；GUI/Win7真机明确未验证。

## Dependencies and scope
后续终态模块、产品尾段接入及续接实验必须依赖本阶段行为/性能验收；仅源码接入不能解除门禁。
尾段算法、spring/Kalman调参、渲染/输入生命周期改写、其他依赖升级、GUI、远程修改、commit/push均不在本阶段。
