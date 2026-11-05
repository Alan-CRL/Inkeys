# 执行计划

状态：用户已于2026-11-05批准进入执行，具体合同见prd.md。先采样/分析，再直接做低风险优化；高风险候选保留确认。

## 顺序

1. [x] 读取最新用户授权和架构/设备/流畅度边界，更新规划与上下文。
2. [x] 固定源码/EXE/PDB身份；先复用现有ARM64工具构建InkeysRepo.sln的Debug|ARM64，保存before日志。ARM64 MSBuild动态定位，同一次PowerShell规范化PATH/禁用node reuse，至少允许5分钟。
3. [x] 采集无窗口history基线与适当离屏/字体资源证据；允许自动启动本任务窗口采样，但不使用Computer Use，锁屏时不依赖交互。
4. [x] 对可自动化真实启动记录Private/WS/线程/模块与回落；按需要做短时WPR/ETL并用CLI分析。环境问题留证，继续不受影响工作。
5. [x] 选取有明确所有权/副本证据且不把大工作移入热路径的低风险改动，限定文件，由trellis-implement直接实施；不得扩大到层架构/设备/后端或高风险生命周期重建。
6. [x] 用trellis-check子代理审核并自修实现、格式、规格和必要测试；按同一solution/config完成构建与适当测试，保存after数据，明确局部与全产品证据差异。
7. [x] 静态确认流畅度与L0/L1/L2、UI共享WARP/Draw3独立hardware-first边界，记录高风险待确认清单。
8. [x] 更新任务结果、相关规格和会话记录，完成Git范围检查；不commit/push。

## 执行约束

- 用户挂机，允许本任务启动相关窗口和非Computer Use控制，优先无窗口/离屏；不绕过锁屏、不打开Office或用户文档。
- 验证超时、退出码及第一个有效错误须记录；不把环境错误当源码错误修复。
- 采集前确认WPR状态，仅停止自有会话，恢复本任务修改的诊断配置。
- 保留产品原编码/BOM/CRLF、关键中文注释，最小diff。不会新增镜像实现或新的构建系统。
- 避免大纹理/字体/图片在输入、绘制、Undo/切页、频繁显示热路径重新分配；这类候选不在低风险自动实施范围。
- 规格JSONL仅注入不截断的资料；执行者按目标文件阅读native-desktop规格的完整相关章节。

## 既有研究

- research/tooling-inventory.md：工具与Codex权限限制、已有二进制。
- research/draw3-memory-map.md：全屏纹理、历史缓存/文档及保存快照。
- research/ui-memory-map.md：Setting、字体、Freeze、共享WARP、PptCOM。
- research/memory-overview.md：跨模块结构与候选优先级。

## 第二批已批准的执行边界

以research/batch2-boundary.md为本批权威范围：先保留510.16MiB批次基线，分别实施2图片恢复、3Laser生命周期、4辅助ULW、5Freeze/白板/隐藏UI回收；1/6后续。用户已批准，直接分派实现/检查，主会话统一构建和采样。

## 候选1释放调查（2026-11-05）

1. 审计Desktop/PPT请求、worker和回执终态；独立审计capture/export/import及白板编码临时副本。
2. 复用既有CPU/离屏测试，必要时添加最小诊断接缝观察真实大payload生命周期。
3. 分别解释存活载荷与分配器/OS高水位，记录成功、失败、覆盖、停止等证据边界。
4. 输出是否能正常释放、仍存风险及是否值得后续优化的结论；候选6不修改。
