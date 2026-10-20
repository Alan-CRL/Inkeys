# 吸附体验修订：20DIP双门槛和UI3过渡

2026-10-18用户在首轮1–3修复验证完成后追加并授权实施，属于同一in_progress任务。当前合同以design.md第4节及本次PRD AC9/11/15/16/17为准；初版40DIP和所有拖动位置一律立即落定的旧描述仅为历史证据，不能覆盖本次用户要求。

## 行为

- 先合入当前发布/拟合倍率再Clamp；middlePairHeight的绝对值与middlePairWidth均不大于BarBottomDockThresholdDip*归一化布局屏幕DPI，才尝试将Y偏移归零。阈值20屏幕DIP含等号，不乘控件倍率。
- middlePairWidth已是主体距对应侧边扣去既有PageControlGapDip*实际controlScale间隙的额外距离，两面共用对称偏移；不能把阴影/outset算进门槛，也不强制X=0。
- 中心碰撞时保留可行自由候选，仍不可行则最后feasible；左右pair和当前另一组位置、raw/fit身份及session/epoch门禁维持。
- 仅吸附Y修正加120–160ms左右的UI3过渡，复用既有曲线/呈现路径；普通自由拖动和非吸附fallback不恢复0.4s位置余移。两面同步且进度单调，owner采样后较早的render batch时间不得回拉；X及时跟随，重复Move/页码发布/Up不反复重启；结束后稳定。
- 吸附中不新增inputLocked，仍允许移动/脱离/Cancel/新手势打断；released pending必须到两面实际几何与呈现/窗口成功才commit，Render Retry/try_lock兜底有界继续。

## 责任与验证

原生产代理只写PageControl.cpp/.cppm、真实内部offscreen用例和自己的研究记录；原测试代理只写page_control_tests.cpp和自身研究记录。主会话只更新任务/spec并统一构建；独立check在源码冻结前审查。所有代理不独占工作区，不覆盖他人文件。

纯策略测试覆盖20双条件二维边界、正负Y、左右、DPI/倍率/奇偶/负原点、既有间隙与碰撞，直接调用生产策略。真实生产离屏检查动画中间帧、双面同步、重复输入不重启、Up继续完成、脱离/取消/新手势及最终多帧稳定。既有headless不编PageControl.cpp，不能用纯bool或tracker字段代替实际AnimatedBounds/Scene证据。

完整产品仍用InkeysRepo.sln Debug|ARM64、vswhere发现ARM64 MSBuild、同invocation PATH规范化，900秒监督；现有--no-window和--bar-eraser-offscreen-test分列运行。精确offscreen入口不创建HWND/不启动Office。新日志使用snap-*名称，首轮结果保留。真实USER32/设备/Office仍NOT VERIFIED；不commit/push/归档。
