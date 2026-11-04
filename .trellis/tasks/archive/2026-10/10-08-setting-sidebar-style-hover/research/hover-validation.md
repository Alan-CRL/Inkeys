# 侧栏共享颜色与无窗口验证

## 真实旧算法复现
- 先只新增 setting_design_tests.cpp 的真实控件回归，LerpColorU32 保持旧逐RGB/alpha定点插值；独立已有 Headless 工程用 ARM64 native MSBuild 编译 EXIT=0（6.9秒，900秒允许）。独立工程输出在 InkeysHeadlessTests/Build/ARM64/Debug，与完整Solution的根Build产物分开。
- 旧产物 --no-window 退出1，FAILED count=2；两个倍率1.0/1.875各323帧、306个控件样本，每倍率117个通道越界，最坏超界0.108497（允许1.01/255）。日志 hover-old-no-window.log。
- 首次移入NavItemEx的第3帧可见通道0.813149，两端为0.952941（透明层合成AppBase）与0.905882（目标浅灰）；旧算法把透明黑RGB当可见颜色参与混合，中间显著变暗。
- 测试始终保留同一ImGui context/window storage，先连续idle两帧再移入，未每帧ResetContext。实际NavItemEx、普通Button、Combo主体、真实Combo popup的Selectable产生DrawData，再遍历clip内白像素/相同色的实际平面三角形，样点位于内填充，避开文本/图标/圆角/描边。测试只做普通source-over合成与可见两端比较，不复制生产颜色插值算法。

## 最小共享修复
- imfluent.cpp::LerpColorU32 以两端alpha作为RGB权重，插值alpha后除以它返回straight RGBA；零alpha返回透明色，t<=0或>=1保留原端点。输出最近整数，测试允许最多1LSB量化误差。
- AnimateColorU32 的持续storage、指数动画时长、目标色、控件交互均未改变；没有关闭动画或调浅目标。关键原因中文注释和 INKEYS PATCH 标记已补；UPSTREAM.md 本地补丁第7项可追踪。
- 真实重复option labels的两个Combo使用不同PopupId/Popup Window，拥有独立storage；旧算法及修后都未复现popup owner复用污染，因此未增加不必要的Combo实现补丁。

## 修后控件与几何覆盖
- 独立Headless修后编译EXIT=0（5.1秒）；初步完整--no-window EXIT=0/PASS animation correctness。该轮各倍率328帧、307样本，0通道越界，最坏0.000492036 < 1LSB。每倍率Nav84/普通Button84/Combo84/Popup Selectable55样本；Nav最大超界0.000430644、Button0.000492036、Combo和Popup0。
- 覆盖首次移入、连续趋向、快速进出、Nav/Button按下和松手一次激活；Popup真实打开/同owner重开/不同owner相同options、快速进出、主题原地切换、press后release一次选择且关闭popup。浅→深→浅不重建context/atlas。检查闲置Combo值不改，Popup选择仅一次返回changed。
- 实际内嵌Fluent字体14个发布语义glyph逐个查cmap并DrawIcon，验证实际顶点存在且在24-DIP槽居中；不是只比较代码点或常量。
- 生产 ResolveNavigationPaneGeometry 直接用于720×520、960×700、1440×900，100/125/150/187.5/200/400%倍率，以及展开/收起（窄窗展开为overlay）的36项矩阵；检查固定区与中段互不重叠、36行高、1DIP分割线顺序、Debug 28高/自然宽居中/compact隐藏、全部剩余高度只分配中段。
- 真实ImGui三个child使用同一生产geometry和真实NavItem/AccentNavigationItem/CaptionNavigationButton：三帧warmup→中段最大scroll，固定child原点和ScrollY保持，最后第八项确实绘制可达；赞助展开/收起均填充，Debug只4个中文caption glyph，无图标。最后补浅深红关闭实际foreground及root/top/footer ScrollMaxY=0检查，已在下列完整Solution及最终no-window通过。
- session测试直接复用既有 ResolveWindowFrameCreationCorrection：倍率2不足client增长、足够client不缩水、工作区不足夹紧；纯helper不执行HWND消息。Setting.cpp私有ensure/pending恢复接线由独立checker审查并纳入完整编译。

## 最终整合验证
- 第一轮最终完整 InkeysRepo.sln Debug|ARM64，BUILD_EXIT=0，实际21.0秒；sidebar-solution-stdout.log / sidebar-solution-stderr.log。全部生产侧栏、共享色彩和Setting.cpp私有UIScale窗口消息编译/链接成功。
- 第一轮最终根 Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window，HEADLESS_FINAL_EXIT=0 / PASS animation correctness；sidebar-final-no-window.log。含真实浅深关闭foreground及固定/root无scroll range等新增检查，没有assert/failed。
- 独立checker发现两处测试覆盖遗漏：Combo主体press/open/release帧此前实际绘制但未颜色verify，末次Popup选择case只有if可能静默跳过。仅补测试：每次打开的pressedBody/openBody均对实际主体DrawData进行target2两端断言；末次先Expect popup确实打开，再press/release检查值与changed一次提交。不改生产Combo或Lerp。
- 覆盖补严后的必要完整Solution增量，BUILD_CHECKED_EXIT=0，17.2秒；sidebar-checked-solution-stdout.log / sidebar-checked-solution-stderr.log。两次均ARM64 native MSBuild由vswhere动态发现，同一invocation保存Path→Remove Env:PATH→恢复单一Path，MSBUILDDISABLENODEREUSE=1，/m:1 /nr:false，允许1800秒；首轮另显式断言Path键数=1。
- 最后根Solution重新编译产物 --no-window，HEADLESS_CHECKED_EXIT=0 / PASS animation correctness；sidebar-checked-no-window.log。每倍率1.0/1.875均328帧315样本，0通道越界，worst=0.000492036；Nav84/普通Button84/Combo A88/Combo B4/Popup55。补严前307samples作为历史保留，最终比它多8主体press/open样本，没有缩减测试。
- 最后i18n check EXIT=0，英文和繁中均366/366；四个准确自有源码/记录路径git diff --check HEAD EXIT=0，UTF-8无BOM、全CRLF保持。继承发布历史的空白诊断仍由前置任务记录，不清理历史文件。
- 新构建与回归日志仅移除行尾空格，保留诊断正文、顺序、数值、原编码/换行及真实失败历史。未git add/commit/push。已有HiMsg/hashlib/传统Setting类型转换等warnings保留，没有为0warning做无关修改。

## 边界
无窗口工程链接Design/Controls/Typography/ImGui/ImFluent，未链接整个Setting.Shell或Pages。真实child/控件fixture直接复用生产geometry/wrappers，但不能宣称完整Shell点击、公共Setting事件/FIFO、真实鼠标观感或DX11/DWM呈现已验证。Shell顺序/路由/稳定ID/overlay/Debug显隐由来源与独立静态复核覆盖。未运行GUI、Computer Use、HWND fixture、Office；不同真实显示器和受限工作区的窗口恢复仍需实机验收。
