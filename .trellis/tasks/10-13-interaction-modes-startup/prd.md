# 产品模式与启动预设：交互模式切换和命名

## Goal

修复设置交互模式切换，规范三语名称、标题与说明，研究启动默认模式判定；后续启动预设需求按用户补充推进。

## Requirements

- R1：修复设置切换交互模式时卡死并拖住共享 UI 渲染线程的问题，定位真实同步链和锁循环；选择后运行时状态与持久化配置一致。
- R2：标题使用「选择交互模式／選擇互動模式／Choose an interaction mode」。
- R3：保留用户提供的三语名称和说明。恢复原有下拉框样式；两条说明放进模式选择块上方的独立信息卡片，使用现有 PPT 提示卡片类似的蓝色信息图标、圆角边框和内边距（参考用户图二），文字按实际高度自适应换行。
  - zh-CN：大屏触控模式；适用于教室、会议室等场景，直接在触控大屏上书写和操作。
  - zh-CN：桌面交互模式；适用于个人电脑、平板及鼠标操控的大屏，兼顾鼠标、触控笔、数位板和触摸操作。
  - zh-TW：大螢幕觸控模式；適用於教室、會議室等情境，直接在觸控大螢幕上書寫與操作。
  - zh-TW：桌面互動模式；適用於個人電腦、平板及以滑鼠操控的大螢幕，兼顧滑鼠、觸控筆、繪圖板與觸控操作。
  - en-US：Large-Screen Touch Mode；For writing and interacting directly on large touchscreens in classrooms, meeting rooms, and similar settings.
  - en-US：Desktop Mode；For PCs, tablets, and mouse-operated large displays, with mouse, pen, graphics tablet, or touch input.
- R4：产品分类以交互场景为依据；Surface 即使用手指触摸也属于桌面交互，普通投影或投屏不能仅因画面大而归入大屏触控。
- R5：保留全部启动自动判定规则，包括尺寸未知时回退大屏；不覆盖已有手动选择。用户已确认本次仅修复切换和文案。

## Acceptance Criteria

- [x] 下拉切换的 Sync 在设置绘制期间转交已有 FIFO；生产队列/guard/原子模式更新的线程回归通过，保留同步与配置保存顺序。真实 HWND 事务与 GUI 点击未执行（R1）。
- [x] 恢复原有 Widgets::combo；上方说明独立信息卡片，三语×四种缩放几何回放通过。默认字体回放不代表嵌入字体像素验收（R2/R3）。
- [x] 文案分类符合 Surface/投影边界；启动自动判定代码保持不变（R4/R5）。
- [x] 最终代码通过 i18n check、增强线程回归、完整 Headless --no-window、Debug|ARM64 solution build和独立审查；构建/Headless沿用受限环境外验证。

## Confirmed Evidence

- 设置入口：Inkeys/Inkeys/UI/Setting/Setting.cpp:3762、3789、3801，已有组合框和 SyncDraw3State/WriteSetting 调用；原下拉框在无窗口 ImGui 回放中可切换，现场故障未复现；本次改为两个直接互斥选项，避免弹出选择路径与会话级缓存。
- 默认判定：Inkeys/IdtMain.cpp:2408；配置覆盖：2519；PaintDevice 读取/保存：Inkeys/IdtConfiguration.cpp:289、555。
- 物理尺寸不可用原因：Inkeys/Inkeys/Display/Display.cpp:650，包括复制/混合拓扑、EDID 缺失和映射不唯一。

## Out of Scope

- 尚未提出的启动预设新功能、配置系统迁移、无关重构、自动提交或推送、GUI 测试。

## Key Decisions

- 用户已审阅规划并确认保留全部启动自动判定，本次只修复切换和文案。
- 自动识别与产品分类仍有差异，作为后续启动预设工作处理；本次不扩大范围。

## 本轮纠正

- 用户反馈单选项实现仍在切换时卡死，整个共享 UI 受影响；原默认字体+同步桩回放没有覆盖生产跨线程调用，不能证明故障已修复。
- 本轮明确要求恢复下拉框、上方说明卡片化；此前直接单选方案被替换。

## 用户验收

2026-10-13 用户确认已经修复，并授权提交本轮 commit；明确要求任务继续进行，后续还将修改启动预设，不执行归档或任务结束。
