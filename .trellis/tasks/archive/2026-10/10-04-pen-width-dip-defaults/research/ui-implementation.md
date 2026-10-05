# UI 实施记录

## 所有权与实际修改

- `Bar.Layout.cppm`：删除 UI 私有基础预设表；调用 Business PenWidth policy，以传入 StateMode 同代目标显示 Context 解析连续 canvas px。新增 `GetBarThicknessPresetMetrics`；preset selected 只读当前宽度槽 Selection 身份，ExplicitDip 一律不选中预设；Shape 保持独立槽。旧函数名 GetBarThicknessPresetPx 现在返回 double 连续物理像素，仅视觉消费，无任何预设值写回 DIP。生产消费者都传入同代 snapshot，兼容无参包装仍留给已有入口。
- Slider range 双精度 DIP：Brush 基础 `1/SystemScale..30`，Highlighter `min(16,1/SystemScale)..100`，动态扩大至当前有效值和当前三档 preset，不在打开时夹断。
- `Bar.Interaction.cpp`：普通笔/荧光/形状/激光点击统一 `SetPenWidthPresetIfRevision`，不写像素。Slider candidate/projection/最终提交均 double DIP；三种 shared width atomics 在 `Bar.State.cppm` 改 double。
- FineDial 精确 origin + signed half-up(delta) 个 1 DIP，范围仅真实步进夹断；独立 dirty 标记，没有调整不开盘取整、不提交。已调整 Slider 进入 FineDial 继承实际 adjustment，取消不写。FineDial→Slider 中间提交只接续自身 expected revision+1；期间新工具/显示 revision 拒绝接续。private commit test 的 setMemory=false 仅用于既有生产测试隔离磁盘。
- `Bar.RenderLoop.cpp`：逻辑 DIP 数字动画与物理 canvas px 预览动画分开，预览也用绘图目标 context。圈点保留连续像素、软笔曲线/激光芯壳比/面板展开动画/预览溢出等视觉语义；布局 zoom 不参与名义笔宽。Popup circle 将 canvas px 除 UI zoom 回布局空间，随后屏幕绘制恢复同一像素宽。
- `Bar.I18nFormat.h` / `Bar.State.cpp`：精确整数 `3 DIP`，非整数 `≈2 DIP`，sub-1 `≈1 DIP`，安全整数展示不反写。FineDial step helper 验证 origin、请求、范围有限性和范围顺序。
- 三语言 `ThicknessFormat` 加 DIP；source sync/check 和 snapshot 经脚本处理，generated key header 无内容差异。sync 首次产生 Need translation 标记，完成现有译文后 check 通过；恢复脚本在 zh-TW 顶部产生的无关注释变化。

## API 协调

使用 Core 提供：`GetPenWidthMetrics(snapshot)`, `GetPenWidthFamily(snapshot)`, `GetPenWidthSelection(snapshot)`, `snapshot.penWidthContext`, `slot.widthSelection`, `SetPenWidthPresetIfRevision(index,revision,save)`；policy 的 `Resolve`, `SystemScale`, `RoundPositiveCanvasPx`, `IsValidDip`。
没有修改 IdtState/config/Draw3/IdtMain/工程/规范或根负责的 animation tests，没有完整构建/GUI/commit/archive。

## 增补真实生产测试

既有 `BarInteractionSession::RunThicknessFineDialProductionTest` (由 `Inkeys.exe --state-mode-production-test` 调度)：
- 实际 State snapshot + Bar helper 的 1440p / 275% 连续 5.333px、UI zoom 1/3 相同 preview。
- preset identity 在 integer px 碰撞仍只选 medium，ExplicitDip 不靠值猜 preset。
- 范围包含 sub-1 DIP、新荧光细档16和合法旧大自定义200。
- 真实 preset setter → FineDial open/no-op commit 保留身份和 bridge revision。
- 小数 origin 进盘保留、±half-step 对称、cancel不写；真实 commit(false) 保存 origin+1 double、ExplicitDip；已调整 Slider 进盘的真实 adjustment 也能提交。
- rectangle selected 消费独立 shape 槽。
- 保留原过期 physics、跨代惯性、cancel后空提交、过期提交测试。
根会话在既有 headless animation tests 测 helper 的精确/近似/sub1/损坏格式与对称 step。

## 执行验证与状态

- `pwsh -NoProfile -File Scripts/i18n.ps1 sync`：首次自动 check 因新译文标记退出1（正常生成工作流），随后清理正确译文的标记。
- `pwsh -NoProfile -File Scripts/i18n.ps1 check`：PASS，en-US/zh-TW 332/332，100%。
- `git diff --check`：PASS。
- byte 检查六个 Bar 文件：原 BOM 保留，全部 CRLF，bareLF=0。
- 独立静态核查：Bar.Interaction 不再有 preset px 写入；ordinary/highlighter/shape/laser 都由 preset setter，preview 不叠 Bar DPI，数字未回写。
- 完整编译及变更后 headless/state/offscreen 运行由根会话集中执行，本文不声称已运行通过。

## 人工与剩余边界

触摸识别/hold锁定手感、PPT、不同目标显示/UI显示的混合 DPI 和 Win7 实机需人工验收。连续浮点生产宽度保持；整数仅展示、测试适配器和 dial tick 标签索引。FineDial labels 保持原整数 UI，不引入小数输入。配置迁移和首输入准备由 Core/根集成，不能仅凭 UI 测试认定全链通过。
