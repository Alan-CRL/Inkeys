# Research: Font preference, experimental settings, and configuration

- Query: 为仅影响主栏与控件UI字体的非整数像素字号实验开关定位配置、设置、i18n、刷新与无窗口回归入口。
- Scope: internal
- Date: 2026-10-24

## Findings

### 已确认范围

用户最新明确仅主栏和控件UI字体在范围内；设置窗口的ImGui文字渲染不在范畴。本调查只把设置页作为新开关的入口，不建议修改ImFont、字体图集或第三方ImGui。

### 配置归属与最小字段

- `Inkeys/Inkeys/Other/Other.Config.cppm:404-438`：`Experimental.Inkeys3.UI3` 已承载当前UI3的Animation、EdgeLighting、Debug和StartupPreview；Draw3另为同级容器。主栏与分页字号属于UI3，新字段建议 `Experimental.Inkeys3.UI3.AllowFractionalFontSize`，类型 `IdtAtomic<bool>`，默认 `false`，`ConfigUploadMode::NoUpload`。这是新偏好，不需要legacy deploy.json迁移。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md:7-24` 区分传统与module配置；该新字段与现有UI3实验字段同消费域，因此归属有源码证据，避免增加传统SetListStruct。
- `Inkeys/Inkeys/Other/Other.Config.cpp:166-179`：bool codec仅接受JSON bool；整数0/1、字符串、null、数组、对象均不接受。
- `Other.Config.cpp:899-921`：ReadAll/ReadMini共用ReadImpl，先ApplyDefaults(paths)，再ApplyDocument；故缺失、非法值、读失败时目标字段保持schema默认false，Mini仅重置/读取选中的路径。
- `Other.Config.cppm:465-485`、`Other.Config.cpp:800-806`：schema自动参与构造、复制、默认/部分读取及写盘；只加一处schema即可，正常不需增加特殊codec。
- `Other.Config.cpp:890`：文件在 `globalPath + L"Inkeys\\Config\\main.json"`。
- `Inkeys/IdtMain.cpp:2399-2403`：启动Mini仅预读StartupPreview和缩放位置。若本次明确不改启动预览字体，无需添加新字段到启动Mini；完整配置在 `IdtMain.cpp:2866` 读取，`2941` 冻结configOnce。运行中可切换的字号策略应读取实时config，不用configOnce。

### 设置页接入

- `Inkeys/Inkeys/UI/Setting/Setting.cpp:1405-1429`：设置会话初始化Experimental.Inkeys3局部bool；新字段可放相邻位置。若可能有其他生产者修改，像缩放设置一样在卡片绘制前读取实时原子值。
- `Setting.cpp:8003-8069`：tabExperimental用Inkeys3子面板展示现有实验卡片，`drawConsoleOutput(id,title,description,bool&,commit,cardHeight=70)` 可复用，仅变化时commit。
- `Setting.cpp:8022-8024`：父panel原高度 `190 + (debug?75:0) + 605`。增加卡片必须加上该卡高度及5DIP卡间距，否则下方内容会被父child裁切。
- `Setting.cpp:8047-8058`：helper标题直接TextUnformatted，说明仅在cardHeight>70时Wrap。新中文标题“允许界面字体以非整数像素尺寸渲染”较长，英文更长；须测量标题可用宽度（正文从20DIP到右侧toggle前），必要时该卡局部允许标题换行，不应修改所有设置字体。
- `Setting.cpp:8116-8134`：硬件加速/兆芯卡是现有国际化+110DIP说明卡的直接参考。
- 建议说明清晰限定主栏和控件，如“关闭时将主栏和控件的字体像素尺寸截断为整数，以改善部分缩放倍率下的清晰度。更改后立即生效。”实际措辞随最终实现即时生效范围调整。

### 安全保存与唤醒

- `Setting.cpp:483-488`：`QueueConfigWrite()`持configWriteQueueMutex，将冻结配置到入队维持同序；`QueueBusiness`的WriteConfig分支（约 `Setting.cpp:463-467`）创建Config并复制全局config，worker仅Write owned快照（`Setting.cpp:225-226`）。复用该入口，不要在ImGui回调直接config.Write。
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:285-321`：`barUISet.UpdateRendering(false)`跳过业务同步，仅通知BarAtomic wait并RenderPipeline::Request(Bar)，符合设置渲染锁内轻请求边界。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:4754-4761`：NotifyLayoutChanged只snapshotMutex短锁递增publishedRevision，然后RequestAll；RequestAll在`PageControl.cpp:505-509`请求PptPageMask（四分页共用PPT/白板）。
- `Inkeys/Inkeys/UI/Bar/Bar.Zoom.cppm:222-227`：RefreshScaleBase现有导出入口会发布显示快照、UpdateRendering(false)、NotifyLayoutChanged。它证明主栏和四分页的设置轻唤醒模式；字号更改不需要重新查询/发布显示快照，可新增语义明确的字号刷新入口，或直接使用现有Request+policy签名。
- **注意**：轻Request或revision递增不等于有视觉damage。字号政策须进入主栏/分页自身稳定帧或字体缓存比较，并在变化时真实失效/完整重绘；否则同内容/同布局可沿旧bitmap直接返回。不能从设置线程持渲染事务锁清DWrite缓存，也不能只给当前可见分页刷新，隐藏后恢复必须比较新政策。
- 依当前渲染设计，最好由每个render owner在下一帧捕获一个政策bool，比较自身上次应用值并清自身字体测量/绘制缓存，统一全帧政策，成功呈现后再承认本帧状态；具体实现由字体链路调查决定。

### i18n与生成物

- `Inkeys/src/i18n/zh-CN.jsonc:539-581`：SettingsUI.Experimental下现有Draw3与ConsoleOutput；建议新增UI3.AllowFractionalFontSize.N/E（或同义非宏冲突键）。
- 需要同步三语言：`Inkeys/src/i18n/{zh-CN,zh-TW,en-US}.jsonc`。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md:268-283`：先改基准及翻译，再执行 `pwsh -File Scripts/i18n.ps1 sync`，随后 `pwsh -File Scripts/i18n.ps1 check`，审查快照和生成header最小diff；不要手工编辑header。
- 生成物是 `Inkeys/IdtI18nKeys.g.h` 与 `Scripts/i18n.zh-CN.snapshot.jsonc`。
- `Scripts/i18n.ps1:1-19` 支持check/sync，无GUI。

### 回归入口

- `InkeysHeadlessTests/config_metadata_tests.cpp:35` 的RunConfigMetadataTests直接使用生产Config，隔离globalPath和临时目录；`345-409`展示schema默认、合法值往返、NoUpload、Mini隔离、冻结副本、Reset、缺失/非法值测试模式。此处追加bool回归最集中，不必将测试散进IdtMain。
- 建议覆盖默认false、缺失/非法类型回false、true/false往返、Mini仅目标路径、Config复制/冻结、ResetToDefaults、NoUpload。使用生产JSON和schema，不用模拟配置结构。
- `Inkeys/IdtMain.cpp:527` 的RunConfigBoundaryTest及 `1528-1529` 的 `--config-boundary-test` 在任何产品GUI初始化前返回，是现有附加真实配置验证。`697-741`的Draw3设备bool矩阵可作为参考，但对本次新增字段优先只改已存在的Headless配置测试。
- 无窗口运行用完整solution Debug|ARM64产物 `InkeysHeadlessTests.exe --no-window`；字体离屏/数值测试由主字体链路接入，不需要ImGui渲染测试。

## Files found

| 路径 | 用途 |
| --- | --- |
| Inkeys/Inkeys/Other/Other.Config.cppm | 唯一schema/导出Config声明 |
| Inkeys/Inkeys/Other/Other.Config.cpp | 严格bool、读默认、Mini、写盘 |
| Inkeys/Inkeys/UI/Setting/Setting.cpp | 实验卡片、配置FIFO、轻请求参考 |
| Inkeys/Inkeys/UI/Bar/Bar.Zoom.cppm | 主栏+四分页刷新参考 |
| Inkeys/Inkeys/UI/Bar/Bar.Main.cpp | UpdateRendering轻唤醒 |
| Inkeys/Inkeys/UI/PageControl/PageControl.cpp | 共享分页revision/RequestAll |
| InkeysHeadlessTests/config_metadata_tests.cpp | 生产Config无窗口回归 |
| Scripts/i18n.ps1 | 三语同步和检查 |
| Inkeys/src/i18n/*.jsonc | 可见标题/说明 |

## Related specs

- `.trellis/spec/native-desktop/index.md`
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`
- `.trellis/spec/native-desktop/rendering-and-ui.md`
- `.trellis/spec/native-desktop/build-and-compatibility.md`

## External references

无。以上结论全部来自当前仓库源码及用户补充范围；未外推第三方库行为。

## Caveats / Not Found

- 设置窗口自身字体渲染明确排除，不能为此开关改ImGui图集、ImFontMain->Scale或第三方代码。
- 本调查没有构建、运行GUI或测试观感，不声称已验证清晰度改善。
- 父panel新增高度、长英文标题以及政策变化在稳定/隐藏后恢复时的真实damage必须由实现/检查阶段复核。
