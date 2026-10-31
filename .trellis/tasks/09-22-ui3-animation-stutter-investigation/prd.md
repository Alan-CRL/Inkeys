# UI3 主栏偶发动画卡顿与动态光影深度排查

## Goal

定位其他用户反馈的 UI3 主栏低概率卡顿、慢放与位置跳变，形成最小修复及验证方案；先修已确认的源级缺陷，再用故障记录确定剩余耗时来源。第一批 S1-S4 已于 2026-09-23 完成实现与无 GUI 验证，原始偶发故障保留现场验收项。

## Background

- 用户补充：重启后可能消失，关闭动态光影据称改善，同一次运行也可能一会快一会慢；没有稳定复现步骤。不能将“只能重启恢复”作为必要前提。
- 前序基线 ab023a17b78e01c9eb4317364492a93b470116db；本轮 bugfix/animation / 94e07b2599adab9de4286aa5fb7526e2c1c681f6，初始工作区干净。
- 调查阶段的真实动画 module 和独立探针证据见 research/validation.md；本轮按已批准范围实施，后续验证另记。

## Requirements

- R1 / 已调查：核实前序 DC、capacity、50ms时钟和通知清退避四项候选，给出当前代码证据、反证和可证伪条件。findings.md 是结论索引，专题材料保存在 research/。
- R2 / 第一批 S1：修复颜色块关闭时同帧重复描边目标，保留平滑几何与批次完成合同。证据：Bar.RenderLoop.cpp:3126,3131,4196,4250；真实 Animation module 第23帧归一描边4.117780523，单目标对照保持1。不得宣称普通关闭永不收敛。
- R3 / 第一批 S2：仅对本次共享光照变化真正产生可见 damage 的 scene 发出光照唤醒；旧光离开、强度/颜色改变、首次订阅和独立业务/恢复需求继续生效。证据：Bar.Scene.cpp:1893-1916；PageControl.cpp:955-978。影响以分页控件可见或退场为前提。
- R4 / 第一批 S3：Bar/PageControl 的只读 hdcSrc 释放准确描述无 GDI 修改，保留 COPY 与四阶段成功提交事务。证据：Bar.RenderLoop.cpp:12576-12596、PageControl.cpp:1037-1055。该修正不以本机性能收益作为承诺。
- R5 / 第一批 S4：用低开销、限频诊断区分光影绘制/新建遮罩、多客户端耗时、呈现失败、原始/实际dt和成功提交间隔，支持无法稳定复现的故障判因。
- R6 / 调查证据：记录 exact 整图缓存拒绝当前平移以及135/375分片调用，时钟/退避丢时间、capacity与viewport集成风险等；它们不自动扩张第一批实现范围。证据：Bar.Rendering.cpp:1759-1764,1977-2014；Bar.RenderLoop.cpp:968,12931-12933,9172,9670。
- R7 / 授权与兼容：用户批准第一批之前只写本任务Trellis调查材料，保持planning，不运行task.py start；不修改产品/既有测试/工程/spec，不启动GUI，不commit/push。获批后保持原编码/CRLF、minimal diff、中文关键注释及现有光照质量/动画速度。

## Acceptance Criteria

- [x] AC1 / R1,R6：调查结论包含源码锚点、触发条件、实验结果、替代解释与未确认边界；不将调用计数或合成模型冒充实机帧耗时。
- [x] AC2 / R1,R2,R6：前序四候选已复核；真实动画模块、生产几何/退避helper、原样分片函数以及无HWND D2D API探针已执行；现有headless二进制--no-window退出0。
- [x] AC3 / R7：prd/design/implement与真实research上下文清单已准备，用户已批准第一批，task.py start 已完成。
- [x] AC4 / R2：获批修复后，关闭/展开/反向及不均匀正dt下，颜色块描边与面板缩放同步；不会依靠逐帧SetDirect或关闭光影隐藏问题。
- [x] AC5 / R3：无交集光照变化不产生额外scene唤醒；旧光擦除、同区域强度变化、主光变化、首次订阅、布局/显隐和恢复请求不丢失。证据为生产调用链静态审查、现有真实脏区算法回归和完整构建；没有完整Scene hooks的运行计数覆盖。
- [x] AC6 / R4：GetDC/ReleaseDC配对及COPY保持，Bar/PageControl成功呈现事务和错误结果保持。
- [x] AC7 / R5：异常记录能区分回调、推进、尝试和成功次数，并包含阶段耗时/资源尺寸/错误；正常热路径不逐packet或逐frame写日志。
- [x] AC8 / R2-R7：获批后针对性测试与 InkeysRepo.sln Debug|ARM64 完整构建通过；ARM64原生MSBuild、同PowerShell PATH规范化、至少5分钟构建时限；GUI验收仍由用户另行授权。
- [ ] AC9 / 最终故障验收：后续用户场景证据或反馈确认原始低概率问题改善/消失；未取得前不宣称唯一根因已证或整个问题已彻底解决。

## Out of Scope

第一批不修改全局时钟/弹簧/退避合同、不做target缩容或改换backend、不直接放宽exact矩阵gate、不泛化PageControl呈现门，不处理独立seqlock与零dt语义风险。D1-D3后续方向见design.md，需依据新增证据另行明确范围。无关重构、降低品质、GUI操作、自动commit/push始终不在授权内。

## Evidence Limits

技术未知项是现场主导阶段、分页控件可见性和真实Windows ULW对边界模型的反应；均以条件化结论记录，不阻塞已确认S1-S4的规划。第一批修复可验收源级行为与诊断能力，不能以构建通过替代用户偶发故障验收。

## 第一批交付

详见 [verification/first-batch-result.md](verification/first-batch-result.md)：完整ARM64构建与新headless二进制测试退出0；未GUI、未commit/push。AC9仍待故障用户反馈/日志。
