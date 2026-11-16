# Legacy config migration and project cleanup

## Goal

清除 Inkeys2 遗留的配置条目与设置 UI 冗余结构，把仍在使用的旧配置统一迁入 Inkeys3 的 `Inkeys::Config`（`Other.Config.cppm`，落盘 `Inkeys\Config\main.json`），并删除未参与编译 / 已无调用方的旧源码，规范 `Inkeys.vcxproj(.filters)` 的 Solution Explorer 分类，最后把这些规范写入 Trellis spec。

## 范围

### 1. 配置迁移与清理

- 迁移来源：`opt\deploy.json`（`SetListStruct setlist`）与 `opt\pptcom_configuration.json`（`PptComSetListStruct pptComSetlist`）。
- **不在本任务范围**：`Inkeys\Memory\memory.json`（已弃用）、`DesktopDrawpadBlocker\interaction_configuration.json`（DDB 外部进程读取，未来另行迁移；其生成逻辑与内容不变）、`update.json` / `installer\update.json`（更新流程数据，非用户配置）、Draw3 自动保存的 `index.json` 等数据文件。
- 仍被**已编译代码**消费的字段迁入 `main.json`；只剩默认值赋值、或只被未编译文件引用的字段直接删除（清单见 design.md）。
- deploy.json 中的 `PlugIn.DesktopDrawpadBlocker`（Enable / RunAsAdmin / SleepTime / Intercept×17）一并迁入 main.json。
- `Save.SaveDays`（按天清理历史画布）删除，不再提供；`Save.Enable`（画布保存开关，驱动 Draw3 AutoSave）保留并迁移。
- 迁移策略：**一次性迁移后直接删除旧文件**。启动时若旧文件存在，读取其中有效值写入 main.json 中尚未存在的键；main.json 写盘成功后删除旧文件。之后不再读写 `deploy.json` / `pptcom_configuration.json`。
- 迁移完成后 `SetListStruct` / `PptComSetListStruct` 及其 Read/Write/Capture 函数退役（DDB 的 `interaction_configuration.json` 写出仍保留，数据来源改为 main.json）。

### 2. 设置 UI 冗余结构清理

- 删除 `RunSettingSession()` 中无引用的会话镜像变量。
- 「镜像 + 每帧与全局比较」与「镜像 + 控件返回值」两类旧写法改为每帧从 `Inkeys::config` 现读、以控件返回值判定修改并保存；只保留确有跨帧语义的状态（滑块拖动中 / 松手写盘的 pending 与 Record、纯 UI 状态）。
- 不改变任何设置项的外观、文案、交互与保存时机。

### 3. 工程文件与死代码清理

- 删除 `Inkeys.vcxproj` 中以 `None` 挂载、不参与编译的 Inkeys2 源码：`IdtDrawpad.cpp`、`IdtDrawpadList.cpp`、`IdtHistoricalDrawpad.cpp/.h`、`IdtRts.cpp`、`IdtFloating.cpp/.h`、`IdtWindow.cpp/.h`（含对应头文件中只服务于它们的声明）。
- 删除已无调用方的编译文件：`IdtDrawpadFacade.cpp` 存根及其声明 / 只被旧代码使用的全局变量、`IdtSysNotifications.cpp/.h`、空文件 `ExtraInclude.cpp`、`Inkeys\Helper\Helper.CrashHandler.old.cpp`。
- 本次**不做**函数级修剪（仍在编译的 IdtDraw / IdtImage / IdtTime / IdtStart / IdtOther 中的未用函数保留）。
- Filter 规范：
  - 第三方库全部位于 `头文件\Additional\<库>`，层级与磁盘 `Inkeys\additional\` 一致（目录名大小写与磁盘一致，去掉多余空中间节点不改变真实层级）。
  - Inkeys3 代码全部位于 `Modules\Inkeys\<Category>`，与磁盘 `Inkeys\Inkeys\<Category>` 一一对应；补齐缺失的 filter 条目（如 `SelectionOverlay`、Bar 新文件）与未登记的模块头文件。
  - 清除 filters 中指向不存在 / 非项目项的过期条目和空 filter。
- `InkeysHeadlessTests.vcxproj(.filters)` 中引用到被删除 / 迁移文件的条目同步更新。

### 4. 规范记录

- 在 `.trellis/spec/native-desktop/` 中记录：新配置统一进 `Inkeys::Config` / main.json、旧配置文件已退役、新增功能采用 `.cppm` + `.cpp` 结构、vcxproj 分类规范（`头文件\Additional` 镜像磁盘层级、`Modules\Inkeys` 镜像模块目录）。
- 修正现有 spec 中与本次决策冲突的旧描述（例如 `SelectLanguage` 不得迁入 main.json 的条款、`SetListStruct` 消费者列表）。

## 约束

- 最小化修改：不改变迁移字段的运行时语义、默认值与取值范围；不重排语言索引等保存值。
- 保持原文件编码与换行（`.cpp/.cppm/.h/.vcxproj` 为 UTF-8 BOM + CRLF）。
- 不提交 Git，除非用户另行授权。
- 构建验证使用 `InkeysRepo.sln`，`Debug|ARM64`，ARM64 原生 MSBuild。

## Acceptance Criteria

- [x] 首次启动：仅存在旧 `deploy.json` / `pptcom_configuration.json` 时，所有迁移字段的值被正确带入 main.json，旧文件被删除；再次启动行为一致。
- [x] main.json 已含对应键时，旧文件中的值不覆盖 main.json。
- [x] main.json 写盘失败时不删除旧文件（下次启动可重试）。
- [x] 代码中不再存在对 `opt\deploy.json` / `opt\pptcom_configuration.json` 的读写（迁移读取除外）。
- [x] DDB `interaction_configuration.json` 输出内容与迁移前一致（字段来源改为 config，格式未变；未做实机对比）。
- [x] 设置窗口所有页面显示与保存行为与迁移前一致（含滑块松手写盘、离页写盘）——用户已实机验证。
- [x] 被删除的文件不再出现在磁盘与 vcxproj / filters 中；Solution 分类满足上述规范。
- [x] `InkeysRepo.sln` `Debug|ARM64` 构建成功；`InkeysHeadlessTests.exe --no-window` 中配置相关用例通过。
- [x] spec 已更新。
