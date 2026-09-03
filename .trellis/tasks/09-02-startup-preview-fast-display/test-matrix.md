# 测试矩阵

## 1. 纯逻辑与格式

| 场景 | 自动化断言 |
| --- | --- |
| Tracker 顺序/乱序/重复/并发 | work units 只增加一次、snapshot 单调、无 data race |
| Conditional plan | Preview off 不含专属 milestone；不存在 skipped-as-complete |
| 时间流逝 | 不报告 milestone 时 actual ratio 不变；Preview 显示后的 3 秒只改变 visibility |
| Failure freeze | 首个 fatal code/ratio 固定；后续报告不增长；不出现 100% |
| 100% gate | 即使先行 milestone 全部完成，Bar committed 前仍 <100%；该 commit 后恰为 100% |
| CRC | 标准 `123456789` IEEE CRC-32 向量和 header-crc-zero 规则 |
| Parser 截断 | 从 0 到 header/payload 各边界短读均 Corrupt、无越界 |
| Integer attack | width/height/stride/payload 的乘加溢出、>8192、>64MiB 拒绝 |
| Geometry | 零/反向/越界 progress rect、monitor/work/offset 溢出拒绝 |
| Version/classification | Missing、Valid、epoch/signature/DPI geometry Incompatible、CRC/结构 Corrupt |
| Serializer roundtrip | 每字段 little-endian、160-byte header、exact file length、无 padding |
| State machine | Valid、Missing/Incompatible、Corrupt、bypass、fatal、stop 的合法/非法 transition |
| Alpha reducer | requested/attempted/committed，四步 present 任一失败不推进，alpha change 强制 full dirty |

## 2. 构建与静态兼容

| Matrix | 要求 |
| --- | --- |
| Debug/Release x Win32 | 完整 `InkeysRepo.sln` 构建；headless `--no-window` |
| Debug/Release x x64 | 完整 Solution 构建；作为开发主验证配置 |
| Debug/Release x ARM64 | toolchain 可用时完整 Solution 构建；不能仅编译 Inkeys.vcxproj |
| ARM64EC | 仓库无配置；审计 `_M_ARM64EC`、pointer width、序列化固定宽度和静态 API，不新增承诺 |
| Imports | dumpbin/静态检查不得新增 Win8.1+/Win10 API 的加载期依赖 |
| Source hygiene | `git diff --check`、CRLF/原编码、无 GDI+/WinUI/DComp、资源 ID 无冲突 |

## 3. Win7 SP1 + KB2670838 / WARP

- 干净 Win7 SP1 + KB2670838 VM，禁用硬件 adapter 或强制 WARP，确认 FL11.0 fallback、D2D1.1 GaussianBlur、FillOpacityMask 和 layered ULW 正常。
- 无 Shcore 路径走动态 load failure + `SetProcessDPIAware()`；进程不因缺失导入而加载失败。
- FL11_1 首次请求返回 `E_INVALIDARG` 时只用 FL11_0 重试并成功。
- blur/shimmer effect 注入失败时仍显示普通 preview/正式 Bar；共享 WARP/pipeline 失败保持 fatal popup。

## 4. DPI 与多显示器

| 场景 | 验证 |
| --- | --- |
| 96/120/144/192 DPI | embedded cubic/blur 不裁边；progress geometry 合法；最终真实 Bar 清晰 |
| 两屏混合 DPI | cache geometry 只在兼容 monitor/DPI 使用；否则 Incompatible |
| 负坐标/上下排列 | signed offset/anchor 无 overflow，Preview 与 Bar screen destination 一致 |
| 启动时主屏变化/拔插 | 旧 revision move 丢弃，按最新 Display snapshot 重定位或安全 bypass |
| awareness 已预设 | `E_ACCESSDENIED` 不被当作失败，不重复调用错误 fallback |

## 5. SuperTop、焦点与 Z-order

- SuperTop 关闭、父进程提权、helper、`-SuperTopComplete` 最终进程：只允许最终进程出现一个 Preview、一个 T0。
- Preview 点击/双击/右键不激活、不穿透到 Bar、不改变前台窗口；Alt-Tab/任务栏无普通入口。
- 初始 refresh、周期 TopWindow、PPT visibility refresh 后 Preview 仍在 Bar 上方；observer 注销后不再访问 Preview。
- owner thread 停止时所有 hide/destroy 均在创建线程；模拟 late post 不产生 use-after-free。

## 6. Cache 与资源

- 无 cache、Valid、epoch/signature/DPI/geometry Incompatible、CRC Corrupt 分别走指定动画。
- 文件 0 byte、header-only、payload-short/long、尾随数据、非法 reserved/pixel format 和 64MiB+ 均安全拒绝。
- cache 目录不存在、只读、磁盘满、temp 冲突、short write、FlushFileBuffers/MoveFileEx 失败均不阻止主程序并保留旧 cache。
- 连续 visual revision 只让最新 revision 替换目标；退出时无 temp 悬挂和后台线程访问已销毁状态。
- capture mode 在 96 DPI 干净配置重复两次得到相同 header/signature/像素 CRC；生产 parser 反读通过，RC resource byte-for-byte 相同。

## 7. 进度与动画

- 人为延迟各启动阶段：进度只在真实 report 后增长，停顿阶段保持真实值；3 秒从 Preview 首帧 committed/show request 计算，重复通知不能重置。
- Preview 显示后 2.9 秒成功不显示 progress；3.0 秒后约 180ms 淡入；已显示时成功先达到 100%、满格保持 300ms、再隐藏并 handoff。
- fatal 分别发生于 progress 显示前/后：立即红色，保留实际 fill；较早提交时在 350ms 总预算内保留红帧，未提交则到预算后 popup。
- 96/120/144/192 DPI 检查以完整主栏（含主按钮）为基准的 X/Y 居中、192 DIP 宽度、目标允许时 48 DIP 双侧边距、3 DIP 指示条覆盖 1 DIP 轨道以及 normal/error 颜色。
- 两个 layered HWND 不做中间 alpha cross-fade；抓帧检查 Valid 路径无 0.5+0.5 合成加深。
- Missing/Incompatible 高模糊换 proxy 后 320-420ms deblur；Corrupt 必须先 Preview 全透明、40ms hold、再 Bar fade，且不 deblur proxy。

## 8. Bar/Window/Draw3 失败注入

| 注入点 | 预期 |
| --- | --- |
| Bar window missing / Register client fail / stop-before-ready | 显式终止状态、Tracker fatal、无 100%、无 cache |
| GetDC/ULW/ReleaseDC/EndDraw 任一步失败 | present not committed、old committed alpha、full retry、业务 dirty 不误推进 |
| Proxy/staging copy fail | Bar 可 committed；本帧不发布 proxy/cache，等待下一帧 |
| Window overlay/setting owner fail | 对应真实阶段停止，red progress + popup，有界清理 |
| Draw3 各子阶段 fail/fallback | 只报告已完成子单位；最终失败冻结，不补齐 Draw3 权重 |
| Freeze ULW fail | 不报告 Freeze ready；TopWindow 消费 failure/timeout 而非假 ready |
| PPT UI client fail | 显式失败；不把后续 Office 连接误当启动进度 |
| Device loss during preview/handoff | 旧 epoch 全清，恢复后重建；超限时安全 reveal Bar 并销毁 Preview |

## 9. Acceptance Mapping

- PR/AC-01..05：tracker、config、SuperTop、RenderPipeline tests。
- PR/AC-06..11：format/cache/capture、alpha reducer、三动画与 failure injection。
- PR/AC-12：完整架构构建、Win7 VM、静态 import 和人工窗口矩阵。
