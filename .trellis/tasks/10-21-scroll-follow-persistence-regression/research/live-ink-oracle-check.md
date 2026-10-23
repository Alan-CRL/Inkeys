# 真实墨迹夹具 oracle 修订独立检查

- 日期：2026-10-23（Asia/Shanghai）。任务仍为 in_progress。
- 范围：只读 E7C 冻结快照至本次 inl 的精确差异、现有原始证据及离线模型；不修改产品源码，不运行构建、产品 CLI、GUI 或新截图，不提交或推送。
- 预审 inl SHA256：`D83E9F6555F5624DD6A48201FEC78E97E13F540EF1AFFE707C7A7A11C536EBB3`。对应完整构建/短 CLI receipt 已交接并核验；本条不声称真实短流程通过。

## Findings (fixed)

无 checker 源码修复。实现者的两项夹具修订经独立静态检查，没有发现需阻断本次受限原生短流程重试的问题：

1. `KnownInkEdge` 并非忽略未知像素：仅在原颜色判断为非墨迹、已偏离已知灰底且位于既有笔划走廊时，验证同一个覆盖率能同时解释固定笔色与灰底的三个通道；覆盖率限制在 0–5%，各通道最大残差不超过 1。四个已有蓝笔边缘观察值可由该模型解释，反色、整体灰度偏移、绿色污染、超小覆盖范围和错误笔色负例仍拒绝。原强 core、最少 256 个 core、双向位移比较、Source GT 99/99 和精确背景 ROI、未知像素失败与失败图不保存均保持。它补充的是已知颜色混合分类，而不是任意未知颜色容许量。
2. `NativeMouseDownBound` 与研究公式逐项一致：一整 HIMETRIC 单位，加 float 比例、乘法和 viewport 相加误差上界。输入为实际整数 cursor Down，不从保存首点反算预期；double 差值且正向 `<=` 合取，NaN 首点不会误过。新增正 DPI、有限输入、估计 raw 小于 2^24、Display monitor/orientation/尺寸/DPI 稳定及 Down 前 client 原点零/全屏 1:1 检查。实际 DPI、保存值、预期值和界限在断言前记录。文件 GUID/页身份、两条 Pen、原 viewport `.001`、真实 Exit index 与 v10 Read/Import 要求未变。

## Findings (not fixed)

- Display effective DPI/2540 与实际 RTS `GetPacketDescriptionData` 系数相等不是通用产品合同。现有样例与 raw/decoded/file 证据仅支持本专用 OS 鼠标、1:1 映射的受限模型；真实 run 必须满足界限，失败不能进一步放宽。没有增加生产 observer 或替换 RTS 解码；不能外推到任意数位板、物理笔/触控或变换坐标。
- 旧 PID10832 仍是失败案例，不能追溯用新分类记通过；其四个 B 边缘点、真实两笔 Exit 文件及 PID62840 的鼠标量化调查说明了修订动机，不替代新二进制完整重跑。
- 当前审查未验证新 run 的 B 像素、完整 Exit codec/首点、十屏/水平/接力或外部应用；这些仍由后续实际案例逐项验收。

## Verification

- 精确 delta / 不变断言静态检查：pass。
- 实现者离线边缘模型 10 例结果：已读，含 4 个实际边缘观察及污染负例；不是独立新 GUI 证据。
- ARM64 完整 Solution 编译/类型与链接检查：pass，EXE SHA256 `03DEA84FEA607475304C5E58B45C7F1F97B9408F124E47723347ECE8E4197251`。原日志 `Build/ScrollFollow/motion-tracker-build-product-live-ink-oracles-1023.log` 确认 Build succeeded、0 Error、3 条既有 hashlib++ C4267；内部 21.81 秒，wrapper receipt 22.939 秒。
- 最小 CLI：独立读 `product-live-stage/offline-5450515/result.json` 及 stderr；16 条坏 argv 全部 exit2 且输出 0 字节，protocol exit0/232ms、state exit0/101ms。protocol 明示 Capture/Host/ACK 是 seams，不能称实屏测试。
- 离线数学：读独立 forward floor/ceil/nearest 生成器与 per-channel coverage interval oracle，540 个前向量化案例满足界限、720 个 ±1/±16 像素污染拒绝、10 个边缘实际/负例结果一致；最大 error/bound 约 .94133。输出 `product-live-stage/ink-oracle-fix/offline-oracles.json`。
- 格式/差异：原 UTF-8 无 BOM、纯 CRLF、EOF 保持；`git diff --check --ignore-submodules=all` exit0。Git 对历史 LF 文档的自动换行提示不是源码格式失败。
- 结论：已通知主会话放行一次既有 `--scroll-follow-live-ink-test` 原生短案例；没有运行新 GUI，无整体 PASS 主张。
- 本 checker 构建、CLI、GUI：未执行，遵守主会话的串行所有权。
