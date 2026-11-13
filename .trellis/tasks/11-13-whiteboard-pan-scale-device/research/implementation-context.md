# 实施/检查上下文索引

本文件是短索引，不替代 prd.md / design.md / implement.md。研究日期2026-11-13；当前基线1b819d2d，产品清理提交4706ec0e；完整摘要已获用户后续批准（“批准并开始实现”），本任务现进入in_progress；仍不GUI/commit/push/归档。

## 用户已确定的边界

- R6：Touch漫游，真实屏幕Pen/PenTail精准短尾，Mouse/外置板保持间接基准；开关/默认不变。
- R7/R8：大屏明确快甩适度延长但有界；整段起步、反向、释放和尾滑适配，按住后1:1。
- R9：Surface Pro 11约28×19cm的Touch仅是快速连续滑动正确性参考，旧距离/时长不要求保留，也未被认定最优。
- R10：无可信EDID也按模式优化，系统DIP+有界意图启发式，不能假冒毫米。
- 不实现Touchpad/新输入后端，不启用GUI，不修改橡皮行为，不沿用旧任务commit授权。

## 必须守住的代码事实

- Draw3.DrawingController.cpp:2532/2609/2621：四类接触及当前前置准入；:2634/2640是旧两轴DIP scale；:2839是Register；:2917是Step，含稳定QPC/phase排序；:3110是输入换算/sourceKey。
- Draw3.CanvasNavigation.cpp:531/544是唯一向量摩擦；:650是Dispatch；:685是有序Step。frame时间只推进一次；下游contentDelta等当前系统DIP含义不能改成mm。
- sourceKey高32是PanDeviceClass，热禁依它解码；不要放opaque profile hash。单位/响应兼容身份另存。
- 发布Down顺序、真实QPC顺序和消费Up顺序可能不同；Register/整帧预选一个profile会错。不能把同帧拆成多次旧Step来绕过。
- 纯内核默认值与IndirectDip保持当前数值；新physical/fallback profile是有意的新数值预期，不能要求全量pan-feel字节仍与旧版相同。
- 任何新增Pending准入都要联动OwnerClass、TouchPanHeld、Touch/native Running下Pen抑制与持续Pen mailbox，防拒收触摸先挡住合法笔。
- 消费历史水位、overflow/Stop吞旧指、逐指真实尾段、反向和Up年龄保护、Down中断、当前残余正投影均保留。

## 详细证据按需要读取

- speed-eraser-current.md：真实五入口/三响应/单位与模式、配置和状态机；不要用路程窗替换导航尾段向量拟合。
- display-trust-and-fallback.md：实际输出/输入映射/业务物理资格；Display与RTS异步，不比较两者generation数值；旧共享显示服务缺口不自动进范围。
- eraser-large-screen-history.md：教室139×78cm/1920×1080/144DPI；普通擦除270–432mm/s是稀疏快照，合成曲线与现场验收必须分开。
- pan-scale-and-device-candidates.md：研究演进，旧“精确保留Surface旧参数”的建议已被R9撤销；设计v1优先。
- evaluate-pan-profiles.py / proposed-profile-analysis.json：独立理想公式；不是产品实现或设备测试。
- plan-math-review.md / plan-integration-review.md：独立研究审查，若与草稿矛盾必须先以最终design修订收敛。
- windows-scale-api-evidence.md：Microsoft官方DPI/EDID/映射证据。

完整主产品合同见manifest里的canvas-pan.md和C++规范；构建前另读`.trellis/spec/native-desktop/build-and-compatibility.md`（为给完整集成审查留注入预算，此文件在implement清单不再内联，check清单保留）。若原生注入因大小降为路径索引，子代理必须自行读取需要的全文，不能忽略未注入条目。
