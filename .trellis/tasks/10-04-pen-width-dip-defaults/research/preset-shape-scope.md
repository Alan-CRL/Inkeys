# 本轮追加：预设数值高亮与形状粗细一致性

Active task: .trellis/tasks/10-04-pen-width-dip-defaults；实际基线 f8054a227174707a984291d875b4fcc6fadf605f，chore/publish。原41项工作区改动见preset-shape-baseline.json，均不可覆盖。本轮用户授权实现，未授权commit或任务结束。

## 需求与边界

- 手动Slider/触摸/FineDial真实提交后，ExplicitDip若与当前场景某预设的精确resolved DIP相同，那个快捷按钮高亮。Preset仍优先依其身份，防止输出量化碰撞造成多个档位选中。
- 高亮只是派生UI表现，ExplicitDip仍保存其来源；不得为了高亮反写Preset、吸附/四舍五入实际宽度或改变换屏行为。
- 数值比较使用当前工具槽Selection和同代目标Context，经现有策略得到DIP；仅允许浮点运算误差级相对容差，不能比较整数文案或量化px。大屏分数DIP要匹配，显示相同整数但实际不同的值不能误亮。
- 形状细/中/粗按钮的点击、读数和高亮全部复用Brush预设策略：普通1/3/6 DIP，大屏参考2/4/6px解析DIP。保留StraightLine/Rectangle独立槽，不能新增画笔宽度记忆跟随关系。
- 保留松手事务、cancel/hold/revision、已有动画/立即候选预览、Laser/Highlighter各自预设；不改显示/引擎/迁移/启动/历史/橡皮。

## 设计/验收

主缺口在Bar选中函数拒绝所有ExplicitDip以及形状已有共享策略，无需修改其布局/点击/读数。扩展现有统一选中函数，查询来自快照的有效Selection，Preset直接比身份；ExplicitDip逐档比较未经量化的Brush/Highlighter/Laser策略resolved DIP。非法来源/值/档位不亮。只为数值计算误差保留小容差并明确边界，低分辨率不同DIP即使同输出整数仍只一个选中。

单个trellis-implement仅独占Bar.Layout.cppm及Bar.Interaction.cpp内已有生产测试；若需新增policy帮助函数，先告知主会话。研究只能写自己的research文件。主会话文档/spec/构建与集成；独立trellis-check完整范围审查，不递归派发，不抢构建、不归档任务。

实现前读取preset-shape-research.md，完成设计自检。测试真实生产Bar函数、提交setter/session和形状显示seam，不复制策略公式。覆盖手调非预设→匹配档、三档Brush/Highlighter/Laser、1440p275%分数DIP、近邻/同文字/同量化px不匹配、Preset身份、取消/过期、不改变ExplicitDip来源/换屏值、直线矩形独立槽与Brush同策略。主会话动态vswhere ARM64 MSBuild完整InkeysRepo.sln Debug|ARM64 + PATH规范化 + 本机已知增量linker一次性LinkIncremental=false，900s；运行--pen-width-production-test、--state-mode-production-test、--draw3-pen-width-test、headless --no-window，必要离屏Bar/i18n。GUI手感仍人工，回退仅本轮增量，不reset既有修改。

研究自检结论：Geometry初始化读数为空，RenderLoop实时通过GetBarThicknessPresetMetrics(SoftPen,index,snapshot)显示，点击走统一SetPenWidthPresetIfRevision，形状实际名义宽度本来就是Brush三档。无需改正确线路，用回归覆盖。最终容差为8*double epsilon*max(abs(explicitDip),abs(presetDip))，不加绝对下限；只能忽略double计算噪声，不能吸附或将float误差当用户相等。查询无状态/配置写入。


独立审查边界决策：异常输出尺寸使策略多个档位退回同一安全DIP时，ExplicitDip匹配按Thin→Medium→Thick选首个匹配，仅一个高亮；Preset仍按有效身份。仅UI查询确定优先级，不更改策略、来源或绘制宽度，补真实fallback fixture。
