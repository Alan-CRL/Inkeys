# 第九轮验证记录（主会话独立复核）

## 范围
R1 通用主栏按钮角标 + 主栏几何按钮接入；R2 几何面板长按固定（0.5s 延迟 + 1.5s 进度环，共享 ButtonFrameRing）；R3 收窄为固定时编辑结束不恢复入口工具（Draw3 不改）。

## 独立审查后的修正
- 进度环出现后因移动超阈值中止：同时置 pressCancelled_，抬起不再退化为短按选中。
- Up 先于到期唤醒处理且已满 2s：Up 内先调用 ConsumeHoldWake 提交。
- 固定时不消费 shapeReturnMode：取消固定后的下一次结束编辑恢复入口工具（工具状态测试已改为断言该行为）。
- 低优先级三项已于第二批修复（见下节）。

## 结果（最终二进制，Build/shape-round9/verify3）
| 测试 | 结果 |
| --- | --- |
| InkeysRepo.sln Debug/ARM64 | 退出 0 |
| --shape-tool-state-test | 0 failures |
| --ui3-geometry-attribute-test | 1716804 checks / 144 frames / 0 failures |
| --ui3-shape-editor-test | 544 scenes / 317275 checks / 0 failures |
| --shape-editor-native-input-test | 37 checks / 0 failures |
| --ui3-input-worker-test | 退出 0 |
| --ui3-multitarget-benchmark | 退出 1，每轮 1–2 个 culled=0 失败 |

## 多目标基准对照
同机在基线 ea2e5a08（本轮源码逐字节备份后临时切回，构建退出 0）上运行同一基准：退出 1，每轮 1–2 个 culled=0 失败，形式一致 → 既有问题，非本轮引入。修改源码已逐字节恢复（cmp 一致）。
注意：首次恢复用了 cp -p 保留旧 mtime，导致增量构建未重编译（verify2 实为基线代码，作废）；touch 后重建得到 verify3。

## 未覆盖
- 角标与长按手感、触摸实机表现未验证；未启动交互 GUI。

## 第二批：低优先级修复（Build/shape-round9/lowfix，二进制 12:56:50 晚于最后源码修改）
- 提交被拒：保留 revision 守卫，按"返回 true 或 revision 恰好推进一次"判定接受（直线重复长按返回 false 但属已接受）；`holdAccepted_` 为真才定格满格，否则环停在当前进度并淡出。原方案的 `Haptics::RejectButtonAction` 经核实只撤销点击作用域内的接受触觉，不产生拒绝反馈，仓库无主动拒绝震动接口，故未调用。
- 角标独立 `angle` 动画值，中途反向连续；测试断言反向帧角度不变，后续单帧变化 ≤ 参考步长 2 倍（反向行程最长约 90°/参考 45°）。
- 新长按开始时在 render 线程清 `ringFailed_` 并重置路径缓存。
- 补测：过期 revision、角标双向反向、注入环失败后重试、环出现后移动中止、2.0s 后 Up 先于唤醒只提交一次。

| 测试 | 结果 |
| --- | --- |
| InkeysRepo.sln Debug/ARM64 | 退出 0 |
| --shape-tool-state-test | 0 failures |
| --ui3-geometry-attribute-test | 1716841 checks / 144 frames / 0 failures |
| --ui3-shape-editor-test | 317275 checks / 0 failures |
