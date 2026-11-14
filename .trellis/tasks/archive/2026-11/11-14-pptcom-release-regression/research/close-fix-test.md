# 本轮关闭修复实机测试

请使用 D:/Project/Inkeys/Repo/Inkeys-draw/Build/ARM64/Debug/Inkeys.exe，并保持“PptCOM 控制台输出”开启。完全退出旧 Inkeys 后重新启动本版。

启动日志应出现：

```text
event=enabled module=a2f05be4-df20-4231-acf0-62321e5b8f1f
```

建议分别测试 PowerPoint 与 WPS：

1. 打开 PPT 并触发联动，关闭最后一个文稿/窗口，观察后台进程是否及时退出。
2. 紧接着打开下一份文稿，验证能否正常打开并重新绑定。
3. PowerPoint 额外检查：取消关闭，以及多个文稿时只关闭其中一个，剩余文稿应继续正常联动。
4. 保留从 Inkeys 启动到复现关闭后约 10 秒的完整日志，分别标注 PowerPoint/WPS 和实际关闭时点。不要只截取最后 CLEAN。

为排除交互控制台选择/暂停对输出的影响，推荐在保存好开关后用 PowerShell 写入文件（此命令由用户手动启动 GUI）：

```powershell
& "D:\Project\Inkeys\Repo\Inkeys-draw\Build\ARM64\Debug\Inkeys.exe" *> "D:\Project\Inkeys\Repo\Inkeys-draw\Build\ARM64\Debug\pptcom-close-fix.log"
```

本轮新增关注字段：

- `close.probe`：zero-documents / documents-remain / count-unavailable。
- `observe.deferred`、`owner.defer`：关闭提示或过期观测阻止了后续旧对象查询。
- `elapsed_ticks / frequency`：排除日志输出后的实际调用秒数。
- `office.event` 中 `event_qpc/event_tid` 是回调现场；行头 qpc/tid 是 owner 输出现场。
- `service-end` 汇总、`pending_cp`、`outstanding`：旧绑定是否真正进入清理并配平。

这些日志用于验证候选修复，不需要用户手动分析计数；发回完整日志后继续核对。
