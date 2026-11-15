# 第四轮验证记录（基线 e8eb2f1b）

## 实施结果
本轮12项及确认后的几何约定已实现。平面外框/胶囊/四侧停靠、对称焦点和等腰标记、平行四边形局部轴编辑、叉勾顺序、窗口裁切修复及箱体外接框内接均包含回归。圆柱/圆锥现有半长斜二测公式正确，补充实际轮廓共轭直径验证，不强制椭圆长轴水平。无存储版本、依赖或工程配置变更。详细缺陷及审查修正见 round4-review.md。

## 构建与自动测试
日志位于 Build/shape-round4。所有最终验证退出码均为0。

| 项目 | 最终结果／日志 |
|---|---|
| 共享完整 solution Debug ARM64 | core-build.log，0错误 |
| 主产品完整 InkeysRepo.sln Debug ARM64 | product-verified-build.log，0错误 |
| 共享 shape-core-only | shape-core-only.log，0失败 |
| 完整 UInk 文件回归 | uink-file-only.log，260对象/1209标准parts，保存重载/标准降级通过 |
| 产品 shape core | draw3-shape-core-test.log，0失败 |
| 产品 shape renderer | draw3-shape-renderer-test.log，471项0失败 |
| 编辑器 UI 离屏 | verified-ui3-shape-editor-test.log / editor-verified-results.log，544场景、4206 commits、235523 checks、0失败 |
| 几何属性 UI 离屏 | final-ui3-geometry-attribute-test.log / geometry-final-results.log，1586620 checks、144 frames、0失败 |
| 隐藏窗口输入/焦点/历史链 | final-draw3-shape-focus-hidden-test.log，SF回归通过 |
| 白板存储 | draw3-whiteboard-storage-test.log，100页及冷撤回/恢复通过 |
| Headless --no-window | no-window.log，通过 |
| 自动保存 | desktop-autosave-only.log，通过 |

按当前Visual Studio定位原生ARM64 MSBuild，同一PowerShell应用PATH workaround，使用现有solution。未修改工具链或配置。保留既有第三方/测试编译warning；PATH workaround后附属步骤报告pwsh.exe不可见，但两个solution退出0且目标程序成功生成，未改源码绕过环境。

## 失败与纠正记录
初轮编译修正局部声明/float-double模板类型错误，以及新增测试变量far与Windows宏冲突。首轮editor的1289个同类像素失败定位胶囊材质未与普通thumb统一，随后恢复真实材质并保留断言。次轮仅case276六个动作失败，原因是新增旋转测试漏恢复actionSink，修复测试夹具后完整矩阵通过；没有降低断言要求。

最后一次构建仅较delivery版本修复该测试夹具，产品逻辑相同。最终编辑器使用Build/ARM64/Debug/Inkeys.exe完整重跑；最终geometry及隐藏链使用delivery版本通过，不无条件重复未变更模块的测试。

## 文件与交付
跟踪修改11个文件，另新增本轮研究/验证记录；git diff --check通过。原BOM保持，工作区CRLF、无混合换行（core.autocrlf=true，索引LF为预期Git转换）。仅清理本次构建生成的4个未跟踪shader cso。

最终程序：Build/ARM64/Debug/Inkeys.exe
SHA256：13998799A130B8DDE02169FF41DAD9C173F557B01E1A7B7A7A0EE371C6AED318

未commit、未push、未归档，父任务保持in_progress。

## 验证边界及窗口归属
没有启动交互式GUI或Computer Use。离屏像素与隐藏fixture不替代实际可见重叠窗口的OS输入投递、笔/触控手感、动画观感及多屏边缘实机验收。

- 外框、控制点、样式栏和其子面板：SelectionOverlay透明HWND，标题Inkeys Shape Editor；共享UI RenderPipeline/D2D，UpdateLayeredWindowIndirect呈现。
- 主栏及构造提示：Bar HWND。
- 图形本体：Draw3画布。

主栏由既有窗口Z序覆盖编辑层，透明留白不再被整块清空。边缘半圆问题同时修复了退场HandleVisual未计入窗口包络和粗角标半线宽/AA不足。
