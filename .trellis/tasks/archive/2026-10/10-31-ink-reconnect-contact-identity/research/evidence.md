# 现场证据与定位

诊断 v4 包 EXE SHA256：002010c7d8bcee2a3530439ee1d243e56e01474154ad983a287cebbb880353a4，与00377811提交前的交付包相同。
- input-lifecycle.csv 共317行，accepted309、reconnected4、gesture_suppressed4，无门禁拒绝；inputs/lifecycle/admission掉样均0。
- 四组重连→同ID下一次落笔抑制：2(5→15)、10(43→53)、4(136→146)、6(246→256)，括号内为含表头CSV行号。
- dev 当前成功接管：Draw3.DrawingController.cpp:10912 替换handle，普通初始化:11008设置touchGestureKey，终态:11469调用OnTouchUp(runtime.touchGestureKey)。
- CanvasNavigation.cpp:184 重复key返回Suppressed，保留此保护。
- 包缺completion.json导致整体incomplete，不将其当成功上屏证明；它不否定上述原始Down处置记录。

用户已批准此定位后的修复计划。任务不处理独立退出问题。
