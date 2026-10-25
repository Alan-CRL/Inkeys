# 主页推荐与专用问题反馈页（2026-10-24）

## 本轮设计与约束

继续当前已批准父任务；用户直接提出实施调整，不创建新任务、不沿用历史提交或 GUI 授权。其他未提交代码、两张 CSO、已清空的 CI signal1 不受影响。

现状：Home 推荐为 Draw/Plugins，common 为 General/Language；作者卡的反馈直接打开问卷，二维码只在 hover。HomeAction::Feedback 现有可复用为页内导航。TitleBarGeometry 目前只有 themeToggle/debug 两个自定义右侧入口，需要加入二者之间的反馈命中区。General Basic 实际包含选择/穿透、主栏图标排列、自启/启动位置/快捷方式，不应再描述界面缩放。

## 共享接口约定与归属

- UI 实现负责 Setting.Pages.h/.cpp、三语言 JSONC、中文快照和生成 IdtI18nKeys.g.h。PageId 追加 Feedback=15，HomeAction 追加 Appearance/Shortcuts（原值不偏移）；NotifyNavigationDestination 对 Feedback 同 Debug 清除侧栏选中条。维持旧 HomeContent 聚合以避免此次无关调用点迁移，只停止 Home 使用反馈 tooltip/底部版本链接。
- UI 增加 `enum class FeedbackAction { None, Issues, Questionnaire }; FeedbackAction RenderFeedback(ImTextureID qrTexture, float qrAspect);`，用真实 PageHeader/NavigationRow/Card 渲染，返回动作，不执行 ShellExecute。二维码使用原 TextureSettingSign[7]，常驻下方卡片，保留原资源和比例，按卡片内容宽缩小；不以 hover 为前提。添加 `RenderFeedbackButton` 与现有 RenderDebugButton 同签名声明，供标题栏接线使用。
- native 接线实现负责 Setting.cpp、Setting.Layout.h、Setting.Shell.cpp。Feedback 枚举追加15，推荐动作跳 General Appearance / Draw，common 跳 General Basic/Shortcuts、Plugins root、Feedback。反馈页消费新 renderer 动作，Issues 目标 `https://github.com/Alan-CRL/Inkeys/issues`，问卷目标原 `https://www.wjx.cn/vm/mqNTTRL.aspx#`；保留原地区社区目标。
- TitleBarGeometry 增加 feedback 位于主题与 debug 之间，计算/title/version预算和 Win32 HTCLIENT 判定同步；新按钮使用反馈语义图标及本地化 tooltip，沿用与 debug 一致的键鼠/浮层关闭/释放屏障/选中背景行为。可在 Shell.cpp 小范围抽取同类标题栏 route button 内部实现，不扩大无关架构。Feedback 无侧栏独立项，和 Debug 一样不保留旧侧栏选中条。
- 根会话负责 tests 与最终整合。原有 IDs/路由数值/首页品牌动画、主题/配置写回/二维码资源加载均保留；不为本需求改工程项或新建构建系统。

## 呈现与文案

推荐顺序为外观与动画、绘制；common 为基本、快捷键、插件、问题反馈。基本描述可用“操作模式、主栏排列与启动方式”。QQ 群标题和群号同行（在正常支持宽度保持同一行）；非简中直接将本地化 GitHub Discussions 标题作为链接，不重复下一行地址。移除作者卡旧反馈入口/tooltip，保留其他联系方式和赞助。页脚仅移除版本号入口。

新反馈页先 GitHub Issues，后问卷，两张块状按钮各有问题和建议说明；其后二维码卡片及扫描反馈/建议说明。所有新增/改动的用户可见文字用简繁英现有生成链，不硬编码中文，不打开外链验证。

## 验证

现有实际 ImGui/ImFluent headless 扩展点击与布局（不复制字符串/常量当测试）：推荐及4个common动作、地区社区标题点击，feedback两种动作，二维码无需hover也产生图像draw command，窄/宽、多语言、明暗、系统DPI；标题栏新几何不重叠、拖动与版本让位、反馈按钮和导航浮层隔离。只用既有 Solution Debug|ARM64 和 --no-window，不启动真实应用/浏览器/Office。先独立检查源码，再完整构建与适用回归，检查实际退出码。

## 实施与验证进度

- 已按上述归属完成12个源码/资源/测试文件的本轮最小增量，保留原 HomeContent 聚合，旧动作/页面编号不偏移；500/500 三语言检查通过。
- 独立代码/测试审查完成，见 settings-home-feedback-review.md。旧页脚链接消失回归使用真实 HomeAction::Version/###link 作用域，修正最初错误的任意 ID 断言。
- 首次 /m 并行构建出现编译器 C1060 堆不足（fmt/format.h），属于编译环境资源限制；只将本次 invocation 改为 /m:1、MultiProcessorCompilation=false、CL_MPCount=1，ARM64 原生 MSBuild 完整 InkeysRepo.sln Debug|ARM64 实际 exit 0，工程/依赖/工具链未改。记录 settings-home-feedback-build.log、settings-home-feedback-build-final.log。
- 首次完整 --no-window 在新二维码 CPU 夹具的 ImDrawCmd::GetTexID 触发断言（exit 3）：动态字库未作图形后端上传，夹具不应查询字体 backend ID。仅将 QR 筛选改为直传 user TexRef ID，保留真实 command/索引顶点和全部二维码几何断言，未改产品/第三方源码或伪造字体上传。独立复核通过后重新完整 Solution 增量编译 exit 0（settings-home-feedback-build-tests.log），随后完整 --no-window 的结果见下文（settings-home-feedback-tests-final.log）。
- 已核对源码 UTF-8/BOM/CRLF 和 git diff --check、两张既有 CSO / build-windows.yml 不变、signal1 原空结构与 HEAD 一致。实际产品 EXE 不含人工 CI 预览值、内嵌 PptCOM.dll 与前轮已验证版本字节一致；主程序 SHA-256 c1a4afd9585ce168663039ae785f7ee460e9b65e09b393b646dc599116783e90。
- 正常720 DIP最小窗保留96 DIP系统拖动区；360 DIP已低于正常最小尺寸，三入口和原caption保留后可用拖动仅62 DIP，源码注释/纯几何回归/规范明确该物理边界。

## 最终运行结果

1. 追加联系方式前的完整 `--no-window` 实际 exit 1，只有36个“主页社区链接点击”断言失败，其他原生/纯几何/设置回归无失败。根因是新测试夹具误将 NavRectRel 只加 window.Pos；vendored ImGui 当前合同以 DC.CursorStartPos（含padding/scroll）为基准，小链接因此点击偏移。仅将测试取矩形改为现有 WindowRectRelToAbs，保留全部真实注册/点击断言，未改产品实现。独立复核见 review 记录。
2. 最新包含复制社区/邮箱/作者QQ的完整 `InkeysRepo.sln Debug|ARM64` 使用 ARM64原生MSBuild/低并发构建 **exit 0**，日志 `settings-home-feedback-contacts-build.log` 和对应 console。主页和原资源/链接保持，FeedbackAction末尾追加两值。
3. 最终使用项目已有 **`InkeysHeadlessTests.exe --no-window --setting-home-only`，exit 0**，日志 `settings-home-feedback-contacts-tests.log`。该入口覆盖真实WARP/硬件离屏、既有设置控制与品牌/导航/滚动矩阵，以及本轮36组（三语言×三倍率×明暗×窄宽）Home/Feedback布局、真实卡片与社区点击、Issues/问卷/Community/Contact四种反馈动作和无hover二维码图像命令/比例。因最终追加仅为设置内容/动作及已定位的测试坐标修正，使用现有设置专用入口，不重复此前完整运行中已通过且未修改的其他原生算法；不将此前 exit1 的默认整套运行写成 exit0。
4. 最终i18n英文和繁体均 **504/504、exit 0**；UTF8/BOM/CRLF及 `git diff --check` 通过，两张既有CSO和build-windows.yml字节与增量基线一致。signal1空结构仍与HEAD一致、EXE不含人工CI测试值，原PptCOM嵌入DLL资源字节保留。最终EXE SHA-256为 `6a50cd76472ba29f23a2c01847a1fc3bcd934ee4f320eca8988bdc60bdf1d4e2`，结构化证据 `settings-home-feedback-verification.json`。
5. 产品及夹具两次后续独立复核无未解决问题；原生标题栏三按钮顺序/完整顶边命中/DIP/窄物理预算回归在前次默认完整运行中通过，最终设置专用运行再次通过实际feedback/modal/侧栏清选中交互。只修本轮实际边界，不改工具链/项目配置或第三方源码。不启动交互 GUI、浏览器或 Office，不验证真实相机扫码/触屏，不 commit/push、不结束父任务。

## 进行中补充：复制社区与作者联系方式

新用户要求是本轮追加，不替代原主页/反馈工作。主页信息继续保留；专用页在 Issues / 问卷 / 常驻二维码之后增加“交流与联系作者”区域，采用本地化 NavigationRow / 只读说明行：地区社区 QQ 群或 Discussions 为 Community 动作，邮箱联系作者为 Contact 动作，作者 QQ 2685549821 常驻显示，不伪造 QQ 直连。保留原地区 Community URL 与 mailto:alan-crl@foxmail.com 目标，仅明确点击后在 Setting.cpp 消费动作。FeedbackAction 尾部追加 Community / Contact，不偏移原值，不影响前两渠道/二维码顺序。

UI 代理仍负责 Pages.h/.cpp 和三语言生成资源；native 代理仅改 Setting.cpp 的反馈动作分支；根会话扩充真实反馈控件点击并复用已有构建/无窗口验证。正在运行的前版完整回归可完成，最终交付再针对新增后的实际版本验证。

## 联系方式复制追加完成

反馈页二维码之后已追加本地化社区卡、邮件联系作者卡和只读作者 QQ 卡；保留原主页同类信息和地域链接。新增两种动作的真实 NavigationRow 点击已包含最终通过的设置页无窗口回归。当前增量与验证完成，父任务保持进行中，不创建 commit 或 push。
