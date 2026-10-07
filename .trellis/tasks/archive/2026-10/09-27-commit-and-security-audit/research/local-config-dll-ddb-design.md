# 本地配置、系统 DLL 与 DDB 边界修补设计

本节是 Work4 安全审查中新证实的三个独立调用链，最终以审查者的 `non-updater-security-boundaries.md` 和当前代码校对。修改权属：主 agent 唯一写 `IdtConfiguration.cpp`、`Other.Config.cpp`、`IdtMain.cpp`、`IdtPlug-in.cpp`、`Display.cpp`；Draw3 Controller 由另一 worker 独占。保持原 BOM/CRLF，不改第三方/生成资源。

## F-032 本地 JSON 读取和写回

`deploy.json`、`pptcom_configuration.json`、`main.json` 在生产启动路径可达。旧读取按 `GetFileSizeEx` 的 64 位值直接截为 DWORD 或允许近 4 GiB，再分配/JsonCpp 解析；旧启动在已存在文件读取失败后仍可能以默认值 `WriteSetting`/`PptComWriteSetting`/`config.Write` 覆盖原件。采用足够容纳未知字段的 16 MiB 单文件上限（当前样本约 2 KiB），拒绝负值/空值/超限/超过 DWORD；读取失败时保留已存在原文件，不把坏文件自动清理成默认。首次启动确认文件不存在时仍允许生成默认文件。合法已有文件的保存/迁移语义保持；配置写入的后续调用也不能绕过失败保护。测试以隔离路径构造空/坏/超限 JSON，核原字节不变和首次创建；不能写真实用户配置。

## F-033 系统 DLL 绝对路径

`Display::QueryMonitorDpi` 当前相对 `LoadLibraryW(L"Shcore.dll")`；Win7 SP1+仅 KB2670838 不保证该 DLL 存在，应用目录若可写会被 DLL 搜索路径优先命中。仅从 `GetSystemDirectoryW` 得到的绝对 System32 路径加载；不存在/加载失败回退现有 DPI 路径。不要用 Win7 可能缺的 `LOAD_LIBRARY_SEARCH_SYSTEM32`/KB2533623。其他可达裸加载按审查结果逐项处理。

## F-034/F-035 DDB 插件启动/清理

`StartDesktopDrawpadBlocker` 启用时从固定内嵌资源更新 EXE，但旧代码忽略提取失败并在未复验 hash 时可 `ShellExecute` 该路径；禁用时只要命名目录存在便 `remove_all`，可能删除未知文件。禁用分支不再递归清理未知目录。启用分支在创建/覆写前拒绝 reparse 的目录和 EXE，提取必须返回成功，启动前按固定内嵌 SHA256 再验最终普通文件，失败不启动；检查 ShellExecute 成功与否并记录。内嵌 `Inkeys/exe/DesktopDrawpadBlocker.exe` 的现有 SHA256 与 `DdbSHA256` 常量完全一致。自定义 `RunAsAdmin` 仍需人工 ACL/UAC/TOCTOU 复核，不能把一次 hash 比对宣称为可执行来源的最终强认证。

## F-036 SuperTop 线程冒用令牌失败边界

`RunToken::SetUiAccessToken` 已成功 `SetThreadToken` 冒用当前 session winlogon 的 token 后，`SetTokenInformation(TokenUIAccess)` 失败直接 return，旧线程不 `RevertToSelf`；调用者仅输出失败并继续启动新进程。改为在所有后续路径先撤销线程冒用、检查撤销结果，失败时不再进入子进程创建。隔离无 GUI CLI 用当前测试进程 token 复制普通 impersonation token、故意传无效 target，使真实 `SetTokenInformation` 失败后断言线程 token 已移除；不用获取 winlogon 或启动 SuperTop，不触及用户已有进程。真实提升/UIAccess、UAC 同意和 Win7 运行仍须人工。

性能/功能边界：仅启动/配置 I/O 路径，不修改 UI3/Draw3 帧率、输入采样、FLIP、DComp/ULW，也不打开有意关闭入口。任何 GUI/辅助 EXE 实际启动须遵守 AGENTS 人工授权门禁。
