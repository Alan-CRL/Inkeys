# Research: UI3 taskbar scale chain

- Query: 调查 UI3 Bar、Ppt、PageControl、Display 的缩放、跨屏/变化通知，以及任务栏厚度基准的最小接入点。
- Scope: mixed (repository + official Win32 docs)
- Date: 2026-10-19

## Findings

### 已确认的需求澄清

主会话转达用户已确认：按当前屏幕任务栏厚度计算；当前屏无任务栏，使用主屏任务栏厚度乘以 当前屏DPI/主屏DPI。顶部/底部使用任务栏矩形高度，左侧/右侧使用宽度；100% 的主栏名义外框高度匹配厚度。默认开启；设置窗口的自身缩放独立。

### Files found / code patterns

- `Inkeys/Inkeys/UI/Bar/Bar.Metrics.cppm:29-37`：主栏外框和 MainButton 的基准均为 80 DIP；按钮网格 two-side 为70，内部边距5，不应以70作为任务栏匹配除数。
- `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp:336`：MainBar 创建使用 BarMainBarWidthDip/BarMainBarHeightDip。
- `Inkeys/Inkeys/UI/Bar/Bar.Zoom.cppm:44-73`：启动基准目前为主屏 effectiveDpiX/96，RefreshZoom 做 dpiZoom*configZoom。
- `Bar.Zoom.cppm:153-164`：初始化归一化 UI.Bar.Zoom 为[0.50,2.00]且两位小数；`FitInitialAfterMainBarLayout` 在配置为100%但整栏放不下时降低 configZoom 并写盘（:167-206）。这会构成“100%严格匹配”与自动fit的边界，实施需明确继续允许 fit 改用户值（界面此时不再显示100%），不能静默改变100%的定义。
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:1162-1367`：ApplyDisplayTransition 每帧独立计算 targetDpiScale*configZoom，动画 displayDpiScale，最后覆盖 barStyle.dpiZoom 与 zoom。因此只改 Zoom 模块启动逻辑会在后续帧被覆盖。
- `Bar.RenderLoop.cpp:461,476,671,720,1389` 等：dpiZoom 同时用于画笔尺寸预设/视觉宽度/滑条范围，必须保留真实 DPI 含义，不能用任务栏厚度/80替代。`CurrentBarBottomDockLine` (:373) 使用真实 DPI 折算白板 inset，亦应保持。
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:6703-6710` 与 `Bar.BottomDock.h:293-305`：直接窗口拖动另算 DPI*configZoom。它必须改用统一 UI 几何基准，否则拖动/命中/呈现尺度分离。
- `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp:74-97`：PublishDisplaySnapshot 在非白板时优先 PptSceneMonitor、否则主屏；发布 bounds/workArea/dpi 到带偶数 serial 的原子快照；订阅回调仅发布目标和唤醒，不做渲染。
- `Bar.Initialization.cpp:100-122`、`Bar.Main.cppm:419,575`：窗口 WM_DPICHANGED 只发布 DPI；新基准若来自显示快照，要避免这种局部更新导致厚度与 DPI 来自不同屏/不同代次。
- `Bar.Interaction.cpp:562-570`：WM_DISPLAYCHANGE/WM_SETTINGCHANGE 调 Display::Refresh，WM_DPICHANGED 调 PublishWindowDpi。
- `Inkeys/Inkeys/Display/Display.cppm:101-105`：MonitorInfo 已有 bounds/workArea/effectiveDpi；目前无任务栏信息。
- `Inkeys/Inkeys/Display/Display.cpp:141-164,381-387`：枚举与快照语义相等比较；新增任务栏信息必须一并加入比较，不然自动隐藏状态下厚度变化而 workArea 不变时会被吞掉。
- `Display.cpp:568-583,773-806`：统一隐藏监测窗口接收 DISPLAYCHANGE/DEVICECHANGE/SETTINGCHANGE；Refresh 只有 SemanticallyEquals 为 false 才发布代次并通知。现有事件可复用，但 Explorer 重启和只改任务栏窗口尺寸未必投递这些消息，应有明确补充失效触发策略。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:554-570`：LayoutMonitor 目前始终选主屏并返回主屏边界和 DPI/96。当前分页不随 Bar 的 PptSceneMonitor 切换。
- `PageControl.cpp:1392,1507,1915,2400`：渲染、拖动候选、位置交接、预算恢复均消费 LayoutMonitor 的值，必须同份 UI scale，不能只改最终绘制。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cppm:1030-1062`：ResolveSurfaceLayout 为 四个PPT控件 用 DPI * bottomPairScale/middlePairScale；白板展开另走独立尺寸。所有文字/按钮/命中/阴影/呈现依赖 result.scale，应沿现有统一最终尺度传递。
- `PageControl.cppm:1108-1142,1179-1228,1258-1356`：Clamp、吸附、碰撞、runtime fit 都有 DPI 与用户倍率计算。特别 :1226-1228 的20屏幕DIP吸附距离只乘真实 DPI，不应乘 UI 用户倍率或任务栏系数。
- `PageControl.cpp:3780-3784`：订阅 Display 变化后 RequestAll。更改 UI 缩放开关/滑条也必须显式 RequestAll，单独更新 Bar 不足以驱动静止分页。
- `Inkeys/Inkeys/UI/Ppt/Ppt.cpp:78-111`：SnapshotLegacyConfiguration -> ToPageLayout 直接复制 pptComSetlist 的 bottom/middle 倍率；此路径没有 Bar.Zoom 桥接。
- 已全仓搜索 bottomSideBothWidgetScale/middleSideBothWidgetScale：写入点为设置 UI 和 IdtConfiguration 读取，未发现隐式叠乘 BarZoom 的桥。当前四分页实际上没有消费 UI.Bar.Zoom。用户“和原来一样叠加”的最终意图应按需求落实为 UI基准 * UI用户倍率 * PPT控件倍率，但不要假称当前代码已有此行为。
- `Ppt.cpp:85-94`：exitScale/showExit 是旧兼容字段，不再参与窗口布局；结束放映按钮已在 `Bar.Button.cpp:642-662` 和 FixedA2(:714)，自然继承主栏倍率，勿恢复旧独立退出窗口。
- `Inkeys/Inkeys/UI/StartupPreview/StartupPreview.State.cpp:107-135`：启动占位有独立 DPI/96*barZoom 计算；`Inkeys/IdtMain.cpp:2271,2327-2335`：miniConfig 只请求旧 UI.Bar.Zoom 并生成预览几何。新 key 和基准应同步，否则启动占位与真实栏不匹配。

### 推荐接入方式（规划建议，尚未实现）

1. Display 统一枚举主任务栏与各屏任务栏，将物理像素厚度与可靠性/是否真实存在作为 MonitorInfo 快照字段。不要拿 bounds-workArea 的差直接当任务栏厚度：自动隐藏可为0/少数像素，其他appbar也影响workArea。
2. 提供共用、可无窗口测试的纯解析策略：关闭开关 baseScale = dpi/96；开启且当前屏有有效任务栏 baseScale = thicknessPx/80；无任务栏时 baseScale = primaryThicknessPx*(currentDpi/primaryDpi)/80。所有查询失败最终回退当前真实DPI/96，禁止零值/NaN。
3. 明确拆分真实 dpiScale 与 UI baseScale。Bar 保留 dpiZoom/displayDpiScale 的真实语义，新增独立 UI 基准/过渡值（或独立倍率并保证动画时真正的DPI仍可用），最终 zoom=uiBaseScale*configZoom。初始化、ApplyDisplayTransition、直接拖动、StartupPreview 都调用同一解析结果。开关变化须触发串行快照/布局更新，即使显示 serial 不变，也必须重新 retarget UI 基准。
4. PageControl 的几何/fit/clamp/碰撞接口显式携带 uiScale（baseScale * configZoom）和真实 dpiScale；不要直接把 LayoutMonitor 的 dpiScale 替换为UI最终倍率。几何尺寸用 uiScale*控件倍率，20屏幕DIP吸附阈值继续dpiScale*20；最终scale不再套[.5,4]的DPI裁剪。运行fit只改运行副本，不把全局 UI 倍率乘回已保存PPT用户倍率，避免重复发布与写盘污染。
5. 当前分页固定主屏，而Bar可跟PPT目标屏：最小改动可保持分页既有屏幕选择，但须在规划中明确“当前屏”指该控件实际布局所在屏。若用户要求分页也跟随PPT屏，需要单列屏幕路由调整，不能只改倍率。白板展开继续独立布局/倍率，除非用户另外指定。
6. SettingUI 自身Scale/DPI链保持不变，只扩展缩放选项页的数据与事件。新版JSON键及i18n由主会话研究。

### External references

- [ABM_GETTASKBARPOS](https://learn.microsoft.com/en-us/windows/win32/shell/abm-gettaskbarpos)：官方说明只取得系统任务栏屏幕坐标矩形，只有cbSize作为输入，其他字段忽略。因此不能靠设置目标HMONITOR向该消息查询每屏任务栏；失败返回FALSE。
- [GetWindowRect](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect)：官方明确此API存在DPI虚拟化，可能包含不可见边框；坐标必须与 Display bounds 保持同一物理像素语义。
- [ABM_GETAUTOHIDEBAR](https://learn.microsoft.com/nl-nl/windows/win32/shell/abm-getautohidebar)：官方说明普通消息用于主任务栏所在屏，指定监视器自动隐藏bar需EX版本。不能仅靠此API覆盖全部普通副屏任务栏。
- Windows Shell_TrayWnd / Shell_SecondaryTrayWnd 可作为待验证的兼容枚举方案；这是Shell实现约定，官方ABM文档并未承诺这些类名或Explorer替代Shell兼容性。实施须有失败回退，不新增硬依赖新Windows API。

### Related specs

- `.trellis/spec/native-desktop/index.md`：UI3唯一生产入口，共享RenderPipeline所有权。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：target/current动画分工、共享渲染线程与独立客户端。
- `.trellis/spec/ppt-interop/native-session-ui3.md:42-48`：runtime fit不写保存偏好；主屏边界与DPI同快照；20屏幕DIP吸附只乘真实DPI；最终绘制/命中/阴影/presentation同一scale；白板独立。
- `.trellis/spec/native-desktop/startup-preview.md`：Preview与Bar committed frame/alpha交接；基准改变需检查启动轮廓一致性。

## Caveats / Not Found

- 未执行GUI、构建或代码修改，仅检索与研究文件写入。Win7/Win10侧边任务栏、Win11自动隐藏/触摸扩展栏与多屏Shell实际尺寸需在用户明确允许GUI验证后补充。
- 自动隐藏时矩形可能移出屏幕，厚度必须从完整任务栏矩形得到，不能用与monitor交集的高度。Explorer启动/重启的短暂缺失应回退，不能归零。
- 当前目标屏无任务栏且主屏查询也失败时，需求未指定精确值；建议回退真实DPI缩放而非硬编码48px。
- 主栏80DIP描述名义外框几何；stroke居中、边缘抗锯齿、阴影与像素取整可能令非透明像素包围盒相差1px。验收需说明比较名义主栏高度，不以包含阴影的HWND高度为准。
- PageControl屏幕路由与“当前屏”边界如上需主会话在方案中明确；不应在缩放任务中无提示扩大为分页跨屏路由重构。
