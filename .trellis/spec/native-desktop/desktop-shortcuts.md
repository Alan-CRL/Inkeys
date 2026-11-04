# 桌面快捷方式设置与启动保障

## 1. Scope / Trigger

修改“通用 > 快捷方式”、名称预设/自定义、main.json 相关字段、启动保障或设置 FIFO 快捷方式动作时适用。源任务 `.trellis/tasks/11-03-shortcut-settings-integration`；此处的桌面文件快捷方式与 `Input.Shortcuts` 键盘快捷键是不同合同。

## 2. Signatures

- `Business::DesktopShortcut::{NormalizePreset, ResolveName, PresetForIndex, IndexForPreset, IsValidName}` 位于 `Inkeys/Inkeys/Business/DesktopShortcut.h`，统一预设身份、语言分支与文件名校验。
- `Config::GetShortcutNamePreference() const -> std::pair<int, std::wstring>`、`SetShortcutNamePreference(int, std::wstring) -> bool`：名称对在 Config 私有 rwMutex 下读取/提交；返回 false 包含无效输入或没有变化。
- `CaptureDesktopShortcutNames()` 冻结同一 I18n::TextSnapshot 的语言与用途名称；`GetDesktopShortcutDirectory()` 取得当前用户桌面目录。
- `ShortcutAssistantClass::SetShortcut()` 是启动入口；重载 `(const std::wstring& name, bool correct, bool create, const std::wstring& desktopDirectory)` 消费已解析请求。
- `GeneralSection::DesktopShortcuts` 在键盘 `Shortcuts` 后；设置 worker 区分 `SettingBusinessKind::CreateShortcut` 和 `EnsureShortcut`。

## 3. Contracts

`Inkeys/Config/main.json` 的 `Application.Shortcut` 全部 NoUpload：

| 字段 | 类型 / 默认 | 规则 |
| --- | --- | --- |
| NamePreset | int / 0 | 0 Inkeys，1 简中品牌，2 批注，3 批注工具，4 自定义 |
| CustomName | Unicode string / 空 | 自定义原文，不随语言翻译 |
| CorrectLnk | bool / true | 修正已有名称/指向 |
| CreateLnk | bool / false | 缺失时补建 |

- 所有语言默认 Inkeys。显示索引不得直接当持久化 ID：简中顺序 `[1,0,2,3,4]`，其余 `[0,2,3,4]`；简中品牌项转到非简中时归一并保存为 0。
- 用途预设走各语言 i18n 资源，名称统一半角括号且不额外添加空格；品牌 `智绘教Inkeys` 只在简中显示。
- 从预设进入自定义，以切换前的显示名称填入；恢复已保存的自定义直接读原文。草稿跨帧保留，不每帧由配置覆盖。
- 名称/语言变化只更新配置，不创建、删除或重命名桌面文件。有效自定义在回车、编辑结束、离页/设置隐藏时提交；无效草稿不覆盖最后有效配置。
- 隐藏提交由共享渲染线程在释放呈现/清输入前执行，退出在置协程停止标志前补交；不得依赖隐藏后还会有 ImGui 帧。提交入口只借用存活协程的草稿，并由同作用域 RAII 在协程销毁时注销，沿既有 FIFO 排空持久化请求。
- 手动创建只操作所选名称；启动保障及原开关即时应用沿用原独立条件。修正只统一目标与参数属于同类的其他名称；补建不依赖修正开关。
- 设置 Create/Ensure 请求冻结名称、exe、桌面目录与开关，worker 才进行 COM/文件操作；不能执行时重新读取活动语言或自定义草稿。UI 渲染线程不得扫描桌面或执行 ShellLink I/O。
- CustomName 是字符串，不能使用 IdtAtomic（只支持无锁平凡类型）。必须和全 Config 快照复制共用 rwMutex；QueueConfigWrite 的入队锁不代替字段同步。
- 文件路径身份使用 `CompareStringOrdinal(..., TRUE)` 按 Windows 不区分大小写的规则比较，不能用默认 C locale 的 `_wcsicmp` 代替；所选名称仅改变大小写时，修正不能把同一目标文件当旧名删除。
- 停止传统 setlist.shortcutAssistant 读写/缺省；CaptureSettingJson 清理旧 PlugIn.ShortcutAssistant，即使 AutoClean=false。不读取旧值迁移，不双写。

## 4. Validation & Error Matrix

| 条件 | 结果 |
| --- | --- |
| 缺字段或类型错误 | schema 缺省；名称选择默认 Inkeys，开关 true/false |
| 未知预设 / 自定义配置文本非法 | 归一到 Inkeys，不生成无效文件路径 |
| 恢复的自定义名称单独有效、但超过当前桌面路径预算 | 在启动/设置恢复时选择并保存 Inkeys，保留原自定义文本；不只是让保障失败 |
| 品牌项在非简中 | 回退并保存 Inkeys，不立即调整桌面 |
| 空白、Windows 非法字符/设备保留名、路径超限 | 就地提示、创建不可用、持久化最后有效值保持 |
| 合法 Unicode 自定义名称 | 原文保存；操作层附加 .lnk，不清洗或截断文本 |
| 创建/保障 I/O 失败 | 返回失败，不由 UI 伪报成功；修正旧名时先成功保存目标再删除旧链接 |

文件名按当前 ShellLink/路径能力校验；保留设备名带扩展名和 COM/LPT 上标数字也受限。依据：[Windows 文件名规则](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file)、[IShellLinkW::GetPath](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishelllinkw-getpath)。两类按完整目标路径与参数分类，不以目标路径子串判断；跨版本历史名称库不在本任务范围。

## 5. Good / Base / Bad Cases

- Good：简中选择 Inkeys(批注) 后进入自定义，输入框为该名称；提交“我的 Inkeys”只写配置，点击创建时 worker 使用已冻结的这个名称。
- Base：缺新配置时默认 Inkeys、修正开、补建关；没有已有快捷方式时不凭修正开关自行补建。
- Bad：用户正在把自定义文本编辑成 CON 时，写回 CON 或让异步 worker 再读取当前草稿；把其他语言第 0 行误写为简中品牌 ID=1。

## 6. Tests Required

- Headless 配置：缺省/非法字段、全部 ID、两个开关四组合、ReadAll/ReadMini/写回、旧值不迁移、全快照字符串独立和并发名称对有效。
- 实际设置控件：新子页位置/独立可见性、两个开关独立事件、自定义回车/结束编辑、无效草稿不创建/保存、左右输入/下拉在窄宽及 DPI 下不重叠。
- 生产队列：入队后改变名称/语言不改变旧请求；名称编辑没有快捷方式文件动作，离页/隐藏提交有效草稿。
- `Inkeys.exe --desktop-shortcut-test` 通过 CommandLineToArgvW 严格检查两参数；多余参数返回 2，关闭 presentation harness 时也返回 2，探针主体不进入该发布产物。有效入口在产品初始化前早返回，仅在隔离临时目录使用真实 ShellLink，覆盖开关四组合、缺失/错误目标/旧名统一、仅大小写改名及非法路径拒绝，不接触真实用户桌面。
- 完整 `InkeysRepo.sln Debug|ARM64`、`InkeysHeadlessTests.exe --no-window`、i18n sync/check；实际通过结果看任务 validation，不凭测试源码存在推断。GUI 须另获授权。

## 7. Wrong vs Correct

```cpp
// Wrong：字符串绕过快照锁，worker 又借用随后还会被编辑的文本。
config.Application.Shortcut.CustomName = typedName;
queue([&] { CreateShortcut(typedName); });

// Correct：名称对受配置锁保护，UI 只排队自有数据；具体字段参照 SettingBusinessCommand。
config.SetShortcutNamePreference(preset, customName);
QueueConfigWrite();
// 仅显式创建/原保障动作触发时，冻结解析名称、路径和开关后 QueueBusiness。
```

## 场景：白板快捷方式扩展（2026-11-04）

### 1. Scope / Trigger

任务 `11-04-whiteboard-desktop-shortcut` 扩展已归档 11-03 的应用快捷方式合同。设置页按应用程序、白板排列，每类都是创建条目、内联名称和两个保障开关，不再有独立保障标题。

### 2. Signatures

- `DesktopShortcut::Kind::{App, Whiteboard}`；`NameOptions.kind`、`CustomPreset(kind)`、`PresetCount(kind, simplifiedChinese)` 与原解析函数共用一个预设映射源。
- `NamesCollide(wstring_view, wstring_view)` 用 `CompareStringOrdinal(..., TRUE)`，两类 Windows 文件名不区分大小写比较。
- Config 新增 `Get/SetWhiteboardShortcutNamePreference`，名称对继续共用 Config 私有快照锁。
- `ShortcutAssistantClass::{ClassifyShortcut, MatchesShortcut, CreateShortcut, SetShortcut}` 接受种类；设置 FIFO 的 `shortcutKind` 与名称、目标 exe、目录、开关一同冻结。

### 3. Contracts

- `Application.WhiteboardShortcut` 四字段均 NoUpload：`NamePreset:int=0`、`CustomName:wstring=空`、`CorrectLnk:bool=true`、`CreateLnk:bool=false`。0 缺省本地化 Inkeys 白板、1 简中品牌智绘教Inkeys白板、2 自定义。只接受真实整数 0..2；不迁移旧配置。
- 白板简中索引顺序 `[1,0,2]`，其他语言 `[0,2]`；品牌非简中归一并保存为0。白板名称不得与应用当前解析名称相同，双向检查草稿；冲突显示就地提示并禁用创建，不覆盖最后有效偏好。
- 白板 ShellLink 为当前 exe + 精确 `-Whiteboard` + `IconLocation(exe,-IDI_ICON2)`；应用类为空参数和原程序图标。分类只接受完整目标路径匹配：空参数为 App，去首尾空白后仅有大小写不敏感 `-Whiteboard` 为 Whiteboard；未知参数为 NotOurs。校正会把白板参数规范为精确形式。
- 修正只删除同类旧名；先成功保存目标再删旧名，大小写同名不能自删。选定文件的目标、类别、参数或图标错误按修正语义重建。启动共用桌面枚举和一次 Shell 通知。
- 手改配置发生同名时，白板回退缺省；仍冲突则只跳过白板保障并记录日志。设置线程不执行桌面 I/O，名称/语言变化只保存配置。

### 4. Validation & Error Matrix

| 输入 | 结果 |
| --- | --- |
| 白板预设缺失、类型错误、越界或超宽整数 | 0，其他字段独立按 schema 缺省 |
| 任一类草稿与另一类重名 | 两类就地提示，禁止此次提交/创建 |
| 指向本 exe 且带未知、超长或空白参数 | 不作为应用或白板旧名删除 |
| 两类同时保障 | 各自只清理同类，互不删除 |
| 创建新名失败 | 保留旧链接，返回失败 |

### 5. Good / Base / Bad Cases

Good：应用 Inkeys 与白板 Inkeys白板 各自统一旧名；Base：白板修正开、补建关，不凭空创建；Bad：仅按 exe 路径清理所有 .lnk，或把下拉行号当持久化 ID。

### 6. Tests Required

`InkeysHeadlessTests.exe --desktop-shortcut-only --no-window` 覆盖真实 Config 与实际设置控件，含预设映射、名称冲突、两类事件与布局。产品 `--desktop-shortcut-test` 用私有临时目录真实 ShellLink 覆盖参数、图标、长未知参数、隔离和大小写保护；完整 Debug ARM64 Solution 与 i18n check。运行结果见任务 validation，不能由此文档推定通过。

### 7. Wrong vs Correct

Wrong：`GetArguments` 用小缓冲后把截断内容用于删除决策；只按路径匹配所有同 exe 链接。
Correct：完整读取参数并拒绝容量饱和，按精确目标与受限参数分类，同类修正后再删除旧名。
