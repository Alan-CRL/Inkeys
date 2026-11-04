# 光标续作最终验证（2026-11-04）

## 结果

- 完整 `InkeysRepo.sln Debug|ARM64`，vswhere 动态定位 ARM64 原生 MSBuild；同 PowerShell invocation 规范化 PATH，10 分钟构建上限；最终退出 0、0 error。
- i18n sync/check 退出 0，20 locale 均齐全、730/730；只增加精细控制及模式的 7 个文案键，生成头和基准快照同步。
- 最终 `InkeysHeadlessTests.exe --no-window` 完整回归退出 0，`PASS animation correctness`；配置、Bridge、输入、设置、旧光标和本轮矩阵均在同一最终源码版本下验证。
- 最终设置专项 `INKEYS_SETTING_CURSOR_ONLY=1 ... --no-window --setting-home-only` 退出 0；真实 Combo 键盘选择 4 态，各一次事件，无关字段不改。20语言 × 1/1.5/1.875 DPI × 340/780 DIP，蓝色信息条位于精细控制上方，窄布局不产生横向范围。
- `Inkeys.exe --draw3-pen-width-test` 退出 0；未附着 Window 的真实样式邮箱连续 DPI 更新，经生产 Controller/helper/resolver 验证一次换算与热点。
- `Inkeys.exe --draw3-renderer-upload-test` 退出 0、`PASS failures=0 no_hwnd=1 no_swapchain=1`。正式PS与完整guarded PS一致：96 DPI可见13×20；144为19×30；192严格25×39/469像素；264为34×54。尖端与输入热点保持。
- 37 个修改跟踪文件与编辑前文件系统字节备份比较，UTF-8/BOM/CRLF/LF类型保持；`git diff --check` 通过。源身份及最终二进制SHA见 `Build/cursor-continuation/source-state.json`、`validation.json`。

## 自绘箭头结论

本机系统192 DPI，当前实际系统箭头handle与LoadCursor相同，可见25×39/493像素，64×64为透明图像画布。旧自绘真实GPU为23×38/429像素，已有DPI链并未漏乘。默认盒校准为13×19.5 DIP后，真实GPU的192 DPI bbox已与原生25×39一致；保留12×19归一参考轮廓、尖端热点和现有DPI换算，不修改shader/笔宽或增加用户可调倍率。

已有Cursor诊断门禁和100ms分支新增短frame-scale记录，供实机仍异常时核对当前Context/Display DPI和viewport；关闭时无新增采样。该记录不改变全局320字节buffer，也不冒充桌面视觉验收。

## 验证环境与失败对照

1. 沙箱首轮构建在多个未改文件1:1报C1902 PDB manager mismatch。同工具链、solution、配置在沙箱外越过该阶段，未改SDK/项目/调试格式。
2. 两次有源码增量的链接触发既有LNK1000/IncrBuildImage。每次先保存日志，将工作区内生成的Inkeys.ilk移至忽略的验证目录留存，再同命令重新生成，均通过；未改源码或禁用增量链接来绕环境。
3. 沙箱完整回归从StartupPreview/ConfigMetadata既有首个原子写入起批量失败（早于本轮用例）。确认精确路径、--no-window参数和本轮启动时间后只停止该测试进程，保留日志；同一最终二进制在沙箱外完整回归退出0，证实环境执行/写盘差异。未改原子写盘代码或测试预期。
4. 构建的既有第三方、旧Controller/Setting和旧测试数值/隐藏声明warning未在本轮清理；最终无error。没有签名或Git身份相关操作。

## 边界

没有启动主程序、设置或其它GUI，没有Computer Use；GPU为当前Windows上的离屏WARP，guarded PS通过不代表真实Win7已验收。真实屏幕笔、数位板、多指触摸以及同屏观感仍待用户实机确认。自定义Windows光标主题/辅助功能大小不在本轮匹配范围。

任务保持in_progress，等待用户实机验收。没有commit、push或归档；上一任务的提交授权未延续到本任务。
