# 光标类型规则修订：本机验证（2026-10-19）

当前权威需求为 [cursor-policy-revision.md](cursor-policy-revision.md)。本报告对应鼠标/笔两类型选择及固定设备边缘策略；首次三场景可选高亮的 verification.md 仅保留历史。产品源码已冻结，Win7 真机视觉验收仍未执行，任务保持 in_progress。

## 最终行为

- 绘制设置只保留上“鼠标”、下“笔”两个下拉，选项都是“默认光标”“画笔光标”。MouseStyle 默认 0（系统），PenStyle 默认 0（原圆形），两项独立保存、严格恢复合法整数，均 NoUpload。
- 鼠标类型只作用于软笔/硬笔；鼠标荧光笔始终矩形粗细预览，所有鼠标 Shape 仍系统光标。笔类型只改变软/硬笔，荧光笔矩形及 Shape 原圆形保持。
- 鼠标画笔/荧光笔边缘固定高亮；屏幕笔固定不高亮，数位板和无法识别/冲突的笔固定高亮，不提供开关。三个退役高亮键保存时清理，AutoClean=false 的其它未知键保留。
- Host 传原始设置，Controller 在活动 runtime 确定实际工具后临时投影，Window 与 resolver 使用同一有效设置；按住时选择其它工具不会覆盖当前笔画工具或丢失原始偏好，释放后重新解析。
- Touch 系统光标继续固定隐藏，穿透、命中保护、橡皮/Laser 专用策略和 legacy 显式 opt-in 保持。本次规则修订没有再次改 Window、HLSL、Win7 guarded 字节、墨迹或历史。

## 验证结果

| 检查 | 本轮实际结果 |
| --- | --- |
| 独立 Trellis check | 配置/UI/三语及 core 全范围静态审查完成。发现 1 项按新选择提前投影、丢失活动工具策略的问题，owner 窄修后复查关闭；见 check-cursor-revision.md。 |
| 完整 Solution | 动态定位 VS ARM64 原生 MSBuild，同一 PowerShell invocation 规范化 PATH、禁 node reuse；InkeysRepo.sln Debug\|ARM64，/m:1 /nr:false，600 秒上限。首次编译完成但增量链接器 LNK1000 崩溃；保留 .ilk 后相同配置完整链接，最终退出 0、error 行 0、stderr 0 bytes，产品/PptCOM/PptCOM.Tests/headless 均生成。 |
| Headless | 新生成 InkeysHeadlessTests.exe --no-window，300 秒上限，退出 0；Config metadata tests: 0 failures，最终 PASS animation correctness，stderr 0 bytes。真实配置的四组合/非法恢复/旧键清理/冻结副本、Bridge 两类型与原始快照、生产 resolver 工具/来源/热点/DPI/固定边缘等回归都在本集合内执行。 |
| 生产状态 CLI | 新生成 Inkeys.exe --state-mode-production-test，120 秒上限，退出 0；两条 StateModeProduction PASS。入口在配置/单实例/窗口初始化前返回，不创建 HWND。 |
| 生产光标工具切换 CLI | 新生成 Inkeys.exe --draw3-pen-width-test，120 秒上限，退出 0；Draw3PenWidth PASS。新增真实未附着 Window 的 Highlighter/Shape 活动工具锁存、新选择、释放后原始 System 恢复断言；不初始化 Host/RTS/GPU、无 HWND。 |
| i18n | 本轮既有 sync 生成后补齐变化条目的英语/繁体翻译，最终 check 退出 0；en-US、zh-TW 各 370/370，100%。没有沿用首版 372 条结果。 |
| 最终 PE 数据核验 | 仅以 LoadLibraryExW 的 AS_DATAFILE\|AS_IMAGE_RESOURCE 数据映射，不执行入口；SHADER301–304 均为 DXBC、逐字节等于当前四份 CSO。guarded 31160 bytes 在最终 EXE 恰出现一次，SHA 字符串匹配。 |
| 格式与任务上下文 | 29 个 tracked 文件原编码/BOM/换行身份与 Git checkout filters 基准一致；新文件/任务文档 UTF-8、EOF/空白、JSON/JSONL、引用存在检查通过，git diff --check 退出 0，staged 为空。task.py validate 退出 0，implement/check 各 7 个有效条目。长 input spec 的注入截断提示仍按手动读取完整相关章节处理。 |

## 链接器环境问题

第一轮唯一有意义的错误是 MSVC link.exe 14.44.35229.0 的 C0000005 / LNK1000（Internal error during IncrBuildImage），不是源码编译错误。待该次 solution 完成后，核对绝对路径并将唯一 Build/ARM64/Debug/Inkeys.ilk 移入本轮 verification 目录留存，用完全相同的 solution 配置重建。主程序完成完整链接，最终 MSBuild 退出 0；没有修改源码、项目、LinkIncremental、debug/PDB、依赖或全局工具链设置。

Microsoft 的 [.ilk 说明](https://learn.microsoft.com/en-us/cpp/build/reference/dot-ilk-files-as-linker-input?view=msvc-170)明确：缺少增量状态文件时会完整链接并创建新文件。这里只据此处理本地生成缓存，不把崩溃原因断言为已证实的缓存损坏。已有 pwsh.exe 查找提示仍出现在后处理输出；未为消除该提示扩大改动。

## 原始证据与产物身份

本轮 ignored 日志目录：Build/verification/cursor-policy-revision-20261019/。

- build.stdout.log / build.stderr.log：第一轮退出 1，源码编译完成、主链接器内部崩溃，headless 生成成功。
- Inkeys.failed-incremental.ilk：上述失败的增量状态文件，单文件移入留存，没有递归清理。
- build-full-link.stdout.log / build-full-link.stderr.log：同配置完整链接，退出 0。
- headless.stdout.log / headless.stderr.log：本轮完整 --no-window 退出 0，配置测试 0 failures，最终 PASS animation correctness；stderr 0 bytes。
- state-mode-production.stdout.log / state-mode-production.stderr.log：退出 0，两条 PASS 按现有方式在 stderr。
- draw3-pen-width.stdout.log / draw3-pen-width.stderr.log：退出 0，PASS 按现有方式在 stderr。
- shader-resources.json：最终产品/headless SHA 与四 shader 资源逐字节核验。

产物 SHA256：

- ARM64 Debug 产品 EXE：BBF413A99B9D502040AE3881E3D24643883E9E2C950CB3C143FAAA88A874FED6。
- ARM64 Debug headless EXE：1B506A348C917E7B7313F82836007438B8C73821C30BC4D7BBEB7B8912557522。
- 正式 PS 源：7EA2564A3A5FCD244DA2124095FA0B262DBA590629C0A5ECC228F758936FF977。
- Win7 guarded PS：31160 bytes，43C7E21FE9AA531271E2036D44998A5DCE6EAC9C032B63965A5417AE293C8D57，本次规则修订未重新生成。

## 尚未实测的边界

没有启动主窗口、设置窗口、任何 HWND harness、浏览器 GUI 或 Computer Use，没有 commit/push。Win7 双光标、首次落指闪现、橡皮和抬笔残留、静止穿透交接、亮暗背景与多 DPI 热点/残影、Win7 WARP/硬件 shader 像素仍需对应版本真机验收。本机 ARM64 Debug 产物不是 Win7 运行包，无窗口测试不能代替这些可见行为的验收。

独立审查还记录一项既有相邻行为：活动笔迹样式在 Down 锁存，而普通光标 appearance 沿最新 ProductVisualStyle 重算，切工具可改变预览宽色。本轮修复了类型投影，不额外修改这条既有样式链，也不据工具类型测试宣称所有活动宽色视觉已经实测。
