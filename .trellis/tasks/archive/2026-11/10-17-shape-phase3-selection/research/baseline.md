# 只读调查基线

2026-10-17，用户批准前已调查，基线d0a1dc73。
- WindowRole未有Selection；IsUiPopup当前仅Bar/PPT，Show/Promote/RefreshTopmost与最小化/反向销毁必须接入。PageControl的成功快照/captureTicket可复用。
- 居中提示是Bar.RenderLoop bottomDockIndicator，不是独立HWND；独立构造提示复用外观/锚点而非覆盖它的业务状态。
- shape_core只有纯ShapeCommand/CreationTransaction；InkCanvas只Append。Controller undoLastVisible逻辑无法撤销编辑，必须统一operation journal。已有UpdateItemGeometry与CompositionRangeTree旧新tile失效可用。
- GeometryCache/Renderer cache键id/revision/scale，逆命令恢复旧revision会撞旧分支缓存。
- legacy Validate只允许identity/旧样式；首次编辑须Promote并物化4DIP圆角，保ID。
- Controller保存三个workspace已经携带Shape；缺编辑mutation通知和cold journal。白板旧history只strokes+visibility，v3需journal与redo/Clear。
- Construction原仅tool/start/end，UInk v2 map严格；21类需typed参数v3，旧v1/v2继续读。
- 画布RTS唯一，UI overlay不能绑定第二个RTS；既有selectionMode是电脑交互/穿透，不是本轮对象编辑态。
