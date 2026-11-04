# 主页与专用问题反馈页独立审查（2026-10-24）

Active task: `.trellis/tasks/09-06-setting-winui3-design-rebuild`。审查最新 PRD、实施计划、settings-home-feedback.md 接口约定及当前增量源码/测试/三语言资源。只读产品与测试源码，仅写此研究记录；未 build、运行测试、GUI/浏览器/Office、commit/push。旧 PPT 内容修改、既有产物与日志不在本轮修正范围。父任务保持进行中。

## 结论

未发现未解决的产品实现阻断问题。发现一处新增测试的错误 ID，已由 root 修正并只读复核，见下节。完整构建与实际测试退出结果由主会话继续登记，本文不提前宣称通过。

## 发现与修复复核

初版页脚版本入口消失回归使用 page->GetID("REMOVED_VERSION_LINK")，该 ID 与原 Link 的 HomeAction::Version 作用域及 ###link ID 合同不符，即使恢复旧入口也不能检出。已告知 root；现已确认改为 ImHashStr("###link", 0, page->GetID(static_cast<int>(HomeAction::Version)))，并保留 HomeContent 的 fixture 版本文字。断言改为核对真实原控件 ID 的生命周期，不再用未注册的任意字符串充当入口。

## 用户要求与路由

- 推荐区从左到右（窄列时上到下）为外观与动画、绘制；Appearance 与 Shortcuts 仅追加 HomeAction 枚举值，不偏移旧动作。
- 常用设置按 Basic、Shortcuts、Plugins、Feedback 排列，原显示语言项移除；Basic 文案改为操作模式、主栏排列与启动方式，不再误称设置界面缩放。
- 简中 QQ 标题以实际文字宽度登记，同一行接群号链接；繁中/英文直接将本地化 Discussions 标题作为原 Community 动作链接，不再重复下一行地址。原按语言分支的 QQ/Discussions URL 保留。
- 作者卡已删除旧反馈链接及 hover 二维码逻辑；联系方式/赞助保留。主页 footer 只删除版本入口，网站/GitHub/Bilibili 等原动作保留。
- 新 PageId::Feedback 与 native tabFeedback 都是 15，未与 0–14 的旧枚举冲突。主页 common Feedback 与标题栏按钮都走统一 navigateTo；Appearance/Basic/Shortcuts 指向正确 GeneralSection，Plugins 重置插件总览。
- 专用反馈页依次为 GitHub Issues、原问卷反馈 NavigationRow、常驻二维码卡。所有新增说明明确支持问题与建议，新增/修改用户文案使用三语言生成链。
- RenderFeedback 仅返回 FeedbackAction，无 ShellExecute。原生页面消费者点击后才打开本仓库 /issues 或原 wjx 问卷地址，未在渲染/加载阶段访问网络。二维码继续用 TextureSettingSign[7] 及原尺寸比例、既有加载/重建/释放所有权，无新增纹理、设备或异步下载。

## 标题栏、导航与布局

- feedback 几何在 themeToggle 与 debug 之间，三入口各保留 32 DIP，与 native min/max/close 不重叠；标题/版本让位计算和 HTCLIENT 判定同时接入。
- 360 DIP 属于正常 720 DIP 最小窗以下的物理预算：三入口、原 caption 按钮和 96 DIP 拖动区无法同时完整保留。实现以中文注释明确缩减到剩余正拖动区域，未改变 caption 宽度；720 DIP 及以上仍保留 96 DIP。相应测试按预算约束验证，而非强行要求不可能的几何。
- 小范围抽取 RenderCaptionRouteButton，Debug 原 ID/图标和交互保留，Feedback 复用 selected/hover/press/foreground alpha、键盘启用、关闭导航浮层和 release 屏障路径，并增加本地化 tooltip。
- NotifyNavigationDestination 和 RenderNavigationPane 对 Feedback 与 Debug 同样 ClearIndicator；Feedback 无侧栏独立项，旧选中条不会继续留在 General/其他项。标题栏点击通过原有 navigateTo 保留离页保存和主页动效终止行为。
- 原 hero、品牌曲线、开屏策略、字体/主题/配置写回及 HomeContent 聚合接口保持；没有因新增页面改写主栏/PPT 或此前已清理的 signal1。已只读确认整个 signal1 空字段块与 HEAD 相同。

## 测试审查与证据边界

- 新实际 ImGui renderer 测试通过真实导航 ID 注册取 item 矩形后点击，分别核对两个推荐动作、四 common 动作和地区社区链接；不复制页面高度/按钮坐标公式。
- Feedback 的 Issues/Questionnaire 动作由实际 NavigationRow 点击产生；闲置帧返回 None。二维码使用唯一 texture ID 检索真实 draw command/索引顶点，核对无需 hover、在渠道后方、可用宽度与原图比例，而非只比配置常量。
- 新内容在三语言、明暗、多个系统 DPI 和窄/宽列组合下进入现有测试；标题栏 geometry、真实导航 modal 覆盖、清侧栏选中和不触发正文动作也有回归。
- 本代理仅阅读这些测试，未运行。主会话仍须记录完整 Debug|ARM64 Solution、实际 --no-window/适用离屏 renderer、i18n、格式与产物验证退出结果。未启动实际页面/浏览器，也未验证外链网站内容或实际相机扫码。
- 已读取本轮7个 C++ 源/测试文件字节，均保留 UTF-8 无 BOM/CRLF；最终整体 diff 与原有脏文件保留由主会话统一核验。

## QR CPU 夹具修正复核

主会话首次完整 --no-window 遇到的是新增测试解析尚未上传的动态字体纹理时触发 GetTexID 断言，非产品反馈页故障。已只读核对本地 vendored imgui.h：ImTextureRef(ImTextureID) 将 _TexData 置空并保存 _TexID；动态字库则通过 _TexData 指向 ImTextureData，ImDrawCmd::GetTexID 在该纹理尚未上传时明确断言。

新增 QR 循环现仅匹配 `!command.TexRef._TexData && command.TexRef._TexID == qr`，与显式用户纹理 ID 合同一致。唯一二维码纹理的真实 draw command、ElemCount、索引/顶点范围，以及无需 hover、渠道顺序、宽度和比例断言均保留；没有伪造字库上传或忽略二维码不存在的情况。新增简短中文注释准确。此局部夹具修正无新审查问题。

产品源码未因此改变。主会话正在重新增量构建完整 Solution 并重跑完整无窗口套件；复核时测试重跑结果仍待完成，本代理未执行测试或 GUI。

## 社区与作者联系追加、导航矩形夹具修正复核

本次两项小增量已只读核对，无新增阻断问题；未构建、运行测试或 GUI。

- RenderFeedback 在常驻二维码卡之后追加交流/联系分组，先 Community、再 Contact，并只读展示原作者 QQ。新动作追加于 FeedbackAction 尾部，不偏移原 Issues/Questionnaire；原主页作者/社区入口未被搬走或删除。
- 简中社区卡标题使用原 QQ 群标题和群号，说明准确指向 QQ 用户群；繁中/英文使用既有 Discussions 本地化标题，说明与 GitHub 讨论区一致。邮箱与个人 QQ 均为已有公开联系信息；个人 QQ 不另造未约定跳转。
- 原生消费者仅在点击返回 Community/Contact 动作后使用与 Home 完全相同的地区 QQ/Discussions 目标及原 mailto；renderer 本身仍无 ShellExecute。四个新增用户文案键具备简繁英对应文本，504/504 生成链结果由主会话登记。
- 本地 vendored imgui_internal.h 的 WindowRectRelToAbs 以 window->DC.CursorStartPos 转换，而非 window->Pos；imgui.cpp 对导航矩形也通过 WindowRectAbsToRel 从 LastItemData.NavRect 写入。新 focusRect 使用该实际内部 API 还原窗口 padding/scroll 下的坐标，符合真实导航合同。
- 原社区链接 test 先取得真实 ID、确认 NavIdIsAlive 后再取矩形点击的步骤未被削弱；移除错误的仅加 window.Pos 计算后，测试仍要求真正点击产生 Community 动作。新增反馈测试扩展为实际点击 Issues/Questionnaire/Community/Contact 四个卡，QR draw command 和布局检查继续保留。
- 主会话报告上次完整套件只有 36 次 Home community 点击检查失败，其余检查通过；该次数与多语言/主题/DPI/宽度组合重复一致，但本代理不据此代替重跑。当前主会话正在重新编译并执行最终完整无窗口套件，最终退出结果仍待登记。
