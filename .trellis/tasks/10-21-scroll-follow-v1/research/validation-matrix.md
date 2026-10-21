# 验收矩阵（本轮持续更新）

基线feature/move / 3ace726b。2026-10-21用户批准全部后续实现；测试仅无窗口或完全不可见HWND，禁止Computer Use、可见内容、前台/置顶。正式实屏必要验收时停止，不自动commit/push。旧诊断和当前正式生产路径分别记录，不相互代替。

| 阶段 | 项目 | 当前状态 / 证据 |
| --- | --- | --- |
| 原Mag设计 | 同线程初始化/窗口创建、readyPromise | 保留原WindowService设计；普通Freeze分工不改，无第二Mag/lease/owner RPC |
| 基础读回（上轮） | 真Inkeys原Mag无callback冷首帧/变化/静止/排除 | 3ace726b已验证，两新进程全部接受样本63/63、14张PNG整图零差异；允许延迟，非当前worker正式连续证据 |
| 正式取帧 | Start/Stop/监督/质量/元数据/单DIB/Freeze互斥 | 已实现并顺序check；原MagnifierThread串行PW，不依赖callback。API与边界见production-capture[-check].md |
| 正式取帧测试 | fake backend、阻塞消费者/超时/迟到/Busy/停止竞态 | 定向exit0约2.48s；原source/client差异但identity映射1:1。新监督idle/active各64次join通过，无HWND |
| 正式取帧构建/回归 | ARM64完整solution、--no-window、格式 | check最终29.32s exit0/0error，62既有warning；--no-window约109.92s exit0/stderr空；XML/BOM/CRLF/diff通过 |
| 正式取帧实屏 | 新worker真实变化/排除/持续FPS/资源/异常OS销毁 | 未验收。当前禁止可见内容和操控，上轮owner结果不能代替；已保留最终必要可见门槛 |
| Tracker | 独立LK/纯平移/可信度/特征接力及严格离线误差 | 实现/独立check通过；6918合法帧全prefix通过，4px误接受修复，ARM64/target/--no-window全exit0；明确1/2px少数运动合并边界 |
| 隐藏测试基线 | 实际WindowService/Host输入/present/重复Stop | 既有--draw3-host-metrics-smoke exit0，私有日志hidden-baseline-8aa93957a1724173bbe29945f6cc2636，所有自建窗visible0/topmost0 |
| 会话/Draw3 | Begin/absolute latest槽/seq和scene/End/present/gate/Tile | 实现/独立check通过：实际Tick/Frame/Tracker控制矩阵、全隐藏真Host/跨代/输入/End恢复/clamp/final；新所有权与退出监督问题已修，系统窗口ACK仍seam |
| 主栏/生命周期 | More按钮/三语SVG/补缺配置/工具工作区退出 | 资源/迁移及Core接线独立check通过，config/state-mode/shortcut纯业务CLI exit0，i18n426/426；实际Bar可见/系统交接仍未验 |
| 存储/编辑 | A->follow->B->history->真实Exit/Clear保存重载->再follow | 四场景Shape/Pen独立check exit0/4.194s：8真UInk、CPUfallback释放后disk安装、原A/codec点/宽/style逐值相等/GUID、B新视口、真mask覆盖及Undo/Redo，viewport-only最后Publish立即Stop均通过。四组既有storage基线通过；任意跨进程冷载仍非已有产品能力 |
| 最终检查 | ARM64+x64尝试、全部允许测试、i18n/PE/格式 | 最终ARM64 exit0/8.143s；x64同配置修复生成PDB后完整solution exit0/6.791s及5纯CLI通过；两架构PE/47文本/4XML/i18n/diff通过。产品full --no-window exit0/157.972s及退出8例复用理由见保存check |
| 必要可见验收 | 正式取帧、桌面穿透/辅助交接/墨迹、浏览器WordPDF/主栏 | 本轮权限不覆盖；到该必要门槛时停止并报告，不标全功能通过 |
| 硬件/平台 | 笔触控、Win8.1/Win7/WOW64、锁屏/多屏/DPI现场 | 未实测，须分别给补测步骤；不由ARM64或隐藏通过推定 |

## 当前证据

- 捕获子任务research/production-capture.md与production-capture-check.md：正式接口、线程/寿命、实现/审查结果及实屏边界。
- research/execution-current.md：当前阶段、每阶段顺序交接和本轮授权。
- 捕获子任务research/tracker-design-current.md和product-integration-current.md：只读研究与实施建议，不等于测试通过。
- Build/ScrollFollow/production-capture-*和hidden-baseline-*：当前日志（gitignored）。

## 上轮与历史证据

原Mag owner图案/回调/整图像素详捕获research/freeze-printwindow.md；约47ms读回、首等800ms不等于20FPS通过。旧独立服务shared Filter50/NearBlack、旧cold、快速WGC/PW和x64 C1060/LNK1140保留于capture-poc/check/quick报告，原型生产代码已删除，不构成当前正式服务成功或失败的判断。先前Computer Use授权仅其轮次有效，本轮不使用。

## 最终软件交付

四阶段软件部分与顺序独立check已完成；正式worker实际画面及系统交接仍未验收。交付总说明见delivery-current.md，最终双架构构建/PDB留档/PE见final-build-and-pe.md，使用与具体现场步骤见usage-and-live-acceptance.md。本轮未创建可见/前台窗口、未Computer Use、未commit/push，不归档父任务。
