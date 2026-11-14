# 拖动任务收尾：旧实现清理与静态可证优化（2026-11-13）

## 授权与行为边界

用户已明确确认当前电脑惯性合适，要求清理无用代码、实施静态可确认的性能优化，验证后归档任务并提交 commit；不 push。该授权覆盖当前两个相连拖动任务的收尾，替代之前“只读/不提交/不归档”的阶段约束。

保留当前可靠尾段估速、15 DIP 起步、向量摩擦 tau=.30/c=1000、12000 模长上限、残余投影 boost、四类输入偏好及 hover 不制动规则。尺度适配是下一个独立 Trellis 任务，本轮只保留交接资料，不创建或实现该新任务。触控板与双后端规划继续延期，不解除 quarantine。

## 代码所有权与最小范围

- 实施子代理负责 `Draw3.CanvasNavigation.cpp/.cppm` 与 `InkeysHeadlessTests/draw3_canvas_pan_tests.cpp`：删除只有参考测试消费的旧 LSQ2 tracker、PhysicsBased/Legacy 曲线、贝塞尔求解与对应常量/分支；清理随之无用的参考接口、状态和测试。保留有产品用途的检测/booster/摩擦内核及其断言，不按旧数值重写当前手感。
- 搜索证实旧曲线 API 在产品/Headless 中仅上述三文件引用；独立 `inkStrokeModelerTest` demo 有自己的模块副本，本轮不动。
- 如纯测试/未接产品的 history seed 或可选估速分支可被无歧义删去，同步移除相关接口和测试；不要改已工作的生产输入环、设备仲裁、控制器、设置、Freeze 或呈现树。
- 性能优化优先：当前 Controller 已按同一 QPC/phase 稳定排序，Dynamics 每帧仍复制 contacts 并 stable_sort。对已排序 span 直接只读遍历；只有乱序时才分配副本稳定排序。相同 QPC/phase 保持原有稳定次序。此项省去有序生产输入的临时分配、复制和重复排序，不声称动态毫秒或功耗收益。
- 主会话负责文档/spec、构建/运行、基线对比、commit 与归档；check 子代理独立质量审查。

## 验证与验收

- [x] 清理前冻结生产 pan-feel 基线；末行计数2112，实际2240条scenario（包含附加设备场景），全量逐行比较。
- [x] 删除引用闭环，接口/实现/相关测试一致；保留原编码/BOM/CRLF和关键中文注释。
- [x] 针对有序/乱序与同 QPC 稳定次序保持等价，增加必要的真实内核测试，不复制算法充当 oracle。
- [x] 原生 ARM64 MSBuild 动态定位；同一 PowerShell 先规范 Path/PATH、禁 node reuse；完整 InkeysRepo.sln Debug|ARM64，至少5分钟（本轮1800秒）预算。
- [x] Headless pan、control-fence、pan-feel、配置边界/设置、诊断及完整 --no-window 通过；pan-feel 全部实际2240条scenario数值与清理前一致（末行汇总2112）。
- [x] 独立 trellis-check；更新当前 spec，旧曲线只留 Git/历史任务证据。
- [x] 将用户当前机手感确认、其它真机/多系统/纹理性能未验边界和未来路线写入收尾记录，完成两项相关任务归档及 commit，不 push。

本轮不启动主程序或任何交互GUI，不通过更改SDK/工程/工具链或关闭签名绕过环境问题。没有计时基准，性能收益仅以去掉的确定性工作描述。
