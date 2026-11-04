# Windows 文件名与 ShellLink 依据

实现 R9 时核对的官方资料（2026-11-03）：

- [Naming Files, Paths, and Namespaces](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file)：文件名禁止路径分隔符、保留字符及控制字符；设备保留名即使带扩展名仍受限，COM/LPT 的上标 ¹、²、³ 也属于保留形式；Windows Shell 不支持以空格/句点结尾的名称。
- [IShellLinkW::GetPath](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishelllinkw-getpath)：cch 包含终止零，返回路径上限为 MAX_PATH；S_FALSE 可表示成功但未取得路径，不能把它当作已成功识别目标。

上述资料用于校验已有批准范围的输入边界，不扩展本任务的长路径能力或快捷方式身份识别算法。
