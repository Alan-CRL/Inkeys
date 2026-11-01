# 2026-11-01 Observe子阶段实测结果

采集目录：Build/Validation/ppt-navigation-20261101-122321-464-4c57406c。PID54148，exited0、capture_errors为空，EXE内嵌DLL与本轮55EA18DFE829E2FFD3EA4E9503D2F288BE61193A231FC4BF1D08D0CF9542413A匹配。分析4816事件、2374span、15请求，complete、0issues。

## 最慢Previous epoch13

总1610.020ms：managed邮箱1500.262ms，owner109.064ms，focus0.062ms，native队列0.164ms。邮箱由ROT剩余718.024ms和随后Observe782.218ms几乎完整覆盖。

Observe直接子项（只计直接层，不重复累计孙项）：

| 原有操作 | 总耗时ms |
| --- | ---: |
| ActivePresentation |16.547|
| SlideShowWindows计数整组 |186.832|
| SlideShowWindow |16.236|
| Slides总页数整组 |187.371|
| View取得 |62.064|
| View.State |93.903|
| 当前Slide/SlideIndex整组 |187.118|
| View释放 |15.721|
| HWND解析 |16.134|

identity比较、会话发布与最终缓存root临时取得释放均小于0.01ms；直接子项总781.954ms，未归属约0.264ms。count组包含集合获取/Count/Release，current组包含Slide获取/SlideIndex/Release。非零释放时间必须保留，不能为了节约时间依赖GC、FinalRelease或泄漏；计时本身不证明引用计数，配平仍由原有finally、账本测试和独立审查证明。

## 其他样本

Previous epoch4排队415.105ms，碰到已执行中的Observe（整次749.829ms，排队仅覆盖后半段）；View/State/current/Release/HWND共同覆盖剩余等待。Next epoch12排队251.707ms，对应descriptor/fast及Caption读取。说明不是单一getter特异性长挂，Office忙碌时多次原有COM读取累积。

## 执行决策

已有足够证据先做最小ROT可让出修复（rot-yield-fix-plan-20261101.md），不继续盲目删某个Observe检查，也不同时改多个调度层。取消仅发生在COM调用返回后的检查点，清全部扫描临时引用后回原完整Observe→排水；最多连续让出一次以避免发现饿死。此步骤可减少尚未完成的ROT查询，但不能消除已开始的COM调用或Observe本身耗时，最终收益需用户新样本验证。

本报告不把不同轮次时长回填至旧1.23s事件，不宣称已找到Office内部CPU/GIF根因；不修改PPT或自动执行GUI。
