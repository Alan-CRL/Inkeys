# ROT让出修复版人工测试

1. 正常退出全部Inkeys；双击 `D:\Project\Inkeys\Repo\Inkeys\Scripts\Start-PptNavigationDiagnostic.cmd`。
2. 在同一指定PPT用控件3↔4来回约8–10轮，重点4→3 Previous；每次等上一操作完成，不按住连发。页内动画与真正翻页分开，明显卡顿可记方向和近似时间。
3. 正常退出Inkeys，让采集窗口自动收集结束，回复“测完了”和主观变化即可。

这是第一项实际调度修复：有导航排队时取消本轮尚未完成的ROT，先释放全部扫描临时取得，再按原完整Observe校验处理；最多连续让出一次。完整观察仍在、进行中的COM仍不能抢占，因此允许仍有Office自身/观察耗时，不把一次顺畅当全部解决。新内嵌PptCOM SHA256 351A529267C75C3F8588F8887626ABF22F6FECE36892AFA65D4C0DC1416C876B，入口会校验实际版本。
