# 设计

## 标题栏与导航
TitleBarGeometry新增debug按钮rect。Debug复用主题32DIP槽及既有8DIP间距，Bug使用ImFluentIcon_Bug。最小化等位置不变；主题/版本向左整体移动。渲染和WM_NCHITTEST共用几何，debug返回HTCLIENT；保留现有窄窗版本隐藏和最小drag预算。删除主题tooltip，Debug不添加tooltip，选中底色采用现有主题token。
通过现有navigateTo/同步destination流程进入Debug，关闭overlay/flyout，重复点击不切页。移除sidebar debug测量/绘制和footer额外预算，更新相关纯布局内部接口及测试。Debug时清空indicator锚点并停止X/Y补间；返回普通页首次绑定新anchor用Reset，保留普通页之间160ms既有动画和滚动补偿。

## 内容
四项consoleToggle完整迁移到tab9“控制台输出”，原ID/文案/字段/QueueConfigWrite保持。实验页其余项保留。
新增内部Design::RenderEdidCard(const char* text)，产品/现有headless共同调用。合并同一Display Snapshot生成的现有诊断文本，段间换行，保持全部字段、顺序、Unavailable原因。按可用宽度扣实际卡片padding，用Design::TextHeight真实排版测量完整高度并约束卡片子窗口的完整高度，避免AutoResizeY被parent剩余区域截限；确认内部ScrollMax=0。可复用BeginCard但不改vendor。正文超屏由现有父content滚动；缩放、数量、宽度变化即时重算。其他DRAW3_RTS_DIAGNOSTICS条目及宏保留，不扩大功能。

## 兼容
不改公开Setting接口/配置schema/显示器枚举/日志策略/线程/工程工具链。按原UTF8 BOM/CRLF修改。产品源码与测试只改本轮必须部分。任何实施发现的设计矛盾先报告主会话。

## 实施边界补充
360 DIP极窄标题栏加入Debug后按旧identityLeft会使drag不足96 DIP。先使标题文字完全让位，再仅缩减icon后的空白，使drag预算满足原合同；正常720 DIP及以上间距保持。空标题clip不得反向，Debug/主题/caption命中互斥。此为已批准窄窗让位策略的边界实现。
