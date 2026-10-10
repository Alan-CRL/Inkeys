# P1 设计与变更边界

## 归属

新增Inkeys/Inkeys/Business/PresentationLinkContracts.h、PresentationLinkService.h/.cpp，沿同目录PptSession普通C++边界，命名空间Inkeys::Business。仅标准库纯值，单一调用owner；不启线程，不引用IdtMain/Office/Win32/Draw3。产品本轮无调用者。

合同包含Envelope、SourceInfo/能力、CandidateKey/epoch/generation、可空document/page与quality、可靠ShowIdentity、ActivityEvidence、ForegroundObservation、BindingRequest/Confirmation、SelectedSnapshot和Command/Result。adapter必须证明进程创建身份和窗口生命周期；非零HWND/PID不等于已验证，来源本地计数不能跨源证明同show。

P1不冻结共享内存对齐/UTF-8 wire schema/pipe framing；P2/P3连同C#/C++对照测试实现。Unknown/缺失不伪造0/1/最后页，EndScreen只接受明确类型，黑白屏不猜End。

## 目录与恢复

调用方明确建立source/epoch，迟到消息不能自动登记新epoch。初始/断连/sequence gap后full才准入；delta/删除验证后一次原子更新。拒绝旧sequence/旧generation，full可恢复。heartbeat只更新host本地单调接收时间。now显式传入，fake时钟无sleep；活动与页时间分离。

失效先撤销route，再选可靠备用；Busy/Unknown有限宽限不宣称可写。目录/identity/页数/队列/command结果有界。结束/删除与退役epoch不能被旧缓存复活；只有连续可靠跨源实例证据归并，一show动态字段来自一权威source，不拼快页与旧文稿。

## 仲裁与提交

先选show活动等级，再在show内按可用性/新鲜度/页身份与能力/backend偏好选source。同等级保持，无current稳定键决胜。统一foreground精确关联show，PID/WPS/provider弱提示；Inkeys焦点保留外部上下文，自有Focus不独立计数。

PendingChallenge保存epoch/generation/show/activityRevision；仅独立观测计样本。弱优势持续+观察窗口+软切换间隔/反向回滞才prepare；优势/身份失效取消。来源恢复另有窗口。策略全部时限/样本数显式传入并校验，不配置产品初值。

prepare只产出BindingRequest/outbox。confirm核request/source/epoch/candidate/generation、候选状态与最新意图再提交；强切换先禁A命令，迟到B不能覆盖C。准备期间有效A页更新继续；current PageChanged直接推进target。输出Stable/Pending/Unknown与page安全性，不能把安全性冒充Present/UI ack ready。

同show可靠page映射保持showSession/target，只增route；页变化/未知映射/新场次推进target。被动仲裁无Activate。

## 命令

服务仅产出一个route的纯值命令，fake adapter复用执行guard。operation能力、身份/修订、expectedPage/deadline逐次验证。有限记录，commandId在source epoch内去重；发送后断连/超时为OutcomeUnknown，无自动重试。result不写绑定/页面。定义LegacyAutoSelect/BrokerSelect互斥语义，真实owner切换留P2/P4。

## 必要文件

- 上述3个Business新文件：合同、服务接口及实现。
- InkeysHeadlessTests/presentation_link_tests.cpp与animation_tests.cpp：fake和现有无窗口入口登记。
- InkeysHeadlessTests/InkeysHeadlessTests.vcxproj与Inkeys/Inkeys.vcxproj：只登记文件，复用MSBuild/工具链。
- 本任务文档及ppt-interop专用合同spec：按真实签名更新。

不改PptCOM/*.cs、旧PptSession/Draw3/IdtPlug-in/token/UI业务。若需要真实COM/IPC/消费者接管，先报告，不越P1。
