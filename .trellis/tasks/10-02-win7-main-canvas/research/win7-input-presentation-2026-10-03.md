# Win7 无墨迹与应用光标不可见：2026-10-03 调查

## 结论边界

这次 console 已证明一段真实鼠标移动进入了光标反馈链，且已生成橡皮 visual 并提交帧。上一轮的“保守 mouse filter 是最高概率根因”没有足够依据：该过滤位于光标反馈入口，不是 RTS contact 落笔入口。本次证据把共同故障嫌疑推进到 renderer 最终像素/ULW 可见呈现链，但仍不能证明唯一根因。鼠标按下的 RTS 收包、解码、发布和绘制准入必须单独判断。

本机是 Windows 11 ARM64；没有运行用户 Win7 桌面复现，也没有启动可见 GUI。未把 Win11 的检查当作 Win7 修复验收。

## 输入附件与身份

- 原始 IDT：`G:/Inkeys/Inkeys-Win7-ULW-Hit-x64-Release/idt1791013766788.log`（39832 bytes），SHA-256 `0DE953F1DFF2897473F2520F7F8A0ADDBFB965481FBDB15331D989C91FC642EC`。
- 原始 console：同目录 `console output.txt`（20049 bytes），SHA-256 `663BC37352BC8A16708B0457F655A2B8D1236FD438BB3CFD402EB6CBC5D5345C`。
- 现场 EXE：SHA-256 `923E787ED7F7CEEB41E759B5637BA04E2B3CC0D0428C9874BA161F0F77934A08`；与上一轮保存的 `win7-ulw-input-hit-final/Inkeys.exe` 完全相同。
- PE 资源 301/302/303/304 的字节逐一比对，全部与当前生产 Assets 的 PS/VS/UpdateCS/EmitCS `.cso` 相同。这排除了“现场 EXE 漏带这四份当前 shader 字节”的情况，不能证明 Win7 GPU 输出或整份 C++ 二进制源码身份。
- 调查源码 HEAD：`d03713bdf456c9da80adb6e0df0f27ebd1399765`。与用户引用的 `4e004923` 比较，Draw3 目录仅 HistoryProbe/InkHistory 改变；本轮分析的 RTS/WindowControl/RendererPrimitives/TransparentPresentation 在这一区间未改变。
- 新 console 是屏幕缓冲尾段，始于 `seq=13129`，不是进程启动至退出的完整重定向记录；它没有带入上一轮的设备/RTS 初始化记录。

## 直接可确认的现场证据

| 证据 | 可以证明 | 不能证明 |
| --- | --- | --- |
| `mouse-source api=0`；`mouse-filter sourceReject=0 systemReject=0 positionReject=0`；`mouse event accepted=1 reason=accepted-mouse` | 此段来源 API 不可用，鼠标移动经既有兼容入口接受 | RTS Mouse Down 已发布，或所有其他时段都未被过滤 |
| `frame tool=3 owner=2 primary=1 pen=0/0/0 mouse=1/0` | 正在生成鼠标拥有的橡皮悬停反馈；此时没有 Pen 样本 | 鼠标按下/书写的输入联系是否成功 |
| `visual shape=2 x=976 y=951 width=29 height=29 opacity=0.5 fill=1 rgb=1/1/1` | 真实位置与尺寸有效，白色橡皮 visual 生成 | shader 已写出对应白色/非零 alpha 像素 |
| `present success=1 visuals=1 dirty=(959,931,995,968)` | 合成分支和最终 PresentFrame 返回成功；非空脏区覆盖该 visual | 这一区域 backbuffer 非透明、DIB 非透明，或桌面看得见 |
| `system hidden=1 tool=3` | 系统箭头按橡皮工具策略被隐藏 | 应用光标消失是独立输入错误 |
| `[Undo] page=1 item=3/2/1/0 path=hot_preimage` | 该页有四个可撤回历史节点，存在热前像路径 | 每节点都为本次鼠标笔迹、每个像素可见或整个输入链正确 |
| 撤回至空后，`target=DrawpadPresentation revision=3`，随后两窗 hidden | 空白选择态按已定义状态机隐藏两窗 | 前面橡皮悬停不可见由此次隐藏引起 |

console 中 MouseLeave 前后的 hit=0x5034C 只证明指针命中了另一窗口；附件没有给出该 HWND 的 role，不能确认就是 Bar。此前橡皮 visual 已存在，离窗不能解释所有不可见现象。

## 源码路径与上一轮建议的更正

1. `Draw3.WindowControl.cpp` 的 WM_MOUSEMOVE/按钮分支只调用 PublishMouseCursorSample；不会在那里创建绘制 ContactRecord。Win7 没有 Pointer API 不等于没有绘制输入；绘制依靠 RealTimeStylus。
2. `Draw3.RealtimeStylus.cpp` 用 SetAllTabletsMode(TRUE) 包含鼠标；decoder 对 TDK_Mouse 归一化为 MouseLeft；StylusDown 经 binding、DecodeSnapshot、coordinator.PublishDown 入 ContactInput mailbox，controller 消费后才产生墨迹。
3. 现有详细 RTS RecordCallback/FlushRtsCallbackTrace 受 DRAW3_RTS_DIAGNOSTICS 编译宏控制。该宏和 SetRtsTraceEnabled 调用只出现在独立 demo，生产工程未定义/调用；所以旧建议“开启 ConsoleOutput.Draw3/Cursor 就一定能看到 StylusDown/Packets”错误。缺日志不能反推缺输入。
4. `Draw3.PenCursor.cpp` 对 Eraser/Laser 无条件选择隐藏系统 cursor。应用 cursor/laser tip 则由 controller/renderer 写到同一 backbuffer。反馈像素没有显示时，自然会出现“箭头被隐藏，应用光标也看不见”，其他工具系统光标仍正常。
5. `DrawTransientDrawingCursor` 在缺资源、Map InkData 或 Map globalCB 失败时直接返回 void；后续 ULW 仍可能成功。这是诊断盲点，尚未证明现场进入失败分支。
6. PresentFrame 成功不是像素成功。ULW 读回经 CopySubresourceRegion -> staging Map -> 按 RowPitch 复制 DIB -> Primary 命中底层 -> UpdateLayeredWindowIndirect。原有全零 alpha 信息未在成功日志输出。
7. Primary 的 alpha=1 命中底层只在 CPU DIB 合成。即使 backbuffer 全透明，主窗仍可能命中；因此“消息不穿透”不能证明笔迹像素存在。源 alpha 必须在底层合成前取值，黑墨 RGB=0 也不能当成无墨迹。
8. 已核对 viewport：资源初始化 SetScreenSize，Shape/Laser warmup 恢复 saved viewport，历史 operation 在结束时恢复 screen size；未找到可直接证明本现象的零视口遗漏。

## 官方 API 资料核对

- [Win7 Platform Update](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7)：D3D11.1/DXGI1.2 为部分支持，WARP 可到 FL11_0，DComp 不可用。初始化成功不能代替各功能验证。
- [UPDATELAYEREDWINDOWINFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-updatelayeredwindowinfo)：prcDirty 可为 NULL，非空时更新对应区域；该结构从 Vista 起受支持。没有依据直接宣称 Win7 不支持 dirty ULW。
- [Output-merger dual source blend](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-output-merger-stage)：slot0 的双源混合使用 o0/o1；生产 cursor/resolve 的 SV_Target1 + SRC1 blend 在接口语义上合理。不能仅因双输出就判定 shader 错误。
- [GetPointerPenInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointerpeninfo) 最低客户端 Windows 8；[SetAllTabletsMode](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-setalltabletsmode) 在早于 Win7 的系统已有且 TRUE 包含鼠标。源码动态读取 Pointer API，modernSourceApi 不参与 DecodeSnapshot/PublishDown 准入；Win7 的 Pen/Touch 来源元数据 Unknown 不等于拒绝普通绘制。RTS 现场回调是否可用仍须取证。
- 保留项目已由用户实测约束的 FLIP_SEQUENTIAL；不以通用 swap-effect 文档覆盖用户现场证据，不擅自替换交换模式。

## 本轮最小诊断与下一次判别

在两个生产实现文件补证据，另对 IdtMain.cpp 的诊断控制台做必要采集修正，保持输入、shader、窗口、swapchain、dirty 区及底层规则：

- Cursor 开关控制 RTS Down 到达/解码/发布结果与 Up 结果；Packets 使用独立原子 tick CAS 限频，不依赖 decoder 状态 gate，回调写现有有界 Cursor 队列，由绘制线程输出，避免回调直接进行控制台 I/O。
- Draw3 开关控制 `[Draw3Diag][ulw-pixels]`：每 presenter 首次、此后最多每秒一次。复用本次 staging 映射/像素复制；记录 dirty 源和最终 DIB alpha/RGB 数量、最大值、非透明 bounds、最高源 alpha 样本、预乘合法性、真实 ULW 成败。未采样模板保持原有拷贝计算。
- InitializeDebugConsole 原先 AllocConsole/freopen CONOUT$ 会覆盖采集启动器的 stdout/stderr 文件或管道；现在识别并保留已有重定向，两路均重定向时不分配交互控制台。普通直接启动时原控制台行为保持。
- 现有 `--draw3-ulw-copy-benchmark` 增加非原点 dirty/RowPitch/透明命中底层/黑墨/白光标/原始异常 alpha 断言，比较采样与不采样输出完全一致。

判别表（输入日志缺席只在开关已启用、完整采集、正常消费且没有 dropped 丢弃证据时才有判别意义）：

| 下次证据组合 | 接下来定位的边界 |
| --- | --- |
| WM_MOUSE 按下到了，RTS Down 从未到达 | RTS 接收/鼠标 tablet/服务与 HWND 绑定 |
| RTS 到达，但 decoder/decode/publish 拒绝 | 明确失败阶段及设备包元数据 |
| RTS publish 成功，持续橡皮悬停/书写时 sourceAlphaNonzero=0 | controller 绘制准入、上传、shader/viewport/混合及最终纹理输出 |
| source alpha/RGB 正常，但 finalDib 数据异常 | staging/DIB 复制、stride、格式及底层合成 |
| source/finalDib 正常，ULW 返回失败 | USER32 错误、真实样式/参数与窗口生命周期 |
| source/finalDib 正常、ULW 成功但仍不可见 | USER32/DWM dirty 更新与目标窗口层级；需要下一步可控全量提交/可见性对照，不能提前认定 |

复现时分别用鼠标在画布中部连续书写和连续移动橡皮悬停位置，每个阶段持续至少 3 秒，确保持续产生脏区提交并让 1 秒采样包含实际操作；静止悬停可能不再提交，不能靠等待保证获得采样。保留启动至正常退出的完整重定向 stdout/stderr 和 IDT log；不要仅复制 console 缓冲末屏。新候选、验证结果和完整采集脚本见本任务的 `verification-2026-10-03.md`。
