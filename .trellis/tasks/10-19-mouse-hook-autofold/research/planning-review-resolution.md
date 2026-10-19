# 主会话规划复核处理结果

以下是规划修订，不是代码修复；所有实现仍待用户批准。

- 已明确滚轮累计唯一Bar交互owner；本地PageControl保留原inputLocked/targetVisible和Selection语义，通过独立有界local通道投递，不成为Hook专用SPSC第二producer。
- expected PPT token一直传到IdtPlug-in最终业务request，禁止入队时CaptureCurrent重新认领；已纳入必要native封装文件，COM ABI不变。
- 保留已有Previous/Next节流，为EndShow增加同token单pending/in-flight边界及终态退休，不改退出确认策略。
- Hook桥以队列提交为抑制边界；提交前失败链传、不留幽灵事件，提交后不因可选wake失败再次链传，owner既有poll兜底；所有嵌套交互有限batch消费，避免timer/Raw/HiMsg饥饿。
- 新策略不读取旧四布尔，不等同于顺手删除历史deploy读写；后者本次保留。
- 新accepted begin独立于原activity count，不丢已结束的有效短笔，捕获和验证workspace/PPT生命周期，旧场景事件不折叠新场景。
- Drawing包括Shape/Laser已由用户明确确认，研究报告中更早的未决建议已被覆盖。
