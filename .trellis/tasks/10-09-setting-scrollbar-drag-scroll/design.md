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
