# Lazy-face 初始化探测调查

2026-11-02，固定 ImGui 1.92.7。imgui_draw.cpp 在构建空白字形时探测 TAB，在 special setup 时探测 FFFD/ellipsis。粗 Unicode 块存在字符，不代表这些具体探测字符存在。新增精确位图使用验证时的真实 cmap，不能人为假定缺字。

成功 special-cache build 的窄 trace 位于 `.trellis/.runtime/i18n-validation/dynamic-fonts-settings-stderr.log`：所有 SC/TC/JP/KR 补缺源反复跳过真实缺失的 cp=9，主源跳过缺失的 FFFD。唯一记录的实际延迟打开为韩文 U+D55C regular，来自混合母语名称，属于必要资源。故延迟机制已经生效，残余内存并非所有 fallback 被无效探测重新打开。

同一次暖态访问路径的实际数据：

| English 100% 页面 | stb 总字节 | FT 总字节 |
|---|---:|---:|
| 语言 | 3,222,190 | 3,304,269 |
| 基本 | 3,540,070 | 3,622,215 |
| 主页 | 4,532,613 | 4,614,386 |

语言页 FT live 为 270,534。以上仍不满足内存门槛，不是通过证据。此前更短访问路径的基线更低，不能跨工作负载混算。

每源的 544 B 保守块图与 8 B 精确控制/特殊字符位图都纳入 ImGui 分配统计。精确 Contains 正例无需打开 face，实际 glyph load 仍使用 FreeType。临时 getenv_s/fprintf 及附加 include 已删除，热路径无诊断日志。

已建议根会话评估规划中的字体专用 R8 GPU 路径：当前尺寸 RGBA 262,144 → R8 65,536，理论差额 196,608 B，必须实测并验证像素等价。若保留简单的 eager shared face 即可达标，应考虑撤销复杂 lazy-face 部分，降低第三方维护和冷初始化成本。本 agent 未自行回退。

## 最终简化决策

根会话在 Alpha8 CPU/R8 GPU 实测英文暖态路径通过后，选择撤销本调查的 lazy-face、544 B 覆盖块和精确探测缓存。保留同 atlas 的 FT_Face 共享、按需 merged FT_Size、真实初始化验证、分配失败保护及整帧错误标志。此决定不删除任何 fallback，也不改变字形或预算；它减少第三方维护和冷路径成本。

本文件前面的 trace 与失败数据属于历史试验，不是最终源码状态。简化后的生产生命周期测试改为真实首次 merged FT_Size 分配失败，而非 lazy-face 打开失败。简化后仍须重新执行相同页面和访问路径的内存门槛；R8 的既有通过结果不能替代新版本复验。
