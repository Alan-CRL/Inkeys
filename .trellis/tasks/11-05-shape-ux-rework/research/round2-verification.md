# 2026-11-14 第二轮验证

## 实施与范围

延续父任务，基线3731e3ab；完成已批准15项范围的代码与测试更新，无commit/push、无GUI/Computer Use、无工程或依赖配置修改。用户实机观感仍需验收。

## 已通过

- inkStrokeModelerTest.sln Debug|ARM64：完整构建退出0（core-verified-build.log）。
- InkeysRepo.sln Debug|ARM64：完整构建退出0（product-verified-build.log）；UI回归修正后增量完整solution再次退出0（product-ui-build.log）。
- --shape-core-only：0失败。
- --draw3-shape-core-test、--draw3-shape-renderer-test（471项）、--draw3-whiteboard-storage-test、--shape-tool-state-test、--state-mode-production-test：退出0。
- --uink-file-only：全部持久化与page file测试通过；260构造对象、1209标准降级部件，退出0。
- InkeysHeadlessTests --no-window、--uink-presentation-only、--desktop-autosave-only：退出0。
- 二十语i18n检查808/808；git diff --check通过，50个修改文件BOM保持、checkout CRLF无混合换行。Git core.autocrlf=true，索引LF不代表工作区格式变化。

## UI复跑

两项生产离屏专项最终均退出0：编辑器544场景、4192次实际提交、236229项检查；形状面板13603次实际提交、1607246项检查、144动画帧，均0失败。最后完整solution构建记录为product-delivery-build.log，退出0。首轮失败日志已保存到Build/shape-round2/ui-editor-first-results.log和ui-geometry-first-results.log。

首轮失败已逐类定位：辅助点命中夹具仍点击原几何点、合成命中fixture未发布visualHandles；新增勾选与标签宽度引起屏幕钳制；弹层完整遮挡旋转柄时仍断言底层像素；取消空列后的宽度基准；自然尺寸面板超屏导致旧“整个面板必须在屏内”断言失效；新增正形按钮未进入预期可见性。更新后仍保留屏内裁剪、实际像素、完整布局和命中一致性检查，不用删除整类行为测试代替修复。

## 环境问题与运行方式

- 沙箱MSBuild子节点持续无法连接，核心构建5分钟后退出1；未出现源码编译诊断。核实后仅停止本次构建树，正常进程环境完成同一solution构建。
- 首次重新链接核心测试时测试EXE仍在运行，产生LNK1168；等待测试结束后构建通过。
- 主程序原输出路径LNK1104，未发现源码错误；通过命令行OutDir指定Build/shape-round2/product后构建成功，未改vcxproj、工具链或依赖。
- MSBuild同一PowerShell先清理Env:PATH并设置MSBUILDDISABLENODEREUSE=1，使用vswhere定位ARM64原生MSBuild。
- PowerShell直接调用GUI子系统EXE无法可靠等待/获取退出码；最终生产专项均由Python subprocess显式等待、重定向日志、CREATE_NO_WINDOW执行，每项超时15分钟。无窗口参数在主窗口初始化前分流。
- 既有第三方转换/编码warning和PATH规范化后的pwsh不可见提示不影响构建退出0，未为清warning修改无关代码。

日志与隔离产物均在忽略目录Build/shape-round2；通过验证的Inkeys.exe及PDB已复制回Build/ARM64/Debug，EXE的SHA256与测试产物一致。最终源码记录以本文件为准。

## 重复问题复盘（trellis-break-loop）

根因属于跨层合同和测试覆盖缺口：上一轮浮条只在换边当帧启动动画，下一帧直接定位；预览Candidate连续不等于接触切换时上屏连续；固定顶角身份与用户按几何最小调整的预期不一致。防线是逐帧动画/命中检查、真实Down首帧预览初始化、显式约束顶角与可保存构造参数。投影视线必须落在投影矩阵核空间；圆底细分段不能分别重置虚线相位。对应可执行合同已更新shapes.md、rendering-and-ui.md。

第二/三次回归进一步定位真实停靠问题：横向未先钳制导致错误进入动态选位；上侧浮条与框外旋转钮冲突。已修正并用全矩阵不遮挡断言验证。最后剩余断言分别是旋转局部边中点与屏幕AABB混用、以及小屏底行出屏仍采样淡出。焦点重合时优先命中Focus，确保隐藏辅助点后仍可拉开焦距。
