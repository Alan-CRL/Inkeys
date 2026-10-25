# 分层导航与通用拆分独立审查（2026-10-08）

依据 `approved-hierarchical-nav.md`，直接审查当前 `fluent` 工作区。历史 GUI、提交授权不延续。本次未启动 GUI、Computer Use、commit 或 push；构建与运行由主会话统一执行，审查代理仅负责检查、定向测试和测试工程修复。最终源码冻结后复查，无未解决的本轮代码缺陷。

## 已核对的生产路径

- 中段只有通用具有四个子项，其余绘制、插件、组件、实验选项、软件版本保留原页面。顶部及底部固定项沿用已有业务事件。
- General/Storage 和 General/Interaction 分别接管旧 Configuration/HotKeys，PageId 编号及退役空洞保留。主页通用进入 Basic；侧栏父项进入最近子页；快捷方式 More 进入原插件详情，不被插件总览导航重置。Build/Version 内部切换保留。
- 通用草稿和业务副作用按当前子页处理；条件隐藏不覆盖保存值。主栏缩放、动画速率维持实时更新与释放保存；离页补齐 pending 提交，设置缩放复用原字体重建、窗口最小尺寸和 FIFO。
- 正文只提交一套控件；生产 `ApplyContentAlpha` 递归处理实际子窗口顶点 alpha，不修改 RGB 或控件值。保留 ImFluent 透明度加权颜色插值。
- Shell 浮层、键盘导航、裁剪与命中使用真实 ImGui/ImFluent；测试直接链接生产 Shell/Pages/Controls，并从仓库简中语言源和内嵌字体构造无 HWND 夹具。

## Findings（fixed）

| 文件 / 区域 | 具体问题 | 修复及证据 |
| --- | --- | --- |
| Setting.Design.h | 动画途中改变速率，用累计 elapsed 除以新 duration 会回退进度 | 归一化进度按 dt/duration 推进；连续帧真实 pane 验证 1→0.1→5 变速仍单调、限界并收敛 |
| Setting.Design.h / Setting.cpp | 超过 100 ms 的可见慢帧直接置零，低帧率可能永不完成 | 有限正 dt 推进，限幅 250 ms；真实 resident/渲染跳过路径显式标记时钟暂停，恢复首帧重基准；0.2 s 连续帧验证真实侧栏完成收起 |
| Setting.Shell.cpp | ImGui NewFrame 先处理 Escape，清除或替换子项 NavId，随后关闭覆盖导航时无法回焦 | 每帧仅记录真实 inline child focus，并检查当前 ID 的受限恢复条件；Down→子项→Escape 与子项→resize compact 均验证回焦到父项，不依赖残留永久 ID |
| Setting.Shell.cpp | 187.5% DPI 下 WorkRect / ContentRegionRect 取整差导致父行与箭头重叠 0.5 px | 父行和箭头共用实际内容边界；真实 ItemRect 的严格不相交断言通过 |
| Setting.Shell.cpp | 分数 DPI 下末行 bottom 多出裁剪区 0.5 px，滚动到底仍不完整 | 从真实末项 bottom 登记 ceil+1 物理像素尾部空间并提交 Dummy；不缩小行高、不放宽裁剪断言，多尺寸/模式/主题均完整进入真实 ClipRect |
| setting_design_tests.cpp | nested child 名包含父路径，夹具把通用子列表误认成中段滚动窗口，投递到错误滚动目标 | 优先识别更深 child，分别记录实际 group 与 list；保留所有包含、滚动与入口可达断言 |
| setting_design_tests.cpp | 淡入夹具在关闭动画的上一帧状态下先 Notify，再启用动画，错误期待部分 alpha | 按生产顺序先真实绘制一帧更新动画策略，再通知新目的地；实际字形与填充顶点 alpha 断言通过 |
| InkeysHeadlessTests.vcxproj | 新链接生产 Shell/Pages 和 i18n 测试夹具带入原 IdtMain/Hashlib，触发已有 CRT C4996 编译门 | 仅三个受影响编译项继承产品已有 `_CRT_SECURE_NO_WARNINGS`；保留 SDL，不修改 vendor 或工具链；完整 Solution 编译通过 |

审查代理直接修改的实现范围为 `setting_design_tests.cpp` 与 headless vcxproj。生产修复由导航/主会话所有者完成，并经本审查重新核对。

## Verification

- Lint / 静态检查：PASS。仓库没有独立 C++ lint/type 工具；执行全工作区 `git diff --check` 和国际化检查，原编码、BOM、CRLF 保持。
- TypeCheck / 构建：PASS。完整 `InkeysRepo.sln`，Debug|ARM64，动态定位 ARM64 原生 MSBuild，同一 PowerShell PATH workaround，1800 s 超时；最终 `build-final.result.json` 记录 exit 0、30.71 s。
- Tests：PASS。最终 `InkeysHeadlessTests.exe --no-window` 主会话确认进程 exit_code=0，日志末尾 `PASS animation correctness`。证据：`Inkeys/Cache/hierarchical-nav-20261008/headless-final.log`。
- 分层导航真实 CPU DrawData：100%、150%、187.5% 各 564 帧、20 次目的地导航。覆盖父行/箭头分离、紧凑两次点击/再次关闭、浮层子项、外部正文及关闭按钮 nonpenetration、Up/Down/Space/Enter/Left/Right/Escape、隐藏子项不可点击与可见裁剪部分可点击、覆盖/resize 焦点、三尺寸/两主题/展开收起滚动与固定区、反向/中途变速/慢帧/关闭动画/正文淡入。
- 共享悬停颜色：100%、187.5% 各 328 帧、315 次实际控件合成取样，0 violations，最大偏差 0.000492036（小于一个 8-bit 色阶）；保留普通 RGBA。
- 内容控件：真实 General Basic/Appearance/Interaction 分页渲染、空闲无 edits、条件隐藏保值、启动加载 toggle 和设置缩放拖动/释放状态通过。Storage/版本/插件等完整业务承接以代码路径与父任务内容映射静态核对。
- i18n：en-US、zh-TW 均 370/370（100%），PASS。

## Findings（not fixed）与有限结论

未发现需要继续修复的本轮源码或自动回归问题。以下属于未执行的实机验收及实现边界，不以无窗口检查代替：

- GUI / HWND / DX11 呈现、真实鼠标观感、截图、跨屏 DPI 和实际浮层阴影尚未验证；本轮禁止启动交互界面。
- 启动项、真实快捷方式、磁盘配置写入、PPT 运行时和重启/关闭等副作用没有通过本轮 headless 直接执行；保持既有 FIFO 的结论来自生产接线静态检查与已有回归，不能称作全部实机业务通过。
- 关闭动画事件发生在当前正文提交过程中，导航同帧使用最新归位几何，正文 alpha 立即为 1；正文宽度布局在下一帧采用归位状态，以保持每帧只有一套业务控件。该单帧布局边界仍需实际观感验收。
- 插件内部原切换方式保留；新增正文淡入通知覆盖顶层与通用复合目的地，未额外重写插件内部路由。

父任务继续保留 in_progress，后续验收按新的明确授权执行。
