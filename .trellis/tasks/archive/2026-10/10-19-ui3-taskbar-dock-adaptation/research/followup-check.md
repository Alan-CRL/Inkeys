# Follow-up Check: 5DIP 与设备条件默认值

日期：2026-10-19。角色：trellis-check。Active task: .trellis/tasks/10-19-ui3-taskbar-dock-adaptation。

## Findings (fixed)

- 本次检查未发现需要 reviewer 直接修复的源码、类型、测试或合同问题；未修改产品源码或测试。
- 审阅前主会话发现的 DPI 失败诊断时序已由实现代理修正，本次核对当前源码确认修正成立：Mini 前仅保存 EnsureProcessDpiAwareness 结果，失败不执行 early Display，原 tracker 建立后的 0xD001 fatal 分支与信息仍在。

## Findings (not fixed)

- 无尚未修复的功能或规格偏离发现。
- 验证边界：真实任务栏升起/视觉、触控设备预览延迟、Win7/多屏硬件时序未做 GUI 验证；本轮没有权限启动 GUI。触控设备在 Mini 前承担现有一次 Display 枚举耗时属于已选实现和已同步合同，不能声称零启动影响。
- 本次检查按主会话分工不重复构建或运行 EXE；新源码的编译/类型检查、Headless 与产品 config-boundary 实际结果由主会话统一执行并记录，不能引用追加调整前的绿色结果。

## Review evidence

1. 已读取保存的完整 hook 输出，并显式补读父任务 PRD/design/implement、check.jsonl、followup-device-defaults/followup-dock-implementation/followup-default-implementation、相关 Bar/任务栏/双画布研究，以及 rendering-and-ui、configuration-i18n-and-assets、startup-preview、native-session-ui3、draw3-integration、native/runtime-and-rendering 的相关合同；注入截断未被视为完整规格。
2. Display.cpp::ResolveDesktopBottomDockLine 的 top/left/right 和 bottom autoHide Hidden 复用 screenInsetDockLine=bottom-lround(5*effectiveDpiY/96)，未乘 UI 基准或用户倍率。普通底部/autoHide Raised 仍用稳定 dockTopPx；Unknown/无任务栏的既有回退保持；ShouldProtectTaskbarBottomEdge 仍仅 bottom&&autoHide。
3. Bar.Initialization 发布该线到完整偶数 serial；CurrentBarBottomDockLine、输入 tracker/blocked-release 和 StartupPlacement/StartupPreview 消费同一线。ResolveBarBottomDockLine 的 scene inset 仍优先，PPT/Whiteboard 5 屏幕 DIP 不受桌面线覆盖。可见边框由 body+stroke 半高对齐，不按透明 HWND 包络测量；两角浮动保持既有 workArea 逻辑。本轮没有改 Window 或画布输入/全屏策略。
4. 配置纯分类沿用 READY integrated/external touch + 尺寸 unavailable 或面积>=1200cm²，阈值含等号；面积用宽整数避免无关溢出。constinit atomic false 缓存不会产生静态硬件初始化；schema 默认 getter 只读缓存。生产唯一 seed 位于 Mini 对象构造前，ReadAll 会先 ApplyDefaults，覆盖全局配置静态构造时的安全 false 后再读显式值。
5. READY touch 在 DPI awareness 成功后提前一次 Display::Initialize；其内部不需 COM、不创建 observer HWND，EDID/拓扑/物理尺寸可靠性仍由原 Refresh 枚举负责。原完整初始化位置复用返回值及快照、不重复枚举，并保留日志与 DisplayReady 进度。失败按原 unavailable 大屏缺省，普通设备保留原显示初始化时机。
6. 传统 PaintDevice/liftStraighten 默认赋值使用同一冻结结果；ReadSetting 的保存值覆盖仍发生在新配置 ReadAll/冻结副本之后，不回写分类缓存。没有使用用户可变 PaintDevice、创建 PC UI 样式或更改 Zoom2 迁移规则。
7. 原严格 bool codec、ReadImpl 默认再文档、ReadMini 选中路径过滤、Reset、值复制/冻结写回及 NoUpload 合同保持。新增 Headless 直接调用生产分类和 Config，覆盖两种默认、缺文件/缺键/非法类型/显式两值、Mini/All/Reset、相反 PaintDevice、复制写回/NoUpload；RAII 恢复 cache 和 PaintDevice。生产 --config-boundary-test 同样覆盖两种 seed，早于正常启动/GUI并自行恢复 seed。预览遗留 fixture 与冻结默认一致。
8. display_tests 使用固定 96/120/144/192 DPI 对应 5/6/8/10px 期望，覆盖 top/left/right、autoHide 双值、负原点及负 bottom、另有 workArea 缺口；底部 Hidden/Raised/常驻和 Unknown/no-taskbar 原回归保留。未用测试内相同计算公式生成期望。
9. 当前配置与启动 spec 已明确冻结设备默认、保存值优先和触控提前枚举例外；渲染 spec 已同步 top/side 5DIP。无需新增本地化键或工程文件。

## Verification

- Lint/static: git diff --ignore-submodules=all --check exit 0；仓库没有适用此 C++ 增量的独立 JS/TS lint。七个增量源码文件 UTF-8 严格解码成功、原 BOM 状态保持、均仅 CRLF；可用基线逐一比对 BOM，无全文件换行变更。
- TypeCheck: reviewer 未独立执行；由主会话唯一完整 InkeysRepo.sln Debug|ARM64 构建验证，结果待其统一记录。
- Tests: reviewer 未执行 EXE；新增 Headless --no-window 与产品 --config-boundary-test 待主会话记录实际 exit code。源码审阅不能替代这两次运行。
- GUI/Computer Use: 未执行；未 commit/push。
