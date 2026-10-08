# 本轮独立实现审查

Active task: `.trellis/tasks/10-04-pen-width-dip-defaults`；基线 `79e5fe0835b39c482cd7c87724c4a8ffa21e2ed8`。
角色为已派发 trellis-check，直接审查/局部修补，未递归派发。读取 native hook 完整保存文件、check 上下文及当前 PRD/design/implement，最新 followup-scope 覆盖上一版本显示/启动恢复合同。主会话拥有完整构建和运行验证；本代理未启动构建、GUI、commit 或归档。

## Findings (fixed)

- File: `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`
- Issue: 主栏整数文案已改，但候选浮窗在 AdvanceAnimationsAndDeriveLayout 中仍把 `L" DIP"` 拼接进 previewText，随后直接赋给 popupNumber；damage envelope 测量也仍包含旧单位。仅测试 formatter 不会发现此真实调用者残留。
- Fix: 去除两处后缀，并将相邻旧近似提示注释同步为整数文字；不改变保存/业务/绘制单位。

- File: `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`
- Issue: 活动预览候选改为 SetDirect 后，core/outer 的 IsSame 为 true；真实 frame 的显式 MarkChanged 手势名单遗漏 thicknessPreviewDragging。不能再依赖旧 SetTar 动画来标记属性组，尤其小数改变但整数文字不变时需要明确呈现保证。
- Fix: 将 thicknessPreviewDragging 加到已有 needRendering/MarkChanged(drawAttributeDirtyKey) 手势名单。保留原动画、需求兜底、Hold 和 revision 行为。未为这一行复制一套虚假 frame 测试。

- File: `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`
- Issue: 新 production probe 直接调用 owner 的 protected CloseThicknessSlider，完整构建产生 C2248；范围内测试源码错误。
- Fix: 进一步核查发现 probe session 的 memberAccess 是空对象，不能使用私有 wrapper。改用已有 public CollapseAuxiliaryPanels(true)，其内部调用真实 CloseThicknessSlider，并恢复测试面板 fixture；不增 public/friend 或放宽接口。源码 gate 在生产 probe 运行前发现空指针隐患，未运行有隐患版本。

## Findings (not fixed)

无待修复的范围内源码问题。真实触摸硬件体验仍待本轮人工确认，这是验证边界，不是发现的代码缺陷。

## 实际路径核查

- Core：唯一生产 InitializePenWidthPreferences 保留目标 Context 准备，以注释禁用启动 Decode/flags/GetMemory，并首次 Restore({}, {}, {}) 使用 Medium。后续先 ApplyContext 再一次性 guard；显式会话值不重置。旧 GetMemory 仅有未编译 Draw2 调用，Draw3 文档/uink 恢复未修改。
- CaptureSettingJson 解码只用于保留原兼容/迁移数据，未用于 runtime 恢复；future schema/AutoClean 安全保存合同仍由真实 fixture 覆盖。
- UI：directTouchPreviewGesture 超过 slop 后 ProjectRelativePreviewWidth -> ApplyCandidateWidth -> 实际 session PublishThicknessSliderCandidate -> UpdateRendering。RenderLoop SubmitTargetsAndLayout 在同帧取得候选 metrics，数字与 core/outer 使用 SetDirect；随后 AdvanceAnimationsAndDeriveLayout 显式标记属性组，实际绘制读取 core/outer.val。无逐帧业务/配置提交。
- 非交互按钮/程序化切换仍 SetTar；Laser shell/core Hold、soft morph、锁定、取消及 revision 提交保留。候选提交才变 ExplicitDip。
- 三语言源 ThicknessFormat、损坏格式 fallback、预设数字及候选浮窗均只显示安全正数四舍五入整数。sub1 最小显示1，显示值不回写。
- 真实 Bar probe 调用 session 发布/公共 panel 关闭取消及共用 production metrics/动画 seam，检查 consecutive fractional val、业务/bridge 不变、锁定、取消和过期提交；覆盖 SoftPen/HardPen/Highlighter/Laser。没有驱动真实触摸消息和完整 coordinator frame，实际 frame 连接以源码确认补充，不冒充硬件验收。

## Verification

- 局部 `git diff --check -- Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`：PASS。
- 编译/类型：主会话 `followup-final-build.result.json` exit0，20.765s，完整 InkeysRepo.sln Debug|ARM64、原生 ARM64 MSBuild、PATH规范化、本次 LinkIncremental=false；此前 C2248 失败及中间可编译但空 memberAccess 隐患均已修复，没有运行隐患版本。
- Tests：`followup-headless.result.json` exit0，95.178s，`InkeysHeadlessTests.exe --no-window`；stdout 最后 PASS animation correctness。
- Production：`followup-width-production/state/config/draw3-width.result.json` 均 exit0。width/state dispatch 实际调用 Bar RunThicknessFineDialProductionTest，stderr PASS，不因 stdout 仅打印 codec/state 而漏掉该接缝。
- UI offscreen：`followup-bar-offscreen.result.json` exit0，29.932s。无 HWND/真实触摸体验推断。
- Lint/资源：无独立 C++ lint 命令，使用完整编译与 diff check；`followup-i18n.result.json` exit0，332/332 两语言 PASS。独立 reviewer 再次 git diff --check exit0（仅任务文档 LF 提示，无 whitespace 错误）。
- 编码/范围：已读取 `followup-format-check.json`，11源码 BOM保持/bareLF0；preexisting_changed=[]，原41项未修改。reviewer另对4个修改源码 byte-check 确认原BOM/CRLF。
- Spec：pen-width/config规范已按本轮用户合同同步，包含实际 helper 参数顺序及 Preview SetDirect 后必须 MarkChanged 的真实 frame 要求。

最终独立 gate：**PASS**，所有范围内已发现问题已修复、主会话最终编译/自动运行通过。真实触摸跟手体验须本轮人工确认；历史上一轮人工通过不替代本轮体验。任务继续 in_progress，未提交/归档。
