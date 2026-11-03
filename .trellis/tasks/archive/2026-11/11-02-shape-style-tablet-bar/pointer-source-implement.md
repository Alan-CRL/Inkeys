# R3 Pointer为根的输入来源仲裁：实施计划（用户已同意，待启动）

依据 pointer-source-design.md。不commit/push，不GUI，每阶段结束无窗口回归；沿用任务既有构建约定（ARM64原生MSBuild、同PowerShell单一Path、禁节点复用、Debug|ARM64≥900s、headless `--no-window` 900s、Release|x64 1800s）。保持原BOM/CRLF、中文关键注释和minimal diff；现有脏改动（共享样式、诊断、其他任务、两个CSO）不覆盖。

## 阶段0：冻结基线与上下文

1. 记录当前dirty基线与已通过的验证记录（touch-registration-validation.md）；确认 implement.jsonl/check.jsonl 引用新设计与分析文件（`task.py validate`）。
2. 读 `.trellis/spec/native-desktop/{ui-pointer-session,pen-haptics,cpp-conventions}.md`，列出需更新的合同条目。

## 阶段1：实测探针（决定“不转交DefWindowProc”是否成立）

目的：在不改业务的前提下，验证设计§“不转交DefWindowProc”的假设，并取得隐式捕获/LEAVE次序证据。

1. 仍在诊断开关内（测试触控反馈），为五窗新增一个**可开关的探针模式**：对接触阶段Pointer消息（DOWN/INCONTACT UPDATE/UP）`return 0` 而不交DefWindowProc，业务暂不接入；记录该模式下WM_TOUCH与签名Mouse到达计数、UP是否回到DOWN窗口、LEAVE/CAPTURECHANGED次序。
2. 构建Release|x64，给用户同一CMD采集方法；两块板（M6板、旧板）+ 手指触屏 + Surface笔各一轮（用户有设备者即可），文件名区分（如 `M6-probe-nodefproc.txt`）。
3. 判定：
   - B、C计数为0 → 采用“不转交”主方案；
   - B/C仍到达 → 采用“类型排除”兜底（设计已保证无重复），告知用户长按右键合成是否仍出现，由用户决定是否接受；
   - UP不回DOWN窗口 → 在设计中补Pointer捕获（`SetCapture`/路由）的具体处理后再进入阶段3。
4. 探针模式不改变业务与默认设置；是否保留为诊断选项在阶段6决定。

## 阶段2：准入函数与接触值类型（无行为变化的纯重构）

1. 新增 `Admit(Mode, msg, wParam, extraInfo, flags)` 纯函数及 `Mode` 解析（动态解析 `GetPointerInfo`；Win7返回LegacyTouchMode）。先只在测试与诊断中使用，不改变窗口行为。
2. 引入值型 `Contact` 取代 `Touch::Packet::input` 的 `TOUCHINPUT`；`DecodeTouch` 由现有路径产出，保持逐字段等价（DOWN/UP/MOVE、PRIMARY、PEN位、hSource、dwID、dwTime、x/y）。同步调整 `Context::Owns`、`DispatchDecoded/Continue/LogPacket`、`ContactTokens::BindDown/Touch` 的入参（暂以适配保持旧匹配逻辑）、Bar/Page Deliver、Page测试包。
3. 无窗口测试：`Admit` 全矩阵、`DecodeTouch` 等价、旧router用例全部原样通过。**验收：行为与M6诊断版完全一致。**

## 阶段3：Pointer解码与五窗接入

1. 实现 `DecodePointer`：在WndProc当次消息内取 `GetPointerInfo`，按设计规则映射为 `Contact`（Kind、primary、pen、device=`sourceDevice`、id=`pointerId`、`dwTime`、`ptPixelLocation`）；API缺失/查询失败只记录并取消，保留调用者GLE；不猜Mouse。
2. router新增PointerMode入口：目标=接收窗，复用probe/语义/注册代次/lease/capture/取消；DOWN新建route generation；`CANCELED/CAPTURECHANGED/接触中LEAVE/隐藏销毁` 走既有 `CancelTarget`。LegacyTouchMode保持现有 `CandidateAt` 路由。
3. Bar：WndProc新增接触阶段Pointer处理（按阶段1结论决定return 0或DefWindowProc+排除）；channel_callback不重复解码；`WM_TOUCH` 在PointerMode下读取关闭句柄并丢弃（诊断计数）。Page四窗同理（其 `dispatchedPointer` 路径与现有 `ProcessNativePointer` 保持）。
4. 悬停消息保持DefaultProc；签名Mouse过滤保持；真实Mouse/Raw/租约不变。

## 阶段4：触觉

1. `ContactTokens` 新增 `BindPointer`：PointerMode下DOWN按 pointerId/device/cursor/time 直接绑定，UP按pointerId收尾；保留旧 `BindDown/Touch` 供LegacyTouchMode。
2. `ObserveBarHapticPointer` 保留（建立/失效凭据），Deliver改用 `BindPointer`；凭据无效只省略震动；诊断保持 `bar_touch`。
3. 测试：Pointer携带凭据的Down/Up/取消、无凭据只省略、Legacy路径不变、每次点击凭据至多一个。

## 阶段5：测试与独立检查

1. `ui_pointer_tests.cpp`：来源矩阵——{仅A、A+B、A+C、A+B+C、仅B(Legacy)、真实Mouse、Mouse叠加Pointer接触(lease互斥)}，断言每个物理接触恰好一个业务Down/Up；Pointer pen/touch、第二键/桶键拒绝、LEAVE/CANCELED/CAPTURECHANGED收尾、同pointerId多次点击generation递增、PPT长按无Move持续、白板Up一次、拖动保持Down owner、跨窗悬停与按住。
2. `message_tests.cpp`：签名过滤与Pointer准入。沿真实生产helper，不复制算法。
3. 独立 `trellis-check`：锁顺序与同线程重入、`GetLastError` 保护、Win7无硬导入、旧router合同未破、触觉不变量、BOM/CRLF。修复任务内问题。

## 阶段6：构建与验证

1. 完整 `InkeysRepo.sln` Debug|ARM64，五个早退生产探针与headless `--no-window`。
2. 源冻结后Release|x64同solution；核PE架构、`GetPointerInfo` 非硬导入、新诊断标识（`source_mode`、`legacy_dropped`、`derived_dropped` 计数）、hash。
3. 生成同一CMD采集说明（新文件名），实机清单：
   - M6板：主栏与分页可点击，日志无B/C业务、业务点击数=Pointer Down数；
   - 旧板：A+B+C设备无重复点击、跨窗不卡死；
   - 手指触屏：点击、PPT按下翻页与长按、拖动；无长按右键；
   - Surface笔：震动保持（用户设备）；
   - 真实鼠标/触摸板正常。
4. 无窗口结果不冒充真机修复。

## 阶段7：规范与收尾

更新 `ui-pointer-session.md`（来源集合/模式/准入合同）、`pen-haptics.md`（BindPointer与Legacy保留）、prd/design/implement 的状态；Trellis `finish-work`/commit 由用户决定，不自动提交。

## 回退点

- 阶段1探针结论为否 → 回到设计§风险，调整为“转交DefWindowProc+类型排除”，不进入阶段3的return 0。
- 阶段2后可单独回退为M6诊断版行为（纯重构）。
- 阶段3/4任一失败，只回退本阶段增量，保留共享样式、诊断与Legacy路径，不使用git reset。
