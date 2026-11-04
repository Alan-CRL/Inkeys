# Setting布局与滚动规范摘录

以下为规划基线规范的原文摘录。当前任务R2/R6/R7明确改变正文轨道与页面动效合同；旧规范中轨道宽度随alpha过渡仅作为现状，不能覆盖已批准的新要求。导航/Combo/flyout等无关宿主继续保留既有行为。

## .trellis/spec/native-desktop/rendering-and-ui.md:2116-2156

#### 1. Scope / Trigger

修改任何设置页面、导航、SettingsRow、控制文本、滚动条或 ImFluent 本地图标扩展时适用。全量实际页面统一设计；HWND/DWM 边界另遵守窗口合同，不新增触摸/动态脏区。设置导航及正文动画复用软件现有动画开关和速率，不新增持久化字段或调度线程。

#### 2. Signatures

```cpp
namespace Inkeys::UI::Setting::Design {
struct NavigationState; // desktopCompact / narrow / overlayOpen
ShellGeometry ResolveShellGeometry(float widthDip, float heightDip, const NavigationState&);
RowMeasure MeasureRow(float width, float actionWidth, float actionHeight, bool hasIcon = true);
RowGeometry ResolveRowLayout(const RowMeasure&, float textHeight);
MultilineRowGeometry ResolveMultilineRowLayout(const RowMeasure&, float titleHeight,
    float descriptionHeight, float inlineDescriptionHeight, float reveal);
void ConfigureRowAnimations(float delta, bool enabled, float rate);
ImFontConfig HarmonyFontConfig(float rasterizerDensity);
float TextHeight(const char*, float widthPixels, ImFluentTextStyle);
void TextAt(const char*, ImVec2, float widthPixels, ImFluentTextStyle, ImU32);
HomeAction RenderHome(const HomeContent&);
void RenderEdidCard(const char* text);
void BeginDragScrolling(const NavigationState&); // NewFrame 后、任何窗口首次 Begin 前一次
void ApplyDragScrolling(const NavigationState&); // 本帧 UI 提交后仅捕获/释放
GeneralEvents RenderGeneral(GeneralDraft&);
}
```

#### 3. Contracts

- NavigationState 的宽窗偏好与窄窗 overlay 分开；Resize 不覆盖 desktopCompact，窄窗展开不改 content rectangle。248/48 DIP pane、900 DIP 阈值和 x=24 图标轴保持；普通行36 DIP、图标16 DIP、menu/footer inset 4。顶部汉堡/主页/语言固定；汉堡默认透明无边框，局部将Fluent Button hover/held填充映射为当前palette的subtleHover/subtlePressed，保留真实按钮颜色动画、ID、键盘焦点与开合逻辑，不改全局普通按钮色。中段六个根入口按通用/绘制/插件/组件/实验/版本独立child滚动；通用包含基本/外观与动画/行为与安全/存储与配置/快捷键五个子页；展开树与紧凑浮层的数量、完整高度、命中和滚动均取同一 GeneralSectionCount，子项缩进24 DIP，底部社区/普通蓝字赞助/重启/红色关闭固定。三处1 DIP细线替代分组标题；Debug使用标题栏Bug图标入口，Feedback使用其左侧反馈图标；二者均不在侧栏绘制/预留行，进入时立即清除旧侧栏选中条。ResolveNavigationPaneGeometry先测固定行/分割带，再分配中段；超短pane不得顶部/底部重叠，fits=false时超出区域受窗口裁剪，实际可达性边界另记。
- RowMeasure/RowGeometry 使用 DIP，TextHeight/TextAt 接收物理像素宽；最终绘制统一乘 FontScaleDpi。设置卡片默认标题＋完整多行说明，action 按实际宽度靠右。按当前内容宽度及 action 自然宽度动态判断文字预算：说明区不足280 DIP时采用标题/action在上、说明下方通栏，返回行内保留16 DIP余量；标题区不足160 DIP时沿用极窄控件换行。以上是项目预算，不是WinUI官方断点，不以控件数量/固定单行判断代替测量，不缩小字号、截断或默认折叠完整说明。长文行高由实际字体测量而来；测量不运行 action，一帧只在实际控件绘制时产生事件。
- 多行说明布局的补间使用当前ImGui窗口与稳定行ID的StateStorage，随context释放；复用NavigationTween、160ms/已有动画速率和设置帧时钟。先使说明下移避开操作，再扩宽；反向先收窄再上移。首次出现按当前预算直接到位，缺席/隐藏后首帧不累计休眠时间，关闭动画即时到位。卡片及下一行的真实ItemSize包含当前换行包络；说明行数随真实宽度离散变化，不靠裁切换取高度平滑，不绘制两套可交互控件或增加配置/线程。置顶间隔完整说明常驻同一卡片，不另设“详细说明”展开块。
- 新字面按内嵌 HarmonyOS 1213/1000 度量校准，字号与行高分别管理（12/16、14/20、28/36）。基线补偿必须通过实际 glyph/DrawData 验证；不能把 ExtraSizeScale 当行高。TextHeight/TextAt 共用相同换行与行距逻辑。
- 旧 ImFontMain/Strong 保留给标题栏边界；校准 regular/strong 和独立 icon face 常驻同一 atlas/session，全部内容页和导航使用统一文字角色，离开内容区后恢复原角色。禁止重新引入按页面分支选择旧字体。正文/说明/标题保持14/20、12/16、28/36，字面按1.213×TextOpticalScale（当前0.97）校准；控件文字单独使用ControlText（当前13/20）。
- 所有普通控件通过 Design 的13 DIP字体wrapper；ButtonWidth与默认Button/AccentButton使用同一天然宽公式，显式尺寸仍由调用者决定。ButtonGroupRow先测各按钮自然宽，再按可用预算换行且每行靠右；Action只在实际绘制时执行。赞助复用普通NavItemEx背景与输入，文字和图标均用与选中条相同的AccentFillDefault亮蓝主题色，选中条统一处理，关闭只覆盖浅深主题criticalText语义色，保持普通背景交互。
- 调试页“控制台输出”完整迁入TouchArea/PptCOM/Draw3/Haptics/Cursor五项，原国际化/稳定ID/字段/FIFO保存保持，IDT_RELEASE不隐藏入口或五项；实验页其余条目及其他诊断宏保持。下方RenderEdidCard接收同一Display快照生成的完整诊断文本，保留拓扑和所有监视器字段及不可用原因；按实际内宽、字体换行和padding确定完整高度，不被父可见区域限制成内部滚动框。卡片与当前主题一致，内部ScrollMax为0，超屏由整页滚动承载；宽度/倍率/文本变化重新测量，快照不可用在卡片内提示。
- Page/Section/SettingRow/Details/Card/Notice/NavigationRow共用文本行框和尾部占位，旧34%操作列禁止恢复。PPT两组缩放等独特内容可专用组合，但仍使用公共控件和真实action测量；缩放按发布行为即时应用、松手保存，同步方向只由本帧真实编辑或加入同步事件决定，不以尚未保存的 Record 值推断。插件详情各自用稳定子页ID保存滚动/展开状态。
- 滚动条由ApplyScrollbars集中设置：8 DIP完整命中轨道、2 DIPpadding、透明背景和低对比灰thumb，hover/drag增强；每次从DIP重算，不能反复ScaleAllSizes。SetAccentColor后再覆写项目token。设置全部实际竖直滚动宿主共用本地Begin部署，原生ScrollbarSize和thumb三态alpha按window Storage显隐进度过渡，真实溢出决定目标，复用160ms/现有动画开关与速度。正文、导航中段、可滚Card/Details/flyout/Combo必须在items提交前获得同一可用宽和裁剪；固定顶部/底部不变。侧栏native右padding已有4 DIP，不能再叠加RowGap；按钮、父项箭头和内联子项的真实绘制/命中均避让当帧轨道。消失尾帧保留渐退宽与无输入装饰，最终零留白；反向从当前值续接，关闭动画立即归位，不强制常驻条、不重复扣轨道、不产生横条。native child装饰可写parent DrawList，不能靠事后改全页顶点或只读child DrawList实现显隐；退出thumb只做必要常数几何处理，避免全顶点扫描。本地Begin只备份/恢复ScrollbarSize、ScrollbarPadding和三态颜色实际字段，不夹用跨Card/Details vendor Begin/End的Push/Pop栈；Details scope须在content回调前恢复，防止嵌套条进度相乘。
- 设置内容拖动由BeginDragScrolling/ApplyDragScrolling分阶段接入当前ImGui1.92.7：首次按下在控件提交后确定唯一实际可滚宿主；已取得overlay捕获后于NewFrame后、宿主首次Begin前投递ScrollTarget，使用上一帧有效态与稳定ID校验，让正常Begin同帧更新Scroll/cursor/clip/items/DrawData及蓝条。不能在帧末才投递持续位移或Render后挪顶点；无输入快速返回，持续捕获不重复ButtonBehavior/FocusWindow或全窗口遍历，不引入Win32触摸适配、默认惯性或额外滚动补间。父子只消费一次物理MouseDelta，边界/低速累计/外部scroll目标与反向重基准须验证。主页真实features/common链接Card与长列表Selectable可登记稳定item/host作为内容拖动候选，使用ImGui原有拖动阈值，轻点保留动作、超过阈值转移capture并取消该次点击；Slider/InputText及native scrollbar保持控制权。storage键以稳定window seed计算，不随当前PushID栈变化。固定NoScrollWithMouse、NoMouseInputs、tooltip、隐藏/未提交及关闭Popup排除，精确PopupId和导航关闭/释放/正文门禁保持；整个ImGui外未归属Down移入不强制捕获。状态随context释放，不保留跨session裸window指针。旧ScrollWhenDraggingOnVoid仅为已提交历史基线，本轮用户已授权优化，不能用其ChildWindows fallback与不可改正文限制替代完整主页回归。
- SetIconFont/DrawIcon 与 NavItemEx 是有 UPSTREAM 记录的 ImFluent 小扩展；图标用其真实 glyph bounds 在固定槽内居中，不混入正文基线。
- ImFluent共享颜色动画在RGB插值时按两端alpha加权，最终仍返回普通RGBA供现有绘制混合；不能把透明黑/白端点的无效RGB直接参与可见色插值，也不能靠关闭动画或改浅目标色规避。保持原时长/状态/目标，UPSTREAM记录本地补丁；回归须用真实控件连续帧DrawData合成取样，覆盖导航/按钮/Combo主体和选项、进出/按下/弹层重开/主题切换。
- Setting用户倍率松手应用后只投递私有ensure-minimum消息；HWND所属线程用最新倍率、既有720×520逻辑minimum及实际outer/client差值，只必要时扩展普通窗并夹紧workarea，不改公开接口、owner/style、最大化几何及原DPI建议路径。最大化/最小化时仅由HWND线程保留pending，恢复普通窗后一次投递重试。工作区小于逻辑最小尺寸时存在物理容纳限制，不能把无窗口几何通过当作全部固定项实机可见。
