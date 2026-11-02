# 消息框调用点语言快照审计

2026-11-02，根会话只读审计；已发给live_locale与dynamic_fonts。

## 已满足的内部合同
MessageBox.cpp构造OwnedRequest时冻结文本、language和按钮；MessageBox.Window.cpp:788-900依据request->language选择/重建字体。已打开消息框的内部语言生命周期无需重新设计。

## 必须处理的调用点
| 位置 | 当前风险 | 最小修复方向 |
|---|---|---|
| Setting.cpp:1055-1095，Information/ConfirmRestart | command.text可能是入队时旧语言，worker随后用多次独立读取的新title/labels/lang拼请求 | 在入队时冻结一致快照及对应正文；或保留key/format参数在显示时整组读取。不可只给worker加一次languageId读取。 |
| IdtPlug-in.cpp:1619-1625，结束放映 | 不同线程中分别读取title、body、language | 通过一次语言快照取得所需字段，固定传入MessageBox。 |
| Bar.Interaction.cpp:6156附近，退出确认 | title/body/按钮/lang分开读取 | 同一发布revision/快照。 |
| Window.Legacy.cpp:125-136 | 兜底窗口失败请求分开读取正文、标题、按钮与language | 现有本地化兜底语义不变，取同一快照。 |
| Helper.CrashHandler.cpp:632-647 | tryLanguageId与tryGetW逐个try锁，可能跨发布拼接 | 保持崩溃路径非阻塞约束，用一次try快照/批量读取；任一失败整组英文回退。不引入阻塞锁。 |

启动前的IdtMain/SuperTop英文失败框没有可供热切换的运行界面，不因本次需求扩大为启动本地化重构。系统已托管历史通知保持原消息，新创建通知应从最新语言取值。

上面调用点行号是实施前基线，实际修改后会移动。要求回归覆盖“先构造/排队旧请求→发布新语言→消费旧请求”以及新请求全字段同版本，不必启动GUI。
