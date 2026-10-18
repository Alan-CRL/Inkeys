# PPT 已接受请求的终态交接修复（2026-10-18）

## 证据与边界

生产审计确认两种内存故障机制：PresentationAutoSave::Run 出队后复制 completion.target 失败，或 PushCompletion 的 deque 分配失败，均可能丢失已接受 Load 的唯一终态，使 Controller 永久保留 loadPending；Host 原先先取出终态，再 make_shared / 普通命令队列 push，分配异常会逃出绘制 Run 并使其停止。以上是源码中的具体异常路径，不代表已证实现场用户发生低内存。

本次只修 PPT 存储请求到 Host/Controller 命令入口的所有权交接。严格 Loaded 物化安装、源身份校验、四次退避预算和未读文档的停写保护均保留。Desktop completion、普通 Bridge 命令以及 Controller/GPU 的其他分配异常不在该保证范围内，不能宣称全应用支持 OOM 恢复。

## 实现

1. SubmitSave/SubmitLoad 在返回 Accepted 前建立 shared completion、完整请求身份和 list 工作节点。任一分配/复制失败返回 Invalid，accepted 不递增、不写文件；Controller 已有 Submit 失败的 IoError 退避继续适用。合并保存时同时更换已预建的 completion，仍沿用原尾部合并和 Clear FIFO 边界。
2. service 预建等待、正在处理和完成三个 list。MSVC list 默认构造也可能分配哨兵，因此 working list 是 Impl 成员，在 service 构造阶段准备。工作节点通过 splice 出队、完成；完成发布不再分配。工作过程中任意异常转换为预建且身份完整的 IoError，不丢 terminal。真实 Load 的临时返回值仍可能分配；异常由此保护边界承接。
3. 完成节点发布前释放原 Save/Load 大请求内容，避免完成队列长时间保留已不再需要的保存快照。相关 move/default 构造使用 static_assert 固定 noexcept 合同。旧的按值 TryTakeCompletion 保留，Host 使用新的 shared_ptr 重载，省去取出之后的 make_shared。
4. WindowController 增加一个预留的 PPT completion 槽及其前方普通命令数量。槽满返回 false，不覆盖；Host 保留该 shared payload。普通命令出队递减计数，计数到0消费 terminal；槽后的相邻绝对目标仍可合并，但绝不跨越 terminal 屏障。HasPendingCanvasCommand 包含该槽。
5. 消费 terminal 后在命令锁外发布 General wake，Host 可以继续交付下一份；不引入后台线程、定时轮询或无限分配重试。已成功进入窗口命令序列的终态与普通命令保持 FIFO；尚在 service / Host 保留的后续终态可以晚于期间进入的普通命令，这与已有按 target/slotGeneration 处理迟到 completion 的合同一致，不能安装到错误页。
6. Attach/Detach 清槽及计数；Host 在 owner join 后清 pending payload。下一代 Start 不继承旧终态。

## 新增回归

### 真实分配失败与 worker

在现有 --presentation-autosave-only 中、仅 DRAW3_TESTING 编译的 hook 使用既有测试 executable 的真实 operator new 故障开关：

- Submit 的 completion 控制块、身份字符串复制、list 节点分配失败，分别覆盖 Save/Load，断言 Invalid、未增加 accepted/replaced、不产生伪 terminal、原文件字节不变。
- Accepted 后真实 Current / PreviousInterval Load 的第一次复制分配失败，仍收到完整 target、slotGeneration、loadKind、pageGuid/intervalOrdinal 的 IoError，loadedSnapshot 为空，原文件不变。
- 完成发布前 arm 下一次真实分配失败；发布结束时故障开关仍 armed，证明这段真实路径没有进行分配。shared TryTake 同样验证无分配。
- 同一 service 随后成功加载原非空墨迹，两份 completion FIFO 逐一可取；重启不遗留终态。既有 worker throw、合并保存、Clear 区间等测试继续执行。

### 生产 WindowController / Host drain

--draw3-ppt-completion-delivery-test 调用 RunPresentationCompletionDeliveryTest：只创建真实 service、WindowController 和输入 coordinator，不创建 HWND/GPU/RTS。覆盖前后普通命令顺序、目标合并屏障、槽占用不覆盖、槽独立 pending、消费 wake、3个真实 service completion 经生产共用 DrainPresentationCompletions 的依次交付、Detach 清槽以及 service 下一代重启。

## 验证状态

- 本子代理未构建、未启动任何程序或窗口；主代理集中执行完整 solution 与两个新增/扩展 CLI，避免共享编译缓存竞争。
- git diff --check 通过；修改使用原 UTF-8/BOM 和 CRLF 格式，不编辑工程文件、依赖、Window.cpp、Bridge 或 IdtPlug-in。
- 构建及运行结果由主代理补充到最终验收记录；本文件中的测试清单不能被读作已执行通过。


## 根会话最终运行验收

既有目标编译及实际worker测试均exit0，真实operator new注入/持锁/旧墨迹保护和生产CLI结果按final-validation-20261018.md记录；原子代理“未运行”描述其自身职责，不能代表最终未验收。源码之后未作无关改动，最终solution与相关CLI再次通过。
