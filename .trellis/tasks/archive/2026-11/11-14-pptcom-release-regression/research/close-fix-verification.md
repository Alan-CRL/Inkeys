# 关闭观测修复交付验证

用户已授权实施，并要求完成后由用户实机复测。

## 本轮修改

- service/CP 两处一致处理 0x800706BA，自动结束不可用旧绑定，沿原 owner 清理事件与 root。
- 2025/2004 发布绑定代次化关闭提示；owner 优先复核 Presentations.Count，0 才结束；正数或不可读只推迟本次，并按原维护节奏恢复取消关闭/仍有文稿的会话。
- 在 ActivePresentation、窗口集合、fresh show 和 View 查询前复核过期观测；关闭提示同时阻止继续 ROT 发现。返回的临时对象仍由原 finally 释放。
- 关闭复核让出时不在同一轮继续 descriptor/命令/ROT 查询；没有引入新线程、强制超时释放或 FinalRelease。
- 回调日志只排入 64 条纯值队列，owner 每轮最多输出 64 条；保存原 event_qpc/event_tid，避免日志输出阻塞 COM 回调或连续事件饿死 owner。
- getter/Item/命令/Release 的 elapsed_ticks/frequency 锁存于实际调用区间，排除 begin/end 控制台写入；用慢输出的真实 COM 测试验证。

## RED → GREEN

- 修改生产代码前，新增黑盒回归在原实现上编译成功、测试 exit1，共 13 项预期失败：RPC 断连遗漏、关闭通知后旧图查询、取消关闭复核、重入关闭、busy+Count0、无 stop 信号的服务/CP 自动清理。
- 实现后这些回归及原 suite 全部通过；另补窗口集合返回时关闭、数量不可读、回调无 I/O、ROT 关闭让出、有界事件日志消费与纯调用计时检查。
- 最终 PptCOM.Tests.csproj Release|AnyCPU 编译 exit0；托管测试和匹配 x64 CLR 的真实 COM 夹具 exit0，47 cases、0 failures。
- 完整 InkeysRepo.sln Debug|ARM64 编译 exit0；沿已确认的 ARM64 原生 MSBuild/PATH workaround/PDB 可用环境，没有修改工具链配置。保留既有 pwsh.exe PATH 提示。
- 最终 DLL 先手动同步到 Inkeys/PptCOM.dll 并更新时间戳，再编译主程序。TLB 逐字节一致，未覆盖。
- 使用现有 Assert-EmbeddedPptCom 确认 EXE 内嵌 DLL 与最终 DLL 一致。
- 原 UTF-8、BOM 状态、CRLF 保持，未引入活动 COM 链式属性访问；git diff --check 通过。

日志：validation/close-red-build.log、close-red-tests.log、close-green-final-build.log、close-green-final-tests.log、close-fix-solution-build.log。

## 交付标识

- EXE：D:/Project/Inkeys/Repo/Inkeys-draw/Build/ARM64/Debug/Inkeys.exe。
- DLL SHA256：89F122E9E9B7E262C7044587CF862F2811E042BB068FCDA98056EFF7A9BF0CFE。
- 模块 MVID：a2f05be4-df20-4231-acf0-62321e5b8f1f。
- TLB SHA256：C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606。

没有启动 GUI/Office、没有 commit/push。实际关闭等待和下一文稿行为仍由用户复测，不把回归通过等同于已解决实机问题。
