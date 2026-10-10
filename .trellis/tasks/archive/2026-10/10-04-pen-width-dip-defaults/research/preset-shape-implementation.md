# Implementation: ExplicitDip预设高亮与形状共用档位

Active task: .trellis/tasks/10-04-pen-width-dip-defaults。Role: trellis-implement；基线f8054a227174707a984291d875b4fcc6fadf605f。未递归派代理、未commit/push/归档、未运行完整构建或GUI。

## 上下文与设计自检

已读取native hook完整保存输出（超限内容补读磁盘），implement.jsonl所列规范/研究及PRD/design/implement；完整draw3-integration磁盘内容并补读首次工具返回截断片段。本轮以preset-shape-scope.md、preset-shape-research.md为最新产品合同；执行trellis-before-dev，确认已有共享策略和形状独立槽，没有扩大修改边界。

原缺口是IsBarThicknessPresetSelected只接受Preset来源，拒绝所有真实手动提交的ExplicitDip。形状初始化/点击/读数/预览已使用Brush策略，不存在另一套1/3/5，需要修共用高亮而非重写正确数值线路。

## 修改文件

- Bar.Layout.cppm：index>=3/非法Selection返回false；Preset有效身份优先；ExplicitDip用真实snapshot的Family+Context调用唯一PresetDip，容差8*double epsilon*max(abs(value),abs(target))。无绝对下限、不比较整数文字/输出px，不改来源、状态或配置。新增limits include及关键中文注释。
- Bar.Interaction.cpp：仅已有RunThicknessFineDialProductionTest内新增回归；没有修改交互状态机或事务实现。

## 生产回归覆盖

- 普通96/264 DPI、大屏1080p96DPI/1440p275%/320×180275%，4工具3档：普通/荧光真实FineDial Begin→candidate→Commit(false)，Laser使用实际Explicit条件setter（Laser不开放FineDial）。从非预设开始，松手后仅相同档高亮；来源仍ExplicitDip，查询不推进state/bridge revision。
- 直线/虚线/矩形各三档真实预设setter、实际Metrics/Bar读数策略与Brush一致；显式非档值→同档条件setter高亮，Brush和另一形状槽保持独立。
- 一个double nextafter匹配，超过double计算容差/一个float ULP不匹配；3.1与3.0显示同整数但不匹配；低分辨率整数px碰撞仍按精确DIP或Preset身份选中；sub1极小场景没有绝对容差；非法Source/Preset/DIP/index不选中。
- 候选取消/旧revision提交不能高亮未提交档；换目标后Explicit真实DIP不变，仅按新场景数值重判断。

## 验证与限制

已执行git diff --check（仅两个源码）退出0；对照HEAD bytes确认原UTF-8 BOM与CRLF保留，无bare LF。完整Solution和所有实际运行测试由主会话统一承担，本代理不将未运行回归宣称PASS。CPU生产probe不驱动真实触摸HWND消息循环，实机按钮/触摸体验待人工；严格数值匹配不会把只有显示整数相同的连续Slider值吸附到Preset。

回退仅本轮Layout判断和Interaction测试增量，保留已提交DIP/默认/触摸预览逻辑及其它任务工作区。独立check由主会话安排，任务保持in_progress。
