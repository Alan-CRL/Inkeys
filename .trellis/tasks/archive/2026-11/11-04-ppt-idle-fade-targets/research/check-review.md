# PPT 闲置淡化完整静态审阅

## 范围与结论

按 check.jsonl、PRD R1–R9、design/implement 和 native-desktop 相关规范审阅完整 diff，包含配置/快照、四分页、主栏输入/渲染、Setting 真实控件与事件、20 语言/生成项、生产离屏/headless 测试及 spec。冻结版未发现仍未处理的阻塞性代码缺陷。检查代理只读产品源码，修复均由 root/runtime 实施；本报告不表示编译或测试已通过。

## 审阅发现与已复核修复

1. **背景上限混入收展显隐**：原先直接对含 shape.pct 的 fillPct 取 min，导致主栏半收起仍停在 30%。现先对正常材质取上限，再乘 visibility，主按钮 .6↔.8 材质与 Scene 全局显隐保留各自语义。修复 `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp:2878`；实际 Shape/D2D 浅深两端与多个中间 pct 的像素回归 `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp:210`。
2. **direct reset 未贡献 dirty**：直接 SetDirect(0) 后 Advance 无法看到变化，可能留下旧淡化像素。现实际值变更时标记 MainGroup dirty 与 needRendering，见 `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:7703`。
3. **恢复与业务 frozen hit 不同源**：新逻辑原先重新命中当前几何，业务却使用 enqueue 时 frozen button。现在同 serial 循环冻结主栏/主按钮区域及实际标准主栏 button hit，准入后 Down 使用同值，其他路径重试稳定快照且主按钮用 Grip 映射。修复 `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:1145`；真实 mailbox 冻结→几何改变→消费回归 `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:2261`。
4. **配置重启抹掉 live 输入**：同一可见/同场次仅配置启用时保留 hover/press，只重新活动计时并消费旧外部序号；场景/场次进入才退休旧接触，见 `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:1311`。主栏也不因仅配置开关重置有效输入。
5. **Raw 观察坐标顺序**：现位于 sample.screen 转 layout、写 x/y 之后，Rigid 变换之前，不以默认 (0,0) 命中、不重复变换，见 `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:1364`。Finish 清 pressed，Leave 清 hover 而保留拖动按压，Cancel/溢出/退出及 deadline 单次消费继续由原 owner 处理。

## 关键合同核对

- 背景/内容分别固定 min(normal,30%/40%)，不改正常配置，不二次相乘。控件自身显隐/禁用/内容转换 alpha 仍独立；PNG 补齐显式常驻段的同一内容通道，其他 PNG 默认 C=1、p=0 不变。
- Scene 显隐与闲置进度独立，正常材质更新不覆盖投影；背景-only 复用屏蔽闲置且 Geometry/Render 一致，局部绘制参数会恢复。
- 五区域独立计时；主栏 externalTurn=false，窄快照无页码，不响应/重置于外部翻页；四分页旧外部规则和 3 秒宽限保留。
- 聚合属性、更多、子面板的开关与真实退场状态，最后保护结束重启完整计时；附属面板画在闲置 scope 之外。
- deadline 使用原 Publish/Consume、共享 worker/scheduler，无新增线程轮询；快照相关通知在原锁外，不把 COM/写盘引入渲染。
- 通用 JsonValueCodec<bool> 严格 isBool，新三字段按 schema 默认回退；Setting 按实际变化字段写回，一次保存/通知，关闭只禁用并允许全部取消。
- 新键、语言说明、生成头和源快照一致，已无 PptIdleFadeOpacity 旧使用。diff 局部，BOM 未见变化；多数文件工作区原为 CRLF，索引 text=auto 规范化为 LF，不能把 i/lf w/crlf 误判为此次格式改动。PageControl.cpp 保留原 LF。

## 验证与证据边界

- 检查代理执行 git diff --check --ignore-submodules，退出 0。PageControl.cpp 的 autocrlf 提示不是全文件 EOL 改动。
- 检查代理未编译或运行 EXE/GUI；root 正在统一完整 Debug|ARM64 Solution 构建与无窗口测试，结果须以新产物日志和真实退出码为准。
- 已有新增源码测试覆盖配置默认/8 组合/非法类型、纯计时/上限、真实 CheckboxGroup 多语言窄宽输入、Scene/主栏 Shape 像素，以及真实 mailbox/deadline。这里只核对源码，未声明这些测试通过。
- 逐附属面板真实 Office/输入设备观感、DWM/ULW 桌面合成与跨平台运行未测。纯 panelOpen 策略回归及源码聚合审查不等价于 GUI 验收；不扩大平台支持结论。

## 冻结身份

本轮 44 个跟踪改动文件的路径和 SHA-256 附在 check-source-snapshot.json。后续因编译/测试修复而变动源码时，应额外复核 delta，不沿用旧冻结身份。

## root 后续编译/测试修复复核

冻结后仅两个跟踪文件变更，root 已逐项复核；当前完整身份在 Build/ppt-idle-fade-targets/final-source-state.json：

- Bar.Scene.cpp 两处背景-only 条件表达式对 IdtAtomic<double> 显式 static_cast<double>，消除 MSVC C2445 及连锁 C2660；数值行为不变，BOM/CRLF 保持。
- Bar.EraserAttribute.Test.cpp 原有正常主题像素期望固定按 0.80，和已有 Light 默认 0.88 合同冲突；改为 GetThemeResidentSurfaceOpacity(BarMainBarFillOpacity) 后的正常预乘期望，不修改产品材质来迎合旧断言。实际离屏套件重新执行退出 0，PageControlUI3 和 EraserVisual 均 failures=0。
- 完整 solution 已实际构建成功；UI20语言专项、分页策略、配置、共享输入、离屏套件均退出 0。完整 --no-window 随后执行完成，退出 0；最终证据见 validation.md。
