# Fluent 分层导航与通用拆分验证（2026-10-08）

本轮依据 `approved-hierarchical-nav.md`，直接交付原工作区 `D:/Project/Inkeys/Repo/Inkeys` 的 `fluent` 差异。基线为 `ff7ad583`；没有创建任务/分支/工作区，没有 commit/push，没有启动 GUI 或 Computer Use。父任务保持 `in_progress`，继续承载剩余实机、窗口与常驻会话验收。

## 实现与内容

- 原常规改为通用，拆为基本、外观与动画、交互与安全、存储与配置；原快捷键与配置保存完整归入对应子页。逐项字段、业务事件、条件显示与保存映射见 `hierarchical-content-map.md`。
- 中段六个根入口：通用、绘制、插件、组件、实验选项、软件版本。顶部和底部固定三段式、填充赞助、红色关闭及调试显隐保留。
- 父行进入最近子页并展开；箭头独立控制展开；紧凑模式先导航，再次点击打开/关闭子项菜单。菜单边界、滚动、外部点击与松开屏障、键盘与 Escape 焦点已接入真实 ImGui 控件。
- 内部复合目的地保持公开 Setting 接口和既有页面编号；旧 Configuration(3)/HotKeys(10) 重定向，6/7 编号不复用。各子页有独立稳定 ID 和滚动位置；最近子页、展开偏好保留于常驻会话，不新增磁盘配置。
- 宽度/覆盖、子列表/箭头、浮层、正文与选中标记使用原软件动画开关与速率；归一化进度支持中途改速及快速反向，隐藏恢复由现有渲染线程重置时钟，不增加线程/定时器。正文仅执行当前页面一次，alpha 只作用当前正文提交的顶点，保留颜色插值修复。
- 条件隐藏和草稿副作用按当前子页门控；滑块离页补提交复用原写入与即时同步路径。Drawing、Plugins、Components、Version、Experimental 原 switch 正文与 HEAD 逐行一致（仅比较时统一 Git/工作区换行）。

## 实际执行结果

| 检查 | 结果与范围 |
| --- | --- |
| 完整 Solution | 动态定位 ARM64 原生 MSBuild；`InkeysRepo.sln`，Debug/ARM64，exit 0；最终构建 30.71 秒（增量完整 Solution，非 clean rebuild） |
| 构建环境 | 同一 PowerShell invocation 规范化 PATH，关闭 MSBuild node reuse；超时 1800 秒，后台进程 Hidden |
| 最终无窗口回归 | `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`，进程 exit 0，`PASS animation correctness` |
| 分层导航 | 100%、150%、187.5% DPI 各 564 帧、20 次导航；意外业务及正文按钮触发均为 0 |
| 实际控件与几何 | 720×520、960×700、1440×900 DIP；浅/深主题、展开/收起/覆盖；固定区、中段滚动、根项可达、菜单翻转/夹紧、子项裁剪命中 |
| 输入 | 父行/箭头独立，紧凑重复点击，菜单选择/外部关闭且不穿透；Enter/Space/Up/Down/Left/Right/Escape，包含焦点子列表折叠及覆盖关闭回焦 |
| 动画 | 0.1×/5×、中途 1→0.1→5、快速反向、连续切页、菜单重开、主题切换、200ms 帧增量、关闭立即归位；正文每帧只绘制一次，实际字形/控件 alpha |
| 内容控件 | 直接调用产品 General/Pages 控件，核对草稿、隐藏值、基本启动开关、缩放拖动/释放；配置/运行时接线另做静态复核 |
| 共享悬停色 | 100%、187.5% 各 328 帧、315 样本；0 violations，最大浮点容差误差 0.000492036；实际 Nav/Button/Combo/Popup DrawData |
| i18n | 原脚本 `Scripts/i18n.ps1 check`，en-US、zh-TW 均 370/370（100%），PASS；简中快照与生成键同步 |
| 格式 | `git diff --check --ignore-submodules` PASS；21 个修改的原文本文件（含会话记录） UTF-8/BOM 保持，工作区 CRLF、无裸 LF |
| 独立检查 | `hierarchical-navigation-review.md`：当前源码无未解决缺陷，自动检查与 GUI 边界分开记录 |

测试工程链接实际 `Setting.Shell.cpp` / `Setting.Pages.cpp`，不是另写的导航模拟。最初测试暴露的 187.5% 父行箭头半像素重叠、末项裁剪和 ImGui Escape 清焦均已修复；最终断言保持严格。构建原有第三方 hashlib 数值转换、Setting 既有纹理整数转换 warning 未做无关清理。

最初受限 runner 产生 C1902 PDB 管理器错误，采用正常用户权限运行同一构建解决，没有升级 SDK/工具链或修改产品配置。新增真实产品源接入测试工程后的旧 hashlib C4996，复用产品已有 `_CRT_SECURE_NO_WARNINGS`，仅限定三个相关测试编译项，不修改第三方实现或关闭 SDL。现有文件原子写入测试也在正常用户权限 `--no-window` 下完成。

## 工作区与交付

原工作区 `Build/ARM64/Debug/Inkeys.exe` 已按当前源码重编。本轮开始保留的 `IdtMain.h`、`PptCOM.dll`、`PptCOM.tlb` 与原始备份字节一致；13 个 `PptCOM.Tests/bin,obj` 原有产物全部哈希一致（仅恢复构建改写的 AssemblyReference.cache）。没有借本轮改动重写或提交用户原有内容。

最终机器记录见 `hierarchical-navigation-validation.json`，含构建路径、退出码、尺寸/倍率、产物 SHA256 和保护文件哈希。详细原始日志保留在忽略目录 `Inkeys/Cache/hierarchical-nav-20261008/` 的 `build-final.*`、`headless-final.log`；不把大日志加入交付差异。

## 尚未执行的验收与明确边界

- 按本轮授权未启动 GUI。真实鼠标观感、截图、实际 HWND/DX11 动画、浮层背景及现有 DisabledAlpha 视觉效果、跨屏 DPI 仍待实机确认。
- 自动控件测试与静态接线审查不等同于实际启动项、快捷方式、崩溃处理器、异步 FIFO 和用户配置副作用的实机验收。
- 关闭动画时导航同帧归位；此前已绘制的正文下一帧使用归位宽度，避免同帧重复执行设置控件。插件内部详情/切换保留原样；新增正文淡入按复合目的地变化推进。
- 父任务已有的窗口恢复、重复 Hide/Show、常驻会话资源、性能基线、设备恢复与材质验收继续保留，本轮不据此将父任务标记完成或归档。
