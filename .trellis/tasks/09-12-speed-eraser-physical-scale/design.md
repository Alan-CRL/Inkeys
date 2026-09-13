# DIP尺寸、动作资格、工具与历史的分层

## 尺寸
EraserSizes是唯一属性源，值16/32/160/Touch16DIP，固定50DIP。固定50是原50px在96DPI下的明确基准标定，不迁移或重解释未被Draw3使用的旧eraserSize字段。DiameterToCanvasPx/FixedDiameterPx只用DIP/px；物理信息不能改写属性。Controller内部对数尺寸为DIP，DiameterDip是业务结果，Diameter是兼容像素几何出口。

## 动作
现有可靠直接Touch用cm/s，鼠标/未知Pen用DIP/s，未知映射回退。进入/退出/大目标速度分开，笔电800/600/1900，大屏DIP650/450/1700，cm25/18/70。始终泄漏的证据不会被普通速度长时间充满；不自动学习、不用加速度尖峰绕过资格。

## 静止与几何
最后有效移动按动作噪声阈值确认，与包时间分开。280ms后独立静止释放，不再叠加650/680ms清扫门。当前工具尺寸由ContactSizeState推进，最终光标和下一段均消费它；历史点保持。
ContactSizeState记录待用尺寸断点。恢复实际模型输出时AppendEraserSizeAnchor追加与旧末点同位的小半径点；之后的新移动段从此半径开始。既有圆胶囊包含关系处理零长度变径，UInk逐点宽度继续保存，无格式迁移。
相同位置包/小噪声不续期，迟到原始输入不因帧时钟领先而丢弃。真正重连仍冻结、移时、重锚。收敛后只等待现有wake generation，不重新请求空帧；不改变MPMC与输入采集。

## 产品观测与验证
HostStartOptions.enableEraserDiagnostics默认关闭；启用时HostRuntimeSnapshot.eraser提供有界实际状态与三个断点坐标/宽度。测试门可注入Mouse/Touch类型；隐藏Mouse样本复用正常光标发布，不安装真实离窗跟踪，避免屏幕外测试窗口自动清掉样本。
--draw3-eraser-hidden-test使用干净Host，并在DComp和ULW验证实际接触光标/停帧/足迹、Undo/Redo及生产UInk断点文件往返。旧--draw3-hidden-test保留原场景，失败不隐藏。所有工作主Agent执行，结果见validation-dip-idle.md。
