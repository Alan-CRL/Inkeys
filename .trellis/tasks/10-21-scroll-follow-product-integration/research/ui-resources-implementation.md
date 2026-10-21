# 视图调整配置、文案与资源实施记录

- 日期：2026-10-21。
- 范围：本任务独立 UI 资源侧；Core 统一拥有按钮实际注册/运行时投影、业务入口、工程/runner 和完整 Solution 构建。
- 用户授权：只做无窗口或真正不可见 HWND 验证；本侧没有创建 HWND、使用 Computer Use、启动普通应用或执行 commit/push。

## 实际实现

- `Inkeys/Inkeys/UI/Bar/Bar.Button.cppm`：在 `Pierce` 后追加 `BarButtonPresetEnum::ViewAdjust`，保留旧枚举值；唯一 preset 容量由 40 改为 41。
- `Inkeys/Inkeys/Other/Other.Config.cppm`：新增稳定官方 `BarButtonId::ViewAdjust = "Inkeys.Bar.ViewAdjust"`；默认 B 为 `MoreBoundary, ViewAdjust, Setting`，新项默认 `twoTwo/Visible=true`。
- `Inkeys/Inkeys/Other/Other.Config.cpp`：匿名函数 `TryAddMissingViewAdjustButton(Json::Value&)` 使用生产 sequence codec 验证现有 B 数组，随后直接在 JSON 原数组中插入缺项。已有 ViewAdjust 完全不改；缺项插在首个 Setting 前，Setting 不存在时追加。其它项及其顺序、Size、Visible、未知插件额外字段原样保留，不规范化或重置 A/B。
- `Config::ReadImpl`：先做既有旧 `ButtonLayout` 拆分，再执行上述补缺；任一迁移发生时直接调用既有原子写盘器写回原文档。`ReadAll`、选中 B 的 `ReadMini`、未选中 B 的 `ReadMini` 以及旧布局拆分均覆盖。未选中字段的运行时值仍保持原 ReadMini 选择语义。直接落盘避免 `configOnce = config` 后差异消失、Bar 无后续迁移时新项永不保存。
- 缺整个 B 字段时继续使用 schema 新默认，不为这次迁移额外造出布局字段；非法 B 继续沿现有 codec 回退，不在补缺时重写坏数组。迁移写盘失败仍沿用旧拆分迁移的 best-effort 语义；后续读取可重试，正常 `Write` 仍有返回值。
- `Inkeys/src/UI/BarViewAdjust.svg`：24×24 的视图边角与二维平移箭头，原生 SVG，使用现有 `rgba(10,0,7,0)` 主题占位；无新位图或缓存生命周期。
- `Inkeys/Inkeys.rc`：`BARVIEWADJUST UI "src\\UI\\BarViewAdjust.svg"`。调用端资源名为 `L"barViewAdjust"`。
- 三语源：`UI.Bar.MainButtons.ViewAdjustLabel` = `视图调整` / `視圖調整` / `Adjust View`。通过 `Scripts/i18n.ps1 sync` 生成 `Inkeys/IdtI18nKeys.g.h` 和 `Scripts/i18n.zh-CN.snapshot.jsonc`，没有手改生成键。

## 真实配置专测交接

新增独立文件：

- `Inkeys/Inkeys/Other/Other.Config.ViewAdjustTest.cpp`
- `Inkeys/Inkeys/Other/Other.Config.ViewAdjustTest.h`

普通函数为 `int Inkeys::Tests::RunBarViewAdjustConfigTests() noexcept`。实现受 `INKEYS_ENABLE_UI3_PRESENTATION_HARNESS=1` 保护；关闭时仅返回 2。Core 负责工程登记和配置/单实例/HWND 初始化前的严格单参数 `--bar-view-adjust-config-test` 路由。

函数要求 cwd 是含 `InkeysRepo.sln` 的仓库目录，全部文件只写入 `Build/ScrollFollow/view-adjust-config-PID-tick/Inkeys/Config/main.json`，并 RAII 恢复 `globalPath`。没有窗口/Host/渲染器/设备初始化。

测试调用生产 `Config::ReadAll/ReadMini/Write`，使用独立固定预期 JSON 比较真实落盘和运行时快照，覆盖：

- 插件与官方项混排、隐藏项、非默认 Size、未知/已卸载插件及额外 JSON 字段、A1/A2 原样保留；
- 已有隐藏 ViewAdjust 的位置、Size、字段和原始文件字节均不改；
- 无 Setting、空 B、重复 Setting 下仅插入一次；
- ReadAll/ReadMini 两条路径、读取后直接冻结复制且不再 Write、重复读取无重写；
- 无关路径 ReadMini 的运行时字段隔离与原文档窄迁移落盘；
- 旧 ButtonLayout 拆分后补缺、Divider/Pierce 保持原退休处理；
- 缺整个 B 的 schema 默认与正常 Write，以及非法 B 的既有 codec 回退。

## 已执行验证

- `pwsh -NoProfile -File Scripts/i18n.ps1 sync`：退出 0，生成物只新增一个键/快照文案；en-US、zh-TW 均 426/426。
- 独立 `pwsh -NoProfile -File Scripts/i18n.ps1 check`：退出 0，两个目标语言均 100% PASS。
- SVG XML、24×24 viewBox、现有主题占位、唯一 rc 路径和生成键一致性：PASS。
- 用 bundled Python 检查 15 个 C++ raw JSON fixture 字面量：均可解析。此项只确认测试数据有效，不冒充 C++ 专测执行结果。
- 九个已有文件保持修改前工作树的 UTF-8 无 BOM、CRLF；三个新资源/测试文件同格式。Git blob 为 LF 规范化存储，与实际工作树 CRLF 分别核对，未将其误当混合换行。
- owned tracked files 的 `git diff --check`：退出 0。差异只涉及本次标识、补缺、资源和文案；无整文件格式 diff。
- 搜索确认 `Bar.Button.cppm` 中容量声明为唯一相关 40 项数组，没有额外 40 次初始化/遍历待同步。

## 待统一执行与证据边界

- 资源实现者没有并发运行 MSBuild。完整 `InkeysRepo.sln Debug|ARM64`、新真实配置 CLI 与其它无窗口回归由 Core 统一构建后执行；本记录写入时二进制专测尚未运行，不能标成通过。
- Core 需将新 SVG、测试 `.cpp/.h` 登记到工程/filters，完成按钮白名单、explicit More Setting 前插入及最终 Selected/Disable 投影。
- 未做可见 UI 效果、真实桌面滚动/系统穿透或完整产品端到端验收；本侧资源与配置检查不替代这些结果。
