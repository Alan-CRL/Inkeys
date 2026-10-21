# 保存与完整回归实施顺序

## 前置和本轮权限

依赖产品阶段顺序check通过后validate/start。本轮批准全部后续代码，仅允许无窗口或全程不可见HWND；禁止Computer Use、可见内容、前台/置顶。必要实屏验收成为下一步时停止，父任务保留未完成。无commit/push授权，最小diff与既有编码/EOL。

## 顺序和所有权

- [x] 读父/本任务prd/design/implement、全部implement.jsonl、产品最终core-implementation/check报告与viewport-only-save-current研究。不得把旧owner诊断、单协议或空持久化root的Host测试当作正式文件验收。
- [x] 保存实现者统一拥有新增真实全隐藏文件/编辑test与必要最小hidden观察receipt、严格CLI/Main/工程登记；产品其它源码已冻结，不回退他人变更。发现生产缺口先定位原路径并最小修复，不另建保存链或冷载UI，不改UInk版本/Shader/墨迹算法。
- [x] 独立私有create-new root、逐组件拒reparse，所有HWND一直visible=0、非激活。autoSaveRoot显式私有、autoSaveEnabled=true；cleanup在窗口/回调context销毁前StopProduct drain。严格早退及harness-off/bad argv验证后才运行专测，不跑旧会ShowWindow的hidden/eraser入口。
- [x] 真实输入生成A并沿Clear保存，严格读实际Committed文件取得A的点/style/页/工作区/视口基准；可靠确认Controller已消费完成并释放CPU fallback，再Undo走真正worker磁盘读取安装A。优先复用现有确定性接缝，必要有限hidden receipt，不以sleep或worker计数冒充已安装。
- [x] 沿生产Begin/latest absolute/End真实owner平移；可信A点/style/页不变，非零基准/反向/水平/钳制/Seq/退休及跨帧顺序优先复用产品已有专测，不重复复制协议实现。允许明确ACK seam，不能宣称真实系统穿透。
- [x] Viewport-only独立场景：A恢复后不新Down/B，仅Follow，Publish最后目标后不预等Applied立即Stop；真实ExitAutoSave/worker drain后按严格索引读Exit .uink，Read Complete/Import Success，核对v10、原workspace/PageGUID、逐点/style和最终base-total/scale1。
- [x] A->Follow->End->真实B->擦除/Undo/Redo：核对B锁存新视口、A原点不改、Follow不建历史/不清既有Redo、新B按原语义清Redo。再沿真实Clear或Exit文件读回各最终内容。共享Read/Import验证文件viewport往返；现有Clear Undo磁盘恢复后再次Follow验证Host，任意跨进程冷载仍未提供，不扩张该能力。
- [x] 单一完整InkeysRepo.sln Debug ARM64原生MSBuild；同PowerShell规范Path/禁node reuse，>=5分钟，正常宿主，不改依赖/SDK绕环境。新增定向及必要相关既有纯CLI/严格隐藏回归，保存日志exit/耗时/边界。
- [x] 顺序trellis-check覆盖本阶段及产品接口，安全机械修复后复验。主会话统筹最终ARM64是否需重编、x64完整尝试、PE、i18n/XML/差异/BOM/EOL和全范围最终check，禁止并发MSBuild。
- [x] 更新实际参数/实测/console用法/未测及现场步骤，所有软件证据齐备后停在真实Mag连续路径与系统窗口/可见内容门槛，不标全功能通过。

## 已有基线与验证记录

产品core-implementation.md记录生产协议/模式快捷键/配置/全隐藏真实owner定向exit0，full --no-window交产品check，不在保存阶段无故重复。既有inkStrokeModelerTest.sln ARM64与四组storage CLI均已主会话通过，见storage-baseline-current.md；当前相关legacy源码相对HEAD无变。PPT legacy fixture私有TEMP必须<=40字符以避开原深层路径预算，禁止为此改生产路径策略。四场景真实Shape/Pen保存用例已通过实施验证，详research/implementation-current.md；独立check已通过，零位移A/codec比较已强化为逐值精确相等。

本轮权限完整记录见../10-21-scroll-follow-v1/research/implementation-authorization.md；先前仅Mag准备和旧可见临时授权均不沿用。最终验收矩阵必须区分真实owner、fake/合成图、ACK seam、文件往返与实屏。

## 最终软件验收与停止点

独立check完整ARM64 exit0/8.143s，新保存四场景exit0/4.194s，8真UInk，普通Pen23/23真实点逐值不变；相关短回归及7坏argv通过。主会话x64完整solution同配置重建exit0/6.791s，5纯CLI通过，两架构PE已查，详细见父research/final-build-and-pe.md。当前仅剩必要正式Mag/系统窗口/实际内容与硬件现场验收，按本轮限制停止；父/本任务不标全功能完成、不commit/push。
