# 2026-10-31 三项审阅修复验证

## 实现

- managed：owner 以现有真实 dynamic accessor 在 fast 复用前读取 FullName/Name；身份改变回 full，身份 busy 不携旧路径。完整缓存另按放映 session 校验，新场次重建，不修改 binding 事件生命周期。
- 导航：真实场次 BeginSession，Complete 保留已完成 epoch，Publish 拒绝迟到/旧请求；Clear 退休整场。
- 白板：失败改为 RePresentWhiteboardPage，绑定原 workspace/generation/page，消费拒绝新 loading 与最终 ExitPrepared；仅走既有真实补呈现，不进入 SetPage。原命令只结算一次。

## 已完成验证

- managed Release|AnyCPU：ARM64 原生 MSBuild 构建通过，测试退出0。新增用例修复前12项失败，修复后全通过；同场普通/周期 Slides.Item=0。
- DLL 已从同次 Release 输出交接；TLB SHA256 C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606，和随附文件字节一致。
- 独立 trellis-check 检查并复核退出终态遗漏修正，最终无未修 findings；未重复构建。
- 本轮 unstaged git diff --check 与源码 BOM/CRLF 检查通过。原已暂存历史 build-arm64-solution.log 含尾随空格，不属于本轮修改，保持原状。

## 构建环境对照

证据目录：Build/Validation/ppt-review-fixes-20261031（忽略目录）。

- 首次完整 solution 在普通沙箱约602.58秒退出1，stdout 只有模块/源文件编译进度及托管构建，stderr 空，没有可定位源码错误；未将其误报为通过。
- 同工具链无依赖函数 /c /Zi /FS 探针在沙箱45秒超时（退出124）；相同源码/参数/工具链在自动审批允许的沙箱外退出0，约0.805秒。证明存在 PDB 服务/IPC 执行边界差异，未修改项目/SDK/编译器绕过。
- 沙箱外首次完整构建126.55秒：源码编译通过，LINK 在 IncrBuildImage 抛 C0000005/LNK1000（171个既有编译 warning，1个链接器内部错误）；只删除已核验位于 Build/ARM64/Debug 的 Inkeys.ilk 生成缓存，未修改源码或 /INCREMENTAL 配置。[微软文档](https://learn.microsoft.com/en-us/cpp/build/reference/incremental-link-incrementally?view=msvc-170)说明缺少 .ilk 会自动执行完整链接并重新生成缓存。
- 同配置重跑完整 solution：退出0，7.54秒，最后增量0 warning / 0 error。链接恢复证明上述为增量缓存/链接器环境故障。日志 solution-final.*.log；已有 pwsh.exe PATH 后处理提示，solution 实际退出0，未为此修改无关配置。
- 分页 --page-control-only：退出0，0.446秒。白板 --draw3-whiteboard-storage-test：退出0，56.94秒，PASS 100 pages（含本轮生产 FIFO/helper/最终退出回归）。
- 最终 EXE 内嵌 PptCOM.dll 只读校验通过：SHA256 98ADB35098925C985B5FA5254084EBEA5C1C29664E32872AFBABB7B8E0E1F94A，与同次交接 DLL 相同；TLB 仍字节一致。
- 完整 InkeysHeadlessTests.exe --no-window：退出0，296.42秒，stderr 空，末尾 PASS animation correctness；日志 headless-no-window.*.log。

## 未验边界

本轮不启动 GUI、隐藏测试窗口或 Office。真实另存/重放、PowerPoint/WPS 正常退出与下一文稿、追加身份标量的实机延迟、原任务等待三点像素验收均未由本轮自动化代替。

本轮无 commit/push；任务延续 in_progress，原实机未验项保留。
