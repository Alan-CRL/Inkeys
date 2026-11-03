# 追加旧版本日志：当前修复覆盖性审查

日期：2026-11-03。用户暂时无法提供复测环境，本轮仅审查日志与当前代码，不改产品、测试或构建产物。

## 结论

当前修复没有覆盖本日志可见的“Pointer + Pen兼容Mouse、未观察到WM_TOUCH”路径。此前五窗Touch转交和完整Pointer副本过滤覆盖的是另外两种已证实的问题：Touch投旧窗、Page按Pen/Touch低位分类拆开Down/Up。不能把这两个修复推广成所有数位板点击均已覆盖。

还存在具体回归风险：旧版本左右白板分页通过Pen兼容Mouse成功点击；当前Page已经删除该业务入口并统一过滤它，如果新版仍出现同一消息形态而没有可用Touch，分页也会无法点击。

## 输入与基线

- 原文件：D:/Personal/Downloads/M6-input.txt，2415行，932378字节。
- SHA256：696F0F2560F59FBA2502E7065972478F70C7108883FB6CFBEA16FDE4DEE623BB。
- L5：draw3-Nov 2 2026T22:01:56，arch=x64；L41：ui_schema v1，build Nov 2 2026T22:02:09。属于此前诊断旧版本，不是00:57:52生成的当前修复包，也不是20261028a二进制证明。
- 本轮归档到ignored TestResults/touch-fix/additional-user-capture-20261103.txt；结构化提取additional-events.json、additional-analysis.txt、additional-summary.txt同目录。
- 不能仅根据文件名判断是哪款数位板；日志没有Windows Ink设置的直接证据。

## 全文件可见统计

原始事件按entry=wndproc计数，排除Bar channel_callback与WndProc双重记录。

| 项目 | 可见记录 |
| --- | --- |
| WM_TOUCH / bar_touch / touch_read / touch_select | 全部0 |
| Bar Pointer Down / Up | 86 / 87 |
| Bar带Pointer签名的Pen Mouse Down / Up | 87 / 87，extra均0xff51570a |
| Bar无Pointer签名Mouse Down / Up | 5 / 5；business_accepted也为5 |
| 左下、右下Pointer Down / Up | 每窗1 / 1 |
| 左下、右下Pen Mouse Down / Up | 每窗1 / 1；均0xff51570a |
| 分页button_activated / direction_on_up | 各2；mode=2，即白板 |
| 全局非Mouse lease acquired / released | 2 / 2 |
| other_channel_owns_lease / nonmouse_session_active / background_miss | 全部0 |
| 诊断累计掉样 | 最后dropped=8，contention=8，full=0；truncated=0 |

无签名Mouse成功不能反推用户一定换了物理鼠标；这些日志只标识程序观察到的来源。缺少的一条Pointer Down同样不能断言OS没有发送。

## 可核实事件链

### A. 主栏笔点击没有业务入口

L138-151：Pointer1727在(812,985)完整Down/Up；L140/147的bar_pointer查询type=3、valid=1，sourceDevice=0x10093。兼容Mouse Down/Up的extra都是0xff51570a，主栏在L142/144及L149/151明确过滤。附近此前已有真实presented button命中（L133-135），但该笔Down没有进入业务队列。后续大量笔点击重复这一形态，而没有可见Touch接纳。

当前代码对应：Bar.Interaction.cpp:3211接WM_TOUCH；:3231及:9757过滤Pointer兼容Mouse；:3056调用ObserveBarHapticPointer，:322-345仅维护haptic token/诊断，不提交按钮Down/Up。新Touch router不会凭Pointer事件自行运行，所以本链未覆盖。

### B. 旧版分页正常，当前存在退化风险

左下L2034-2059：Pointer1731 Down，Mouse Down(0xff51570a)，lease acquired，capture acquired，Mouse Up同签名，button_activated，direction_on_up，capture与lease释放。右下L2108-2134有同样闭环。不是低位变化丢Up，也不是旧lease卡住。

当前PageControl.cpp:2940统一过滤Pointer兼容Mouse；原Pen业务分支已删除。:3065只有WM_TOUCH进入Touch::HandleMessage。若新版仍观察到同一输入流，原成功的两次分页点击也没有业务Down来源。日志只展示白板分页；不将其当作PPT长按实测证据。

## 证据边界

- “全文件没有看到Touch”不等于已证明系统或驱动绝不生成Touch：有累计掉样，也未记录RegisterTouchWindow的实际返回值。不能据此归责具体驱动或推断Windows Ink开关。
- 但连续87组Pen Mouse边沿及完整Pointer记录都没有Touch业务记录，当前代码又无Pointer业务入口，足以确认现有方案缺少覆盖，不应等待硬件才能承认这个缺口。
- raw兼容Mouse中的device=0不是TOUCHINPUT.hSource=0；本日志没有已解码Touch，不能讨论其hSource是否改变。
- 上轮Debug ARM64、五生产探针、headless与Release x64通过的结论保留；它们验证已有路径，不会证明尚未实现的Pointer业务路径可用。

## 后续设计建议（未实施）

保留五窗独立上下文、按Down锁定owner、命中/取消/旧代隔离，以及兼容Mouse副本过滤。另规划直接接纳原生Pen Pointer的完整Down/Move/Up/Cancel入口，与现有Touch做显式来源归属和去重，送入同一套业务适配；Pointer来源不能依赖触觉设备/控制器可用。

不能仅撤掉全部兼容Mouse过滤或恢复旧Pen/Touch低位拆分；也不能等固定毫秒“没Touch”后猜启动Mouse，否则会重入先前的双击/丢终态问题。必须补覆盖Pointer-only、Touch-only、两流并存、跨窗、旧Up/取消、PPT无Move长按、白板单次和可选API缺失的真实生产回归，并保持既有平台边界。具体来源优先级、关联方式和Win7回退需要在下一轮设计明确，本轮不擅自更改批准过的输入方案。

独立代码复核见 [additional-log-coverage-check.md](additional-log-coverage-check.md)。任务保持in_progress；现在不要求用户重搭环境或重复采集同粒度旧日志。
