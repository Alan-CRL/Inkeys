# Research: UI3 规范补读地图

- Query：Trellis 原生上下文存在长度截断时，如何保证后续实现/审查实际读取本任务需要的 UI3 合同？
- Scope：internal；规范引用清单，原规范仍是唯一权威，本文件不替代或修改规范。
- Date：2026-10-28

## Findings

`task.py validate` 检测到 `rendering-and-ui.md` 为306777字节、`canvas-freeze-boundaries.md` 为43487字节，超过单文件32768字节注入上限。校验成功不表示全文已注入。后续 implement/check 在动手前必须从磁盘补读下列相关标题的完整正文，不能只依赖注入前缀；不为此修改全局/项目上下文限制。

| 规范 | 必须补读的章节 |
| --- | --- |
| `native-desktop/rendering-and-ui.md` | 公共按钮、固定面板与多目标事务；主栏按钮统一状态派生合同；UI3 idle 唤醒动画时钟合同；UI3 共享串行调度器合同 |
| 同上 | UI3 PageControl 与 Bar 按钮单一来源合同；UI3 基于变化的脏区事务合同；UI3 共享设备、串行帧与光影缓存契约 |
| 同上 | UI3 动画批次加入与关键帧中点；UI3 复合面板收起与换边关键帧；Whiteboard 分页与 workspace 生命周期合同；Win32 Window、DibSurface 与 HiMsg 合同 |
| 同上 | 主栏与控件静止字号及连续动画目标（2026-10-24）；UI3 复合视觉切换状态机合同（如涉及公共视觉目标）；UI3 第三鼠标光休眠/边缘光影合同（如涉及光效） |
| `native-desktop/canvas-freeze-boundaries.md` | 与三态workspace交接、输入fence、真实背景/首帧/窗口回执、过期token、失败预算/源补偿有关的完整章节；按实际拟改接缝搜索标题及正文，不只读开头 |
| `native-desktop/ui3-render-diagnostics.md` | 完整合同，特别是raw-only细阶段门、软件commit与代理、Idle/epoch断链、完整采样与公共多目标基准 |
| `native-desktop/whiteboard.md` | 三态协调、背景/分页、Desktop基线、最小化与最新PPT事实的相关合同 |
| `ppt-interop/native-session-ui3.md` | session/revision/target/UI-ready与交互资格合同 |

路径均相对 `.trellis/spec/`。先用 `rg -n '^#' <file>` 定位当前标题，再按节读取；本地图不用固定旧行号替代正文。每个候选只改其必要的接缝；实现与审查报告需说明已补读哪些章节。

## Caveats / Not Found

本文件只处理已观察到的注入截断风险，不表示规范本身有问题。当前任务仍为planning，无源码修改。
