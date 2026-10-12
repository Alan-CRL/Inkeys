# 规划 Ink Stroke Modeler 源码编译与库内终态重构

## Goal
规划在保留 Ink Stroke Modeler 原有优秀运动/预测算法的基础上，增加抬笔后的大窗口尾段重构；评估 runtime .cc 本地编译、lib 仅保留 Abseil 的路线，并说明明确可实现的工程边界、必须实验的技术难点与不能保证的效果。父任务后续算法仍研究/规划；阶段1已由用户批准，在子任务 modeler-source-build-baseline 实施。

## Background and Confirmed Decisions

- 历史大曲率即时 Up 末钩仍未解决，运动阶段可能外偏；局部仅 Up 新增段 cubic 已撤回。依据 `.trellis/spec/native/runtime-and-rendering.md:424` 和 `09-19-draw3-low-speed-stop-convergence` 的最新 PRD/research。
- 最新 `10-11-pen-taper-shape-and-stop-smoothing` 改的是派生笔锋，不能替代中心线修复。
- 用户已选择保留真实抬笔点，允许重构尚未落 L1 的 pre-Up 尾段。用户最新要求优先规划库内专用模块及源码/Abseil分离，而非只在产品库外计算。
- 原Update只追加Result；已有参数/SaveRestore不能自动利用未来Up回写大窗口。当前产品接纳几何与模型Result不一一对应，固化和显示/模型时钟归产品管理。
- 历史只读核查三个merge库，各165对象，13为Modeler定义、152为Abseil侧。用户提供完整原始CMake/源码后，阶段1先改为独立构建；最新用户授权后改用vcpkg20250512.1#0，不再手工导入lib或从merge移除对象。
- 历史关联、上游比对、合成几何图/数据与API调查均保留在research/，不将它们当真实模型或实体设备修复证明。

## Requirements

1. 保留历史、上游、生产路径结论与文件/行号证据，区分事实、推断和未验证项。
2. 规划保留正常Move/Predict核心行为及旧Update追加合同的库内终态计算能力；明确尾段替换范围和产品接入责任。
3. 保留真实Up及已落L1的前缀，允许未提交pre-Up派生尾部修改；完成、Stored及重放一致，不持久化prediction，不污染真实速度/压感或物理Up时间。
4. 核查源码/Abseil分离、头文件/CRT/架构及测试归属，提供最小迁移、基线验证和回退计划，不以升级依赖或混链绕过问题。
5. 列明原始轨迹匹配、短窗口、S弯/自交、真实角点、Up形变、Touch续接、宽度与性能的难度/把握，不能承诺无样本依据的普遍修复。
6. 阶段化规划与验收：源码行为基线、库内算法实验、产品完成/保存、物理Up与续接体验分别评估；数学/接口成功不等于笔迹效果成功。
7. 父任务保留后续算法规划；子任务阶段1仅实施同版依赖源码构建、工程接入及基线验收。后续终态模块必须依赖该子任务验收；不运行GUI，不commit/push。

## Acceptance Criteria

- [x] 历史/当前路径、上游与API结论已落盘，带可核查证据及限制。
- [x] 用户真实Up/L1/可变pre-Up取舍与库内源码方向已记录，design/implement反映最新范围。
- [x] 三架构库存、13运行源码、ABI/测试风险和独立源码构建计划明确；实施/编译结果归阶段1子任务。
- [x] 库内模块与显式替换接口、原算法保留、产品时间/压感/重连/存储职责已规划。
- [x] 高把握工程事项、待实验算法和不能普遍保证的效果分别说明。
- [x] 文档/JSON/上下文及Git范围检查完成，保留planning，无产品实现。

## Out of Scope This Turn

父任务本轮不实施终态算法/生产调参、升级依赖、GUI/Computer Use、远程issue/PR、commit/push。阶段1源码导入、vcpkg Abseil20250512.1#0、工程/规范及基线验证已明确授权并转交子任务；其他依赖不升级。
