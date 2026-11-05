# 首批低风险修改边界

2026-11-05；用户已批准先采样分析、随后直接低风险优化。修改前完整InkeysRepo.sln Debug|ARM64已退出0，EXE/PDB GUID+Age匹配，before-bin已保存；无窗口生产history探针退出0，原始日志和171个进程样本已记录。正常启动场景将使用冻结的before-bin采样，源码修改不会污染before输入。

## 已选修改

1. Inkeys/IdtImage.cpp：alpha_drawpad、tester、pptdrawpad改为默认空构造；Inkeys/IdtMain.cpp：删除三行启动resize并更新附近说明，保留日志与LegacySurfaceReady。编译项扫描证明无生产消费者，详见low-risk-ui-plan.md。这不是按需/首次使用分配，不增加任何热路径工作。
2. inkStrokeModelerTest/draw3/uink_draw3_export.cpp：只读sourceCanvas使用const引用，避免整页复制。
3. Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp：非const imported使已有std::move真正转移snapshot。
4. Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp：导入snapshot在校验后move到局部projected，来源之后不再用。
5. Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp：captureCanvas的独占optional内容move到activeCanvases。

具体消费者、行号和安全性见low-risk-draw3-plan.md。除以上6个源文件的必要局部修改外，不扩展实现。

## 本轮保留

- L0/L1/L2、UI共享WARP和Draw3独立hardware-first及fallback、线程/调度不变。
- 不删除私有PPT请求或公共导入的legacy canvases镜像；本轮保留全部格式/内存表示兼容合同。
- 不改Freeze有效DIB、Setting resident/图片恢复、字体、Laser/trusted/selection ULW分配时机、GPU缓存预算/收缩、Undo不可达分支回收。
- 不顺手移除background全局GDI+耦合对象、不做其他清理重构。

## 验证

保留BOM/CRLF并审查diff；完整InkeysRepo.sln Debug|ARM64编译；运行既有history探针并比较digest/内存/句柄；运行对应Desktop/PPT/UInk保存恢复和必要离屏绘图入口；静态检查move之后无读取、引用owner不变、启动里程碑、层与设备分工不变。流畅度按用户要求静态确认，不要求动态性能证据。所有采样说明无窗口/离屏/真实启动范围，不能将像素估算冒充Private Bytes实测。
