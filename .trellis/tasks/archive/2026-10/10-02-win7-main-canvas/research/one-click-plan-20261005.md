# 单 CMD Win7 诊断候选（2026-10-05）

用户要求新版本仅一个CMD，方便WARP及VMware两台Win7分别定位。继续旧in_progress任务；未授权commit/push/archive。

最小差距：当前三入口需人工选择，原PS失败先于精简成功，缺反向重测/完整程序路由分界。新工作仅增加可判别诊断和整合采集，不当成正式修复。

## 所有权与实现合同

- GPU实现者仅HiddenWindowTest.cpp、诊断header、Generate-Win7-PixelDiagnostic.ps1及必要research。保留原13例/正式四项退出合同；追加独立offscreen原PS在精简成功之后重测、完整源码原样复编PS、完整type4 Cursor入口常色PS、Cursor分支提前处理PS及最后原PS重测。每项实际结果单列，新增序列判定不覆盖旧production退出。原PS→精简→原PS先分离暖机/状态，再解释字节码分界。生成器从当前生产HLSL/ink.hlsli建立ASCII临时源；保存源SHA/profile/实际DXBC/signature及其它完整分支未被优化成纯常色程序的证据。所有PS仅显式自检用，正式HLSL/8个CSO/301–304/输入渲染不改。
- 采集实现者仅Collect-Win7.ps1、IdtMain.cpp现有诊断初始化、新Run-Win7-Diagnostics.cmd/.ps1及必要research。单入口PS2兼容：一次pixel probe→普通ULW→强制prcDirty=null。一个时间根、三个stage子目录，summary/observations保留每轮identity/退出/实际现象。复用collector，可选SkipProbe/CaptureRoot避免重复probe，旧默认行为保持。普通/强制环境显式设定并finally恢复；失败/不完整不伪造成功，probe失败仍继续采正常应用。仅精确自有子进程清理；无进程名kill。每轮用户画笔迹、再橡皮和激光各移动3秒，正常关闭后收集一句可见观察。中文提示，唯一CMD；probe整体150秒/单步3秒。
- IdtMain仅在已有非IDT_RELEASE初始化处读取精确INKEYS_DRAW3_WIN7_DIAGNOSTICS=1，将局部Draw3/Cursor enabled与原配置OR，不写配置、不改输入。launcher对子采集临时设环境，省掉手动进设置。其他诊断开关保持。
- collector验证使用实际WinPS+mockEXE覆盖spacepath/大双流/非零/缺输出；不在本机启动正常产品GUI。主会话完整ARM64 Debug+x64 Release、从不显示GPU自检与Headless；check子代理审查/独立shader复编。新独立包一中文README和唯一CMD，旧两个CMD不进入新包。

## 判别/交付边界

用户两台Win7各双击一次，回传各自完整汇总目录。候选记录当前HEAD+未提交delta、EXE/ZIP/source身份与本机验证。retry恢复优先时序；retry失败但完整入口常色通过优先入口后执行；复编/提前分支恢复支持程序结构差异，仍有JIT布局混杂。硬件有像素时比较普通/Full光学现象及参数耗时；透明源不靠ULW补出墨。Win11验证不代替Win7恢复。
