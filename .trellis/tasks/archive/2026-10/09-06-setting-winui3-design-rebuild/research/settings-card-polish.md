# 名片复制、二维码与卡片边框精修（2026-10-24）

## 当前范围与共享接口

接续父任务，本轮按截图精修，不改既有赞助、地区社区、反馈问卷/Issues目标或此前清理的CI标记。用户最后“二维码png需要视频深色主题”按语境解释为适配深色主题。

- Controls归属：Setting.Controls.h/.cpp。新增 `bool CopyHyperlink(const char* id,const char* label,const char* value)`，使用和作者标签/社区链接相同的Body14字体。以稳定控件ID/窗口storage记录当前连续悬停copied状态，点击调用ImGui::SetClipboardText，点击后tooltip为Design.Copied，普通hover为Design.ClickToCopy；移出/未连续渲染复位。独立UI本地剪贴板动作不走Shell/config。根测试替换Platform_SetClipboardTextFn为内存mock，绝不改用户系统剪贴板。
- Controls调查普通SettingRow和BeginCard（反馈/赞助/EDID均用后者）的真实border token/描边几何/alpha差异；在公共wrapper或共享primitive做最小修复，不逐页改色，不升级第三方。可追加 SettingRow / NavigationRow 的可选 iconTint 参数（默认white兼容旧调用），让GitHub透明白色mask按TextPrimary着色；具体签名先向主会话确认。
- Pages+i18n归属：Setting.Pages.h/.cpp，简繁英及snapshot/生成头。作者QQ label与群label均Body14，对齐numberControl14，简介后增加间隔；QQ使用CopyHyperlink，首页email同样复制且不再发邮件。移除首页Contact按钮，Sponsor.N统一“请我喝咖啡~”（TC/EN相应翻译），原Support动作不变。添加Design.ClickToCopy/Copied简繁英键。
- RenderFeedback可加第三个可选参数 `ImTextureID githubTexture=0`，Issues用实际GitHubmark image按TextPrimary着色。反馈QQ行右侧CopyHyperlink，邮件NavigationRow继续返回Contact发邮件。二维码用普通SettingRow右侧紧凑96-DIP图像、左侧标题/说明自然换行，实用最小宽度下top/bottom/right内距相等；布局不使用原240-DIP大图Card。原图在native加载时只把RGB规整为white mask并逐像素保留alpha，然后UI tint=TextPrimary随主题换色，原二维码PNG文件和模块结构保持。必要时短化英文标题/说明以保持语义和紧凑排版，不能裁切正文。
- 根会话负责Setting.cpp/资源声明与新增GitHub实际资产、测试与整合。二维码slot7只改RGBmask，不重建主题切换资源；新增GitHub资产复用既有会话SRV/epoch/释放流程，读取真实图标来源，不用错误Segoe字形冒充GitHub。实现前先确认现有数组容量/预热索引与资源维护。

## 验证约束

不启动GUI/浏览器/Office/外链，不用Computer Use。用真实ImGui平台剪贴板mock检查QQ与首页email的准确内容、点击/持续hover/移出/重新hover、相邻item隔离、反馈QQ复制而反馈email仍发邮件动作；检查Body字体、间距和不同语言DPI宽度。二维码与实际GitHub user texture draw命令分别检测tint/位置，不能解析未上传font backend TexID；矩形换算使用WindowRectRelToAbs。边框比较应观察公共row与Card的真实DrawData/像素，避免只断言相同常量。

编译沿用ARM64原生MSBuild、同invocation PATH workaround、现有Solution Debug|ARM64；前轮已知并发会C1060，使用/m:1与本次CL_MPCount=1、MultiProcessorCompilation=false，不写工程配置。完成后适用设置专用无窗口、i18n、编码/CRLF/BOM、diff与CSO/CI保护检查，不创建commit。

本轮实现和验证均已完成；先前失败及修正过程保留为证据，最终结果见末尾。

### 页脚使用条款追加

在本轮进行中，用户要求在主页最下方“官方网站”之前增加“使用条款”链接。简体中文目标 https://www.inkeys.top/tos/zh-cn.html；英文和繁体中文目标 https://www.inkeys.top/tos/en-us.html。其他页脚顺序和目标保留。

### 资源来源与基础层约束

GitHubMark.png 逐字节复制官方 https://brand.github.com/GitHub_Logos.zip 的 `GitHub Logos/PNG/GitHub_Invertocat_White.png`，来源页 https://brand.github.com/foundations/logo；PNG为294×288、6223字节，SHA-256 `0d4c235fef9efec54174a7c005fc0fe0ce2d63d35c21898ab5148587111397a9`。未生成或重绘标志；白色透明图案由现有图像tint适配主题。下载归档仅供来源比对，确认后移除，保留产品PNG。

首次编译在无窗口目标发现Controls新增IdtI18n头间接引入legacy hashlib++的fopen/_wfopen C4996。已修正分层：通过SetCopyLabels从已有i18n依赖的Shell::BindTextFonts注入文案，不修改CRT警告或工程配置；须以之后的完整Solution编译确认。

### 实施结果与已完成验证

- Home作者QQ、邮箱和反馈作者QQ均使用Body14复制链接；用户QQ群/Discussions同字号，头像简介后加12 DIP间隔。移除Home下方Contact动作，Support保持原赞助页，简繁英咖啡文案同步。反馈邮箱继续原mailto。
- CopyHyperlink以稳定ID/窗口storage处理连续hover成功、移出/缺帧复位；原生hide/show清输入同时清HoveredId，覆盖暂停渲染时hover残留。BindTextFonts注入复制提示本地化字符串，基础控件无legacy i18n依赖。
- 二维码为SettingRow右侧96 DIP，正文自然换行，真实图片按TextPrimary着色；原200×200 PNG字节不变。Issues为新增真实GitHubPNG，RC/工程登记及12槽资源缓存/预热/释放一致。
- 边框差异根因为旧vendor Card的DPI取整描边与内缩填充；项目wrapper与SettingRow共用DrawCardSurface，使用原palette、铺满fill和1物理像素stroke，保留各自圆角/child/样式栈。赞助和EDID无需逐页硬编码颜色。
- TermsOfUse只在枚举末尾追加，footer在Website之前注册，native按简中/其他语言两个精确URL分流；renderer测试只验证动作，不实际打开链接。
- 独立审查记录为settings-card-polish-review.md。反馈QQ右侧actionHeight已按真实Body14链接高申报，消除与左侧正文约7 DIP偏移；文案注入、Terms分流与ClipboardProbe析构顺序复核无未解决问题。
- 完整InkeysRepo.sln Debug|ARM64，ARM64原生MSBuild、同invocation PATH规范化、/m:1 /p:MultiProcessorCompilation=false /p:CL_MPCount=1 /nr:false：exit 0。日志settings-card-polish-build-final.log/console。保留既有第三方数值转换等warning，未改工具链/警告设置。
- InkeysHeadlessTests.exe --no-window --setting-copy-only：exit 0，日志settings-card-polish-copy-tests.log。三语言、浅深主题和7个DPI倍率使用真实字库与CopyHyperlink；平台clipboard全用内存mock，验证准确值、真实tooltip/连续hover/移出/隐藏/缺帧和公共Row/Card描边mesh。
- Scripts/i18n.ps1 check：en-US/zh-TW均507/507、PASS。源文件UTF-8/BOM/CRLF、git diff --check、原二维码/CSO/workflow字节保护通过；signal1块与HEAD完全一致。新Inkeys.exe已含两个精确Terms宽字符串、GitHub/原QR原始PNG以及原PptCOM.dll完整字节，不含四组CI人工预览文本；hash/检查记录见settings-card-polish-verification.json。

没有启动GUI、浏览器/外链、Office或Computer Use，没有改系统剪贴板，也未commit/push。父Trellis任务保持in_progress，不替代历史实机验收。

### 二维码布局回归跟进

首次完整 --setting-home-only 实际退出1，共36条失败均为新增QR等间距断言，其余断言/渲染检查无失败。原测试按图片上下各24 DIP筛Stroke，会混入前一张卡16 DIP padding+8 DIP gap附近的抗锯齿边，已改为按QR图像最早vertex index在同DrawList倒序找到最近的Stroke/透明AA连续primitive，读取完整Stroke边界，保留原1.5物理像素容差；独立审查确认不漏判缺失边框。

定向入口随后加入实际Home/Feedback renderer，在全7个DPI、3语言、浅深主题、360/900 DIP列宽复测。900 DIP通过，360 DIP暴露实际布局问题：公共SettingRow在说明列不足280 DIP时会把说明移到整个操作区下方，破坏二维码等三边间距。已追加默认false的keepDescriptionInline参数，仅扫码行true，其他行保留原自适应；说明继续在左列自然换行，图像/行高度由原测量处理。此为真实产品修正，需重新编译及运行相关回归；不按放宽断言或只缩小取样范围掩盖。

### 最终验证（已完成）

- 窄列QR产品修正之后，完整InkeysRepo.sln Debug|ARM64再次编译成功（exit 0），日志settings-card-polish-build-qr.log/console；使用原生ARM64 MSBuild与既有低并发参数，未改工具链、依赖或告警设置。
- --no-window --setting-copy-only最终exit 0，日志settings-card-polish-qr-tests.log。此入口已加入真实Home/Feedback，覆盖7 DPI×3语言×2主题×2列宽=84种布局组合，以及真实复制/持续hover/移出复位、反馈邮箱动作、咖啡原路由、条款独立动作、GitHub/QR tint、96 DIP比例、QR等三边间距与共享描边。原1.5物理像素容差保留。
- --no-window --setting-home-only最终exit 0，日志settings-card-polish-settings-final-tests.log。既有WARP/hardware离屏渲染、缓存/滚动/导航/字体/EDID、主页及新卡片测试全部完成，没有failed/Assertion；此为修正后的实际完整设置回归，不使用首轮exit 1作为通过证据。
- 最终源码格式、保护文件与编译产物已再次核对，详见settings-card-polish-verification.json；CI signal1与HEAD相同，原二维码/CSO/workflow及PptCOM字节均保持，新PNG与精确条款地址进入EXE。i18n仍为507/507。git diff --check通过。
- 最终独立审查包含QR精确primitive取样、仅扫码行keepDescriptionInline选项、SetCopyLabels分层和Terms语言分流，无未解决问题。

本轮未启动GUI/浏览器/Office/Computer Use、未调用实际外链或系统剪贴板，未创建commit/push。没有声称人工窗口观感或相机扫码验证已执行；这是按仓库当前非GUI验证规则保留的实机边界。父任务继续in_progress。

提交前最终文案修正：按用户最新要求，主页/侧栏共用Sponsor.N恢复HEAD原文案（简中“赞助我们”、繁中/英文亦恢复原翻译），同步简中snapshot。该要求覆盖前文咖啡命名；既有跳转不变。遵照用户要求仅运行i18n检查，不再执行编译/额外回归；前述编译/运行结果对应改文案前版本。继续授权commit，不结束/归档任务。

提交后追加小修：用户最终明确仅去除标题栏问题反馈按钮的悬停提示，撤回移除选中状态的要求。RenderFeedbackButton向既有helper传nullptr，不改变selected、点击路由或隔壁调试按钮。该单行可逆显示修正未新增测试/编译或GUI验证；用户授权补充commit，父任务继续in_progress。
