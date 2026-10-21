# 验收矩阵（持续更新）

2026-10-21 用户后续将当前交付收窄为：共用原定格 Mag、清理多余原型、验证 Inkeys.exe 内 PrintWindow(2) 与可选回调。本轮软件内诊断与原完整滚动批注的完成状态必须分别记录。旧独立原型的测试不替代当前源码结果。

| 阶段 | 项目 | 当前状态 / 证据 |
| --- | --- | --- |
| 既有设计 | Mag 同线程初始化/创建与就绪等待 | Inkeys3 已保留，原 WindowService / Coordinator 普通线程路线不改；Inkeys2 精确历史见子任务 research/inkeys2-mag-thread-history.md |
| 清理 | 独立服务、runtime lease、旧测试与复杂生命周期接线 | 已移除本任务自己的独立原型；普通 Window.cpp、Coordinator 与工程登记回原基线 |
| 本轮构建 | 完整 Debug ARM64 solution | 正常宿主完整构建 exit0，首次40.88s；最终冷启动测试源码增量构建 exit0、19.09s，3 warning/0 error，无 C1902；最终审查结果另见子任务 report |
| 本轮读回 | 原 hidden pair 冷首帧及持续变化 | 最终两个新进程 run2/run3 均 exit0，7个唯一图案阶段全部读到；run2 首次62/63被拒绝并重试，随后9次63/63；run3 9次63/63。只有 alpha0 准备态，不先 opaque Reveal 预热 |
| 无回调必需路径 | 冷启动变化与恢复后继续读回 | run2/run3先不登记回调完成phase1–3与静止3次，之后恢复nullptr完成phase7；两轮都取得正确变化图像。允许等待/旧帧，不依赖回调 |
| 本轮排除 | 蓝覆盖窗与静止图像 | GetPixel 先确认蓝窗确实挡住源，原角色 SubmitFilter 后读到正确网格；每轮静止3次、原定格揭示/隐藏2次均通过；已接受的14张PNG离线逐像素核对均零差异 |
| 本轮回调 | 登记、触发、退出、恢复 | 两轮均登记成功、6次进入/退出、与 Mag owner 同 TID；3个阶段观察到退出后读回正确当前图案；恢复 nullptr 后仍可读回。只证明本机实测行为，不宣称所有系统最终整帧完成保证 |
| 本轮耗时 | PrintWindow(2) 本身 | 两轮均值约47ms，31–63ms；首帧策略800ms、稳态100ms，回调信号可提前；没有进行连续20FPS吞吐验收 |
| 本轮清理 | 原窗口与 Mag 寿命 | 原pair销毁、原owner MagUninitialize 成功，测试进程正常退出；GDI11→13、USER4→5，不能宣称零资源增长或长期有界 |
| 本轮回归 | --no-window、i18n、XML、diff、编码/EOL | 完整 --no-window exit0，i18n两语言425/425、XML/diff/BOM/EOL及PE检查通过，非法额外参数exit2；顺序check通过，无需代码修复 |
| 捕获完整阶段 | 连续服务、取消、卡住系统调用、重复启停/异常销毁 | 本轮只做软件内诊断，尚未提供连续捕获会话，原完整阶段不标记完成 |
| Tracker | 静止300帧、十屏长距离/接力/反向/亚像素、动态干扰/拒绝 | 未进入 |
| 会话 / Draw3 | latest-only、seq/session/scene拒绝、End屏障、接触/双窗/Tile/Resize/设备/钳制 | 未进入 |
| 产品 | More按钮、中英繁资源、工具/Freeze/工作区互斥 | 未进入 |
| 存储 | A→跟随→B→编辑历史→保存读回→再次跟随 | 未进入 |
| 其他平台 | x64、旧 Windows、WOW64、真实笔/触摸 | 本轮未重建/实跑，不能由 ARM64 结果推定 |
| 业务GUI | 浏览器/Word/PDF、惯性/长滚动、锁屏与解锁 | 本轮未测；软件内专用图案测试不能代替 |

## 当前证据

- 捕获子任务 research/freeze-printwindow.md：当前实现、命令、实际窗口/线程、像素、回调和清理结果。
- Build/ScrollFollow/freeze-printwindow-final-build-*.log、freeze-printwindow-run2.log、freeze-printwindow-run3.log：最终原始日志（gitignored）。
- 最终PNG仅保存已经匹配专用图案的帧；不保存无效帧可能包含的桌面内容。

## 历史证据

旧独立服务的 cold 曾通过、shared Filter50/NearBlack 失败、x64 C1060/LNK1140 与快速 WGC/PrintWindow 探针，保存在 research/capture-poc.md 与子任务 research/check.md、quick-probe.md、quick-check.md。对应生产服务和旧测试现已移除，不构成当前共用路径仍失败或已全面验收的结论。先前 Computer Use 观察也不等于本轮完整业务GUI测试。
