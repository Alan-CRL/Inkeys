# 既有调查与根因候选

2026-10-13 用户澄清整组主栏/墨迹会消失，并确认设置已经显示只是被墨迹压住。

- IdtState.cpp:528-555 true-exit辅助输出未ready时明确 SetDrawpadSurfaceVisibility(Hidden)；:619过期退出场景也隐藏。
- IdtPlug-in.cpp:551-573 EndSession 提前Bar刷新，:817-821后续发布Desktop并条件Selection；SyncDraw3State只发布模式/窗口，不一定再请求Bar，内容hasContent不变时旧高亮残留。
- Window.cpp:1698-1752 owner attach只SetWindowLongPtr，解除owner会SetWindowPos(NOTOPMOST)；:1590设置Show仅显示和焦点调用，没有即时根刷新。
- SyncDraw3State:787-794 owner提交同步发生，250ms仅为失败重试，不应误称所有mode变化都等待250ms。
- TopWindow周期刷新可能掩盖缺失；Ppt BeginTopmostRefreshPublication仅进入放映置pending，false清pending。
- MagnifierHost隐藏只针对顶层owner根；Microsoft Window Features 明确 hiding owner不影响owned可见性，不是WS_CHILD父子裁剪。实际完整Z序未运行验证。

本轮执行用户明确要求的连贯显示覆盖旧true-exit双隐藏合同，保留输入接收/就绪guard。无需再做早期历史查询。
