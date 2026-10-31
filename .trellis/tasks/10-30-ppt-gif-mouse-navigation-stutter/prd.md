# 调查并修复 PPT 动态 GIF 页面鼠标翻页卡顿

## Goal

消除 Inkeys 搭配 `D:\Personal\Downloads\【10】羧酸 酯基.pptx` 放映时，第 3/4 页鼠标翻页出现的秒级无响应，并让等待状态对用户可见。修复必须减少 Inkeys 自身可避免的 Office COM 工作，同时保留文稿、页面和墨迹身份安全，不用模拟键盘或提前开放画布掩盖问题。

用户已批准并完成修复实施；详细结果见 [research/validation.md](research/validation.md)。

## Background

- 调查已确认：原始页码先变化，`PptCOM` 随后在唯一 Office owner 上同步读取完整 descriptor；普通翻页和周期刷新都会枚举全文稿 `SlideIndex/SlideID`。
- 指定 PPTX 第 4 页含 100×80、26 帧、12,704 字节 GIF。三轮实机进入该页测得约 1.05–5.58 秒 descriptor 延迟，最大约 5.66 秒到输入恢复；用户报告的 10 秒长尾未原样复现。
- 鼠标控件通过 Inkeys COM `View.Next`，键盘由 PowerPoint 直接翻页；键盘仍会受到后续 descriptor 与输入门禁延迟。
- native outstanding 会静默合并翻页命令，COM 结果没有向分页 UI 表达等待或失败，造成重复点击看似无效。
- 完整证据见 [research/investigation.md](research/investigation.md)。

## Requirements

- R1：普通同场翻页不得默认执行全文稿 `Slides.Item` 拓扑扫描；周期维护也不得仅以固定时间到期为由执行全量扫描。
- R2：绑定变化、页数变化、当前 SlideID 缺失或不匹配、显式失效及需要完整拓扑的结束页，必须走完整校验或既有安全降级，不能复用可疑拓扑。
- R3：允许非当前页的后台重排推迟到访问不一致页面时发现；发现前不得把错误页面作为可信墨迹目标，发现后必须先完成全量校验再开放输入。
- R4：非必要 descriptor 维护不得排在已排队翻页命令之前；不得新增跨线程 Office RCW 访问或声称已进入 Office 的调用可取消。
- R5：分页控件在 native 翻页请求 outstanding 时显示明确的等待反馈，保留旧页码但不伪造进度或成功；反馈不得替代 Draw3 Present 和 PageControl ack。
- R6：保持现有 COM 接口、TLB 方法顺序和 descriptor JSON schema 不变；保持 PowerPoint/WPS 支持范围不变。
- R7：补充默认关闭的分段诊断，能区分当前页读取、全量拓扑读取、owner 排队/执行和 native/UI 等待。
- R8：保留调查原始证据及 10 秒长尾、具体 COM 子调用尚未完全验证的边界。

## Acceptance Criteria

- [x] AC1（R1/R2）：managed 测试证明普通同场翻页的全量 `Slides.Item` 读取次数为 0；每种失效条件都会触发完整校验或既有安全降级。
- [x] AC2（R2/R3）：SlideID 重排、增删页、重复/非法 ID、当前页不匹配和 Office busy 测试中，不会把旧页墨迹或旧拓扑开放给新目标。
- [x] AC3（R4）：owner 测试证明已排队 Next/Previous 在非必要全量维护前执行，且所有 COM acquisition/release 仍配平。
- [ ] AC4（R5）：真实分页控件在点击后的下一次可见提交中显示等待状态；命令结束、换页、退出和失败路径均能清除或更新状态，不出现永久等待假象。生产状态与实机命令路径已验证，长阻塞下三点标记像素仍待捕捉。
- [x] AC5（R6）：不修改公开 COM 方法签名或 descriptor schema；新 managed 产物按现有交接链同步后，native 旧接口调用和兼容路径继续工作。
- [x] AC6（R7）：诊断默认关闭，开启后可用同一 session/target 关联耗时，且不记录文稿路径、标题或墨迹。
- [ ] AC7（实机）：指定 PPTX 冷/暖第 3→4、第 4→5 分别测试鼠标和键盘；等待反馈立即可见，普通翻页不再发生全量扫描，记录至少各 5 次样本的分布，不用单次最好值宣称修复。冷暖各 5 轮和 fast/full/QPC 已完成；可见性验收与 AC4 同项保留。
- [x] AC8（回归）：完整 Solution、managed、headless 和适用的 i18n 检查通过；真实 Office 验证与未运行的 WPS/其他版本明确分开记录。

## Out of Scope

- 用发送键盘消息替代 COM 翻页，或删除 raw/descriptor、Draw3 Present、PageControl ack 身份门。
- 为本问题重写用户 PPTX、优化 PowerPoint 内部 GIF 解码，或承诺消除 Office 自身全部冷加载耗时。
- 未经用户另行批准的 git commit/push；扩展 PowerPoint/WPS 正式支持矩阵。
- 在第一版等待反馈中破坏性修改 COM ABI 以返回丰富错误码；如后续确需该能力，另设计兼容接口。

## Open Evidence Gaps

- 10 秒长尾未原样复现，当前最大同类样本约 5.66 秒。
- 尚未拆分具体是哪一个 Office COM 属性调用形成最长阻塞；实施时先补 R7 诊断，不能用猜测替代测量。

## 2026-10-31 审阅修复补充

用户已批准延续任务修复三项问题。要求：另存/首次保存/新放映场次不得继续沿用旧文稿持久化身份；完成先到的导航请求不得被迟到发布复活；白板冷页失败恢复不得作用于其他场景或页面。继续保持普通翻页不做全量枚举、dynamic 与手动 COM 释放、现有 ready 门禁。回归和边界见 [审阅修复计划](research/review-fixes-plan.md)。本轮不 commit/push、不运行 GUI；既有实机未验项保留。
