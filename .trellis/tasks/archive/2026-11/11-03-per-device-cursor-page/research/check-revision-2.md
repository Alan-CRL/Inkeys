# Revision 2 全范围静态复核（2026-11-04）

## 范围与结论

按 trellis-check 重新读取当前 PRD R1–R7/AC1–AC8、design、implement、revision-2-scope、runtime-revision-2、popup-revision-2 及适用 native-desktop 规范。在此前已通过的配置/按设备显隐/设置/i18n范围基础上，重点复核箭头细边和共享Combo客户区上下避让。未改产品代码或测试，没有并行运行构建、EXE或GUI。

发现的一个明确测试编译阻塞已由主代理协调测试owner完成最小修复，复读源码确认关闭；除此未发现明确产品源码缺陷。初轮静态复核时主代理尚在完整solution构建；最终执行结果已在本报告末节闭环。

## 发现及协调

- **P1：同作用域变量重定义。** `setting_design_tests.cpp` 的 `CheckBelowComboPopup` 原 line3493声明 `auto firstOpen=open().second;`，新增 line3535又声明同名lambda `auto firstOpen=[&] {...};`。两者同函数词法作用域，类型也不同，构成明确C++重定义，新增 `firstOpen()` 调用无法按预期编译。主代理已确认交测试owner将新增lambda及三处调用改名 `openFirstFrame`，只改命名、保持测试行为；随后复读确认新增lambda已改为openFirstFrame，三处调用均同步，原3493变量不再冲突，源码层面关闭；最终仍以构建结果确认。此问题未由本代理编辑。

## Arrow 细边

- 新 `CursorArrowOutlineWidthPx(scale)` 为 `max(1物理像素,0.5 DIP×scale)`，96/144/192/264 DPI得到1/1/1/1.375px。生产ApplyCursorStyle仅Arrow调用，预览AddPolyline复用同一helper；Pen/Dot/Highlight描边沿原公式。
- 已校准13×19.5 DIP盒、归一化HLSL轮廓、热点、中心偏移和实际DPI链均未改。描边在原轮廓内部计算，减细边缘不应扩大bbox。
- Controller probe预期独立写为max(1,.5×dpi/96)，contact测试使用六档独立预期并覆盖Touch Arrow；没有仅将同一helper返回值与自身比较。
- 实际GPU测试在192 DPI读回(x8,y24)黑边、紧邻(x9,y24)不透明白填充；旧2px边会让第二像素仍黑，断言具有区分能力。正式及完整guarded路径均运行同检查，原25×39与(8,8)热点断言保留。新动态结果由主代理填写，不能用旧轮bbox通过替代本次边缘像素通过。

## Combo 预算、尺寸与输入

- 共享ComboBox在BeginPopup之前计算当前viewport工作区上下预算，默认向下；below不足desired且above更大时向上，选择侧不足时固定高度并保留列表滚动。普通与显式motion使用同一popupSize/popupPosition，不新增native窗口，也没有为修复改现有API/BelowComboMotion名称。
- 普通路径去除AlwaysAutoResize，第一帧即SetNextWindowPos/Size/Constraints；WindowMinSize临时压至1×1，避免旧最小窗口撑出客户区。约束、窗口padding、退出装饰的style pushes/pops均成对，BeginPopupEx未打开时仍会回收相应style；decoration路径保持Begin/End配对。
- 显式motion沿所选侧偏移，位移以该侧剩余budget钳制；布局每帧重算，因此resize和方向翻转不会沿旧viewport绘制。余量不足时只渐变不强制平移，避免溢出。状态/透明度/selected/scroll仍用原对象，无新跨线程状态。
- 退出装饰仍带NoInputs/NoFocusOnAppearing/NoBringToFrontOnFocus且不入OpenPopupStack，实际选取仅 `!decoration` 时写回。原Escape/F4/Tab/键盘首尾、鼠标点击、列表Clipper及AllowContentDrag路径保持。
- 早退SkipItems/ItemAdd不可见与disabled/exiting仍退休motion；available<=0关闭popup，未留下另一个可接收输入的窗口。

## 测试有效性

- 新普通Combo首帧fixture保存 `popupBounds` 值快照，随后用于断言的不是被下一帧更新过的popup指针尺寸，能检测首帧溢出。实际F4开框，四项由真实NavRect取得坐标后鼠标Down/Up选择，检查每项包括最后项单次事件。
- 正常下方、打开时resize转上方、上下均不足时ScrollMax>0及End/Enter到最后项均有生产控件覆盖。显式motion保留中间opacity、动画倍率、反向、滚动退出、无输入退出及遗漏/版本失效，并增加向上退场边界。
- 实际RenderCursorPage缩成480DIP高、真实ScrollMax滚到底，打开最后的##cursor-dot-hide并鼠标点击Both，验证1次事件；接入全部locale×三DPI的cursor-only矩阵。
- popup-only同时覆盖普通/动画fixture、既有Solid/Acrylic Combo交互和DragScrolling，范围足以发现共享控件回归；最终以本轮执行日志为准。

## 全任务回看与边界

原四态字段NoUpload/严格整数默认1、Bridge/原子快照14–15位、设备/样式门控、Mouse/Touch排除和倒转橡皮优先未受本轮改动。配置、20语言及deferred保存接线未新增变化；先前完整通过记录仍保留但不冒充本轮结果。spec已同步Arrow细边与Combo预算/动画合同。

剩余验证：本轮完整Debug|ARM64 solution、GPU黑边/白填充probe、生产DPIprobe、cursor/popup定向无窗口测试退出码；格式由主代理对编辑前文件系统备份核对。用户确认大小正确属于其本轮实机反馈，细边观感及底部下拉实际操作尚需用户回核；本代理没有GUI/实体设备/Windows自定义方案验收。

## 最终执行结果与源码身份闭环

已直接读取 `Build/cursor-revision2/validation.json`、test-results.json、arrow-gpu.stderr.log及research/validation-revision-2.md：

- 最终完整 `InkeysRepo.sln Debug|ARM64` buildExitCode=0；主代理记录0 errors。
- arrow-gpu、pen-width、popup-focused-headless、cursor-ui、general-ui五项退出码全部0。
- Arrow正式/完整guarded PS实际192 DPI像素均为黑边BGRA(0,0,0,255)，紧邻白填充(255,255,255,255)，严格25×39 bbox和(8,8)热点保持；其它三档DPI也通过。因此本轮细边的自动像素验收已闭环。
- popup-focused-headless设置部分使用 `INKEYS_SETTING_POPUP_ONLY=1`，其它算法/配置/输入/Bridge/contact等仍运行，并以 `--no-window` 执行；该结果没有冒充未过滤完整设置集。原/scoped Combo首帧上下避让、resize、受限滚动、真实鼠标/键盘选择、退出装饰输入隔离及既有Solid/Acrylic/DragScrolling均通过。
- cursor-ui为全部20语言×三DPI×两宽；真实RenderCursorPage滚到底后向上打开并鼠标选择最后一项通过；general-ui覆盖通用设置和既有PPT消费者。
- i18n本轮没有新增文案，验证保持20语言730/730、退出0。
- 复读确认新增测试lambda为openFirstFrame，三处调用同步；新增选项点击坐标为optionPoint，旧firstOpen与旧point不再造成重定义/新增遮蔽。该修复仅测试局部命名，未改产品行为。
- 本代理逐项重算source-state.json的40个文件SHA并全部匹配，最终 `git diff --check --ignore-submodules=all` 通过；UTF-8/BOM/EOL按主代理编辑前文件系统备份核验，未拿Git归一LF替代原行尾。没有产品修改发生在本轮复核中。

本轮静态发现的问题已关闭，无已知未关闭源码缺陷。未进行GUI/Computer Use或实体设备验证；桌面细边观感、窗口底部上开实际体验及真实Win7仍留给用户回核。没有commit、push或任务归档，任务保持in_progress。

## 复核源码身份

```json
{
  ".trellis/spec/native-desktop/draw3-cursors.md": "8f7aeeaf13d3ad0d3199bde8b96f77912c796c397dd7e2fd32192888dba80a62",
  ".trellis/spec/native-desktop/setting-motion.md": "e4a4e322f272d8bbac8a868d63e6e6adcfb54848208b24c119ff3c28f9a8035a",
  "Inkeys/IdtI18nKeys.g.h": "eecc801aae3921d3d0b40e5b060cb3a23a1da625e7a20d2b8666004ae1903b89",
  "Inkeys/IdtState.cpp": "8c3ff7989943fdb3b5b83b05d2b7f1cc8fe9a061861a6c8bb7556a87bf181415",
  "Inkeys/Inkeys/Drawing/Draw3/Draw3.CursorSettings.h": "f63ef3a16d9d3ecba444c0ebdf98e1060954ed70a5e1d7abe167aa8a4a5b054d",
  "Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp": "73b1bff8467bab339a648bd0827ee139f0be6b7892879e152c05d15e8953c44a",
  "Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp": "4564cc625147f4f0cd255b6005c6bd65055348910246bd35ff37b5f22edd699e",
  "Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp": "6952d734651431ca7725178802cb21404ff9112d13a7bafeeff827c723eaba15",
  "Inkeys/Inkeys/Other/Other.Config.cpp": "e5d5f5ee9d2b81733f63cf368a0786e5049a212e262c4e1a2c87e8df5a0489db",
  "Inkeys/Inkeys/Other/Other.Config.cppm": "4cb595d1042063401aca5a2147de411fe4e785b4863b8fbaaed21ca9eef588aa",
  "Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp": "b9f69b25649bda3baf6a404adcda1cd6bf9c7aa691662a8ee0245ecc9894c0e8",
  "Inkeys/Inkeys/UI/Setting/Setting.Controls.h": "e61a491aedcb19e848d91d9d9ec7c97d33d6105e1b84ad7d84f98c6369509801",
  "Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp": "653feee1750ef73df583039b1a1165154ef2462018d61aacbfe6388c89fd0a03",
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
  "InkeysHeadlessTests/draw3_contact_tests.cpp": "e1493711ed5055a07f3ed2fe1b23efd33b30e1dc059ecb0fa127a3686e00c1f3",
  "InkeysHeadlessTests/setting_design_tests.cpp": "00d0aaf227b97b81f1beb7c8773a559faff95f777b2784f3acdfa4a99cb185fd",
  "Scripts/i18n.zh-CN.snapshot.jsonc": "51902fe0b0e5f754260907fb1a59f7a4317e7bbaf4680c3ed03dd033c9a82037",
  ".trellis/tasks/11-03-per-device-cursor-page/prd.md": "59e57c04ca14aa8ccddb604e5786a0975453f4f1bb8ce7e10e3e8009c603732f",
  ".trellis/tasks/11-03-per-device-cursor-page/design.md": "92d8a2da278a0410549701c979b6e6d708b5bc12c88f65547991c26183286508",
  ".trellis/tasks/11-03-per-device-cursor-page/implement.md": "320eb82d5fbc5c8baa4f12fc0bf86ca353544f87ab2d113c4f26e21f78ae6918"
}
```
