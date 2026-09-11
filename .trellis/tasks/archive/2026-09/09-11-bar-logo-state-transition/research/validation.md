# 验证结果

## 原生构建与测试

- 当前工作树 theme、基点0606cbe0，未修改原 draw stash7872c2c0。
- 使用 ARM64 host MSBuild 完整构建 InkeysRepo.sln Debug|ARM64，单次超时1800秒，VcpkgManifestInstall=false复用已安装依赖。
- 产物位于 Build/LogoIndicatorFix/ARM64/Debug，日志 Build/LogoIndicatorFix/build.log；子进程环境键统一大小写，以避免此前 Path/PATH 重复故障。Roslyn命名管道按先前已证实的限制使用获准的沙箱外构建。
- 初次完整构建通过139.5秒。审查发现的无用局部引用删除后，最终完整构建通过22.9秒，0 errors，6条来自未修改hashlib++的编译警告。
- 全部 InkeysHeadlessTests.exe --no-window 通过，输出 PASS animation correctness，日志 Build/LogoIndicatorFix/tests.log。
- --bar-logo-profile-output 由实际生产 Resolve/ApplyAttributes 导出150组状态，文件 Build/BarLogoValidation/profile.json。

## 静态审查

- Trellis check确认单SVG/实例、绘制态门控、颜色来源/Geometry接管、动画反向/量化最后一帧dirty、普通SVG隔离、成功缓存快照及设备重建输入保留。
- 唯一局部清理项是移动颜色逻辑后遗留的未用frameDrawingState别名，已删除并重新完整构建。
- 12个跟踪文本修改通过原BOM/EOL检查，新原生文件保持CRLF；git diff --check通过。
- 构建生成的跟踪Inkeys/PptCOM.dll已恢复HEAD内容，不混入主题修正。

## 离屏回归

150组生产属性状态通过。36组Dark原始节点合成逐像素一致；旧双位图合成最大差2.239216/255、最大平均差0.029074/255。Light整笔真实色、断口、screen中性和非绘制默认态均通过，联系表已查看。详见regression.md及Build/BarLogoValidation报告。

## 限制

未启动产品GUI。实际鼠标/桌面覆盖、DPI切换、设备缓存失效/恢复路径本轮只有代码审查，未进行新的实机UI验收。无窗口算法与SVG离屏测试不等同完整D2D呈现验证。当前修正不提交commit。
