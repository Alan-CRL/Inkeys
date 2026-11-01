# 页码动画数值取证准备验证（2026-11-01）

## 已证与未证

最小离屏复现结果见page-number-animation-repro-results-20261101.md：0ms短Status实际像素仍完全等于旧数字，返回3或4/56后主、次数字均可出现动画；普通Numeric控制组即时。它证明代码机制，不证明现场经过Status。用户偏好三点，保持原策略和提示，不做修复。

## 取证与独立审查

仅4源码：PageControl.cpp（既有复现fixture与PPT配置context）、Bar.Scene.cpp（原真实策略分支观察）、PptWaitingDiagnostics.h、ppt_session_tests.cpp。独立check确认原setter次数/顺序/决策保持，无wstring额外getter/COM/渲染I/O或分配等待。ContentScope独立TLS，同快照PPT session/layout/publication/model/surface/recovery/currentPage/totalPage，强制epoch/direction/attempt0；白板和null屏蔽outer，原Draw/Nav TLS不变。

新增stage=content-policy；content_policy 1 Initial/2 Immediate/3 Animated（0未知）；old_kind/new_kind 0 Numeric/1 Status/-1未知。content_target_changes为逻辑目标字符串变化bit1/2，不等于跨Status数字变化；setter_changes为原setter返回bit1/2，Animated选择与真正Start/幂等分开。公开scale/pct千分数前后值非法=-1，不能猜精确timeline/midpoint；AnimationEnabled=2048记录开关。日志不含文字/路径，仍默认关闭、16384固定槽、真实join后输出。仅出口buffer2048，最大字段测试防截断。具体合同已同步ui3-render-diagnostics.md。

## 验证结果

- 最终完整InkeysRepo.sln Debug|ARM64，ARM64原生MSBuild动态定位，Path规范化/保留原搜索值、禁node复用、900秒预算，exit0/26.21秒/0error，10个既有hashlib/布局类型转换warning，未改工具链或配置。
- --ppt-session-only exit0：defaultoff/null/nested/恢复、未知数值、epoch0与最大格式等回归。
- --bar-eraser-offscreen-test exit0，PageControlUI3 failures0。四组合用明确local Recorder+ContentScope进入真实跨module Scene hook，确认实际Numeric→Status→Numeric的Animated/原setter_changes=3及metadata；同轨迹无scope Scene逐帧BGRA相等，同值heartbeat不重复产生policy。避免早期CLI未Prepare造成“开关两次都off”。此前三点/数字/状态既有回归也通过。
- EXE SHA256 1EB8AEBA88055BE9F5F3EFC3E5BA99C07B7B200AF04CAEA3870C1BE41A5E0750；Headless C59D2E9B264AA2C6D93FEAA0EA1871CFE4C6F4D3BC23D40E32096EC68A7B1300。
- PptCOM源三文件/DLL/TLB与既有baseline SHA完全一致，内嵌DLL仍1061DDF51BD7CF646030AC74830DCD67CE74508448D879D70BBEE3EAA4D850E3；TLB仍C652B5DD...，本轮没有COM获取/释放变化。
- PrepareOnly exit0，162721-735-2075a9f2仅prepared-only，不是人工样本；实际main.json/deploy.json SHA不变，无GUI。
- 四源码UTF8无BOM/CRLF和git diff --check通过，原日志Build/Validation/page-number-animation-20261101；未启动应用主窗口/Office或操控桌面，未提交。

## 人工取证

原一键入口启动后按平常方式翻页，遇到一次数字缩小/淡出即可记下大致当前页、方向，正常退出Inkeys收集。无需大矩阵；若没有出现，明确未复现，不把正常数据说成解决。根据实际kind/policy/返回位判断是否短Status；记录不代表光学显示/精确中点，现场结论仍需用户反馈和关联。此版只取证，动画策略、三点、COM与ready均未改变。
