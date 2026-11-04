# 补充旧版本日志的当前修复覆盖审查

日期：2026-11-03。范围为只读产品/测试审查及主会话提取的旧版本日志片段；不改产品、不build、不GUI、不commit。

## 结论

当前五窗Touch修复不能被称为已经覆盖本次日志。新提供日志可见的输入形态是 WM_POINTER + 带签名兼容 WM_MOUSE，而整个采集未观察到 WM_TOUCH/解码记录。当前业务仅由WM_TOUCH建立；Pointer仍仅被动触觉旁观，主栏过滤兼容Mouse，Page又已删除旧Pen兼容业务入口并统一过滤。因此，如果新版本再次收到日志所示消息流且仍无可用Touch，主栏业务不会开始，旧版已成功的分页点击也可能被新过滤阻断。

这是源码层面确认的覆盖缺口及条件性回归风险，不是新版本真机失败的实测结论。之前自动测试证明Touch路由/生命周期正确，不能证明“没有Touch可见输入时仍能操作”。

## 旧日志证据（行号由主会话按完整文件提取）

- 旧构建标识Nov 2 2026 22:01:56，2415行；原始归档TestResults/touch-fix/additional-user-capture-20261103.txt，SHA256 696F0F2560F59FBA2502E7065972478F70C7108883FB6CFBEA16FDE4DEE623BB。
- L138-151：主栏Pointer Down/Up可见，触觉旁观type=3、valid=1、source=0x10093；同点兼容Mouse Down/Up extra均0xff51570a，分别被bar_queue及WndProc过滤。没有可见业务Touch/lease/capture接纳。
- L2033-2065与L2108-2134：左右白板分页接纳笔兼容Mouse，取得租约及capture，Up进入按钮业务，随后释放capture/lease。L2130明确direction_on_up，mode=2；不是只有触觉成功。
- 主会话完整统计：WM_TOUCH/bar_touch/touch_read/touch_select均0；没有other_channel_owns_lease/nonmouse_session_active；末段累计dropped8、contention8、full0，无truncated。不能将此段失败套用先前“Up被Touch分类过滤而遗留Pen lease”的因果链。

主会话进一步仅按entry=wndproc统计、排除主栏双重raw：Bar Pointer Down86/Up87，带签名Pen兼容Mouse Down87/Up87（174个边沿，extra均0xff51570a）；另5组无签名Mouse Down/Up对应5次business_accepted。左右分页各1组完整Pointer+Pen兼容Mouse，合计2次button_activated/direction_on_up，lease acquired/released各2。没有background_miss或非鼠标会话/lease竞争拒绝。Pointer少一个Down可以被现有非零丢行解释，不能宣称系统漏发；174个兼容Mouse边沿无任何Touch记录是当前覆盖判断的关键。
## 当前代码接点

- Bar.Interaction.cpp:3056只ObserveBarHapticPointer；WM_TOUCH在:3212进入HandleMessage；:3231与:9757仍按IsPointerGeneratedMouseMessage过滤兼容Mouse。
- PageControl.cpp:2940原生入口统一过滤Pointer签名，:2370worker也过滤非touch副本；:3065仅WM_TOUCH进入公共Touch HandleMessage。旧独立Pen会话已按此前批准方案移除，没有Pointer业务替代入口。
- UiPointerInput.cpp:1017 HandleMessage解码Touch，再:961 DispatchDecoded选择五个目标与Down owner；这条修复链必须实际收到Touch才可发挥作用。
- IdtMain.cpp:3440仍RegisterTouchWindow(hwnd,0)。UI五窗路径未发现新增EnableMouseInPointer/RegisterPointerInputTarget之类配置。RegisterTouchWindow不等同于证明当前设备/模式为每次笔操作向这些WndProc派发WM_TOUCH。

## 必须纠正的字段解释

该文件的device=0x0是Raw/兼容Mouse Sample未填设备句柄的默认值，绝不是TOUCHINPUT.hSource=0证据。UiPointerDiagnostics.h:98-105读取extra和Pointer contact，但没有查询设备句柄；没有WM_TOUCH，就没有对应hSource的现场值可审查。不能由device0推断设备身份缺失、同hSource多触点或Touch source改变。

就当前源码而言，真实hSource=0本身不会被Context::Owns、Session::Begin或Bar/Page适配拒绝；但这个独立源码事实无法解释本文件的无Touch业务入口。

## 覆盖与未覆盖矩阵

| 形态 | 当前处理 |
| --- | --- |
| Touch持续投旧窗，但Touch Down/Up存在 | 公共Touch解码、真实屏幕probe及锁定owner转交已覆盖 |
| 兼容Mouse低位Pen/Touch分类变化，仍存在可用Touch | 两端高位签名统一去重，Touch负责业务；已覆盖此前分拆会话模式 |
| 本文件可见的Pointer+兼容Mouse、未观察到Touch | 当前无可达业务Down入口，未覆盖 |
| 同一可见Pen兼容Down/Up都0a、旧分页能点击 | 新Page将两者过滤；若同流重现，无Touch替代则有条件性回归风险 |
| 实际设备/系统内部绝不生成Touch | 旧日志不能证明；只能确认采集窗口/时段没有观察到Touch记录 |

## 完整性与后续边界

原始WM_TOUCH本身以batch Move限频，丢行统计也非零，不能从0条日志推导系统/驱动内部完全没有生成Touch，或未来测试模式不会提供Touch。也可能是未记录窗口、输入配置/运行环境/包版本差异等；本审查不替用户认定Windows Ink开关状态。相反，反复可见Pointer和compat Down/Up已足以说明业务需要考虑这一可见流，不能继续假设Touch必然补齐。

后续建议保留五份独立本地Context、旧Touch窗转交与既有生命周期保护，补直接原生Pen Pointer业务入口（平台可选API按兼容边界动态解析），并设计其与Touch的同接触去重/owner锁定及终态。不能简单恢复按低位Pen分类拆分的旧分支、撤掉所有过滤或按固定timeout猜“无Touch”。这是一条待规划的新输入接入路线，本轮未实现，也不凭静态审查承诺实机通过。

## 验证状态

阅读当前router/适配/过滤和既有无窗口测试，未发现新的hSource0产品拒绝条件；本轮没有修改或运行测试。此前完整ARM64/headless/Release x64结果仍见touch-fix-validation.md，但不能替代本次新增无Touch入口场景的设备覆盖证明。当前M6设备验证仍待进行。
