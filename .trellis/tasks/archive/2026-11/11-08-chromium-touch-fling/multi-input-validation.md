# 多设备漫游与惯性设置：实施与验证

用户已批准 multi-input-stage-plan.md 并要求实施，基线961ffdf8。当前代码、最终原生构建和完整无窗口验证已完成并冻结，任务保持 in_progress，不自动 commit/push/归档。本记录使用本次实际产物，不沿用前轮 PASS。

## 已实现

- Pen/PenTail真实producer导航历史、Selection准入、同一视口应用与逐接触惯性门控；移动模式hover不刹停，持续Pen不自我Stop，真实Down停旧coast。
- Win10/11 PTP双指：DM owner/manual pump只输出累计数值，原生惯性与自有曲线不双算；64条阶段/Running历史、admission revision、接手截止和停止责任确认。
- 绘制→通用“白板拖动惯性”总/五类内联，默认总开、Mouse/PenTail关、其余开；启动/Host同代发布、热生效、关闭保留子项，二十语言七键。
- 真实生产/Config/ImGui/bridge回归，保留Mouse节拍与原摩擦/可靠尾段。

## 当前实际结果

| 验证 | 退出码 | 耗时 |
| --- | --- | --- |
| build | 0 | 126.532秒 |
| config-boundary | 0 | 2.641秒 |
| control-fence | 0 | 0.687秒 |
| pan-feel | 0 | 2.578秒 |
| headless-pan | 0 | 1.141秒 |
| setting-general | 0 | 34.203秒 |
| diagnostic-output | 0 | 1.828秒 |
| headless-all | 0 | 575.61秒 |

国际化：实现agent及独立check均运行 ./scripts/i18n.ps1 check，20语750/750，exit0。pan-feel为2112行、failures=0、no_gpu=1/no_hwnd=1。真实control-fence包含Pen/尾终态、混源/逆序/热禁、native区间/旧gate/通知寿命及publisher deadline，不只测试末端镜像helper。

## 构建与修复证据

1. 首轮完整solution exit1/132.218秒，第一个有意义错误LNK1000 IncrBuildImage（前轮同样的本机增量链接缓存故障）。只删除已验证绝对路径的 Build/ARM64/Debug/Inkeys.ilk，未改项目/SDK/工具链。
2. 二轮 exit1/7.735秒，C2039 TouchpadNavigationRetirement在模块实现单元不可见；GMF直接include共享header修复，属于源码可见性问题。
3. 三轮完整build PASS/43.453秒；首次control-fence发现Pen仍被上游IsNavigationDevice排除，以及大型Rig的Debug测试栈溢出。只扩producer predicate到Pen，四类真实历史测试保持原断言；39个局部/FeelRun夹具改堆持有，不改工程栈或减测试。
4. 四轮完整build PASS/34.422秒；control-fence仅余旧fixture把合法Pen先前-285px误作零原点。加强前段精确-285±原有1e-3，再严格检查被拒Mouse不改变双轴基线，保留归属/隔离/终态断言。
5. 最终完整solution与quick结果见上表；首轮及中间证据保留 Build/Logs/multi-input-build-attempt*.log/json 和control-fence-attempt*.log，未掩盖环境与源码问题。

## 独立审查与格式

review见research/multi-input-review.md。已修真实生命周期缺陷：逆序独立手势误抑制、按publication猜coast来源、native历史Running/接手裁剪/旧admission队列、Stop通知后self访问及句柄寿命、COM退休失败/partial-init责任、普通deferred递归、禁惯性边沿末点。修后无剩余确定源码缺陷。

42个产品/测试/生成文件严格UTF8/CRLF，40个已跟踪文件BOM与HEAD一致，2个新增小header；独立check重算source SHA256全部匹配。没有.sln/.vcxproj/依赖/SDK改动，git diff --check通过。此前scale-stage计划/research保留，不将其作为本阶段实现。

完整产物与source哈希清单 Build/Logs/multi-input-artifact-hashes.json：
- Inkeys.exe：d02f763a18a47f2f668ccfc4994d804e9e4a7a103bddb5fdb5c3e29f89600b18
- InkeysHeadlessTests.exe：d2af06489e6f9aaeb8bc89c906ae4b0682aec36968da5fbbfb2f48443c57686a

## 方法与实际限制

原生ARM64 MSBuild经vswhere动态定位，完整 InkeysRepo.sln Debug|ARM64 /m:1 /nr:false；同一PowerShell先规范PATH并禁node reuse，构建1800秒预算。原生CLI均CREATE_NO_WINDOW及既有GUI前提前返回入口，专项600秒，完整Headless1800秒。原始日志/结果JSON在Build/Logs/multi-input-*；未启动应用主窗口、设置窗口、浏览器或Computer Use。

Win10/11真实PTP消息、RTS共存、物理笔/笔尾手感及光学延迟未做真机GUI验收；合成phase/无窗口探针不冒称实际硬件支持。新能力只目标PTP；旧系统/legacy驱动保留既有输入降级。用户未授权新commit/push或结束任务。

## 最终质量门

完整 Headless --no-window exit0/575.61秒，末行 PASS animation correctness；诊断与全部quick专项均exit0。最终原生ARM64完整solution exit0/126.532秒。两类环境/源码中间故障均已解决，当前没有未解决的编译或自动回归失败。

六份执行规范已同步设备准入、默认/热开关、DM始终识别INERTIA而按实时策略门控、历史Running/接手截止/admissionRevision、owner退休与独立句柄确认；独立check复核无剩余确定缺陷。后续仅用户真机输入手感/PTP/RTS共存验证及另行范围指示，不自动开始屏幕尺度、GUI或提交。
