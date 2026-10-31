# 一秒中止回归修正

用户实机确认普通点击与拖动在管理员前台/UIPI 场景可用，但不松手也固定只能拖动约一秒。现有 pending Raw Down 启动期限恰为 1000ms；Bar/PPT 将补充 Cancel 无条件按业务 Dragging 提升为整次取消，这是本轮需修正的行为差距。

## 最新合同

- 1000ms 可以保留为未绑定 Raw 补充的启动期限，不得延长/删除期限来掩盖问题。未绑定补充超时，只关闭补充，不取消仍有效的原生拖动，即使业务已经 BeginDrag。
- 区分业务已拖动与 Raw 实际已绑定。Provider 的取消 generation=0 表示未绑定；非零表示该私有通道曾取得 Raw 归属（包括队列溢出丢失初始 Down、消费者尚未见 Down）。共享 MouseGesture 统一判定取消语义，两个客户端不得再用 `|| Dragging()` 绕过它。
- 已绑定实际拖动明确失去传输/溢出仍可显式 Cancel；来源接管/桌面失效/模式切换/销毁继续独立取消。普通候选仍保留有效原生 Up。绝不能超时伪造 Up、提交或惯性。
- 保持原生 HiMsg/native Move/Up 在补充未建立时驱动拖动，超过一秒继续；真正 Up 才正常完成，重复旧 Up/Cancel 不串代。
- 范围限 UiPointerInput.cpp（必要声明）、Bar.Interaction.cpp、PageControl.cpp、现有 ui_pointer/page_control 无窗口测试和任务/spec 记录。保留 hook/安装卸载、触摸、Win7、编码/BOM/CRLF，无 GUI、无提交推送。

## 回归断言

实际 MouseChannel + MouseGesture：原生 Begin/BeginDrag，Raw 从未到达，模拟999ms/1000ms/1500ms/多秒；1000ms pending Cancel 不终止业务，其后原生 Move 仍接受，Up 只完成一次。另覆盖待绑定匹配失败取消、已绑定实际拖动 InputUnavailable/Overflow 仍取消、候选 Raw 失败保留点击、旧代次取消不终止新会话。PPT 策略相同，两个适配器只消费统一裁决。完整 solution Debug|ARM64 + --no-window + 两个既有早退出探针在正常主机环境验证；受限环境配置/原子文件测试已知可能失败，不改配置绕过。
