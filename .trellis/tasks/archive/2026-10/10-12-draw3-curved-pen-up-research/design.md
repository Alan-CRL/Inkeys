# Design: 保留原算法的库内终态尾段重构

## Status and Decisions

2026-10-12，父任务协调分阶段交付，阶段2已获用户批准，独立模块和离线验收现已通过；产品接入仍规划。用户选择保留真实 Up、允许重构未落入 L1 的 pre-Up 尾段；最新要求是让计算成为 Ink Stroke Modeler 的专门模块，评估 runtime .cc 源编和 Abseil 单独静态链接，并明确没有把握的难点。阶段1已验收提交c34d3a56；阶段2独立终态模块与离线验收由子任务 modeler-terminal-tail-reconstruction 执行，产品接入和Touch续接留后续阶段。

本文件替代上一轮“产品库外计算”为首选的架构；原有几何分析、边界和合成例子的限制继续有效。

## Architecture

保留原版 Down/Move/Predict/Update(kUp) 的前向数学流程，阶段2新增库内独立 terminal_tail_reconstructor，已有显式API与离线验收；产品接入仍规划。模块根据原始输入尾部、已接纳的平滑参考窗口、固定入口和真实 Up，返回显式 TailPatch；Draw3 校验并应用替换，统一完成绘制与 Stored。

```text
原 StrokeModeler -> 原平滑参考、活动笔迹
真实 Up + 有界 raw + 可变 reference + 固定入口
                     |
库内 TerminalTailReconstructor -> TailPatch
                     |
Draw3 冻结/组合 -> 共用完成几何 -> L2 / Stored / 重放
```

第一版推荐独立无状态求解器，可复用 scratch，不改变 StrokeModeler 布局或 Save/Restore。计算归库；窗口、L1、压感、显示时间、重连和文档归产品。以后可加类方法 facade，但不要让旧 Update 隐式重写已输出结果。

## Why Original Up Cannot Do This Directly

原 Up 只从最后锚点继续积分，末端迭代没有 pre-Up 大窗口。增强终点拉力或改步长仍只作用新增后缀。Kalman 加速度权重只影响预测，同参数 Save/Restore 不使未来终点反向影响旧 Move。

已有 wobble/stylus deque 会淘汰历史，Project 还向前消费投影段，不能直接借它回拟任意旧窗口。新模块需要独立 raw 尾部；可以复用基本几何函数和原模型形状作为先验，不无差别重复平滑原输出。

## Source and Abseil Build

只读三架构库存已确认：每个 merge.lib 含165个COFF对象，其中13个定义Modeler，剩余152个属于Abseil侧且没有Modeler定义。运行源码确切13个，排除依赖GMock的 internal/type_matchers.cc。清单/成员/hash见 research/modeler-source-build-and-abseil-split.md。

阶段1依用户最新授权改用vcpkg固定Abseil20250512.1#0，复用现有static-v143 triplet，指定C++20并配套使用包内头/Release静态库；不再导入手工Abseil lib，不从旧archive移除对象。原merge保留独立控制组/回退，迁移目标不得混链。权威路线见子任务research/vcpkg-provider.md；原standalone记录保留历史证据。

runtime cc进入唯一的源码目录，复用现有MSBuild直接登记ClCompile与filters；多消费者时使用显式共享清单，不新建构建系统。统一每个目标的头文件来源，保留本地numbers.h兼容处理，核对PCH、C++20、obj名、架构与Debug/Release。

保留 /MT、NDEBUG、iterator level 0、lts_20250512与std别名ABI；Abseil仅按授权从.0切到vcpkg.1匹配头/库，其他依赖版本与工具集不变，不切整工程MTd。InkeysHeadlessTests Debug目前MTd，不适合直接混链该Abseil；算法回放使用ABI匹配的既有真实模块测试/无窗口生产probe。

源码迁移会有意改变当前“第三方仅头文件、固定merge库”的规范。实施时必须同步README/provenance和集成合同；不能仅加cc而保留旧pragma/默认库混链。旧库对照程序已保存，原手工库构建记录保留；当前vcpkg三架构provider、四配置回放及各30paired性能验收已通过；覆盖范围内位级一致，GUI/实体设备/Win7真机仍未验证。

## TailPatch Contract

概念接口：ReconstructTerminalTail(request, reusableScratch) -> outcome。

输入包括：实际消费raw位置/物理时间/来源、实际接纳reference及caller source id、不可改连接点/入射方向、真实Up和预算。容差使用一致单位，库不认识DIP/L1/DirectX。

输出包括：Apply/NoChange/Rejected与原因、明确替换范围、修正位置及有序来源映射。有误或无改善时不部分改调用方数组。连接点可共享但坐标原值不动；所有修改点在固定边界之后。

reference以接纳几何为准：realPoints可能包含gate/pin/fallback，不与modeledResults一一对应。人工Up连接不应作为强曲率先验。物理/model/display三时间轴不能靠被钳制的点time互相推断。

补丁不伪装普通Result，不再送旧“整个窗口到Up距离单调”的门禁，否则合法S弯/loops会被裁坏。原Up门禁保留作安全基线，新补丁检查固定边界、精确endpoint、finite、有序分支、位移/弧长/自交等。

修改后的几何位置不再对应原Result的velocity/acceleration。首版只返回位置及来源映射，Draw3转移已确认基础半径/time，不二次推进widthEstimator，不从视觉速度模拟压力或反馈模型。

## Geometry and Window

原平滑参考 m_i，修正 q_i = m_i + d_i。候选目标：保持模型形状、接近有序raw、平滑修正位移场、可信末方向软约束。固定入口位置/可靠入射方向，endpoint硬约束为raw Up；不用巨大有限权重近似。

正则化针对 d 的变化，避免把 q 的真实曲率一律压平。使用弧长/非均匀网格，匹配限制在当前有序分支；不能同时间硬对齐有滞后的model，也不能自交时选任意最近段。圆弧自然内向修正，不假设统一圆心。

先比较端点驱动平滑位移（低复杂度对照）与raw约束加权重构（主要候选）。算法、权重、节点和预算待真实回放，不新增求解依赖、不复制玩具权重。

只能在未L1窗口内扩展。raw缓存更长不等于几何可改范围更大；普通Pen与HardPen保护区不同。若空间不足，需另行规划与taper解耦的重构保护时间/弧长/点数，在运动时提前延后L1，不能Up后改已固化前缀。

修正量相对窗口太大时，固定入口、精确终点与低曲率可能不可兼得。失败回原endpoint-safe路径并记录未改善；截掉异常段后也应构造到真实Up的替代段，不以截短作为默认成功。

## Physical Up and Reconnect

Pen/Mouse通常直接完成；Touch可续接时物理Up可能先送kMove，直到超时/淘汰才真实kUp。仅挂ProcessUpEvent不能称所有设备立即Up修复。

先验证不可续接完成边界与deferred完成的共用helper。仅最终完成处理的版本在等待期间没有立即改善，必须报告延迟，不能当完整产品验收。

若需物理Up立即改善，生成可撤销preview patch：等待时不提交可变区，baseline/model不改；真正完成冻结同一几何，reconnect丢preview并继续原模型。preview回baseline的可见跳变需验证，可能需要过渡/运动期机制。补丁不改变原续接速度/方向证据。

Cancelled、新Down、对象池、页面边界、多contact清各自缓存/版本；物理Up显示锁不被未来模型time或等待超时覆盖。

## Width, Rendering and Persistence

首画、Stored、保存/重放共用冻结补丁；不能只移动L0。从未taper基础半径和来源映射转移属性，再使用共享笔锋/胶囊规则；真实Up时间与稳定共享点保留。强半径梯度、末端压力和弧长改变后笔锋比例需回归。

旧/新bounds同时清除重绘。未来若实时修正，L1也必须提交修正真值，不能下一帧跳回旧参考。

## Confidence and Unknowns

| 事项 | 当前判断 | 下一证据 |
|---|---|---|
| 13源码与vcpkg Abseil | 源码/provider三架构及基线验收PASS | 包ABI、链接来源、三架构 |
| 原Update合同及显式patch | 阶段2四配置原API bit exact、96窗硬边界/失败原子通过 | 真实产品原子应用与来源属性转移 |
| L1不动、endpoint一致 | 可作为硬合同验收 | 输出范围和端点断言 |
| 新源码等同旧binary行为 | 19case/128412assert覆盖范围内bit exact | 真实设备/GUI及后续算法样本 |
| 圆/快转立即Up末钩改善 | 阶段2两合成轨迹的12窗口记录达独立物理改善，真设备未验证 | 实际产品接纳/实体设备/幅度与抽动 |
| raw匹配、S弯/自交/真实角点 | 算法主要难点，没有普遍保证 | 有序正负样本矩阵 |
| 短窗口零折弯、零形变 | 部分边界下不可兼得 | 空间/失败率、保护窗口决策 |
| Up无抽动、reconnect无跳变 | 当前没有把握承诺 | preview/final/恢复视觉证据 |
| 压感/Stored一致、成本 | 工程可约束，均未验证 | 生产probe与性能回归 |

能给出可测试的工程、模块和回退，不在没有真实样本时承诺所有笔迹自然、所有设备无跳变或没有任何形状权衡。

## Evidence

research/modeler-source-build-and-abseil-split.md：三架构库存、13清单、ABI、工程与官方工具。
research/modeler-terminal-reconstruction-api.md：协议、缓存、时钟、重连、宽度和门禁。
research/local-history-and-current-path.md、upstream-status-and-diff.md：历史、现状及原版能力。
research/tail-refit-toy.json/.png：先前合成例子只证明一组数学边界可行，未证明库迁移或真实修复。

阶段1的生成lib、导入cc、project/spec、编译与无窗口回放由子任务记录；父任务不据规划宣称完成。先证明源码基线，再验证算法收益。
