# 名片复制、二维码与卡片边框独立审查（2026-10-24）

Active task: `.trellis/tasks/09-06-setting-winui3-design-rebuild`。已阅读最新 PRD/实施计划、settings-card-polish.md 以及实现/测试源码。仅写此研究文档；未修改产品或测试、build、运行测试、启动 GUI/浏览器/Office、访问系统剪贴板或 commit/push。父任务继续进行中，旧 PPT/CI 清理、旧产物/CSO 不在本轮修正范围。

## 结论

未发现未解决的实现阻断问题。发现的反馈 QQ 行垂直对齐问题已由 root 修复并复核；最新完整构建、专用/完整无窗口运行和最终格式/资源产物检查由主会话登记，本审查不提前宣称测试通过。

## 发现及修复复核

初版反馈作者 QQ 右侧 CopyHyperlink 以普通 32 DIP 控件高度传给 SettingRow，但实际链接高为 BodyText.size + 2*SpacingXSmall（当前14+2*2=18 DIP），从 action bounds 顶部绘制会使号码字面高于左侧正文约7 DIP。已提示 root，现已确认改为按真实链接高度申报 actionHeight，未修改公共样式或其他行；测试对 clipboard.values.back 也补了空集合安全保护。

## 复制、字体与动作

- CopyHyperlink 使用稳定调用方 ID/###copy-link，并将成功状态和最近提交帧保存在当前窗口 storage；移出、非连续渲染或 HoveredIdPreviousFrame 不匹配时清除成功状态。状态在 tooltip 前取完，不受 tooltip 提交额外 item 干扰。
- 点击调用真实 ImGui::SetClipboardText，普通连续 hover 显示本地化“点击复制”，点击后当前 hover 显示“复制成功”；未引入全局复制状态或持久化字段。
- 显式 Body14 字体与作者 QQ 标签/群标题/群链接保持同字号；简介后新增12 DIP间距，首页 QQ 和邮箱复制各自原值，不再通过邮件动作。作者下方 Contact 按钮已删除，只保留原 Support 赞助动作。
- Sponsor.N 的简繁英均改为咖啡文案；路由仍是 Support，未另建赞助地址。
- 反馈作者 QQ 使用同一复制 helper；反馈页邮箱 NavigationRow 仍返回 Contact，由原生消费者走原 mailto。社区地区分支、原 Issues/问卷目标未被改写。
- 原生 Hide/Show 输入清理中将 HoveredId 与 HoveredIdPreviousFrame 清零，覆盖隐藏期间 ImGui 帧数不推进、窗口 storage 仍在的边界；无新的 HWND 同步等待或跨线程 clipboard 调用。

## 二维码、GitHub 资源与生命周期

- 反馈二维码使用普通 SettingRow 的96 DIP右侧 action，左侧标题/说明仍由生产多行布局测量；有效图像比例决定实际 actionHeight。当前简繁英说明保持问题/建议语义，普通360/900 DIP测试列下无需标题堆叠，三边 padding 由同一行几何计算。
- 原 Home_Feedback.png 与 HEAD 逐字节一致，SHA-256 为 b980e7a63e6b206b81fc0a00509f7693d097aa01595f4125a57a3a1fbc0a28a7。只读解码确认它是200×200 RGBA，非透明像素 RGB 为黑色，模块/抗锯齿由原 alpha 区分；native 改RGB为white mask并保留各像素 alpha、UI用TextPrimary tint 的方式不会将其变成实心块。未改PNG或模块分布。
- 新 GitHubMark.png SHA-256 为 0d4c235fef9efec54174a7c005fc0fe0ce2d63d35c21898ab5148587111397a9，已只读确认与下载归档 GitHub_Logos.zip 内 `GitHub Logos/PNG/GitHub_Invertocat_White.png` 逐字节相同。来源按主会话记录为 https://brand.github.com/GitHub_Logos.zip；本代理未再次联网下载。
- GitHub native load 路径按64×系统DPI生成白色alpha mask，Feedback实际接 TextureSettingSign[11]，Issues 图像显式 tint TextPrimary。默认 iconTint 白色使原四个插件彩色PNG调用保持原样。
- settingSign、TextureSettingSign、settingTexturePixels 三处均扩至12；LoadTextureFromMemory 按目标槽位更新尺寸/像素缓存，所读 DibSurface::pixels 范围是 width*height。所有 cleanup/recreate/cache 清理仍按范围或size遍历，包含slot11；requiredTextureIndexes已包含11，失败仍走原资源清理路径。未新增device、线程或主题切换资源重载。
- RC资源名、PNG路径、vcxproj与filters登记对应，只有必要图片项；原二维码加载slot7及原有槽位未偏移。

## 卡片边框

- 已从本地 vendor源码确认旧 Card 的 StrokeRect 使用随DPI取整的描边宽度，并将背景填充内缩；SettingRow 原本使用铺满背景与1物理像素AddRect，两种边界确有差异。
- 当前公共 DrawCardSurface 供 Row 与 Card wrapper 使用，统一完整背景与物理描边；Row维持4 DIP圆角，Card维持原8 DIP OverlayCornerRadius，不是全局改成同一形状。
- BeginCard wrapper保留原ChildBg/Border、三项样式压栈、AutoResizeY/NavFlattened/AlwaysUseWindowPadding、NoBackground、完整rect绘制和滚动条scope；EndCard继续用原EndCard弹栈，数量匹配。fill/border继续走原palette/card token，未逐页造颜色或修改vendor。

## 测试审查与待运行边界

- ClipboardProbe替换并恢复平台SetClipboardText回调，点击进入内存mock；实际tooltip文本从ImGui LogToBuffer读取，测试覆盖精确QQ/邮箱值、持续hover成功、移出恢复、邻项独立、隐藏输入重置/缺帧重置。不会写用户系统剪贴板。
- 真主页/反馈renderer回归增加两条首页复制、反馈QQ复制、反馈邮件动作和咖啡原路由；控件矩形继续用vendor WindowRectRelToAbs，不重新使用只加window.Pos的错误公式。
- GitHub与QR使用两个唯一用户纹理ID识别真实draw command/索引顶点，先检查TexRef._TexData为空，不读取未上传字体TexID；颜色与QR顶/底/右间距从实际顶点和行surface边界核对。
- Row/Card边框回归观察真实描边mesh UV和边界，避免只比较同一个颜色常量；Card/Row不同圆角仍保留。实际CPU回归不等同最终屏幕像素或相机扫码实测。
- --setting-copy-only复用既有真实字库/语言/主题/DPI设置，只跳过WARP等无关路径方便定向定位；无参数默认完整入口继续包含原测试，正常路径也调用新复制/描边检查。主会话仍须报告最新源码编译及适用套件的真实退出结果。

## 文案注入、使用条款与测试剪贴板最后增量复核

已只读复核，无新增问题；未构建、运行测试、GUI 或系统剪贴板操作。

- Controls 不再包含 IdtI18n/生成键头；SetCopyLabels 将上层传入的两个文案复制到内部 std::string，并对空输入回退默认英文，未保存临时 c_str 指针。Shell::BindTextFonts 在既有本地化边界注入 ClickToCopy/Copied，复制控件的状态/ID/tooltip语义保持，避免基础控件引入 legacy 聚合/哈希依赖。
- HomeAction::TermsOfUse 只在枚举尾部追加；主页 footer 在官方网站之前增加本地化条款链接，仍通过真实 Link/动作消费者处理。简中对应 tos/zh-cn.html，繁中和英语对应 tos/en-us.html；三个原有网站/GitHub/Bilibili 动作和此前删除的版本入口状态保持。新真实动作点击回归使用其实际 ###link 作用域ID。
- RunSettingDesignTests 每个字体 session 在构造 ImGuiSession 之后立即安装外层 ClipboardProbe，覆盖该 session 的现有控件测试及新主页/反馈复制。内部局部 probe 仍能嵌套捕获并恢复外层 active 指针和平台回调；析构顺序确保平台回调先恢复，再销毁 ImGui session。没有接触实际系统剪贴板。
- 完整构建与最新适用回归结果继续由主会话登记，本结论仅为源码/测试合同复核。

## QR 等边距测量最后修正复核

本次仅复核新增测试测量逻辑，产品实现未变，未发现漏判或越界问题。未构建、运行测试或 GUI。

- 原按 QR 上下各24 DIP检索整页Stroke的范围，能覆盖上一卡片在RowGap/AA边附近的顶点，不能可靠指代二维码自身行。新方法先由唯一QR用户纹理命令及真实索引计算其同一DrawList的最早vertexIndex，再逆向定位紧邻图片之前最近的完整Stroke顶点与连续描边primitive，消除了该跨卡片空间范围假设。
- 当前SettingRow的提交顺序为背景/描边、标题/说明、图片action；标题/说明颜色与Stroke不同。遍历允许同RGB透明AA顶点连接primitive，仅用完整Stroke色顶点计算bbox，符合本地imgui_draw.cpp的AddRect/PathStroke连续提交方式，未把透明AA外包络当可见框线。
- 逆向和顺向循环均检查索引边界；缺少纹理/描边时bbox保持无效，后续正宽度/QR可见性断言失败，不会默默跳过。当前夹具每帧只提交一处QR图片，DrawList和firstVertex归属一致。
- 原顶/底/右间距检查及1.5物理像素容差、QR比例/紧凑宽度/tint/无需hover与动作检查全部保留，未通过放宽阈值掩盖产品几何。
- --setting-copy-only现也调用真实Home/Feedback renderer，随现有7倍率、3语言、2主题、360/900列宽执行该测量及联系方式点击/复制；仍使用WindowRectRelToAbs和仅用户TexRef匹配，不解析未上传font TexID。
- 主会话报告上次适用完整设置套件仅有原QR范围测量断言在部分DPI失败；最新测量修正的实际重跑结果由主会话继续登记，本文不提前宣称通过。

## 窄列二维码说明保留左列复核

主会话后续精确取样确认：修复夹具相邻描边误采样后，360 DIP仍暴露真实产品布局问题，通用 DescriptionMinimumWidth=280 会把说明移到整个操作区下方，使二维码上下内距不再相等；900 DIP已满足约束。因此之前审查结论只覆盖当时静态边界，不能据此推断窄列等边距运行已通过。

最新最小产品修正已只读核对，无新增问题：SettingRow 声明/定义末尾一致新增 keepDescriptionInline，默认false；只在已有 below 目标条件前加 !keepDescriptionInline。QR是唯一传true的调用，其他控件和NavigationRow封装均继续走原默认行为。原测量、160 DIP操作区stack兜底、description宽度/高度、tween ID/storage/时钟/滞回和提交逻辑均保留。

QR固定传true时首次布局将below目标置0，说明在左侧真实可用宽度自然换行，不再退到二维码下方；实际不能容纳左右两列时，原measure.stacked兜底仍在，不裁切正文或伪造字高。常规360/900列下按实际96 DIP action与左列文案布局，当前简繁英内容保留原语义。原图片比例、tint、padding token和测试物理像素容差未改。

本代理未构建、运行测试或GUI。最新增量编译、定向及适用回归仍由主会话完成并登记实际结果。
