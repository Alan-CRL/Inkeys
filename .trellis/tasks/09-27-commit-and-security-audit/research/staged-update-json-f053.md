# F-053 本地 staged update.json 类型错误可终止更新线程

日期：2026-09-28。F-014 的 HTTP/ZIP/旧名候选修补与本单元分开。`AutomaticUpdate` 只有在非 mandatory、已检测到新版本且自动更新开启、`installer/update.json` 可读时才走本地缓存复用分支（`Net.Update.cpp::AutomaticUpdate`）；首次 Setting Repair 的 mandatory 流程不直接经过它。旧代码只核 `isMember`，随后对 `edition/path/channel/arch/hash.md5/hash.sha256` 直接 `asString`，hash 为数组、字段为对象时 JsonCpp `LogicError` 可从 detached 更新 worker 外逃，导致 `std::terminate`。主进程 `IdtMain` 两个启动读 index 入口是另一组已由主 agent 修补的路径，不把其可达性混同这里。

## 红证与生产入口

先在 `Net.Update.cpp` 抽生产实际调用的 `TryReadStagedUpdateMetadata(path,out)`，保留旧 `Json::Reader + isMember/asString` 行为；`AutomaticUpdate` 从该函数取候选，只有完整成功才赋给后续工作局部变量。`Net.Update.cppm` 导出 `RunStagedUpdateJsonBoundaryProbe(const std::wstring&,bool) noexcept` 与 `IdtMain` 的最早 `--staged-update-json-boundary-test <absolute path> valid|invalid` 由主 agent 独占接线。Probe 只读给定隔离文件，经**同一个生产 reader**验证 expectedValid，reader 抛异常时 probe 捕获并给 exit2；不进入配置、单实例、HWND、更新网络或真实 installer。

主 agent 完整 `InkeysRepo.sln Debug|ARM64` Build exit0（`TestResults/release-hardening/f053-staged-json-red-build-debug-arm64.log`）。独立 TestResults 目录 `f053-json-9edc3dae794b4fceb5efff67b3e69323/` 中四份本项目测试 JSON 用 `Start-Process -WindowStyle Hidden -PassThru` 精确等待：good pid6124 exit0；`[]` pid29652 exit2（`Json::Value::find requires object`）；hash 数组 pid66512 exit2；edition 对象 pid35004 exit2（`Type is not convertible to string`）。这证明本生产 reader 在类型错误上实际抛异常；CLI probe 自身捕获不能误说旧 detached worker 安全。

## 绿候选与边界

当前修补的 `TryReadStagedUpdateMetadata` 为 `noexcept`：以 `CreateFileW` 的只读句柄核非空且不超过 64 KiB，完整读入、剥 UTF-8 BOM；JsonCpp `CharReaderBuilder` `stackLimit=32`，拒 root/hash 非对象、任何必需字段非 string、尾部杂数据；全部转换先落局部 `StagedUpdateMetadata`，最后才一次性赋给输出。`std::exception` 或其它异常均转 `false`，无 `locale("zh_CN.UTF8")` 依赖；线程保留原 `fileDamage` 及 `IsSafeStagedUpdatePath`/哈希语法/实际文件哈希后续门，不在 catch 后用部分状态继续执行。接口与 CLI 默认不触碰真实文件，正式自动更新路径只从产品现有 `installer/update.json` 读取。

主 agent 串行完整 `InkeysRepo.sln Debug|ARM64` Build exit0（`f053-json-f041-lane-final-build-debug-arm64.log`）；同一隔离 TestResults 四 fixture 经显式进程等待：good pid19636 exit0、`[]` pid30440 exit0、hash-array pid51796 exit0、edition-object pid20476 exit0，四者 green stderr 空；红版 good exit0、三个畸形类型 exit2 的原始日志保留。当前完整 `InkeysHeadlessTests --no-window` exit0/PASS（`f053-f041-final-headless.*`）。这些证据证明读文件/类型边界与真实生产 helper 对受测输入不抛异常；真 Win7、真实自动更新 worker、同时写/重解析点竞争或用户配置恢复仍未验证。

文件归属：本 subagent 只写 `Net.Update.cpp` 生产 reader/probe；主 agent 独占 `Net.Update.cppm` 接口与 `IdtMain` CLI。没有改 ZIP/HTTP/旧 EXE 候选函数。`Net.Update.cpp` UTF-8 无 BOM、全 CRLF，`git diff --check` exit0；不提交。
