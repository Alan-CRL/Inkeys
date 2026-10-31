# 设计
PptCOM Windows2022独立构建/导出，上传DLL与独立TLB；native矩阵needs PptCOM，按artifact ID下载到Inkeys目录并独立编译；Package收集三个EXE，沿用包结构与上传。
PptCOM输出DLL/TLB的artifact ID；下载和构建错误直接失败，清理仅使用当前run的精确ID。
Harness使用EnableUi3PresentationHarness，Debug=true/Release=false且命令行可覆盖，统一宏；关闭时剔除runner/auth测试/固定轨迹，产品钩子沿无fixture分支；内部参数拒绝保留early处理。
父任务汇总验收，不自动改动其他活跃任务。

实施阶段用户修正：Inkeys 下 DLL/TLB 继续 Git 跟踪且不忽略，由维护者在 PptCOM 源码变化后手动同步；两个工程无自动复制或依赖。中英文编译说明同步独立构建、Harness 开关和 CI artifact 交接。
