# 等待三点链路版人工短测

1. 双击原入口：`D:\Project\Inkeys\Repo\Inkeys\Scripts\Start-PptNavigationDiagnostic.cmd`。
2. 在同一PPT环境用控件在第3/4页间短测，出现一两次明显等待即可，留意三点是否出现；无需重复大矩阵。
3. 从Inkeys菜单/设置正常退出，保留采集窗口等收集完成，再回复“测完了”。这版UI数值记录在正常退出后统一输出。

版本EXE AECD45C3CA5618C67798BD6148C87640E83F512B54AE9CC466E5E97887C6D98B；PptCOM仍1061DDF5...，本轮未改COM访问/释放或UI行为。完整ARM64、PPT定向和实际三点离屏/启停像素等价均通过；实机根因与性能长尾仍待这次可区分样本。
