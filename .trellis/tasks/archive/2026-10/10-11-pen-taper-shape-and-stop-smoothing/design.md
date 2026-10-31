# Technical Design

## Boundaries
保持现有firstTipIndex选择：从可见几何末端时间回看liveTipDuration，包含既有预测参考。L0开头与taper起点不同。稳定L1提交游标与保护窗口不动，普通Pen Fixed/SimulatedPressure沿现有taper入口应用，HardwarePressure及hardPen等仍禁用。

## Shape
S(x)=u*u*(3-2*u), u=clamp(x,0,1)。原候选笔锋弧长为Lsource，参考直径D取该范围内未收锋半径最大值的2倍。有效L的tipScale=1-0.7*S(L/(2D))。有效尾段内按弧长SmoothStep由100%过渡至tipScale；之前恢复原始笔身半径。每帧从real/prediction复制后的原始半径计算，保留EnforceCapsuleTangency。
均匀笔宽时最大额外半径斜率约0.2953125，比现有0.95低；原基础半径亦受空间约束。

## Dwell / Resume
沿用configured T，不新增时长设置。停笔年龄来自显示时钟和lastMovementInputTime，运动预测仍沿用其未来时间参考；stationary/model/terminal合成输出不得更新真实运动年龄。候选L=Lsource*(1-S(a/T))。停笔时以上一帧有效L封顶，避免追加追赶点重新拉长。增加最少的per-stroke临时状态，Reset、新真实位置变化及成功reconnect明确重置封顶（相同QPC的真实Move也应解除），完成态可读取该状态但不能依赖上一帧已收锋半径或修改const stroke。
收缩起点落在线段内部时，在派生L0/完成几何中增加插值点，以保持粗笔身与重新压缩的曲线连接；不改中心线路径、realPoints、committedIndex或L1共享首点。

## Up / Storage
RebuildL0DrawPoints和BuildCompletedPenTail共用同一形状求解。ResolvePenDisplayTime仍锁物理Up；terminal追加结果不能重启笔锋年龄。快速Up允许端点安全接纳后的最终真实尾段不同于prediction；同位停稳Up不重新变细。FinalizeStoredStroke现有稳定前缀+完成尾段连接方式不变，插值点可持久为最终几何但无格式变更。

## Test Interface
新增 --draw3-pen-taper-test 早退出生产诊断入口。直接执行真实几何与StrokeModeler、帧时钟和完成/Stored路径，不复制收锋算法作唯一证明。不创建HWND，不初始化Host/RTS，不写用户配置。可采用现有RunPenWidthProductionProbe附近的production probe模式；不新增构建系统、不调整demo。

## Compatibility
保留Win7/FL11/CPU-GPU布局与已有公切线及脏区合同。只增加临时CPU状态和诊断入口，无配置/UInk迁移。回滚可恢复本任务源码diff，不接触其它任务或第三方文件。
