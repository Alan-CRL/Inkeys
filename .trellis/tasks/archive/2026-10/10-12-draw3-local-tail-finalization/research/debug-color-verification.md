# 临时尾段反色诊断，2026-10-13

用户实际体验反馈无改善，尚不能确定生产触发/拒绝/形变量原因。阶段三保持 in_progress；本轮仅增加可撤销的颜色与调试输出，不修改拟合算法、参数、shader 或永久笔色，不 commit/push。

产品三个文件：InkDocument 接口/实现保存仅内存的可选线段起点；StrokeGeometry 在实际写回后标记、taper 插点映射、统一 DrawStoredStroke 拆分正常/反色尾段。RGB 精确反色，alpha 原值，旧前缀正常。L1完成及历史L2/tile重绘消费同一标记。保存格式不变，重新加载没有反色。

临时开关：Draw3.StrokeGeometry.cpp 的 kDebugCompletedTailVisibility=true；false 同时关闭颜色与逐笔 OutputDebugStringA 输出，求解不变。日志记录 decision/status/reason/points/raw/window/changed/displacement/turn；OutputDebugString 需要调试器或 Windows 调试输出采集工具，不是磁盘日志。

## 验证

- 既有 InkeysRepo.sln Debug|ARM64，ARM64原生MSBuild；同PowerShell规范PATH，监督timeout3600秒。
- 首次增量链接出现LNK1000 IncrBuildImage，命令行 /p:LinkIncremental=false 完整链接通过，不修改产品工程配置。最终构建日志与结果：Build/modeler-baseline/stage3-main-native/ARM64-Debug/tail-debug-full-link.log、tail-debug-build-result.json。
- 最终实际产品 --draw3-pen-taper-test exit0：72model/36resume/331608断言/0失败。验证实际apply标记、caller/solver拒绝不标记、原样式不变、裁剪颜色边界、RGB/alpha、非法索引；新增真实ApplyLiveTipTaper插点在标记前/第一条变化边内的映射与无诊断几何逐点一致检查。
- 最终产品 EXE SHA256=a4af42540809ac431779584c1802ab46faf39253a7eb07e47db0dd449e6103fd。
- Headless在沙箱中发生临时文件写入相关11失败（Config/atomicwriter/SpeedEraser）；外部相同程序复测结果见 tail-debug-tests.json 与 tail-debug-headless.log，不修改测试或配置规避。
- diff检查通过；3个产品文件BOM/CRLF保持。独立审查debug-color-check.md；审查后已补建议的taper插点专项测试。

无GUI/GPU实际显示/实体笔验证；黑色反色为白色在白底可能不可见，人工宜选红/蓝笔。未反色仍需排查实际触发、回退与显示链；不能据此百分之百确定算法原因。

外部 Headless --no-window 最终复测 exit0；沙箱首次失败为环境限制，未改变任何测试源码。全部本轮无窗口验收通过，人工验证仍待进行。
