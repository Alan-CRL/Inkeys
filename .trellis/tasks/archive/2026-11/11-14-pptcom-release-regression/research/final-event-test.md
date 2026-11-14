# 最终关闭通知候选版：用户复测

EXE：D:/Project/Inkeys/Repo/Inkeys-draw/Build/ARM64/Debug/Inkeys.exe。

完全退出旧 Inkeys，保持“PptCOM 控制台输出”开启后启动。日志模块应为：

```text
event=enabled module=230cb63b-1639-4077-bf63-257114a882e5
```

1. 优先复现 PowerPoint 关闭最后窗口，观察后台退出是否仍等待约 28 秒。
2. 随即打开下一份文稿，检查打开和联动。
3. 检查取消关闭、多个文稿关闭其中一个，以及 WPS 对照。此候选会在最终文稿关闭通知后重新发现联动绑定，其他文稿不会被关闭，但可能短暂重连。
4. 分别保留从启动到后台进程退出后约 10 秒的完整日志，并标记实际关闭时点；若仍长时间不退出，请覆盖整个等待过程。

本轮重点：2031 的 event_qpc、close.final → cleanup.begin、cp.unadvise.begin/end 的 elapsed_ticks、root release.end 与 service-end 汇总。2031 之后不应再为确认关闭读取 Presentations 或 Count。

推荐用文件保存以排除交互控制台选择/暂停影响：

```powershell
& "D:\Project\Inkeys\Repo\Inkeys-draw\Build\ARM64\Debug\Inkeys.exe" *> "D:\Project\Inkeys\Repo\Inkeys-draw\Build\ARM64\Debug\pptcom-final-close.log"
```
