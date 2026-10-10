# PptCOM P1 通用合同与仲裁纯逻辑

> 用户2026-10-10明确授权下一阶段实施、必要阶段commit，并确认创建P1子任务。依赖已归档P0；本轮只实施P1，不进入P2-P5。

## 目标与边界

在native Business层交付可独立验证的纯值联动服务，使多个backend/transport的候选通过同一合同进入目录，优先选择活动放映，在同一放映内择优来源。短暂弱提示和来源恢复不能导致反复切换；翻页与明确前台变化保留快路径。

保留dynamic/ROT、事件/轮询、线程/apartment、COM所有权与释放、ABI/GUID及随附DLL/TLB。服务本轮未接入生产消费者，不创建COM、共享内存、管道、helper、HWND、线程或UI，不改变Draw3准入。不实现字节序列化或真实第三方adapter。

## 可观察验收

| ID | 要求与验收 |
| --- | --- |
| P1-AC1 | 纯值Envelope覆盖版本/kind/host/source/epoch/sequence；完整目录、增量/删除、来源元数据/能力、document/show/page身份、显式Unknown与可选字段；有界目录与值校验，非法批次无部分发布。 |
| P1-AC2 | 来源明确注册新epoch；初始/恢复要求full。重复/乱序/旧epoch不更新；sequence缺口撤销route并要求full。heartbeat只更新存活，不刷新页与活动；失联撤销route，恢复稳定后择优。 |
| P1-AC3 | 一个source多候选；可靠同session/PID/进程创建身份/真实show HWND/生命周期证据合并同show；路径/标题/页数不归并，同文件不同实例或同PID不同show独立。退出、观测缺口与PID/HWND重用不沿用旧场次。 |
| P1-AC4 | 服务统一等级：已验证真实前台show > provider/PID/WPS弱提示 > 正在放映。权限/backend/transport不让后台抢前台；重复来源不叠加分；同等级保持，初选稳定。 |
| P1-AC5 | 可控时间验证弱挑战持续、多次独立观测、取消/身份变化、软切换间隔/反向回滞、来源稳定恢复；重复revision/heartbeat/页包不算独立活动。策略参数独立显式传入，不把测试值当实测产品阈值。 |
| P1-AC6 | 当前真实前台优先，迟到provider不能盖过；Inkeys焦点保留外部上下文，自有Focus反馈不计挑战，随后真实其他show仍快速切换；明确前台或失效绕过软防抖。 |
| P1-AC7 | 请求→匹配确认→提交；准备B时又切C、epoch/generation变化或失败则旧确认拒绝。弱挑战继续当前页；强前台变化/失效先撤销A命令route，B未就绪Pending/Unknown。被动绑定不发Activate。 |
| P1-AC8 | 同show换source保持logicalShow/showSessionRevision，routeRevision推进；页面映射证明后才保留target，未知/不同页推进target并不安全。不同show独立场次；PageChanged绝对更新不等防抖，不直接宣布Draw3 ready。 |
| P1-AC9 | 命令只投递一个确认route，含ID、source/epoch/candidate/generation/show、route/target修订、deadline及可选expectedPage；服务与fake执行端校验。旧目标/过期/不支持拒绝，迟到ACK不改新绑定；timeout为OutcomeUnknown，不改投或重翻，有限去重/结果。 |
| P1-AC10 | 复用InkeysHeadlessTests fake来源/可控时钟，完整InkeysRepo.sln Debug ARM64构建、--no-window运行、格式/diff与P0 COM基线。Win7/跨权限/真实Office未运行标NOT VERIFIED。 |

## 依赖

P0签名commit 6d4931b7为起点；总体需求/设计见父任务。P2接内置共享状态与消费者，P3接helper/权限/管道，P4补真实COM多候选，P5集成；本轮fake目录不代表COM已可枚举全部show。
