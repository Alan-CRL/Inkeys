# Observe细分定位版人工交接

1. 正常退出所有Inkeys；双击 `D:\Project\Inkeys\Repo\Inkeys\Scripts\Start-PptNavigationDiagnostic.cmd`。
2. 用指定PPT在3↔4页点控件约5轮，重点4→3的Previous；有明显卡顿可记方向/大致时间。第3页的页内入场动画与真正换页分开观察。本次不必重做完整键盘矩阵。
3. 正常退出Inkeys，保留采集窗口等自动收集完成；回复“测完了”即可，AI读取新采样目录。

本版会细分Observe身份/窗口/Count/View/State/当前页/HWND以及原释放步骤，未修改调度/翻页行为。新内嵌PptCOM SHA256为55EA18DFE829E2FFD3EA4E9503D2F288BE61193A231FC4BF1D08D0CF9542413A。旧采样仍保留，不能凭旧日志反推新成员计时。
