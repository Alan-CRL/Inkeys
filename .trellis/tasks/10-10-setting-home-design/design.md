> 2026-10-11 运行时修复直接授权：先阅读 texture-crash-revision.md。真实 DX11 纹理创建失败/部分资源清理及 DeviceLost 传播优先；保留第五轮已批准入场行为。

> 2026-10-11 第五轮已批准：先阅读 brand-entry-revision.md。并行书写后完整停留300ms，再背景渐退与曲线路径归位；仅Show播放、Hide复位主页顶部及实际尺寸门禁覆盖旧冲突约定。

> 2026-10-11 第四轮直接授权：先阅读 soft-sheen-revision.md；克制背景/文案反光、字标不参与及本轮commit边界覆盖旧冲突约定。

> 2026-10-11 第三轮直接授权实施：先阅读 ripple-revision.md；圆头接缝与明显横幅波纹要求覆盖下方冲突约定。

> 2026-10-10 第二轮已批准实施：先阅读 follow-up-plan.md。本轮布局、删除入口及动效要求覆盖下方第一轮的冲突约定；其余要求继续适用。

# 技术设计

## 边界
Setting.Pages.cpp RenderHome → Setting.cpp HomeAction；ShellExecuteW在当前业务范围已宏映射异步队列。保持ImGui/ImFluent/共享renderer、字体、配置、副作用、稳定ID，不新增设备/纹理或动态动画管线，不改其他页面/vendor/网站/全局卡片和按钮。

## 字标与横幅
来源D:/Project/Inkeys/website/docs/.vuepress/theme/components/NewHome3/glyphs.ts、softPen.ts、styles.ts。完整default hard/rainbow/medium笔画及弧长渐变、浅深两套彩虹。网站只读。
本地矢量辅助头/数据缓存归一化几何，由现有ImDrawList缩放平移绘制；保留抗锯齿/圆接头，不逐帧采样曲线，不换成普通字体/截图。
8 DIP圆角，内容宽>=700 DIP用28内边距否则20；字标宽可用内宽80%、上限520 DIP，等比含下伸。高度按字标/实际文本/按钮求和，标语正常可读字号，字标/标语/入口居中，按钮不足则上下排。渐变限制横幅内，局部主题色不污染其他页。

## 文本/链接/布局
所有文案进三语言JSONC；复用welcome/hero键，新增键避免Win32宏冲突。标语分段后合并全文保持PRD内容。
版权署名正文层级，说明次要正文。许可句有前缀+完整license链接+后缀；正常宽inline，窄宽自然逐词/行换行，每个license片段均可点击，不省略、不把其余说明变链接。
开源说明同样保留完整句子，内嵌GitHub/GitCode链接。文本至少280 DIP、gap24；按钮列等宽纵排，过窄移下自然排列/换行；实际测量决定高度。
HomeAction末尾追加License/GitCode保留旧值，GitHub/Support复用；目标URI留业务边界，纯绘制仅返回动作。
新点击项支持既有AllowContentDrag候选/释放屏障，保持键盘焦点/hover/pressed，拖动取消动作。其他主页ID/行为保持。

## 格式与国际化
保留原编码/CRLF，关键逻辑中文注释，最小diff。JSONC改基准/翻译，经Scripts/i18n.ps1 sync生成header/快照再check，不手改生成头。新头若需工程登记才作必要登记，不清理旧资源。
