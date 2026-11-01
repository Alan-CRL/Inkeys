# 2026-11-01 精确定位 Observe 子阶段与安全调度设计（用户批准执行）

## 当前证据和本轮顺序

真实Previous总1355.671ms，邮箱1198.672ms，owner156.192ms，focus0.086ms；同owner维护剩余345.258ms+Observe853.360ms几乎完整覆盖排队。Next也有703.529ms排队。请求在ROT已开始时到达，单纯扫描前检查pending并不够。Observe哪些现有成员最慢尚无数据，不能凭猜测跳过安全校验。

用户明确要求准确定位并开始执行，特别强调PptCOM.cs dynamic与Trellis约束。本轮首先实现原有Observe调用的细分计时和分析器子阶段显示，准备同一入口的新诊断版；同时完成只读安全调度设计。先获得真实子阶段证据，再决定最小调度实现，不能把加日志冒充性能已修复。所有GUI/Office人工操作由用户执行；AI仅构建/无窗口验证；不自动commit/push。

## 修改边界

- 只围绕ObserveBoundShow现有调用增加默认关闭PptPerf子span，固定名称区分ActivePresentation、应用文稿计数（错误回退）、SlideShowWindows.Count、SlideShowWindow、窗口Presentation（missing Count回退）、Slides.Count、View取得、State、当前Slide/SlideIndex、HWND解析与最终会话发布。
- 在已有GetTotalSlideIndex/GetSlideShowWindowsCount/GetCurrentSlideIndexFromView帮助函数内，必要时再拆原有object acquisition与标量读取（也可同时服务command-precheck），不新增或重复COM访问。不为计时把链式访问/反射/PIA包装引回来；HWND既有受限标量例外保持不变。
- 每个临时对象仍按每笔取得具名finally子先父后Release，root借用不释放；保留busy/missing/断连/结束/原有generation返回，不能GC/FinalRelease补漏。不能改跨线程owner、调度顺序、2秒超时、ABI/schema、支持矩阵、用户焦点/等待UI。
- disabled无格式化输出；日志错误不影响IErrorInfo/异常/释放，begin/end覆盖失败路径；保持BOM/CRLF最小diff。
- 分析器新增维护子阶段明细：选同owner线程及匹配parent/时间区间，输出各直接子项总时长和排队重叠；父子不重复相加，缺失/冲突为unknown，跨pid/频率仍拒绝。继承已有10项自测并补真实v1嵌套/错误/不完整子项用例。

## 所有权

- managed实施：PptCOM/PptCOM.cs和PptCOM.Tests/ManagedCompatibilityTests.cs（只有必要才扩展现有测试文件，先通知）；不改native/脚本/任务/spec。
- analyzer实施：Scripts/analyze-ppt-navigation.py，仅必要的既有测试代码；不改launcher/产品/任务/spec。
- 研究代理：只写research/owner-scheduling-options-20261101.md，读实际代码+规范+采样；区分必须的新鲜场次/窗口校验、可后置的页码/HWND/ROT维护，指出安全让出边界与busy/轮询/旧请求/资源账本要求。不实际调度改码，不凭聚合数据声称可跳过某个成员。
- 主会话：任务文档/规范/构建/交接/独立审查，不能重写或还原前一轮未提交诊断改动。

## 验收与人工交接

- 新旧enabled/disabled调用顺序、次数与acquisition/release账本一致；busy、缺失、断连、Count0、换场、reentry/迟到generation既有测试继续通过；子span启用/失败配对且不泄露路径/标题。
- 分析器嵌套不重复计、同owner过滤、部分/缺失/身份冲突unknown通过；用已采集旧v1日志兼容分析，不能把不存在的子阶段算为0ms。
- managed Release|AnyCPU测试通过；显式DLL交接、TLB字节不变；原生ARM64 MSBuild完整InkeysRepo.sln Debug|ARM64（sameInvocation规范化PATH，>=5min预算，已验证的PDB允许环境）；只读校验EXE内嵌DLL及PrepareOnly不启动GUI、不改配置。
- 沿用Scripts/Start-PptNavigationDiagnostic.cmd交给用户复测，优先重复4→3 Previous；既有采样和本轮新采样分开，不覆盖。真实子阶段未有前，报告为定位准备完成/性能修复待据实收窄，不标记原任务完成或归档。
