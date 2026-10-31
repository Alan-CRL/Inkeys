# 已批准的修正：HiMsg 为交互入口，Raw 只补充拖动

本节是 2026-10-17 用户在管理员前台点击回归调查之后明确确认并要求实现的最新合同，优先于本任务旧文档和既有 spec 中 Raw 独占 Move/Up、必须绑定 Raw Down 的条款。

## 行为差距与修改边界

当前原生 Down 可以播放反馈，但原生 Up 被丢弃，普通点击依赖 Raw 接收和精确配对。修正后普通点击必须完全保留原有 HiMsg Down/Up 路径；Raw 未注册、无数据、配对失败都不能破坏普通点击。只在具有连续跟踪需求的拖动交互中融合私有 Raw 补充，原生 Down 建立命中和归属，不等待 Raw Down。未达到拖动阈值的主按钮/PPT 拖动区域点击仍用原生 Up；Raw Up 不得替普通点击执行业务。实际拖动时有效原生 Up 与 Raw Up 只能结束同一会话一次，迟到事件不得串入下一次按下。

- Bar 普通鼠标 Down/Move/Up 继续进入 HiMsg；触摸仍为 WM_TOUCH -> 带来源的 ExMessage -> HiMsg。WM_INPUT 永不进入 HiMsg。
- Bar 交互线程只在对应连续手势中开启/消费补充；PPT 仍为自己的 owner/context，普通窗口消息为原有入口，Raw 只补充拖动。保留笔兼容路径。
- 共享 Input 层负责有界传输、可选跟踪、来源/代次、重复终态退休。停止把两条消息的 time+point 完全相同作为普通会话成立的前提，不任意认领历史无关 Raw Down/Up。
- 适用主按钮移栏/停靠、颜色连续选择、粗细滑轨/相对拖动/浮窗、FineDial 和四个 PPT 窗口拖动。保留原有点击与拖动阈值、即时更新/提交/取消、轨迹/速度和惯性、长按与位置保存。
- Raw 失败只撤销补充能力，不得伪造 Up 或取消仍然有效的普通点击。真实桌面切换、模式变化、销毁、来源接管等明确取消仍须生效。明确的拖动队列溢出不能丢失结束、不能卡住。
- 不改两种 hook、Draw3、COM、HiMsg 第三方源码、激活/权限/部署策略；不扩大为无关重构。保留文件编码、BOM、CRLF，关键处理写简短中文注释。

## 执行所有权

- Input implementer: UiPointerInput.h/.cpp、ui_pointer_tests.cpp；先明确共享 API 并通知两客户端，生产测试必须直接覆盖该共享策略。
- Bar implementer: Bar.Interaction.cpp 及必要同目录声明；普通按钮不能再依赖 Raw，按实际手势使用补充并归并终态。
- PPT implementer: PageControl.cpp/.cppm、page_control_tests.cpp；普通按下立即按原消息进入，Raw 仅补充拖动，保留 pair 位置并发协议。
- 主会话: 本文、任务文档、jsonl、最终 spec、集成构建、检查与交付。

## 必需回归证据

1. Raw 不可用/永不发 Down/时间与坐标不一致时，原生 Down+Up 仍完成普通点击。
2. 拖动区域未过阈值点击仍有效；Raw 不得替普通点击提交。
3. 越界拖动 Raw Move/Up 可补充；原生先 Up/Raw 后 Up、Raw 先 Up/原生后 Up 只完成一次。
4. 快速相邻会话、历史 Raw Up、释放早于消费、触摸/笔接管、明确 Cancel、溢出退休不得串代；FineDial 路径和时间不能重复计算。
5. 完整 InkeysRepo.sln Debug|ARM64、headless --no-window、既有生产探针；无 GUI/隐藏 HWND，不能将自动化 PASS 称为管理员前台或 Win7 触摸实测。
