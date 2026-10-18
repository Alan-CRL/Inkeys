Win7 WARP 主画布兼容修复与对照诊断（单 CMD）

正常使用：完整解压新包后，直接双击 Inkeys.exe。实际 Win7 WARP 设备默认使用已通过 Aero 开/关两次实测的完整 guarded shader。
其它系统或 Hardware 设备默认保留原版 shader；这份新 EXE 仍需正常启动回归，已有实测验证的是同一修复字节码。
需要继续定位或验证 VMware Hardware 时，再运行下方唯一 CMD，比较同一 EXE 的原版与完整修复版。
候选保留 DefaultInk 全部计算、细笔覆盖率补偿、变粗细、抗锯齿、光影及双源 Add/Retain 公式。
候选只把默认笔输出放到明确的正向控制分支，并保留未知 type 的旧输出边界；不删功能，不降低画质。
自检里已有的删除功能群变体仅用于隐藏 GPU 定位，正常应用两轮不会选择它们。

1. 对照诊断：在 Win7 上完整解压到新目录，先正常退出已运行的 Inkeys，再双击唯一入口 Run-Win7-Diagnostics.cmd。
   不要只复制 EXE 或混用旧脚本；diagnostic-manifest.json 会核对 EXE、版本和 105/69/18 用例身份。
2. 01-pixel-probe 自动执行 WARP 与独立 Hardware：每个后端最多 15 分钟，没有人为等待。
   每个可用后端保留原 87 项，再执行 18 项 guarded 候选检查，共 105 项。
   旧 Win7 上原版 FAIL 是已知对照；脚本继续两轮应用。Hardware unavailable 不当 PASS。
3. 02-original-shader 显式强制原版，暂时绕过自动修复；旧 Win7 WARP 仍不出墨是已知故障对照，Hardware 请按实际观察记录。
   新建空白测试页，先普通笔约 5 秒，再细笔、粗细变化、荧光笔，等待 3 秒。
   之后测试橡皮和激光各至少 3 秒、撤回/重做、切选择模式后点击桌面、定格开启/关闭后续画。
   不测试时注明未测。正常退出应用后，在采集窗口输入本轮观察。
4. 03-guarded-shader 显式启动完整 guarded 修复版，用另一空白测试页重复相同步骤。
   请记录是否立即出墨、是否切橡皮才刷新、橡皮及激光是否可见，以及细笔/变宽/荧光笔是否保持预期效果。
5. 回传脚本显示的整个 Win7-Diagnostics\时间目录，包含三个阶段、summary.txt 和 observations.txt。
   失败、不完整、unavailable 都有定位价值；不要只回传一个 console 或部分目录。

两个应用阶段只切换 shader 选择，ULW 强制全量开关始终关闭，输入、交换链与呈现策略保持一致。
脚本临时启用 Draw3/Cursor 日志，并依次指定 shader 覆盖值 0/0/1，不写诊断开关到配置，完成后恢复调用者原环境。
只有 diagnostics 精确为 1 时，guarded 精确为 0/1 才覆盖默认策略；未设置或其它值沿自动选择，普通启动无需环境变量。
正常应用仍按其既有方式保存你创建的测试页/笔迹；建议使用专门测试页。

summary 的 guarded-status=PASS 只表示 18 项自动候选检查通过，不能替代实际画布与视觉效果观察。
若原版参考图在 Win7 为透明，reference_status=unavailable/comparison=unavailable 会如实保留，不能称像素相等。
采集完成、应用 exit=0 和自检基线退出码分别有含义，任何一个都不代表整机 Win7 验收通过。
两份 Win7 实测已经恢复主画布、橡皮和激光；定格后荧光笔偶发加深与 Win7 Hardware 路径仍待独立验证。
本机 Win11 验证不替代 Win7 WARP/VMware 实测；请分别返回每台设备的完整目录。
