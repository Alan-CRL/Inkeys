# 执行计划

## 文件所有权

- recorder implementer：Draw3.Performance.h/.cpp（类型/预算/存储/导出/GPU/纯CPU计量probe）。先发布接口与文件schema，其他实现者消费；不改Renderer、Host/Controller接线或Scripts。
- graphics implementer：Draw3.Renderer*.cpp/.cppm、TransparentPresentation、Host/Controller需要的GPU接线、HistoryGpu/LaserParticleSystem必要owner旁挂、新Draw3.ResourceProbe.h/.cpp（含case协议/CPUprobe）。不改Performance文件、Scripts、IdtMain、工程。
- package implementer：Scripts/Draw3Performance全部用户脚本/分析/测试/README/打包；不改C++。
- 根：规划/规范、IdtMain早期分发和工程登记、整体接口协调、构建/验证/打包/最终收尾。共享工作树，任何人不回滚他人编辑。

## 顺序

1. [x] research recorder/graphics/package落盘，收敛接口；PRD/design/implement+真实context manifest验证并start。
2. [x] 记录器先落稳定头文件契约与schema；graphics/package据此并行实现，正常off与原始绘制语义审查。
3. [x] 根接入早期probe分发与项目文件，核对预算、协议/能力marker、身份/退出和CLI拒绝分支。
4. [x] Trellis check独立审查并安全局部修复；重点预算/off、边界/结果、GPU窗口、PS2、worker超时与原始证据真实性。
5. [x] 原生ARM64 MSBuild动态定位，同invocation规范PATH；完整InkeysRepo.sln Debug|ARM64，最少5分钟，保留日志和自然退出码。按本机既有串行/依赖参数，不升级工具链。
6. [x] 纯CPU性能/launch/config/resource-probe-test、Headless --no-window；WindowsPS5.1与pwsh脚本回归；有变化/失败才重测。
7. [x] 确证无HWND后小范围原生ARM64离屏probe真实CLI验证（有限case、外部超时），环境不可用单列；不打开产品或GUI。
8. [x] Release|x64及ARM64完整solution与新增入口CLI验证，重打包2，检查7+新增用户文件/manifest/ZIP全streamSHA。
9. [x] 更新native-desktop性能诊断spec、验证记录与PRD勾选；Trellis journal/archive使用--no-commit，无commit/push。

## 检查

git diff --check；对照Build/Draw3PerformanceV2Validation/baseline.json保持BOM/CRLF/EOF；不改既有工作无关部分。新增C++编译首先定位实际首错，不因SDK/PATH/权限/工具链问题污染代码。脚本静态PS2语法/API检查+fake workers/fixtures；v1真实兆芯包复制到Build后重分析，外部原包不改。每轮命令/状态/限时/日志/第一错误入validation.md。
