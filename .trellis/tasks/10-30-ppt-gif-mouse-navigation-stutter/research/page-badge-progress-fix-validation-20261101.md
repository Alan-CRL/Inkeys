# 恢复进度页码角标修复验证（2026-11-01）

## 行为与安全边界

用户批准准备/恢复进行中统一三点，三点移到页码区域右上角、前后翻页共用，不放箭头。已实现：Recovering保持原非None控制态及中心不可交互，仅视觉Numeric+Preparing；真正错误/冲突和原Retry继续Status。Bar.Scene聚合方向等待到Page id3单个角标，top+7、right-20/-15/-10、radius1.6 DIP按原scale绘制，dirty覆盖Page，Arrow无点。

发现并处理了目标数字提前暴露边界：原Recovery控制currentPage可已为4而未就绪，不能用它直接显示。Ppt StablePageNumbers只按真实PublishPageState的合法InkReady/BasicControl发布更新，同session缓存、初始化/换场/退场清空；PptState新增numericPageOverride/displayedCurrent/Total纯视觉字段。Pending或Recovering保持稳定数字，无cache明确-/-；Recovering先到旧InkReady、None先到仍Pending均不暴露未来页码。原current/total/target/pageAckEpoch/EndShow/ACK及Host、PptInfo恢复门逐句保持，不动COM。

## 验证

- 独立trellis-check无阻断，无需自修；六源码Ppt.cpp(m)/PageControl.cpp(m)/Bar.Scene.cpp/page_control_tests.cpp UTF8无BOM/CRLF、diff通过，保留此前未提交采样改动。
- 完整InkeysRepo.sln Debug|ARM64，动态定位VS ARM64原生MSBuild，同invocation规范化Path/PATH、禁node复用、900秒预算；exit0，49.69秒，0error/88既有类型转换warning（接口变更引起其它模块重编），没有升级工具链/改工程或无关源码。
- --page-control-only exit0：显示/control隔离、缓存重置/EndScreen/BasicControl、旧ACK及原等待水位。
- --ppt-session-only exit0：既有数字采样/默认off/并发/错误码回归。
- --bar-eraser-offscreen-test exit0、PageControlUI3 failures0：四面三倍率24组左右方向同角标、箭头像素不变、数字本体不遮、9999/999角标包络不碰槽、partial=full/清除。control4+target0准备时保持display3，None先到保持旧数字，Ready4同帧去点；Basic target0结束。Recovering0/20ms旧观察断言更新为Numeric即时/无动画，ReadFailed/Conflict仍保留真正Status动画与Retry；采样启停BGRA一致。
- PptCOM源3文件/DLL/TLB与原baseline逐SHA一致；内嵌DLL1061DDF51BD7CF646030AC74830DCD67CE74508448D879D70BBEE3EAA4D850E3，TLB C652B5DD...未变。
- EXE SHA256 C9C824228C432842643EDB178369CACE6EB36AC12261863C20C8DA63787A4183；Headless76337D3C3FBF23015D0930F6FA06A50273F6C24A4D8BB8F83ABE7A956B8BCF40。
- PrepareOnly exit0，170656-901-8bdd322a仅预检样本；实际main/deploy配置SHA不变，无GUI。日志Build/Validation/page-badge-progress-fix-20261101。

## 后续人工确认

代码与软件验证通过，真实桌面尚待用户短测。原一键入口启动，普通3→4及返回一次，观察数字不再缩淡、三点在页码右上角同一位置且箭头无点，准备期旧数字保持、新数字就绪后角标清除；正常退出采集即可，无大矩阵。等待实际耗时未作性能修复，旧退出/未验Office组合等边界保留。本轮未提交/未归档，AI未操作桌面或启动GUI。
