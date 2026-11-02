# 当前实现与规划基线（2026-11-02）

基线：`feature/i18n` / `8bf8f41d`。源码与最新规范优先于历史任务；`6a8fd4ea` 已完成动态字体和语言热切换，`8bf8f41d` 已精简语言说明。

## 已复查历史

- `.trellis/tasks/archive/2026-11/11-01-i18n-eight-locales/`：八语翻译、固定槽、Noto 校准；其中“切换需重启”已过时。
- `.trellis/tasks/archive/2026-11/11-02-settings-dynamic-fonts/font-validation.md`、`research/live-locale-implementation.md`：FreeType/R8、实际页面、Bar/PageControl 与消息快照。旧11语结果不可冒作本次20语结果。
- `trellis mem search FreeType` / `字体` 已尝试；索引主要返回本次请求和转录片段，未提供比当前源码／归档更强的新增决策依据。

## 源码证据与改动判断

| 源码位置 | 当前行为／本次影响 |
| --- | --- |
| `Inkeys/IdtI18nLocales.h:24` | 11项唯一身份表；数组位置是保存值，不能为显示排序重排；NativeLabel统一附加代码/Beta |
| `Inkeys/IdtMain.cpp:3038` / `:3193` / `:3223` | 系统首次匹配、索引校验、元数据加载，自然消费扩展表 |
| `Inkeys/IdtConfiguration.cpp:251` / `:546` | 传统SelectLanguage读写，不改schema |
| `Inkeys/IdtI18n.cpp:75` / `:112` | prepare/commit、中文基准叠加、revision校验、一致发布；无需再开发切换事务 |
| 同文件 `:301` / `:306`起 | invalid index固定11须更新20，补完整旧索引断言；实际资源/Language/LANGID/format/并发循环已遍历Locales |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp:1902` | 主脸优先、SC/TC/JP/KR同字重补缺，Legacy/Design四正文角色与独立图标 |
| 同文件 `:1988` / `:5069` | 候选atlas注册/上传后commit、失败回旧绑定；帧边界同字族可直接commit；成功后缓存失效、Bar/PageControl唤醒再保存 |
| 同文件 `:2984` | 当前菜单行号直接等于保存索引；新增显示排序必须双向映射，不能直接把排序后的行号写SelectLanguage |
| 同文件 `:5390` | 生产字体专项已遍历全部语言3轮；强制Replace循环不能单独证明同字族快路径 |
| `Inkeys/Inkeys/UI/Setting/Setting.Typography.h:12` / `:70` / `:80` | 语义字号、现有字族/角色校准、LightHinting、密度1、基线对齐与Alpha8；不新增profile |
| `Inkeys/Inkeys/UI/MessageBox/MessageBox.Layout.cpp:119` | 默认按钮独立按PRIMARYLANGID；须补七种主语言，地区英/西共用 |
| `Inkeys/Inkeys/UI/MessageBox/MessageBox.Window.cpp:477` / `:791` | JP/KR私有Noto集，其余沿既有系统字体候选，不统一替换为FreeType |
| `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp:1194` | 全Locales真实DWrite glyph-run缺字、固定槽与生产字体刷新检查 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:4671` | 四表面实际ConfigureSurface语言变化已有离屏回归 |
| `InkeysHeadlessTests/setting_design_tests.cpp:5806` | 全译文Regular/Bold、母语菜单、窄宽Combo；选中值假设需随显示映射更新 |
| 同文件 `:6144` | 页面矩阵已循环Locales；100/150/200%全语，125/187.5/400%当前只放行中英日韩；新语分数倍率须补覆盖 |
| `InkeysHeadlessTests/setting_font_benchmark_tests.cpp:59` | 静态样本表仍11语，须追加九语才实际覆盖 |
| `InkeysHeadlessTests/message_box_tests.cpp:216` / `:284` | 默认按钮预期表须扩展；20语GDI+无窗口测量循环可复用，补特殊字符正文 |
| `Scripts/i18n.ps1:631` / `:685` / `:708` | 自动枚举目标、check、sync；验证key/顺序/token/换行。SelectClear/ClickThrough允许0/1换行；sync写文件，规划阶段未运行 |
| `Inkeys/Inkeys.rc:240` / `Inkeys/resource.h:93` | JSON登记与当前最高资源322；新ID拟从323分配，先复核碰撞及_APS_NEXT_RESOURCE_VALUE |

## 字体预检查

Python标准库读取sfnt cmap format4/12并排除glyph ID0，对比母语名、越南语重音、土耳其语/波兰语/乌克兰语/罗马尼亚语等样本；文件哈希、样本和逐字符缺失保存于 `font-cmap-preflight.json`。

- Harmony SC/TC Regular/Bold四文件：九语样本均无缺失。
- Noto JP/KR Regular/Bold四文件：部分土耳其/波兰/罗马尼亚/乌克兰字符缺失，须保留Harmony回退。
- 新九语采用Harmony SC主脸，预期无需字体文件或profile改动。最终译文全部字符及真实FT/DWrite/GDI+结果才是验收依据；cmap不证明字面大小、重音位置、塑形或裁切。

## 规划阶段验证和环境

- `pwsh -NoProfile -File ./Scripts/i18n.ps1 check` exit0，十个非基准语言均679/679。尚未实施新增译文、sync、构建或产品二进制测试。
- Windows SDK winnt.h及本地.NET CultureInfo已确认PRD中的九个LANGID。
- 创建前 `git status --short --ignore-submodules` 无主仓库改动；默认递归status因外部HiMsg worktree gitdir权限失败，后续忽略子模块检查主仓库，不改子模块。
- WindowsApps的python alias在runner中不可用；现有可用运行时为 `C:/Users/alan-/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`，未安装依赖或修改全局配置。
- 已用vswhere找到当前VS及其 `MSBuild/Current/Bin/arm64/MSBuild.exe`；实施时重新动态定位。当前spec与较旧依赖说明存在年代差异，遵守用户指定的完整solution入口，不修改项目依赖。

## 最新用户补充

菜单全部20项按语言标识字典序显示；底层表仍保持旧11索引、追加九项。展示层共享一个由Locales生成的排序索引视图，避免“菜单排序→旧配置被重解释”的回归。具体顺序和映射验收写入PRD/design。
