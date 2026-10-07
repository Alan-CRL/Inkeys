# F-044 旧版回收与大小写别名安全收口

日期：2026-09-28。状态：安全修补完成、独立 Draw3 Debug|ARM64 编译与隔离测试通过；主 Solution/Release 与修补后独立复审待执行。源于独立审查 `ppt-atomic-independent-review.md` 的 R-044-1、R-044-2、R-044-3。范围只含 `Draw3.PresentationAutoSave.cpp` 与 `presentation_autosave_tests.cpp`；未改 F-041 绑定选择、Draw3 Host/Controller、主进程或工程项。

## 取舍与 Win7 合同

R-044-1 的原实现先按路径检查属性并读取旧 UInk，关闭读取句柄后再 `DeleteFileW(path)`；同权限外部进程可在间隙替换候选文件或父目录。named mutex 仅约束遵守本服务协议的 writer，不能关闭该竞态。不能把重复读路径或检查 reparse 属性当作完整修复。

同一候选 HANDLE 验证和标记删除的 `SetFileInformationByHandle(FileDispositionInfo)`、`FILE_DISPOSITION_INFO` 官方最低客户端是 Windows Vista，理论上不需要 Win7 SP1 在 KB2670838 以外的系统补丁：[API](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)、[结构](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_disposition_info)。但安全自动回收还必须锚定 `presentation/files` 和主备索引、排除 reparse/junction 与对象身份变化，并以同一打开对象完成内容验证和删除。当前没有 Win7 真机与目录、文件、索引并发替换的完整对抗证据。因此首发选择**不自动回收任何已提交 UInk 版本**：移除 `RetireUnreferencedVersions` 及成功保存后的调用，也不在别处按目录枚举删除孤儿。现存 `DeleteFileW` 仅用于本次事务的临时 index/失败新文本文件清理，不碰已提交旧版或未知文件。

R-044-3：`ParseUInkGuid` 接受大写，NTFS 通常以大小写不敏感的文件名解析。`ReadIndex` 现在使用只折叠 ASCII A–Z 的 `fileGuid`、`relativePath` 唯一键，拒绝两个 entry 经 Windows 常见路径规则指向同一物理文件；v2 版本路径要求规范小写逻辑 GUID 和事务 GUID。v1 固定路径的单条大写 GUID 仍可严格读取，不要求全量旧索引先改写。其他身份字段和 F-041 会话侧路由不在本修补中改动。

## 测试与当前结果

- 先写红测：三次正常提交后旧版和未知同形文件都应保留；PageIndex 与 Clear 保存三次应有三份物理版本；手工 v1/v2 索引含大小写不同的同 fileGuid/path entry 时应返回 `IoError`；单条大写 v1 仍应 `Loaded`。`inkStrokeModelerTest.sln Debug|ARM64` Build exit 0，完整 `inkStrokeModelerTestTests.exe` exit 1，6 项预期新断言失败，日志 `TestResults/release-hardening/f044-gc-alias-red-build-debug-arm64.log`、`f044-gc-alias-red-debug-arm64.{stdout,stderr}.log`。红灯中的单条大写 v1 读取无失败。
- 生产修补后的同范围 `inkStrokeModelerTest.sln Debug|ARM64` Build exit 0；沙箱外仅隔离本项目数据的完整 `inkStrokeModelerTestTests.exe` exit 0，六项新红断言转绿。日志 `TestResults/release-hardening/f044-gc-alias-green-build-debug-arm64.log`、`f044-gc-alias-green-debug-arm64.{stdout,stderr}.log`。完整 stdout 末尾为 `All draw3 contact input tests passed.`；无 `[PresentationAutoSave] failed`。修补代码已冻结，`git diff --check` exit 0；两个文件保持 UTF-8 BOM 与 CRLF。主 Solution/Release/Win7 不由此推出 PASS。
- 没有实施校验后/删除前的外部文件、目录、主备索引替换对抗测试，因为最终产品路径已彻底取消旧版删除，测试无法再触发该操作；红测直接证明旧实现会删除版本，绿测应证明服务不再自动删除。若将来恢复自动 GC，必须先按相同 HANDLE 与目录/索引锚定设计新增确定性对抗测试，不能复用本轮“不删除”绿灯作为删除安全证明。

## 容量、性能与剩余门禁

不回收意味着每次成功 PPT 保存都留下完整新物理 UInk；index 发布失败也可能留下未引用孤儿。总磁盘增量约为 `Σ每次 UInk 字节数`，没有固定天数或版本数上限。当前 UInk 默认读取上限为 128 MiB（`uink_model.cppm:426`），这不是磁盘总占用上限；示例平均 20 MiB、50 次提交约 1 GiB。可用预算 `B`、平均每版大小 `S`、每日保存数 `C` 时，粗略占用时间 `B/(S×C)` 天；这些数是估算，不是本机生产轨迹测量。取消 GC 也去掉原先每次提交后可能重读旧全量 UInk 的 worker I/O，但不能据此宣称用户可见延迟已测得改善。

低磁盘剩余空间、长会话与大 PPT 需记录每文稿版本大小、提交次数、自由空间、排队/完成耗时及失败孤儿数。保存失败必须保持最后已提交主/备索引与对应物理文件可读；未 durable 的新请求允许在用户授权的 15 秒强制退出时丢失，不能写成保存成功。无自动清理时可提供人工检查步骤，但不得建议脚本删除不明文件。磁盘耗尽风险保留 P2；若真实使用轨迹显示可在合理会话内耗尽可用空间，应升级发布门禁并另设计可证明安全的容量策略。

旧 v1 的单条大写 `fileGuid/relativePath` 已可 Load；既有 Save 分支与请求快照仍按字节比较 GUID 文本，手工大写 v1 entry 的再次 Save 可能 `SourceChanged`。生产旧 writer 固定输出小写，未有自然生成大写文件证据；这项条件性兼容限制需独立决定是否纳入 F-041/兼容验收。schema v2 新文件也不保证旧二进制降级读取。

Win7 SP1+仅 KB2670838 文件系统/Office/WPS 真机、三架构 Release、磁盘满/拒绝写入、断电、真实 15 秒重启与跨进程可见恢复仍未由本单元验证。图形路径未改：用户实测 Win7 `FLIP_SEQUENTIAL` 应保留，首发透明方案仍限 DComp/ULW。
