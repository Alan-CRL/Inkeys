# 运行验证发现

## 严格等待GUI子系统CLI
Inkeys.exe是GUI subsystem，PowerShell直接&调用可能在实际测试退出前返回，空日志/旧LASTEXITCODE不构成成功证据。后续统一Start-Process -PassThru -Wait -WindowStyle Hidden并重定向stdout/stderr，取Process.ExitCode；测试参数均为wWinMain早退/无窗口路径。

首轮完整solution Debug ARM64编译exit0；严格CLI ConfigBoundary和SuperTopToken exit0；General headless专项exit0。严格single-instance专项exit1；严格shutdown-supervisor-tests exit62，新增restart-continuous-lease失败，其余既有监督/UEF/截止case日志均PASS。红证据保存在validation/first-*。不得把这轮红证据混作修正后结果。

## 对象命名空间
core实际只读API探针发现当前Codex runner Local对象解析到AppContainerNamedObjects，硬编码Sessions/.../BaseNamedObjects会把合法lease误拒。修复改为OpenMutex Local预期目录ID键获得reference对象，查询并比较reference与收到lease的完整内核名和Mutant类型，不退化为名称后缀比较。具体修正后的结果等待同次新构建。

## API依据
- [Microsoft DuplicateHandle](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-duplicatehandle)：跨进程复制指向同一内核对象；所用进程句柄需具备复制权限。
- [Microsoft NtQueryObject](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryobject)：动态解析API；公开说明Basic/Type信息。名称信息不是该页承诺的公开兼容合同，因此动态失败必须拒绝交接；Win7与UAC未实测，不宣称通过。
- [Microsoft handle inheritance](https://learn.microsoft.com/en-us/windows/win32/sysinfo/handle-inheritance)：明确句柄复制和继承的进程局部值语义；不能在子进程创建前猜测其句柄整数。
