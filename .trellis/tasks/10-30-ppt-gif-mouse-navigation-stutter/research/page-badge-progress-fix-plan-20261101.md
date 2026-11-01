# 恢复进度显示分类与页码角标修复

用户明确批准：准备/恢复进行中统一三点，真实失败/冲突文字和重试保留；三点移到页码区域右上角，前后翻页共用一处，不放箭头上方。

最小边界：保留PptRecovery.Kind::Recovering及其所有控制含义，PptInfo target0/Pending、Ppt::PagePresented拒绝、Host恢复门和原Retry资格均不动；只在Ppt Snapshot/IsPptPagePreparing与BuildPageContent把Recovering进度映射Numeric+准备角标，保留中心按钮原恢复进行中不可交互规则。失败/Conflict/ReadFailed/PresentFailed/Failed继续Status和原Retry。不改变普通Pending的其它操作资格、白板switching文字、COM/ABI或动画通用策略。

Bar.Scene保留方向等待输入，但将任一方向waiting合并为页码按钮(id3)右上角单个三点；两箭头无点，dirty覆盖页码角标新增/删除；位置按现按钮局部bounds与scale，不新增绝对屏幕坐标，不遮当前/总数字。回归点数改为一个badge的3点/1branch。

预期文件：UI/Ppt/Ppt.cpp（同Snapshot准备投影），PageControl.cppm/cpp（视觉分类与现有headless/离屏fixture），Bar.Scene.cpp（角标位置/dirty），必要headless page_control_tests.cpp/ppt_session_tests.cpp。不改IdtPlug-in、Draw3、Ppt.Recovery控制映射、PptCOM、项目配置。保留已有未提交诊断改动，原UTF8/BOM/CRLF和中文关键注释。

测试：原实际Recovering0ms/20ms样例应改为正常Recovering始终Numeric即时且没有内容缩淡、总数不变；真正Failed/ReadFailed状态仍保留原中点动画与重试。需明确替换早前“观察旧bug”期望，不能为了通过继续强制Recovering成Status。Page角标对左右方向同位置，箭头像素不变，数字子区不受角标影响；generic准备/Recovering均3点1branch，partial=full、清除恢复原像素；恢复过程中旧ACK依旧拒绝、target0与modePending不变，首末页/noop/白板/隐藏/换场保持。

独立check→完整InkeysRepo.sln Debug|ARM64/ARM64原生MSBuild/Path规范化/900秒→page-control-only/ppt-session-only/真实离屏回归→内嵌DLL与不变managed哈希/PrepareOnly配置SHA。人工短测交用户，不GUI/自动commit。

## 控制数字与显示数字的额外边界

IdtPlug-in的Recovery覆盖会把currentPage设置为待准备目标数字，之前Status文字遮住；改Numeric不能直接暴露它。保留PptState.currentPage/totalPage的业务/ACK/EndShow身份，增加最小纯视觉override（默认无override），同session缓存仅由InkReady/BasicControl真实发布更新，换场/退出清空。任何Pending投影稳定数字，无cache明确-/-；恢复InkReady/BasicControl同帧撤销override，避免PublishCanvasRecovery(None)先到暴露未来数字。字段参与等价比较，实际文字与content-policy数字使用一致投影；原控制门完全不变。必要可在既有Ppt.cppm放窄纯值缓存helper供生产与headless共用，不新增GUI测试接口。
