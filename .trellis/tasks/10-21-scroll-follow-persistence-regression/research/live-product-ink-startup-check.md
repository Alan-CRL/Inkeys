# 独立实屏证据检查：ink 启动阶段失败（PID56220）

2026-10-22；任务继续 in_progress。本检查只读原始文件，没有源码修改、构建、CLI、GUI、输入或截图操作；未重复 core 正在调查的取消原因源码。此前最终离线检查保留原报告与 hash，没有被此失败覆盖。

## 结论

当前 D6A348E 实屏 ink 尝试 **未到达产品、画笔、屏幕GT或文件验收**。本次 exit1/783ms，security=0，在 window-service 阶段记录 fixture-owner-failure-or-cancel。失败来源待 core 定位，不能据此判断 Mag/PrintWindow、RTS、样式ACK或墨迹实现是否失败，更不能将其记为墨迹通过。

## 原始版本与范围

- launcher 前缀：`Build/ScrollFollow/live-product-ink-desktop-confirmed-220145046-test`。
- stdout 首行实际 root：`Build/ScrollFollow/product-live-56220-220145468`，PID56220，main TID30788。
- 外层 JSON 实际 exit1、elapsedMs783、stdout823B、stderr2511B。
- launcher 的 `hashScope=file-before-StartProcess`，EXE SHA256为 `D6A348E46F78E0FC53F5C39B921EFAB11A7BB4B498DE4CACC03940964FA456ED`，文件 LastWriteUtc为2026-10-22T11:13:15.8833640Z。此为运行前磁盘文件关联，不称进程内存 hash。
- 日志声明新独立 OS-ink-two-wheel-off-b-exit case、无TestPorts/手动Follow目标/手动ACK；这一声明不是已执行相应路径的证明。

## 本次实际已发生的证据

1. source_paint sequence1、offset(256,512)、CPU生成图案hash04f97d148251701f。这是绘制侧准备，不是实际屏幕GT通过。
2. 原Mag记录 owner TID7076、host0x104132C、child0x351472、filters5、source_unfiltered1、selection_transparent1、prefer_dcomp1。只证明该阶段创建/准备，不证明正式FreezeCapture Start/Read或连续图像验收。
3. source_foreground记录 api0/actual_own0。当前实现允许继续核自身Source屏幕和实际命中，本次在后续window-service等待即终止；不能单凭这个字段认定前台限制为唯一失败原因。
4. FAIL stage=window-service、reason=fixture-owner-failure-or-cancel、error0/security0。此错误文本没有分辨所有owner失败/取消源；checker没有替core猜测原因。
5. stderr显示Drawpad与DrawpadPresentation自建窗按hidden创建后销毁，未证明有效产品呈现。没有pen-ready、StartProduct成功、A/B Down、SourceGT成功、Capture追踪、window ACK或像素跟随成功行。
6. root目录仅有823B result.log，与launcher stdout字节完全一致；没有PNG、AutoSave目录、索引或UInk。因此没有可独立核图、核file/GUID/viewport/首点的材料。

## 退出证据边界

结尾product_running/service_running/mag_ready/capture_occupied/own_left_held/log_failed均0，result1，专用进程按外层记录正常返回失败码；没有AV或watchdog超时记录。

但state_tid/state_returned和mag_tid/mag_returned均0，不能套用此前完整成功case的两条owner返回回执。原Mag早期owner创建行与这些计数不能直接合并成“全生命周期返回通过”。本次只记录到达范围及退出资源状态，不宣称20次启停或完整清理矩阵已重测。

## Findings (fixed)

本次没有修改源码，未发现需要checker机械自修的新问题。按主会话分工，将启动失败原因留core独立定位。

## Findings (not fixed)

- window-service取消/失败的具体来源未定，当前日志不足以区分；不得归因于用户锁屏（security明确0）、外部应用、Mag或RTS。
- 样式→真实ACK、OS笔划消费、非空辅助窗、32/-16墨迹像素跟随、Off、B锁存新视口和真实Exit文件均未执行到有效验收。
- 原最终无GUI检查已通过的范围继续有效，见 `live-final-offline-verification.md`；其协议seam/旧Headless字节不能填补本次实屏缺口。

## Verification

- 原始证据一致性：pass；launcher与root日志相同、PID/root/版本/耗时关联准确。
- 新lint/typecheck/tests：本次没有修改源码，没有运行；不重复既有最终离线结果。
- 实屏ink：未验收，启动阶段失败。

## 原始文件指纹

| 文件 | 字节 | SHA256 |
|---|---:|---|
| Build/ScrollFollow/live-product-ink-desktop-confirmed-220145046-test.json | 337 | `9B8ECC932880A79485805B04398C9429FC1E928751F7CEE7DB488C34A575F338` |
| Build/ScrollFollow/live-product-ink-desktop-confirmed-220145046-test.log | 823 | `C550975761C4EE05B3324C1C2A79B89335EE8EF9CD6136E9C1C7BC98BE50684B` |
| Build/ScrollFollow/live-product-ink-desktop-confirmed-220145046-test-stderr.log | 2511 | `27FAEF6CFE33B72E1489400E3BEC97F1DB15231974E22126DF4B09752A33B931` |
| Build/ScrollFollow/product-live-56220-220145468/result.log | 823 | `C550975761C4EE05B3324C1C2A79B89335EE8EF9CD6136E9C1C7BC98BE50684B` |
