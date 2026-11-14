# Trellis 检查记录

日期：2026-11-06。检查范围：本任务全部生产源码、测试、接口和合同变更；不包含历史demo或第三方代码。

## 已修复

1. `Draw3.RealtimeStylus.cpp`：启动预检HDC成功之后，Enabled回调的第二次HDC查询可能失败；原实现忽略StageAndPublishDecoders失败且仍将输入标记initialized。现以独立的原子startupMappingResult记录Enabled中的窗口映射查询/过期结果，InitializeAttempt在put_Enabled成功后读取；公共失败归WindowMapping并沿既有安全停产后Win32Mouse回退，不重复BasicXY。单context元数据失败仍可在慢路径补建，不误变成公共映射失败。
2. 同文件生产CPUprobe：原“构建期间失效”断言只检查WindowMappingCurrent谓词，即使漏掉真实发布前检查也可通过。现通过窄WindowMappingApi注入，在真实Enabled→StageAndPublishDecoders的HDC查询中推进revision，断言E_ABORT及共享比例未发布；另覆盖预检后第二次查询失败和context元数据错误的独立语义。

3. `Draw3.RealtimeStylus.cpp:2692`：实际编译发现Windows SDK `minwindef.h`将`near`定义为空宏，使probe局部lambda声明变成无变量的`const auto = ...`。将该局部helper及全部调用定向改为`probeNear`，不改变系统宏或全局编译环境。

## 其他检查结论

- 接触长度按属性单位/分辨率换成digitizer轴长度，再组合abs(S/C)；新增InvalidDigitizerScale附加在枚举尾，保持已有状态值，映射到InvalidTransform并输出准确诊断名。
- 手掌只修改定尺为sqrt(W×H)，识别area仍为W×H；阈值、观察窗、最大面积、Up锁定、续接与设置预览代码未改。现有测试加了真实inch/pixel、设备计数重编码、各向异性、有限值/范围、阈值及光标/新旧半径检查。
- 全部生产ResolveContactLengthTransform调用者均已更新；隐藏测试仅签名补参，不执行窗口测试。
- Host的窗口通知不调用COM、不等待writer；绘制owner通过strong ref、callback lease和state gate取消，Shutdown关门并排空后才释放借用对象。
- WM_DISPLAYCHANGE保守失效合理，因为相同消息值也可能伴随方向/拓扑变化；重复相同DPI及同monitor窗口位置消息合并。主会话已同步明确文档例外。
- Win10 DPI函数均GetProcAddress动态取得；新低版本API未新增静态高版本依赖。HDC在同一线程局部配平释放。
- 源码UTF-8/BOM状态及worktree CRLF保持。Git对象规范化为LF（text:auto/core.autocrlf=true），不能把对象原始换行直接当作worktree编码回归。

## 验证

- git diff --ignore-submodules=all --check：通过。
- 编译/类型检查：主会话集中执行的完整InkeysRepo.sln Debug|ARM64已通过（exit0）。期间修复上列near宏冲突；工具链增量链接器LNK1000由主会话仅删除生成Inkeys.ilk后重建恢复，未更改源码/项目来绕过环境故障。
- 无窗口定向回归：Headless Palm 152项/0失败；生产RTS 59项/0失败；生产Palm probe通过。RTS失败夹具中的E_FAIL日志是预期注入，不是失败用例。
- 完整Headless --no-window：主会话在沙箱外使用同一SHA256产物完成，exit0，PASS animation correctness，stderr为空；沙箱内配置写盘失败和300秒截止的前一次记录单列于validation.md。
- ARM64导入检查：主会话确认两个可选DPI API未静态导入。本检查代理没有重复构建或运行GUI。
- Windows 7 x86/x64、Surface、异常设备、多屏及真实驱动行为未实测，不以静态检查替代。

## 未修复问题

本轮未发现仍需修复的确定性源码问题。最终编译及probe执行结果由validation.md记录；如测试揭示新问题，再复核。
