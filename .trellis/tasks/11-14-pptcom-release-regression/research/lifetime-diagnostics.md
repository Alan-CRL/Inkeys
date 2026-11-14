# PptCOM 实机日志采集

使用：D:/Project/Inkeys/Repo/Inkeys-draw/Build/ARM64/Debug/Inkeys.exe。

1. 在设置的调试软件页开启“PptCOM 控制台输出”，然后重启 Inkeys。沿用既有开关，不需要设置环境变量。
2. 从启动开始保留控制台输出。新模块会出现 `[PptComLife] ... event=enabled module=...`。
3. 打开 PPT、等待绑定，按原步骤复现关闭 PowerPoint 后残留和不能打开新文稿；关闭后继续保留约 10 秒输出。若再次打开文稿失败，也保留这段输出。
4. 把完整输出（不仅最后的 CLEAN）交回分析。可以在开关已保存后，用 PowerShell 重定向启动以保存文件：

```powershell
& "D:\Project\Inkeys\Repo\Inkeys-draw\Build\ARM64\Debug\Inkeys.exe" *> "D:\Project\Inkeys\Repo\Inkeys-draw\Build\ARM64\Debug\pptcom-lifetime.log"
```

## 日志含义

- `call.begin/call.end/call.fail`：getter/Item/命令调用与 HRESULT；有 begin 而没有对应完成的调用需重点核对。
- `acquire`：已有 COM 返回的一次取得；`obj` 是弱托管 RCW 标识，`parent/target` 便于关联来源。
- `transfer`：本次取得转移到 root 或事件连接，不增加取得次数。
- `release.begin/release.end`：仅执行一次原释放，`remaining` 是返回的 RCW 计数，不是 IUnknown 原生 AddRef 总数；不能因非零就盲目多 Release。
- `owner.maintenance`：当前绑定、场次、root 标识和 pending_cp，来自纯值快照，不额外读取 Office。
- `office.event/cp.*`：事件 DISPID、订阅 cookie、Unadvise 结果、退休与重试。
- `service.stop/service.finally/cleanup.begin/gc.begin/gc.end/service.end`：服务为何结束、是否进入清理、是否卡在释放或 GC。
- `summary/outstanding`：逐次取得/成功释放计数及仍有已跟踪取得的对象。重绑时的新 bestApp、仍活动的 root/CP 可以合法存在，正 balance 不单独证明泄漏。
- `untracked_release/dropped/output_failures` 非零表示追踪或输出不完整，不能据汇总宣称全部配平。

本版仅增加诊断，保留原 GC、逐笔 Release 和事件重试策略。诊断表不强引用 RCW，日志也不为检查对象而获取新的 COM 属性或 IUnknown 指针。
