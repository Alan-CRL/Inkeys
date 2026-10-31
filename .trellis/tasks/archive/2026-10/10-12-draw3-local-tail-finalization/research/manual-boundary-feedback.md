# Research: 人工验收触发边界反馈

- Query: 近竖直 1/7 不必要平滑、小折线 6 漏修的调查。
- Scope: internal
- Date: 2026-10-13

## Findings

### 文件与合同

- `Inkeys/additional/ink_stroke_modeler/terminal_tail_reconstructor.cc`：自适应检测、拟合及拒绝条件。
- `C:/Users/alan-/AppData/Local/Temp/Inkeys-Draw3-tail-2484.log`：现场 62 条记录；头部 build Oct 13 2026 10:48:48，exe 指向 stage3-main-native ARM64-Debug 当前产物。
- `.trellis/spec/native/terminal-tail-reconstruction.md`：触发应为可见横向折线，位移仅诊断，保留真实角点和有界窗口。
- 已阅读 workflow、prd、design、implement；遵守 researcher 隔离，未读 implement/check jsonl。

### 已确认的检测缺陷

检测在 cc:1105-1115 从末端逆扫，最多占输入弧长 35%；角大于 activation_angle 才设置 anomaly，遇到超过 activation_angle*0.2 的渐变方向则停止。cc:1138 使用 anomaly*sin(min(turn,pi/2)) 估算可见性。角超过 90 度时恒乘 1，因此几乎沿原路径倒退的 pin 被按整段长度计为横向可见。接近 180 度时该量与真实法向距离显著不一致。

主会话现场解析结果：第 55 条 applied，173.3 度、pin 4.006 px，真实横向约 0.469 px，低于产品最低 0.5 px；当前式仍按约 4 px 触发。其最终扩窗 70.767955 px、位移 5.297243 px。第 62 条 applied，166 度、pin 9.889 px，真实横向约 2.388 px；不能一概排除所有钝角，因为这一条横向仍可见。上述几何数值由主会话计算，本研究验证了源码机制与日志状态。不能从顺序直接断言第 55 条就是截图的 1 或 7。

### 小折线未修复的不同路径

- cc:1135 raw 与模型尾部的距离<=visibility*0.25 时提前 unchanged。这里参考点到任意 raw 线段的最近距离，没有当前这一步的严格时序分支匹配。
- cc:1138 低于可见阈值提前 unchanged；reference turn 非零、attempts=0 是这一分支的证据。日志中存在 1.207、1.515、1.565、0.528、0.429 rad 的此类记录。
- 检测没有找到符合条件的末端突变也会落入 unchanged，turn=0；与 raw 一致提前返回同样可能显示 turn=0。当前原因不能区分二者。
- cc:763-768 修正段内任一选中 raw 三点角超 max_raw_corner_angle，立即 sharp_corner。cc:1205 对该原因不再扩窗。日志确有多笔 sharp_corner。
- excessive_turn 可来自导数退化、数值曲率、总转角、反曲、保持正常回弯、半空间或手柄检查，不应只理解为终点角大。日志有多个尝试后此原因；目前不够细。
- 日志还存在 prefix_intersection_rejected：求解已 Apply，但产品稳定前缀相交检查未发布。数字 6 自带环与交叉，因此这是必须核验的候选原因，不能看到小钩就默认阈值过高。

### 最小后续计划

1. 先将近共线倒退、斜向可见回拉、正常直线做对照，修正真实横向量的测量；不可直接把所有大角度排除，也不恢复最大位移否决。
2. 将 unchanged 分成无异常、raw 一致、低于可见度；excessive_turn 分具体检查；同时记录检测索引、实际横向、阈值、pin 长。诊断仍有界。
3. 现场回放按几何画图定位实际 6；分别处理小可见钩、真实 raw 角、原有自交与新生自交。只有已证明的误拒才改保护，不能全局降低阈值或关闭角点/相交检查。
4. 保留原 8 笔、内侧扩窗及自由末向回归，增加正常竖直/近共线回退 NoChange 与可见小钩 Apply 的尺度对照。

## Caveats / Not Found

未建立截图笔划与 62 条记录的可靠一一映射，因此小钩 6 唯一原因未确认。未修改算法、未运行 GUI、未重新构建。外部资料不需要；本轮是内部实测和源码调查。阶段三继续 in_progress，人工验收未完成。
