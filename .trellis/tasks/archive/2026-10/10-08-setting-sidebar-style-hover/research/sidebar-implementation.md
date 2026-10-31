# Fluent 三段式侧栏实施记录

日期：2026-10-08。依赖 `10-08-setting-publish-baseline-sync` 已完成17视图/12更新状态映射、完整Solution和无窗口回归。此记录覆盖本分工侧栏几何与控件包装；共享颜色动画、DrawData测试、UPSTREAM补丁由runtime分工记录。

## 生产实现

- `Setting.Shell.cpp` 保留现有 NavigationState、248/48 DIP宽度、900 DIP覆盖断点、字体角色与业务返回码，将导航分为三个稳定child。顶部与底部均NoScrollbar/NoScrollWithMouse；只有中段允许滚轮和ImGui键盘导航自动滚动，隐藏滚动条以保留完整行宽。
- 顶部固定汉堡及主页、语言。中段固定顺序常规、绘制、插件、组件、快捷键、实验选项、配置保存、软件版本。底部固定社区名片、赞助；分割线后重启、关闭，最后展开时调试。
- 删除“偏好设置”“支持与维护”标题，以三处1 DIP细分割线表达顶部/中段、中段/底部以及支持/操作层级。没有恢复预设、性能或失效页面。
- 全部正常导航行继续36 DIP高、16 DIP图标，child左右4 DIP inset；图标24 DIP槽中心在pane x=24，展开文字从x=48开始。主页/语言移入固定child后，所有item/pageID在展开/收起/覆盖往返时保持稳定。
- 赞助使用内部 `AccentNavigationItem`：真实 `ImFluent::AccentButton` 绘制、交互及焦点，独立 `DrawIcon` 和同角色 `TextAt` 保持导航轴；文字/图标用 TextOnAccentPrimary，选中状态保留同前景左标记，紧凑时保持填充与名称tooltip。
- 关闭使用原NavItemEx普通背景，仅把TextPrimary作用域换为Palette.criticalText，同时涵盖文字和图标。浅主题196/43/28，深主题255/153/164，不改变hover/pressed目标或动画时长。
- 调试使用内部 `CaptionNavigationButton`：真实Fluent Button，Caption12 DIP、自然文字宽+24 DIP内边距、28 DIP高、居中、无glyph，透明常态/低强调subtle交互；compact隐藏，desktop展开与overlay展开显示。选中Debug页保留低强调选中填充。

| 段 | 入口与原PageId/事件 | Glyph |
| --- | --- | --- |
| 顶部 | Home0、Language1 | e80f、f2b7 |
| 中段 | General4、Draw5、Plugins8、Components9、HotKeys10、Experimental14、Configuration3、Version11 | e7b8、ee56、e74c、e70b、e765、ec4a、e81e、e946 |
| 支持 | Community -2、Support12 | e716、e789 |
| 操作 | Restart -3、Exit -4、Debug13 | e72c、e711、无图标 |

图标与图二/发布源语义一致，仍使用内嵌Segoe Fluent Icons.ttf，不增外部字体。Version对Build2仍显示选中。赞助/调试只返回既有PageId，社区/重启/关闭保持原Setting.cpp FIFO和公开接口；展开偏好/选项后DismissOverlay合同不变。

## 实际高度预算与边界

`Setting.Design.h::ResolveNavigationPaneGeometry` 是生产调用的纯几何helper，Shell传入ImFluent实际NavItemHeight与Caption字体测得的debug自然宽，而非128 DIP固定底栏预算。返回menu/top/middle/footer、topItems[2]、footerItems[4]、separators[3]、debug、middleContentHeight、minimumHeight、debugVisible与fits。

顶部预算：56 DIP汉堡区域 + 两行36/4/36 + 12 DIP分割线区域 =144 DIP。展开底部预算：两对36/4/36 + 两处12 DIP分割线区域 + Debug的8/28 + 底边8 =220 DIP；compact没有Debug，底部184 DIP。八项中段自然内容高316 DIP。

| 逻辑客户区 | pane高（减32标题栏） | 展开中段高 | 收起中段高 |
| --- | --- | --- | --- |
| 720×520 | 488 | 124 | 160 |
| 960×700 | 668 | 304 | 340 |
| 1200×900 | 868 | 504 | 540 |

中段高度不足316时独立滚动，固定项位置与滚动值无关。overlay只增加pane可视宽度，正文矩形仍由既有ResolveShellGeometry保留，不推正文。

过短pane不使用负高度，也不令footer向上覆盖顶部：footerTop至少topHeight，middle可以0；fits=false显式表明无法容纳完整固定区域。正常logical720×520满足完整可达。现有WM_GETMINMAXINFO按EffectiveScale（systemDPI×userUIScale）设置最小尺寸，但提高用户倍率原本只重建字体，已有窗口可能暂小于新最小值：960×700在倍率2成为480×350，pane318小于展开固定364。协调者已授权checker以私有HWND消息在倍率变化时仅必要地grow client并夹workArea，补丁已完成并冻结；最大化/最小化状态保留pending，在恢复时补发，避免覆盖原窗口状态。本分工不改变字体、行高、窗口样式或Setting.cpp。小于完整固定区的实机workArea/最大化边界不能从无窗口测试推断全部可见，保留父任务实机验收。

## 验证记录

- 已静态检查三个child Begin/End、style/font Push/Pop、页面顺序、原事件与PageId路由，以及不含旧组标题/Preset/Performance/128预算。
- 首次独立Headless修后快速编译暴露Controls.cpp新增GetColorU32缺显式声明；已在Controls.cpp/Shell.cpp各补imfluent_internal.h一行，未改项目或依赖。
- 补include后的独立Headless编译退出0（5.1秒）；生产geometry/真实包装/14glyph/分数DPI child没有assert或failed。runtime实际颜色两倍率的Nav/Button/Combo各84采样、Popup55采样均violations0，worst0.000492小于1LSB；最终完整套件退出0/PASS。
- 本分工四文件UTF-8无BOM、完整CRLF、git diff --check退出0；旧组标题、Preset/Performance、128 DIP旧预算静态搜索无命中。
- 最后完整Debug|ARM64 Solution由runtime使用ARM64原生MSBuild执行（同一PowerShell invocation的PATH workaround，允许1800秒），退出0，用时21秒；包含所有geometry/wrapper/窗口修复和最终测试改动。
- 最终根Build产物--no-window退出0/PASS。720/960/1440客户区、六倍率、compact/expanded和浅深fixtures确认实际root/top/footer ScrollMaxY=0，固定区不动，中段第八项可达；实际Nav红前景、Sponsor强调填充、Debug自然宽28 DIP/仅四个Caption文字glyph的16vertices，以及14图标和按下/释放均通过。Headless直接调用生产geometry与Controls包装，并组合真实ImGui child；不链接完整带i18n/业务的Shell，完整Shell通过Solution编译及独立只读复核，不能把此测试当GUI点击/呈现验收。
- 本分工未改国际化源/生成物；最终i18n检查由runtime统一执行并在其记录确认。
- 本分工没有改ImFluent.cpp/UPSTREAM.md或setting_design_tests.cpp；共享hover修补由runtime完成，不通过关动画/调浅色掩盖。
- 未GUI/Computer Use、未commit/push、未git add，原工作区与chore/publish未改。

本分工生产侧栏与内部包装已完成，最后成功编译/回归后没有源码新修改。真实鼠标观感、HWND消息、跨屏DPI/最大化恢复、DWM/DComp和超小workArea物理可见性仍由父任务实机验收，未声称已通过。
