# 逐点工具 验证与交付

折线与任意多边形已接入共享ConstructionSession及生产输入：每次真实Up确认一顶点，预测只改变当前点；完成前不落文档，Complete仅追加一个逻辑对象/创建历史。多边形至少三点并通过简单多边形校验，闭合不重复首点。Back/Undo回退步骤，Cancel/工具/页面/Clear取消。

步骤提示复用P3的Bar场景、标准按钮与独立list，不新增HWND。与P3编辑、UInk v3和白板cold v3联调完成。详细命令、日志、独立审查与人工边界见 `../10-17-shape-phase3-selection/validation.md`。

本轮无自动commit/push；工作记录保留，等待用户代码审阅和后续提交。
