# 光标续作静态复核（2026-11-04）

## 初轮范围与结论

按 `trellis-check` 读取 PRD、design、implement、check.jsonl 与 native-desktop 的光标、输入、配置/i18n、笔宽、设置约束。当前实现中的小圆点落笔隐藏模式、配置接线、设置顺序及新增翻译未发现需要修改的明确源码缺陷；本轮未改产品代码。

初轮复核时 DPI 项尚未关闭（后续状态见下方最终复核）：用户明确确认是同一屏上的自绘“鼠标”箭头明显小于系统箭头。现有理论 DPI 链和新增 Window/Controller resolver probe 不能替代实际 GPU 可见像素范围；运行时代理正在复核离屏像素，最终结论需结合该证据。

## 已核对的数据流

1. `Other.Config.cppm`：`Drawing.Cursor.DotHideOnContact` 是 NoUpload 整数，默认 1；0/1/2/3 对应关闭/屏幕笔/数位板/两者。
2. `Other.Config.cpp`：只允许 Json int/uint、可表示为 int 且范围 0..3；bool、float、超宽与其它类型回默认，不发生窄化猜测。
3. `IdtState.cpp`：使用 `RestoreDotCursorHideMode` 后发布完整 CursorSettings；既有 State/Bridge 相等与全值复制包含新字段。
4. 原子快照保留有效位 13，新增模式位 14–15；设置变化仍经已有 SetCursorSettings 请求重绘，未引入额外线程或共享全局状态。
5. resolver：按实际样本/既有来源先选择设备行，再判接触隐藏；只有软/硬笔的 Dot/HighlightDot 命中设备掩码时隐藏。Unknown/Conflict 及 opt-in 注入鼠标延续 Tablet 行；真实 Mouse 不参加隐藏，Touch 继续独立逐 runtime 路径。
6. 选中橡皮或倒转笔先于 Dot 隐藏，Highlighter/Shape 专用外观不接 Dot 规则；非 Dot 笔设备 contact 不再受旧统一开关压制。productSettings=nullptr 独立接口保持旧语义。
7. `Setting.cpp`：草稿读取归一值，只在新事件发生时写该字段，复用原有 deferred SyncDraw3State 和冻结 QueueConfigWrite，不在 ImGui 中新增 Controller/HWND 调用。

## 设置与文案

- `RenderCursorPage` 当前顺序：四设备行 → 数位板识别 Toggle → 原蓝色 Informational Notice → 精细控制 SectionHeader → 四态 ComboRow。Notice 原说明及拆行代码未变。
- 新行无 GetCurrentInputMessageSource 时也可用；该条件只禁用原“识别驱动注入”开关，符合屏幕笔/Windows Ink 数位板仍可设置隐藏模式的边界。
- 20 locale 只增 7 个文案键，语义包含两种 Dot 样式、所选设备 contact 隐藏、hover 可见；四项顺序一致。新增英文、简/繁体、日/韩、法/德、西/葡、俄/乌等文本未见颠倒 Off/Only/Both 或误改设备行语义。未进行母语者审校。
- 主代理提供的 `Build/cursor-continuation/i18n-check.log` 显示所有目标语言 730/730、PASS；生成头及基准快照与新增结构一致。

## 测试与证据边界

- 新 resolver 测试覆盖 4 态×5 样式×6 来源行、软硬笔、contact/hover、专用外观及倒转橡皮优先；打包对全部合法设备样式与模式往返。新增配置测试覆盖默认、四态保存重启、ReadMini、严格类型、冻结副本与 NoUpload；Bridge 测试覆盖仅新模式变化。
- 新设置测试使用真实 RenderCursorPage/ComboBox 键盘选择，检查模式顺序、事件次数、无关偏好不写、非法草稿回默认及蓝色信息条相对位置；覆盖窄/宽与 locale/DPI 矩阵。最终以主代理运行日志为准。
- 本代理只运行静态 `git diff --check --ignore-submodules=all`，通过。按分工未并行编译或启动共享 EXE，没有 GUI 或设备测试，没有 commit。
- 最终交付前：更新 draw3-cursors.md（模式字段/位域/可见性/设置顺序/DPI实际结论），更新已批准的任务状态文字；引用完整 ARM64 构建、Headless、production DPI/pixel probe 的实际退出码。AC1 不能只因理论尺寸或无窗口 API 测量通过而标为视觉验收完成。

## 最终校准版本全范围静态复核

主代理请求复核后重新检查全部当前产品、测试、20语言、生成产物与 draw3-cursors spec，并读取已更新 PRD/design/implement；没有发现需改动的明确源码问题。本代理仍未编译或运行 GUI/EXE，最终运行结果由主代理补齐。

### Arrow 默认大小与像素证据

- 原生测量 JSON 区分64×64透明画布、当前真实Arrow句柄与非透明可见25×39 bbox；当前句柄匹配IDC_ARROW，System/当前monitor均192 DPI。LR_SHARED四种请求返回相同句柄的事实已注明，未将其冒充不同DPI的原生样本。
- 旧生产及完整guarded shader真实像素读回均显示四档DPI正确变化；192 DPI实际23×38，而非appearance盒的24×38。新共享盒13×19.5 DIP补足被参考轮廓最右0.95占比缩窄的可见宽度，修正有测量依据的默认大小差异，未额外乘DPI。
- Arrow配置仍由同一ApplyCursorStyle路径完成尺寸、中心偏移和1 DIP描边转换；Pen、Dot、墨迹宽度、输入样本与热点不变。Touch Arrow工厂也继续复用共享常量，未另造光标路径。
- 设置预览仍保留七点参考轮廓，X/12、Y/19归一到同一13×19.5盒；几何与生产shader参考一致，填充拆分和尖端热点未变。没有修改HLSL或guarded字节码，故无需生成新的shader资源。
- 新GPU测试复用已有无HWND UploadValidation和真实DrawTransientDrawingCursor，分别覆盖当前产品PS及完整guarded PS。192 DPI新增独立严格25×39、左上(8,8)断言，其余四档核对可见尺寸/热点。最高264 DPI的箭头在(8,8)起点加53.625像素高，仍处于64×64目标内，不会用被目标裁切的bbox冒充结果。

### 新诊断与合同同步

- frame-scale记录在原CursorDiagnosticsEnabled以及原frame key变化/active 100ms分支内，只读取既有产品Context、显示标尺和viewport字段；关闭诊断不增加采样，未引入API、锁、线程或新循环。单独短记录避免旧320字节日志挤出来源字段；其频率沿用既有frame门禁，并非无条件每帧输出。
- 模式接线、严格配置、原子位域、专用工具/Mouse/Touch排除、inverted eraser优先与设置事件写回仍与初轮复核一致。
- draw3-cursors.md现已包含四态字段/默认/位域、笔接触可见性、设置顺序、DPI基准与理论/实测区分、LR_SHARED证据限制及无窗口测试入口；design/implement已记用户批准与基准校准。

### 风险与最终验证边界

- 单机当前默认Arrow基准校准不承诺复刻用户自定义系统光标主题或辅助功能指针大小。本次已证实的差异仅2×1像素/面积约13%；这不能证明用户所见“明显小”的根因是漏乘DPI。真实同屏观感、真实运行Context和实体笔设备表现仍需用户验收，必要时用既有开启门禁的frame-scale/visual日志继续定位。
- 主代理告知首轮完整solution、20语言×三DPI设置专项、upload与pen-width均退出0；这属于校准前证据，不在本报告中冒充最终校准版本的执行结果。最终校准构建曾遇环境增量链接LNK1000，由主代理留存.ilk并重新生成；没有为了环境问题修改产品源码。
- 本代理最终运行 `git diff --check --ignore-submodules=all` 通过；最终build/test退出码、产物身份、源文件BOM/EOL由主代理统一归档。

## 最终自动验证记录收敛

已直接核对 `Build/cursor-continuation/test-results.json`：最终校准版本 cursor-gpu、cursor-ui、pen-width 均 exitCode=0。主代理报告最终完整 `InkeysRepo.sln Debug|ARM64` 退出0；该版本没有再修改产品源码。

- GPU：最终 `cursor-gpu.stderr.log` 显示 production/guarded 两条真实PS路径四档全部通过，192 DPI严格可见bbox25×39、非零alpha469像素，热点左上(8,8)。96 DPI为13×20/128像素，144为19×30/276像素，264为34×54/875像素；两shader输出一致。`[Draw3Upload] PASS failures=0 no_hwnd=1 no_swapchain=1`。
- UI：最终真实光标页Combo/事件/窄宽布局专项退出0，范围为全部20语言×1/1.5/1.875 DPI×340/780 DIP；原生数位板识别不可用时精细控制仍可操作。
- 生产probe：最终 pen-width 入口退出0，包含真实Window样式邮箱连续DPI更新到resolver的尺寸/描边/热点验证，以及既有宽度/工具锁存回归。
- 源码身份：本代理逐项重算并匹配 source-state.json 的37项当前SHA。该文件记录当前BOM/CRLF/loneLF，主代理已与编辑前文件系统备份核对格式；没有把Git归一化LF blob冒充编辑前文件行尾。
- Spec新增frame-scale记录的Enabled、原frame状态/100ms、320字节分条边界已再次核对，与产品分支一致。

完整 `--no-window` 回归目前仍等待最终退出，不在本报告中提前宣称通过。首轮沙箱在既有原子写盘处失败，主代理已留存日志、匹配PID终止后将同一binary放到沙箱外运行并越过ConfigMetadata；这是执行环境/写盘权限差异，不应修改产品逻辑绕过。最终full headless退出由主代理补充。

最终GPU校准像素验收已通过；真实GUI同屏观感、实体笔设备、Windows自定义光标主题或辅助功能指针大小仍未验证。前述“最终待运行”仅指当时的静态检查时间点，GPU/UI/生产probe的最终状态以上节记录为准。

## 最终静态复核源码快照

```json
{
  ".trellis/spec/native-desktop/draw3-cursors.md": "d594ee01cff69f74c9002a612cd80d7ca15848ab3663464e4b66240c587a4a40",
  "Inkeys/IdtI18nKeys.g.h": "eecc801aae3921d3d0b40e5b060cb3a23a1da625e7a20d2b8666004ae1903b89",
  "Inkeys/IdtState.cpp": "8c3ff7989943fdb3b5b83b05d2b7f1cc8fe9a061861a6c8bb7556a87bf181415",
  "Inkeys/Inkeys/Drawing/Draw3/Draw3.CursorSettings.h": "11d58e4a2450594c099f6ec787a4725f97b0a3e76af3b149e111c13f997e9820",
  "Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp": "a52ab5c8b9b48618a9336f3e4b05f1a2409957a433dfe18a75fff36875e93886",
  "Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp": "b43f98c4667a305902d3a6980e019b60a2d90cac377b515081a4da6bab7e1332",
  "Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp": "b69551a8bc7d3457fe9f903e7aaa9ccfa8a6449ba3b96ed9bd6797fd56f709fe",
  "Inkeys/Inkeys/Other/Other.Config.cpp": "e5d5f5ee9d2b81733f63cf368a0786e5049a212e262c4e1a2c87e8df5a0489db",
  "Inkeys/Inkeys/Other/Other.Config.cppm": "4cb595d1042063401aca5a2147de411fe4e785b4863b8fbaaed21ca9eef588aa",
  "Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp": "de05f7e2d7b5c7747c4f54d8a13c6a566604efe34dcf8b0329749032a163b7f6",
  "Inkeys/Inkeys/UI/Setting/Setting.Pages.h": "236421c40340b39a3892b0fe48da664a6f4527772189ded1a42ce3dd0f3fd628",
  "Inkeys/Inkeys/UI/Setting/Setting.cpp": "f801abc00e1215ab6b127d8f534d7f353338bc7cff1b562745082a926a66f0cb",
  "Inkeys/src/i18n/de-DE.jsonc": "a5c68695789760f931a4461eee03e425251f6db275ed3bdb5c1446c6320224ce",
  "Inkeys/src/i18n/en-GB.jsonc": "f1184af4266097aca61a22dd5dafc4d76bd1e6b92c767b78939a1d830418f173",
  "Inkeys/src/i18n/en-US.jsonc": "25ceac02647facd78fd8f39026dc73fa14155470dcc1c60e07a7f19fad746180",
  "Inkeys/src/i18n/es-ES.jsonc": "b493599432ee9bf29a37872d555fa2a4800ddaa9e71afce762bb20136472346f",
  "Inkeys/src/i18n/es-MX.jsonc": "b6dc2fa9e90840a29d8a23339aa2f6f3ea5068fff17bc8ce9d88a9cf14f3bac5",
  "Inkeys/src/i18n/fr-FR.jsonc": "02f7560f13f080d0148e17166516b29f950dad5fa9d3cd4354b93451f065d8b7",
  "Inkeys/src/i18n/id-ID.jsonc": "8386daafdcfbdd5a7a3458509569b6cc35097b226fb5ead3b4ec370029534553",
  "Inkeys/src/i18n/it-IT.jsonc": "7be0b5bc4275a28f46c01f965f369dd58750e7e74fd562c4980bc6172677e1a1",
  "Inkeys/src/i18n/ja-JP.jsonc": "a2626f66a6f7bf9a154a70fd93f82c761cb5359153d2c422e3185d2c868af8dd",
  "Inkeys/src/i18n/ko-KR.jsonc": "552bbb8524ff820e888fed66270f1930e8f726b63ccabff28c798ff9d79c17b1",
  "Inkeys/src/i18n/pl-PL.jsonc": "3902ad00d1c3ab4b2bb927976b3f4e07ca28b0bf0c05ee320428c3d42a79b8c8",
  "Inkeys/src/i18n/pt-BR.jsonc": "5123e346391c0b1ad2a24855e194b013196337556626f384950ae43cb3a0391a",
  "Inkeys/src/i18n/ro-RO.jsonc": "5b3c37270051573048b41a83e961201b98c618b24923ad3c2e12dc22c5cc339e",
  "Inkeys/src/i18n/ru-RU.jsonc": "b76f7c0a46bbf1264a067b4b89f6bd91bc92fd6766887738c985097239945cb9",
  "Inkeys/src/i18n/tr-TR.jsonc": "be48adb33221da6d4cf3f8d6c0d9600b60c83f01bd1cc3194beafcd377f7110b",
  "Inkeys/src/i18n/uk-UA.jsonc": "1e01ef5ed1db6e4e8a729cc112170292044936b2f25e8c92bfb021e95eb50163",
  "Inkeys/src/i18n/vi-VN.jsonc": "57ac5520796da835c673c644e4e7d783bf7d6d7247a42d71c5678692811d4efd",
  "Inkeys/src/i18n/zh-CN.jsonc": "51902fe0b0e5f754260907fb1a59f7a4317e7bbaf4680c3ed03dd033c9a82037",
  "Inkeys/src/i18n/zh-HK.jsonc": "bbed07f7e2ceb650ab741dcd6117d56b2b8c9b5e60db1235125e87b8c38823c3",
  "Inkeys/src/i18n/zh-TW.jsonc": "2ea9698ccf34a6f4d14dcf3abf038d0ca77e037bee9d7b7ba89319e4c0259726",
  "InkeysHeadlessTests/config_metadata_tests.cpp": "a15a6573ee272d2d8640f77e3e266b109fa0bf4f83cfa7c8c1445732330953a0",
  "InkeysHeadlessTests/draw3_bridge_tests.cpp": "4086219054e4550f57c0941559bfdbd308c73a3d2c2e914b2e547fe684da763a",
  "InkeysHeadlessTests/draw3_contact_tests.cpp": "3e3d20cc5e637fdf1c49097e84a31395500f16d8c6ccfc93ed2d66ad886d853e",
  "InkeysHeadlessTests/setting_design_tests.cpp": "b0677b9da72df90042f060d10e806062d5c100f774c0c0fd48faca74bcc69bc2",
  "Scripts/i18n.zh-CN.snapshot.jsonc": "51902fe0b0e5f754260907fb1a59f7a4317e7bbaf4680c3ed03dd033c9a82037",
  ".trellis/tasks/11-03-per-device-cursor-page/prd.md": "43b2a4608dddb8e1aff5917b24b645b7848cf616b56e8c4f5efc5e4acb67470d",
  ".trellis/tasks/11-03-per-device-cursor-page/design.md": "573e17493e7f0f33c11b387e34df4cdb55d6d8c1fe7e8401c154226130671240",
  ".trellis/tasks/11-03-per-device-cursor-page/implement.md": "92c7c947d9399c0619318b9e037e7b91bf24c2089680f43553e1cbb6bed96f45"
}
```

## 主会话最终闭环

最终完整 `InkeysHeadlessTests.exe --no-window` 在沙箱外使用同一冻结源码/二进制退出0，末行 `PASS animation correctness`，stderr无失败；最终ARM64 solution与GPU/UI/pen-width专项均退出0。37项源码SHA与source-state一致，最终完整Git状态（含子模块）和diff检查仅含本轮预期改动。验证汇总见 `research/validation-continuation.md` 与 `Build/cursor-continuation/validation.json`。未启动GUI，未commit/push/归档；任务保留in_progress等待用户实机验收。
