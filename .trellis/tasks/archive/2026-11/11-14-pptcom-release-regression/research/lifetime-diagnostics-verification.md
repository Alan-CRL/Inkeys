# 控制台生命周期诊断实施验证

## 改动与边界

- 新内部 PptComDiagnostics：ConditionalWeakTable 弱对象标识、纯值待清理账本、获取/转移/释放计数、原 RCW 返回计数、输出异常隔离。待清理明细最多 4096 项，超限明确计数。
- 已接入真实 dynamic getter/Item/命令、ROT 取得、连接点 Find/Advise/Unadvise/Release、root 转移、服务退出/维护、GC 与清理汇总。
- 通过既有 SetConsoleOutputEnabled / ConsoleOutput.PptCOM 控制。COM ABI/GUID、原对象访问和释放次数、子先父后顺序、GC 三步及事件重试策略不变。
- 明确禁止 COM xx.yy.zz 属性链；新访问和原访问均保留具名中间对象，未引入活动链式 COM 属性。
- 诊断不输出文稿名或路径、不获取 IUnknown、不持有 RCW 强引用，不调用 Marshal.GetHRForException，输出错误不覆盖业务异常。

## 验证

- ARM64 原生 MSBuild：PptCOM.Tests.csproj Release|AnyCPU 编译 exit 0，DLL/TLB 生成成功。
- 现有控制台测试及真实 x64 IDispatch 夹具：46 cases、0 failures，exit 0。
- 新增断言：重复同 identity 的每笔获取/释放、原 RCW remaining 与转移/汇总；日志开启时有副作用的命令只执行一次；关闭日志无输出；输出抛异常仍释放；ConditionalWeakTable 不保活真实 RCW；连接点 busy 一次后原 cookie 重试；原生 CP 获取恰次；线程 IErrorInfo marker 保持。
- 先手动复制本次 DLL 到 Inkeys/PptCOM.dll，再更新交接时间戳触发资源编译。TLB 哈希不变，不覆盖。
- 完整 InkeysRepo.sln Debug|ARM64：使用同 invocation PATH workaround、禁 node reuse、ARM64 原生 MSBuild，在已确认可用 PDB 服务的环境构建 exit 0。保留既有 pwsh.exe PATH 提示，未改项目/工具链。
- 现有 Assert-EmbeddedPptCom 检查通过；EXE 内嵌 DLL 与 PptCOM/bin/Release、Inkeys/PptCOM.dll 一致。
- 源文件原 UTF-8/BOM 状态和 CRLF 保持，git diff --check 通过。

日志：validation/lifetime-managed-build-final.log、validation/lifetime-managed-tests-final.log、validation/lifetime-solution-build.log。

## 未验证

没有启动 Inkeys/Office GUI 或 Computer Use，没有声称实际 POWERPNT 残留已修复。交由用户按 lifetime-diagnostics.md 采集真实日志后继续调查。没有 commit/push。

本次 DLL SHA256：`8C56727AEFAA6FE74C977E78AD94E19A00A8C72CA4487CA9A954C4E842C362A9`。
