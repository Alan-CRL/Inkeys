# 设置侧栏滚动条与旧版按住拖动复用：设计

## 范围与已确认依据

子任务属于09-06-setting-winui3-design-rebuild，直接在fluent完成，父任务继续in_progress。本轮按用户要求先研究旧代码，尚未进入实施。依据research/sidebar-scrollbar.md、research/settingui2-drag-scroll.md及research/spec-boundaries.md。

旧实现为ScrollWhenDraggingOnVoid：本地旧设置工作区与主仓历史7218d7191c5edd52f7b37a864ad035ce4625c65d的Setting.Base.cppm相同，历史Setting.cpp在每个内容末尾调用(0,-MouseDelta.y)。旧helper正文原样迁入；命名/所属内部Design文件和部署调用上下文可以调整，内部判断、稳定ID、ButtonBehavior、ChildWindows fallback与SetScrollX/Y保持原样。不引入新的触摸/Win32消息处理、位移阈值、惯性或点击取消算法。

## 侧栏中段滚动条

- 仅解除##settings-nav-list的NoScrollbar，使用原ImGui自动竖直滚动条和ApplyScrollbars的8 DIP命中轨道、2 DIP padding、透明轨道与主题灰thumb；不强制AlwaysVerticalScrollbar或HorizontalScrollbar。
- 在Begin后的真实ScrollbarY为true时，对ImGui已经扣除轨道宽的WorkRect/ContentRegionRect右边界额外预留RowGap(4 DIP)。仅调整中段绘制作用域，无条时恢复原宽，不再重复减8 DIP轨道。
- 普通项、通用父项、箭头和内联子列表共用调整后的可用右边界。父项仍先预留箭头槽；内联child自动继承缩短宽度。实际按钮背景与命中框同步缩短，不能只裁剪绘制。
- 原固定顶部/底部、图标轴、蓝条锚点与滚动补间逻辑保持。紧凑48 DIP及宽度动画中需验证实际glyph/focus ring与点击范围，不允许为图标扩大按钮命中到滚动轨道。
- ImGui自动显隐可能在内容测量后下一帧收敛；按当帧真实ScrollbarY留间距，连续帧验证展开/折叠与resize，不复制导航树高度公式制造第二份显隐逻辑。

## 旧拖动算法的调用部署

当前真正滚动宿主包括正文##setting-content、侧栏##settings-nav-list、短窗可溢出的通用导航浮层及条目较多的Combo弹层。正文自动高度页面、EDID卡片、顶部/底部固定区和动画裁剪子列表不应另行消费同一段位移。

选用设置session局部的统一调用部署：在该session本帧全部UI和弹层提交完、ImGui::Render之前，为当前指针/已抓取的旧overlay ID确定唯一最近的可滚窗口，运行原helper。部署仅从本设置ImGui context挑选本帧实际活动、可输入、可滚且非NoScrollWithMouse固定区；不对其他UI context或供应商代码修改。

- 优先沿真实hovered窗口的父链寻找最近有效滚动宿主；已有旧overlay ActiveId维持离开窗口后原held行为。先完成候选选择再调用，避免FocusWindow重排g.Windows影响遍历。
- 调用时仅在局部作用域保存/切换CurrentWindow及必要的原内容裁剪环境，结束立即恢复。ImGui的SetCurrentWindow为私有static，不能调用不存在的公开接口或伪造Begin/End栈。frame末已End的窗口ClipRect可能已弹出，按研究恢复旧helper原调用时的内裁剪；helper不绘制，不增加无意义的DrawList操作。
- 父子都存在滚动范围时只由唯一宿主处理一次，不重复SetScrollY。不调用固定/动画裁剪/不可输入的window，也不让关闭的Popup接收拖动。
- 原MouseDelta已是物理像素，沿用(0,-MouseDelta.y)，不再乘DPI。SetScroll目标与旧版相同由下一次Begin应用，不伪造当帧滚动或后验修改蓝条顶点。
- 使用真实生产root flags测试原helper与ImGui MoveId/ActiveId行为。若发现调用上下文问题，只做必要部署/既有固定root边界修正并记录，不修改旧helper内部或引入消息适配；无法满足时先报告具体证据。

## 接口与文件边界

预计只修改Setting.Design.h的内部声明、Setting.Controls.cpp的旧helper与调用部署、Setting.Shell.cpp的中段可用宽、Setting.cpp的session调用，以及现有setting_design_tests.cpp。沿用已有工程编译项，不新增配置、外部窗口API、依赖或构建系统。vendor只读。

## 验证与边界

真实ImGui CPU连续帧/DrawData验证统一条的实际绘制、显隐、命中间距、末项可达、3种pane模式、多倍率与快速折叠/resize，确保轨道/间距点击不路由。拖动验证mouse/touch MouseSource、上下边界、反向、释放、离开、滚动条与滚轮兼容、真实控件ActiveId行为及单宿主/不双滚。正文/导航/flyout/Combo分别覆盖并核对context字段恢复；保留所有既有导航/蓝条/Debug/EDID/汉堡回归。

实际完整ARM64 Solution、--no-window和国际化/编码/差异检查后trellis-check独立审查。GUI与真实触摸呈现单独留待人工验收，不自动commit/push。实现文件格式和中文边界注释遵守根AGENTS。

## 后续设计约束（2026-10-09）

第一笔实现已提交 b8beea0b。先通过生产 RenderHome、内容容器、实际滚动宿主与原 helper 调用环境定位主页遗漏；用户追加性能优化授权：允许按当前 ImGui 1.92.7 接口优化旧 helper 及部署，优先减少额外帧延迟和热路径开销，保留控件输入与单宿主规则；不引入新的 Win32/触摸消息或默认惯性。

侧栏间距以真实窗口 padding、ScrollbarSizes、WorkRect/ContentRegionRect、原按钮绘制与命中边界测量为准，修正重复空白。不能只改测试预期或裁切背景。

为设置滚动宿主建立必要的本 session/context 动画状态，复用已有 tween、动画开关及速度；显隐判定继续由真实 ImGui 溢出结果决定，不复制页面高度公式。内容可用宽与实际按钮范围同源过渡，条透明度独立保留退出阶段。处理出现首帧、消失尾帧、反向、缩放、浮层关闭、session重建，以及动画关闭即时归位。保持 native 滚动条的实际滚动/命中行为，vendor只读。

具体 API/调用时机和实测原因记录 research/followup-research.md，实施代理据此选择最小可行部署并记录。产品范围仍限设置内部及现有无窗口测试，不重构其他页面。

### 已收敛的实施部署

- 共享本地滚动 Begin 包装在原生 Begin 前按实际窗口 Storage 的显隐进度设置 ScrollbarSize 与三态thumb alpha，保留原生溢出测量/轨道分页/thumb输入及几何。出现阶段内容与命中使用同一native宽度；消失阶段只保留渐退宽度和无输入装饰，内容必须在真实items提交前使用该宽度。
- 子窗口装饰可能合并到parent DrawList，因此退出thumb不得每帧全页顶点扫描；必要缓存只含稳定几何，按当前theme/DPI重绘，实际DrawData验证与native相符。
- Combo/Expander若现有vendor调用把Begin与items封装在一起，仅为在真实item提交前接入共享动画，采用必要的设置内部局部提交包装，保留Fluent外观/稳定ID/键盘/点击/滚动语义，不修改vendor或复制无关控件实现。
- 已捕获拖动在NewFrame后、首次Begin前使用稳定host/overlay身份与上一帧有效态投递ScrollTarget；正常Begin同帧更新Scroll/cursor/clip/items/蓝条，不在Render后平移顶点。零输入不遍历窗口；首次点击在控件提交后判断归属。
- 主页大面积链接Card和长列表Selectable通过稳定item/宿主ID登记允许内容拖动，使用ImGui已有MouseDragThreshold仲裁，轻点保留动作，超过阈值移交唯一滚动capture并取消该次点击；Slider/InputText及native scrollbar保持原控制权。
- 低速累计使用实际物理位移一次，验证整数取整、小位移、外部wheel/target与边界反向，不向内容滚动增加宽度动画之外的插值或惯性。性能报告将当帧反馈与CPU采样分开，不能将约60Hz推断当成实机端到端测量。
