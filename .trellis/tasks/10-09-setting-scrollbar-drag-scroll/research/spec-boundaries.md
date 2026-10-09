# 本子任务规范边界（实施及独立检查）

原规范：.trellis/spec/native-desktop/rendering-and-ui.md 的 Setting Design / Hierarchical Navigation / Theme 段落。该文件超过单文件注入预算，本研究文件保存与本子任务直接相关的现行合同供代理注入；疑问仍需查看原对应段落，不能把摘录理解为更改原规范。

- NavigationState 的宽窗偏好与窄窗 overlay 分开；Resize 不覆盖 desktopCompact，窄窗展开不改 content rectangle。248/48 DIP pane、900 DIP 阈值和 x=24 图标轴保持；普通行36 DIP、图标16 DIP、menu/footer inset 4。顶部汉堡/主页/语言固定；汉堡默认透明无边框，局部将Fluent Button hover/held填充映射为当前palette的subtleHover/subtlePressed，保留真实按钮颜色动画、ID、键盘焦点与开合逻辑，不改全局普通按钮色。中段六个根入口按通用/绘制/插件/组件/实验/版本独立child滚动；通用包含基本/外观与动画/交互与安全/存储与配置，子项缩进24 DIP，底部社区/普通蓝字赞助/重启/红色关闭固定。三处1 DIP细线替代分组标题；Debug使用标题栏Bug图标入口，侧栏不再绘制或预留调试行。ResolveNavigationPaneGeometry先测固定行/分割带，再分配中段；超短pane不得顶部/底部重叠，fits=false时超出区域受窗口裁剪，实际可达性边界另记。

- 滚动条由ApplyScrollbars集中设置：8 DIP命中轨道、2 DIPpadding、透明背景和低对比灰thumb，hover/drag增强；每次从DIP重算，不能反复ScaleAllSizes。SetAccentColor会重建ImFluent预设，必须先调用再覆写项目token，否则先写入的项目样式会被预设覆盖。侧栏中段使用原ImGui自动竖直滚动条；按当帧ScrollbarY在已扣轨道的可用右边界额外留RowGap(4 DIP)，WorkRect/ContentRegionRect同步，根项、父项箭头及内联子项真实绘制与命中一同缩短，无条时恢复；不重复减8 DIP、不强制常驻条，动态内容显隐允许原测量帧收敛，固定顶部/底部不变。

- 设置内容拖动原样复用历史ScrollWhenDraggingOnVoid正文（稳定##scrolldraggingoverlay、KeepAliveID、ButtonBehavior与ChildWindows fallback、SetScrollX/Y），不新增阈值、惯性、点击取消、MouseSource或Win32触摸适配。ApplyDragScrolling在本设置context全部UI提交后、EndFrame/Render前只选唯一实际活动可滚宿主：优先原overlay捕获续接，其次hover父链最近合格窗口；固定NoScrollWithMouse、NoMouseInputs、tooltip、隐藏及已关闭Popup排除，精确PopupId开放态与导航关闭/释放/正文门禁保持。正文、导航中段、实际溢出flyout与Combo均覆盖；父子不能双重消费同一MouseDelta。局部恢复CurrentWindow和End前InnerClipRect调用环境后原样还原，不伪造Begin/End栈或改字体/表格；不调用私有SetCurrentWindow。沿用(0,-MouseDelta.y)物理像素，SetScrollTarget由下一Begin消费，不直接改当帧Scroll或已生成蓝条顶点；原ActiveId控件操作保持；整个ImGui区域外按下时MouseDownOwned=false，移入仍由原库抑制HoveredWindow，不强制获取其输入；root MoveId出现不等于原生HWND移动，是否新增NoMove必须有实际边界证据。

- 分层导航使用稳定复合目的地，旧Configuration/HotKeys分别映射通用Storage/Interaction，PageId编号不变。新会话子列表默认折叠（groupReveal同步为0）；父行点击在开合间切换，展开时进入最近子页（首次Basic），收起只保留当前正文/最近子页且不重启正文淡入；窄窗父行展开保留overlay，真正选择叶页才关闭overlay。箭头只切展开；紧凑父项首次进入、再次弹出子项；Hide/Show不清空已选择的展开偏好。浮层优先下方、夹紧客户区，关闭点击和鼠标释放期间保持输入屏障，焦点回父项；固定区不随中段滚动。无窗口菜单外点击测试必须确认真实点击点在浮层之外，并断言业务值、目的地、最近子页和导航次数都不变，不能只看菜单关闭。

- 导航/子列表/浮层/正文/选中条由设置渲染线程推进；动画进度按dt/duration累计，速率中途变化不得回跳，反向从当前可见值开始。同一选中锚点的滚动/布局位移必须同步平移标记坐标与正在运行的补间，不重置进度；真正选中锚点切换才重定向过渡。Debug选中时不绘制侧栏蓝条，并清除旧item/window锚点、停止X/Y补间；返回普通页的首个锚点直接Reset归位，之后普通页之间继续既有切项动画。标题栏点击Debug复用导航流程并关闭overlay/flyout，重复点击不重启切页；Debug页后续可重新展开导航，不能在每帧同步目的地时强制关闭浮层。选中条保持3×16 DIP，用上下半宽圆弧形成完整圆头；ImGui PathRect会将窄条圆角钳制到半宽减1px，不能只设置半宽radius；圆角几何和浅深主题赞助图标/文字颜色用实际DrawData验证。隐藏/跳过绘制后的首帧时钟重基准，低帧率仍按限幅dt推进。子列表动画按实际裁剪与几何命中，正文只运行一套控件并对实际顶点alpha淡入，不混改RGB。

`RunSettingDesignTests` 复用实际 Design/Typography/Controls 和 ImGui/ImFluent CPU core，无 HWND、无 DX11 backend。多行说明回归用实际简/繁/英资源、浅深主题和六倍率连续帧，包含真实滑动条、按钮加下拉框、较高自然宽换行组及下一张卡片。说明顶点在action回调入口截取；Combo箭头也使用TextSecondary，不能在整卡结束后仅按颜色采样冒充说明字形。覆盖导航往返与 overlay、真实中英文测量、action 右锚/重排、SC/TC regular/bold 字形与多行 TextAt 顶点、NavItemEx 图标轴、真实控件 DrawData、末项 SettingRow 后 End。动态 atlas 纹理仅在 CPU 测试中模拟完成，不将测试纹理 ID 传入产品。EDID卡片回归须验证首次帧、文本/宽度变化及超视口完整高度、内部ScrollMax为0和整页滚动；字形采样同时限定实际TextPrimary颜色与字体纹理，纹理化抗锯齿卡片边框不能仅按非whiteUV当作字形。完整 Solution Debug|ARM64 后运行 `InkeysHeadlessTests.exe --no-window`，仍不声称验证了实际 DX11 呈现或窗口交互。

新增内容拖动/中段条回归使用生产ApplyDragScrolling和RenderNavigationPane，观察真实ScrollbarRect、ItemBounds、thumb DrawData、Scroll/ScrollTarget及ActiveId；Mouse与TouchScreen来源都通过既有ImGui输入队列，不声称实机触屏已验收。覆盖三pane模式/浅深/倍率/动态显隐、轨道与gap不路由、正文AutoResize与双range/固定child单消费、flyout/长Combo/已关闭Popup与释放屏障、上下边界/反向/离窗/释放、控件所有权、context/clip恢复，以及生产root flags。不能在headless额外NoMove掩盖嵌套void的原MoveId行为，也不能只检查样式值声称实际滚动成功。

## 本轮授权与验证边界

用户明确要求新建父任务的子任务并原样复用settingui2拖动算法，已于2026-10-09审查prd/design/implement并回复“批准并开始实现。”，任务in_progress。父任务继续进行中，直接fluent，不GUI/Computer Use、不自动commit/push。工程验证复用完整ARM64 Solution及--no-window；编码/BOM/CRLF与中文边界注释遵守根AGENTS.md。
