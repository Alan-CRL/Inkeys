# 页码动画最小复现结果（第一阶段）

已通过现有真实Scene/D2D/BGRA离屏入口复现候选机制，不改变动画策略。

- 完整InkeysRepo.sln Debug|ARM64使用动态定位的ARM64原生MSBuild、同进程Path规范化/禁node复用、900秒预算，exit0，20.09秒，3条既有hashlib C4267，无源码错误。
- Inkeys.exe --bar-eraser-offscreen-test exit0，PageControlUI3 failures=0。日志Build/Validation/page-number-animation-20261101/repro.*，该轮EXE哈希存repro-artifact.json。
- 四组合：Status停留0/20ms后返回Numeric3或4，总数入/出均/56；短Status次槽目标为真实Wait，而不是把其目标也保持/56。随后仅同值heartbeat40×20ms；纯Numeric控制组即时更新且无动画。
- 0ms分支在返回前主、次槽相对旧数字的BGRA差异均为0，相对最终Status分别差4064/948字节，明确状态文字没有显示。回3/56后160ms，两槽相对最终数字仍差876/500字节，active=1；回4/56对应988/500，active=1。640ms及800ms均差0且idle。
- 20ms分支也复现两槽动画像素差异，640ms收敛；内部timeline/精确中点不扩新API读取，保留unknown，不从kind推断光学可见。

该观察样例描述现实现机制，不新增产品规范；未来若修复策略改变，应更新相应观察断言。它不证明真实用户现场发生了Status。下一阶段仅加默认关闭的实际policy/类型数字取证，保留三点、COM、ready、布局与动画决策。
