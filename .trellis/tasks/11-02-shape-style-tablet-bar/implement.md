<!-- 当前阶段：Touch注册/Pointer来源诊断补齐，用户已授权且实现与自动验证完成；见touch-registration-diagnostics-plan.md、touch-registration-validation.md、touch-registration-capture.md。下文保留已完成历史阶段。 -->
# 实施与验证计划（用户已批准）

## 范围

只实施 PRD R1/R3-R7 的共享样式；R2 数位板点击保持暂缓，不改输入过滤。一个实施目标即可，不为暂缓项创建实施子任务。代码保持原 BOM/编码/CRLF，只改需求必需部分，关键边界加中文注释。

## 顺序

1. 读取 curated specs/research，并按需读取draw3-integration.md中Host/桥接章节（整文件超过注入上限，不放清单），记录工作区基线；确认本机工程为 InkeysRepo.sln/MSBuild。禁止单独构建 Inkeys.vcxproj。
2. 在现有业务状态锁下建立权威样式：Brush 三范围颜色，Highlighter/Laser 各自全局颜色，三组各自全局 Selection。StateMode 保留值型快照与工作区工具身份，恢复/committed getter 投影最新权威样式。
3. 统一形状 getter/setter、初始化与 Selection 路由到 Brush；移除已编译路径对形状独立槽的依赖并更新相关比较器/旧测试。历史 None 源码不顺手改写或重新启用。
4. 实现公共 RGB 浅深分类（沿现有299/587/114与128000阈值，等值归深色），按实际已接纳白板主题选 Brush 色。覆盖首次进入、主题、实际页切换和冷加载成功；失败/stale不错误推进范围。
5. 依 design.md 同步合同完成主题/页身份与样式接纳，确保旧样式不得用于新主题首笔，UI与绘制使用同源结果。沿现有线程/锁顺序，不在Host回调持锁时同步调用重型业务/窗口；不得仅轮询UI或仅挂themeEdited。
6. 调整现有保存快照：从权威状态组合 Primary Brush色、全局Highlighter/Laser色和宽度，防工作区旧副本反写；保持已有codec/未知schema/原子保存和启动恢复停用。不新增用户未要求的启动恢复或白板颜色跨进程schema。
7. 更新真实生产测试及pen-width.md/whiteboard.md中的独立形状槽、工作区宽色隔离和“主题不改变画笔”的旧合同；不改无关规范。
8. 独立 trellis-check 审查完整最终范围，修复与本任务相关问题；主会话完成下列验证并报告局限。不 commit/push。

## 测试与构建

- 扩展真实状态/Bar/Draw3测试，不复制实现公式。覆盖 PRD AC1-AC7，尤其两个连续主题/页事件、主题后立即Down、cold load失败/stale、旧UI revision、同类背景、荧光/激光跨场景隔离和新旧工作区反向恢复。
- 先发现 Visual Studio 安装中的 ARM64 原生 MSBuild，不硬编码VS版本；完整 InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64。同一PowerShell调用先 Remove-Item Env:PATH -ErrorAction SilentlyContinue 并设置 MSBUILDDISABLENODEREUSE=1。
- 构建进程总超时至少900秒；短工具等待仅返回会话，不重启构建。检查退出码与第一个有意义错误，环境问题不得用改源码/依赖绕过。
- 构建成功后按真实输出路径运行 InkeysHeadlessTests.exe --no-window，以及 Inkeys.exe 的 --state-mode-production-test、--pen-width-production-test、--draw3-pen-width-test 等适用初始化前早退探针。新增测试接入已有探针，不启动主窗口。
- git diff --check、逐文件BOM/换行核对、diff范围检查。原全局输入hook、Raw/Touch/笔过滤与R2链路应无改动。
- GUI/高漫M6实机消息和真实PPT交互不在本轮验证；不能将headless通过写成真机通过。

## 风险与回退

风险集中在 IdtState.cpp/.h 的工作区恢复/快照与保存、Draw3主题生效/首笔、Bar直接Brush字段读取及旧独立槽测试。新增共享状态必须参与现有生产测试Scope的保存/恢复，避免测试污染后续用例。

仅撤销本任务引入的源文件增量即可回退；不 reset、覆盖其他会话工作或改全局配置。若后续发现范围外问题，报告不顺手修复。2026-11-02用户已批准最终方案，按本清单实施。

## 完成状态

步骤1-8完成；独立check修复及最终构建/生产探针/headless结果详见research/validation.md。未启动GUI，未commit/push；R2等待日志。
