# 页码偶发动画：最小复现与取证边界

用户已同意前一步“最小复现和取证”，未授权改变动画策略。本轮保留三点、正常状态文字、COM和ready逻辑，不提交。

先仅在PageControl.cpp既有--bar-eraser-offscreen-test中增加真实Scene/BGRA测试：稳定3/56→Status仅推进到中点前→Numeric4/56或相同3/56，总数始终/56，之后仅同值heartbeat；与纯Numeric控制组逐帧比较。记录观察到动画，不能把预期现行为当修复；不修改生产决策。

复现成立后，最小现场取证复用PptWaitingDiagnostics固定槽与正常退出flush。在PageControl配置调用点提供session/layout/publication/surface等纯值上下文，Bar.Scene实际UpdateContent记录old/new kind、选择Immediate/Animated、current/total变动布尔及可零分配读取的主次scale/pct等数字。若必须新增窄scope仅服务该配置边界，严格RAII屏蔽/恢复，默认off零读钟/格式化/采样；render无I/O/分配/等待，不读取COM、不输出用户文字/路径。epoch0说明内容观察，不借旧命令。若精确timeline/midpoint不可无侵入取得，先保留unknown而不扩公共UI API。

预期文件：PageControl.cpp（测试与配置context）；Bar.Scene.cpp（实际policy数字观察）；PptWaitingDiagnostics.h（固定数值字段/事件和scope）；必要既有ppt_session_tests.cpp仅验证本采样的off/嵌套/输出长度/边界。所有生产行为逐句保持，不改PptCOM/ABI/绘制策略、工程配置/新GUI入口。若scope明显扩大先报告。

主会话统一完整Debug|ARM64 solution（ARM64 MSBuild/Path规范化/900秒）、既有离屏及定向测试；worker先交付复现测试冻结，先验证后决定窄采样细节。源码保留UTF8/BOM/CRLF，中文关键注释，独立check后短测交用户；没有现场kind轨迹前不得称已确定实机根因。
