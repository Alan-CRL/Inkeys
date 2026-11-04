# 人工反馈调参验证（2026-11-01）

用户要求半径约1.5倍、两层线条更细、500ms后出现。已集中PageLoadingStyle：中心线半径5.4DIP、arc1.2DIP→可见外半径6（旧4，1.5倍），track0.6DIP（旧1），arc旧2；门槛500ms（旧1000）。250度弧、一圈约1s、180/140ms显隐、.75→1缩放和原角标right15/top6.5位置均未改。路径端点/绘制/全旋转AA dirty与旧尾帧共用同一参数源。

只改参数及匹配测试，不改业务/ACK/COM/全局配置、deadline协议或worker算法。四文件：helper、Scene、PageControl及headless page_control_tests。独立check无生产问题，补glyph测试非空/合法尺寸/真实inkPixels>0守卫，防无字形的误通过。

验证：
- 完整InkeysRepo.sln Debug|ARM64、动态VS ARM64原生MSBuild、同invocation Path规范化/禁node复用，900秒预算。首轮exit0/36.61s/6既有hashlib warning；补测试守卫后同solution exit0/30.98s/3既有hashlib warning，均0error。
- --page-control-only exit0，499/500、新段/取消/session/off与20ms旋转7.2度（周期未变）回归。
- 首轮及补守卫后--bar-eraser-offscreen-test均exit0，PageControlUI3 failures0；actual callback门槛、partial=full/旋转/尾帧/静态模式，最大9999/999由数字图减空文本图提取实际非零glyph mask，环图在全部字形像素逐字节不变。因此保留原锚点，无需移动数字/环。细轨道正确允许抗锯齿混色，不为exact不透明灰像素强行调粗。
- 既有worker/协议实现未变，不重复大矩阵；PPT managed源/DLL/TLB与baseline SHA一致，内嵌DLL仍1061DDF5...，没有COM获取/释放变化。
- 最终EXE SHA256 9E6B4B294E11758329A07A886BCDA134F4C042A88397E46EDD17AEC8D9C3B6C1。日志Build/Validation/page-loading-ring-tuning-20261101。
- PrepareOnly exit0，193559-796-49426716仅预检，实际main/deploy配置SHA不变，无GUI。
- UTF8无BOM/CRLF和git diff --check通过。无GUI/Office/ComputerUse、无commit/push/归档。

软件验证通过，新尺寸/线条观感仍交用户短测；不声称PPT实际延迟已优化。原1秒版本验证属于历史，新版显示门槛为500ms，旋转周期仍约一秒。
