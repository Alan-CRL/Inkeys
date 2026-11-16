# Implement — Legacy config migration and project cleanup

每个阶段结束都执行一次 `InkeysRepo.sln` `Debug|ARM64` 完整构建（ARM64 原生 MSBuild，超时 ≥ 5 分钟），并检查 `git diff`，确认编码为 BOM + CRLF。

## Phase 1 — 工程文件与死代码（与配置无耦合，先做）

1. 删除 None 项的旧文件：`IdtDrawpad.cpp`、`IdtDrawpadList.cpp`、`IdtHistoricalDrawpad.cpp/.h`、`IdtRts.cpp`、`IdtFloating.cpp/.h`、`IdtWindow.cpp/.h`，同时删除对应的 vcxproj 项和 filters 项。
2. 删除 `IdtSysNotifications.cpp/.h`、`ExtraInclude.cpp`、`Helper.CrashHandler.old.cpp`。
3. 删除 `IdtDrawpadFacade.cpp`，并清理它的声明以及只被旧代码使用的全局变量。动手前先 grep，确认每个符号都没有调用方。
4. 把 `IdtDraw.h` 从 `ClCompile` 改为 `ClInclude`。
5. 整理 Filters（规则见 design §4），登记未登记的模块头文件，删除空 filter 和过期条目，同步更新 `InkeysHeadlessTests`。
6. 构建，然后用脚本核对：vcxproj 中每一项都有 filter；每个 filter 都已定义且非空。

## Phase 2 — Config schema 扩展

1. 在 `Other.Config.cppm` 中添加 design §1.1、§1.2 的新键，默认值与旧结构体构造和 `IdtMain` 中的默认赋值保持一致，并加上读取校验钩子。
2. 把 `Info.ch` 改为直接引用新字段。
3. 补充 Mini 读取集合（语言、主题等早期启动需要的键）。
4. 为新增的 schema 补充或调整 HeadlessTests 用例：缺失、错类型、越界回退，以及写回后重读。

## Phase 3 — 迁移函数与消费者切换

1. 实现 `MigrateLegacyConfigFiles()`，按 design §2 替换启动路径上的 `ReadSetting` / `PptComReadSetting`。
2. 把消费者逐一切到 `Inkeys::config`：Net.Update、IdtMain、Window.Legacy、Bar.Interaction、Bar.Button、IdtState、Ppt.cpp、IdtPlug-in（DDB 写 interaction JSON、PPT 链接）。
3. 删除 `SetListStruct`、`PptComSetListStruct`，以及 Read/Write/Capture/Retry 函数和 `PptSettingsPersistence.h` 中退役的部分。`setlistUpdateMutex` 在没有剩余用户后删除。
4. 把 IdtMain 中针对 deploy/ppt 的生产自检改写为迁移函数自检：只有旧文件、新旧并存、坏文件、写盘失败不删除这几种情况。
5. 修改 `Bar.Presentation.Test.cpp` 等测试的初始化代码，删除对 setlist 的引用。

## Phase 4 — 设置 UI 清理

1. 删除无引用的镜像变量。
2. 把 A 类、B 类写法改为现读 config（design §3）。
3. 统一走 `QueueConfigWrite()`，移除旧的写入宏和队列函数。
4. 构建后跑 `InkeysHeadlessTests.exe --no-window`。设置页的真机交互验证由用户负责（按规则默认不启动 GUI）。

## Phase 5 — Spec 与收尾

1. 更新 `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`：新键表、迁移合同、删除 SelectLanguage 不迁移等过时条款、更新 SetListStruct 消费者表。
2. 更新 `directory-structure.md`：Additional 镜像磁盘层级、Modules 镜像模块目录、新增功能使用 `.cppm` + `.cpp`、新代码不再新增 Idt* 文件、新配置只进 `Inkeys::Config`。
3. 视需要同步 `settings-attention-notes.md` 等文件中提到 `setlist` 的段落。
4. 写入 journal。只有用户授权后才提交。

## 验证命令（参考）

- 构建：`<VS>\MSBuild\Current\Bin\arm64\MSBuild.exe InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m`
- 无窗口测试：`ARM64\Debug\InkeysHeadlessTests.exe --no-window`（路径以实际输出为准）
- 残留检查：`grep -rn "deploy.json\|pptcom_configuration\|setlist\.\|pptComSetlist" Inkeys --include=*.cpp --include=*.h --include=*.cppm`，结果应只剩迁移函数和测试。
