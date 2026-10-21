# 可信二维视觉追踪与特征接力实施记录

## 顺序
- [x] 读取父prd/design/implement及本任务规范上下文。
- [x] 独立VisualMotionTracker生产实现、标准图像接口、可信度和固定锚点接力，加入现有Headless真实图像算法测试。
- [x] 原Mag无callback基础真机验证已通过；本阶段独立离线矩阵6918合法帧全部prefix通过，含12屏/水平10屏/反向/静止300/微步2048/全初点离屏/干扰/失败暂停。正式worker实屏不能由此代替，仍保留父任务门槛。
- [x] 使用完整InkeysRepo.sln Debug|ARM64及定向测试验证，检查首个意义错误与退出码。MSBuild同PowerShell先规范化PATH、MSBUILDDISABLENODEREUSE=1；超时至少5分钟。
- [x] 独立trellis-check完成，确认4px误接受红例并局部修复，严格target/full回归通过，原6918合法矩阵数值未退化。

## 实施阶段结果

最终完整ARM64 solution exit0/17.61s/0warning0error，最终--no-window真实exit0/157453ms/stderr0B，含14组6918合法运动帧与全部负例；严格CLI额外参数3组合exit2/0B。源码已freeze，原BOM/CRLF、XML、diff与静态依赖检查通过。最大累计误差：静止0px、微噪声.00613px、亚像素12屏.91984px、水平10屏.35779px；全部prefix严格目标通过。当前尺寸Process P95约8.10–23.06ms，不代表真实主屏或整体FPS。

实际接口、最终参数、完整矩阵/计时/文件及日志见[research/implementation-current.md](research/implementation-current.md)。接下来独立trellis-check；不标整个产品验收完成。

## 本轮继续实施（2026-10-21）

用户已批准完成正式取帧及全部后续阶段，上一轮仅诊断的范围限制结束。本轮验证仅无窗口或完全不在屏幕显示的HWND，禁止Computer Use/前台/可见测试；必要可见验收时停止。当前授权见[implementation-authorization.md](../10-21-scroll-follow-v1/research/implementation-authorization.md)，其优先于本文旧轮次边界。原定格Mag的无callback基础验证见research/freeze-printwindow.md（捕获子任务）；后续正式路径须分别验证。本轮不自动commit/push。

## 独立Check交接

完整ARM64 solution真实exit0/18.99s(外部19470ms)/0warning0error；target exit0/90771ms；完整--no-window exit0/153486ms/stderr0B；argv3例exit2/0B。源码和工程7文件hash最终未变。4/10px静止主流下少数清楚运动正确Ambiguous；1/2px合并边界明确保留，不宣称任意混合区域可分。详[research/check-current.md](research/check-current.md)。可以进入产品实现/隐藏测试，不代表实屏功能通过。未commit/push。
