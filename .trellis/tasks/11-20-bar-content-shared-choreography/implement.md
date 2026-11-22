# C5 执行计划

> 分工：Claude 实现与构建测试；每阶段结束由 gpt-6.2-sol High（fast，只读）审查，Claude 逐条读码核实后修复；Astra 仅在 P1 抽取规模失控时启用。
> 工具：`%TEMP%\inkeys-comp\build.ps1`（ARM64 MSBuild）、`tests.sh <tag>`（golden 对 `comp-base` + 几何 + 橡皮 + headless）、`codex-run.sh sol-read <brief>`。Sol/Astra 运行期间不并行跑 Inkeys.exe 产品测试。
> 每个阶段结束都用临时索引生成树快照（`GIT_INDEX_FILE=… git write-tree`）作为回滚点，不提交（提交需用户授权）。

## P0 复现与基线（不改产品行为）
- [ ] 新增动画轨迹基线测试入口（离屏、固定 dt），在 HEAD 录制基线文件到 `Build/ui3-trace/base`。
- [ ] 新增失败用例：预览 X 像素、More 回弹峰值且相机不变、底板与末端按钮同相位、选择收拢、分割线卡片拖入 A1/B1/A2。确认它们在 HEAD 上按预期失败。
- 验证：构建通过；golden 35/35 不变；轨迹基线可重复录制（两次逐字节一致）。

## P1 抽取共享层（真实主栏零变化）
- [ ] 1.1 显示投影与内容样式纯函数，`ApplyMainPresentationLocked` 与 `Update*ButtonStyle` 改为调用（副作用原位保留）。
- [ ] 1.2 批次决策 `ResolveMainBarBatch` 与显式参数版 `Sync*Duration`。
- [ ] 1.3 按钮目标、隐藏锚定（列表版）、底板目标、More 目标三个写入函数。
- [ ] 1.4 小尺寸提示门禁与定位共享。
- 每个子步骤后：构建 + golden 35/35 逐字节 + 轨迹基线逐帧一致；全部完成后补跑几何、橡皮、第二宿主、属性脏区、headless。
- 审查门：Sol 审 P1 diff（给“消去移动行后的真实差异”），重点核对求值顺序、观察器/脏区身份、换边与折叠分支。

## P2 预览改用共享层
- [ ] 删除 `Advance()` 自写的批次、曲线与缩放；按钮列表保留隐藏实例；先定位后推进 More，推进 X 的 SVG。
- [ ] 相机与预览区：目标包络缩放、一行 More 峰值预留、≥2 行向下扩展、下方约一行间隙、稳定裁框参考。
- [ ] 沙盒状态（默认绘制/软笔、设置可见）与内容样式。
- 验证：P0 中预览相关失败用例转绿；golden、第二宿主不变。
- 审查门：Sol 审 P2 diff。

## P3 设置侧输入适配
- [ ] 单一会话下的按下/按压视觉/越阈值交接/抬起派发；沙盒动作（工具切换、More、X、面板项、外部按下收起）。
- [ ] 悬停提示只在小尺寸。
- 验证：新增交互用例（按压、交接、重复生效防护、More 规则）；headless。
- 审查门：Sol 审 P3 diff。

## P4 组件页体验
- [ ] 垃圾桶胶囊（位置、样式、文案 `TrashLabel`，20 语言）。
- [ ] `ResolveDrop` 新增预览区外语义，提示文案对应（R5 表）。
- [ ] 分割线拖入修复（按 P0 复现结果定位）。
- [ ] 幽灵按钮主栏材质底板。
- [ ] 预览纹理 tint 透明度与页面动画位移。
- 验证：R5 表逐行用例、分割线三段、页面动画 tint；`pwsh Scripts/i18n.ps1 sync` 后补译、`check` 通过；`setting_design_tests` 通过。
- 审查门：Sol 终审全量 diff（附实测结果表与“有意未做”清单）。

## P5 收尾
- [ ] 更新 spec：`settings-component-editor.md`（预览合同改为共享编排、沙盒、放下表、垃圾桶、页面动画）、必要时 `rendering-and-ui.md`（共享层边界）。
- [ ] 全量门禁：golden、轨迹基线、几何、橡皮、第二宿主、属性脏区、headless、i18n check。
- [ ] 交用户实机验收；提交需用户授权。

## 回滚点
- P0/P1/P2/P3/P4 各自的树快照；P1 任一子步骤破坏 golden 或轨迹基线即回退该子步骤，不带病进入下一步。
