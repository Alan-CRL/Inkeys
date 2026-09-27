# 暖状态短快划增长研究（2026-09-25）

## 当前基线与问题边界

本轮从干净 `bugfix/eraser` HEAD `4d1727916a7d2474841156de753ee3d24d2ad7c7` 继续活动任务，不切 Git 状态。用户尚未在教室大屏对最新场景曲线实测；报告的是 Touch/ScreenPen 短时间快动偏大、Mouse 正常。`5997d23b` 未缩短 Touch/ScreenPen 的时间常数，`cf4a1874` 主要扩展诊断；旧 `94e07b2` 同配置控制器也有短快划增长，不将体验变化未经实证归因于最近提交。

当前 `ResolveConfig`：Touch start/full/decay=25/60/180ms、tau=120/100ms、log rate=6/8每秒；ScreenPenHybrid start/full/decay=60/160/280ms、tau=140/120ms、log rate=4/6每秒。`FollowTarget` 以 `strength*decay` 的泄漏积分累计证据，用 start/full 形成许可尺寸，再由 Follow 跟随；这些值不是硬性等待时间。强激励下 Touch 满额证据约73ms，故暖状态短快划可能迅速扩大。

先用同一生产 Controller 的 B=32、中灵敏度、面积关闭合成回放核对旧输出。Surface 28×19cm/2880×1920/192DPI，先 0.6×enter 移动2秒；强快段速度分别为 Mouse 2850 DIP/s、ScreenPen 525 mm/s、SmallTouch 375 mm/s。三者不是同一真人动作或实机轨迹。记录请求与实际快段时长、全程峰值和后续走势，测试和产品接入证据分开。

## 同一生产源码的 ARM64 命令行探针

完整 solution 的主项目与 headless 同时报 `C1041 vc143.pdb`；普通/沙箱外构建、备份并重新生成中间 PDB 均复现，原 PDB 已恢复。检测到 Visual Studio 与 `mspdbsrv` 运行，但尚未证明它们正在编译本仓库或是 C1041 的唯一根因；主会话未关闭用户窗口/服务。独立探针使用本机 ARM64 `cl` 对仓库原 `Draw3.SpeedEraser.cpp` 编译，不启用 PDB，命令行退出0。初始旧源码的 240Hz/120fps 对照与用户给定数值吻合：100/150/200ms 的 Mouse 为32/32.0119/32.8015，ScreenPen 为32.2693/37.1074/45.3882，Touch 为43.0987/58.4983/80.3089 DIP。旧配置复测与初始旧源码相符，不能把问题归因于最近提交。

候选参数应用到同一生产源码后，ScreenPen 100/150/200/500/1000ms 为32/33.0491/38.0394/112.005/157.386 DIP；Touch 为32/32.8963/37.957/108.05/155.941 DIP。Mouse 三段完全不变。两种直接模型的 0.9×160 DIP 首次到达约704/746ms；快段之后继续普通移动的峰值未超快段末值。对照及 50/80/120/250/300ms、阈值时刻见 [warm-burst-comparison-20260925.csv](warm-burst-comparison-20260925.csv)。这是合成控制器证据，仍须完整工程和真实设备验证。

独立帧/输入频率探针在 Surface ScreenPen/Touch 的 60/125/240/1000Hz × 30/60/120/144fps × 80/150/500ms 共96组中，按实际量化脉冲时长对照1000Hz回放，快段末最大相对差约0.591%。60Hz请求80ms实际为83.333ms，不能写成精确80ms。Touch普通拖擦面积辅助另用同一生产 Controller 对比旧增长响应：0.15/0.25/0.40/0.80/1.20s 的尺寸逐项一致，最大差0 DIP，清扫证据始终0；这是局部分离面积增长路径的依据。

临界可达性继续使用生产 Controller 的3秒局部圆轨迹：Surface/教室/经验Touch和物理/DIP ScreenPen各测刚过enter、目标约2B、接近large、超过large，共20组；实际与对应目标的最大差0.0775 DIP，0组失败。每组的 `strength*decay` 平衡证据上限均覆盖合法目标，未出现只在最强快扫才能变大的区间。逐项速度、目标、实际及稳态证据上限见 [warm-burst-reachability-20260925.csv](warm-burst-reachability-20260925.csv)。教室场景100–322mm/s仍为32 DIP，400/432.376≈32.415/33.113，600/800/1000/1300≈42.155/67.133/109.347/160 DIP。

后续完整 solution 在仅本进程 `CL` 追加 `/FS`（其余 Debug|ARM64 配置不变）后退出0，最终 headless 退出0；初期未加 `/FS` 的 C1041 仍作为构建环境事实保留。专项隐藏测试只余先前四条 Window Service owner 断言，相关 Touch/ScreenPen 参数、光标/几何和面积回归未报失败；详见本轮独立 validation 文件。前述无PDB探针不再代替已执行的完整构建，但仍用于可重复的全程数值证据。
