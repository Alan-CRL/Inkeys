# 实施计划

## 1. 配置与共享模型

- [ ] 在 `Other.Config.cppm/.cpp` 增加四个 NoUpload 百分比字段、默认值和严格读取校验。
- [ ] 在 `Draw3.CursorSettings.h` 增加默认值/归一/按样式查询 helper 和四个快照字段。
- [ ] 将打包与 Controller 原子快照扩为 `uint64_t`，保持现有低位合同；贯通 `IdtState`、Bridge merge、Controller 与 Touch 工厂。
- [ ] 扩展 `config_metadata_tests.cpp`、`draw3_bridge_tests.cpp`、`draw3_contact_tests.cpp`：默认、ReadMini/ReadAll、复制/冻结、NoUpload、全部边界/非法类型、单字段 revision、打包往返及旧设备回落。

## 2. 光标外观与渲染

- [ ] 在 `Draw3.PenCursor.cppm/.cpp` 增加外黑边字段、透明度和环宽计算，接入有效性、等价比较、主 resolver 和 Touch 路径。
- [ ] 在 `Draw3.RendererPrimitives.cpp` 上传外黑边宽度；同步 VS 的 `uv.y` 与 PS 的三层 coverage/预乘合成。
- [ ] 保持普通/高亮透明度语义、1 px 物理黑边、直径 ÷ 8 白色环、原外框/热点/脏区不变。
- [ ] 先通过完整 Solution 生成当前正式 CSO，再用既有生成器更新 `Draw3.Win7GuardedShader.h`，随后最终重建并核对 guard、签名和资源链。
- [ ] 扩展 resolver 与 `--draw3-renderer-pixel-test` / upload 验证，覆盖多 DPI、细/粗圆点、PenIcon、25/50/100% 以及正式和 guarded PS；确认非目标 shape 像素回归不变。

## 3. 设置页与 i18n

- [ ] 扩展 Cursor draft/events，在指定位置用 `SliderResetAction` 增加四行，将每次编辑量化为整数百分比，接通即时同步、释放/重置/离页保存。
- [ ] 更新预览的黑/白环、填充/整体透明度及浅色 `Card` 背景；复用共享尺寸和环宽规则。
- [ ] 修改 zh-CN 基准，运行 i18n sync，补齐二十语言翻译，再运行 check；不手改生成头。
- [ ] 扩展 `setting_design_tests.cpp`：四行的实际滑块和重置交互、事件隔离、save-active 状态、两主题预览颜色，以及二十语言 × 340/780 DIP × 1/1.5/1.875 倍率布局和页底 Combo 回归。

## 4. 审查与回滚点

- [ ] 搜索四个 JSON 键、helper、outerOutlineWidth、打包位和 i18n key 的全部读写/镜像，确认没有遗漏 Config → State → Bridge → Controller → renderer 或预览消费者。
- [ ] 审查外观等价/重绘、透明度只改瞬态光标、Touch 与专用工具隔离、HLSL 非负 coverage 和 CPU/GPU 字段一致。
- [ ] 保持目标文件原编码/EOL；缓存和验证输出不纳入任务变更，已跟踪 CSO 只由项目既有 FXC 链重建并核对资源一致性。
- [ ] 若 shader 或打包验证失败，回到对应阶段整体修正，不通过禁用 guarded 路径、缩减 UI 或放宽非法配置校验绕过。

## 5. 验证命令与证据

当前机器为 AMD64，使用 x64 host MSBuild 和 `Debug|x64`；MSBuild 绝对路径在实施时从本机 VS 安装解析，构建等待至少 5 分钟。

```text
pwsh ./Scripts/i18n.ps1 sync
pwsh ./Scripts/i18n.ps1 check

MSBuild.exe InkeysRepo.sln /p:Configuration=Debug /p:Platform=x64

pwsh ./.trellis/tasks/archive/2026-10/10-18-draw3-cursor-policy/research/Generate-Win7-GuardedShader.ps1 `
  -Fxc <当前 SDK 的 x64 fxc.exe> `
  -ProductionPixelCso Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.cso `
  -OutputDirectory Build/Validation/cursor-opacity-guarded

MSBuild.exe InkeysRepo.sln /p:Configuration=Debug /p:Platform=x64
Build/x64/Debug/InkeysHeadlessTests.exe --no-window
Build/x64/Debug/Inkeys.exe --draw3-renderer-upload-test
Build/x64/Debug/Inkeys.exe --draw3-renderer-pixel-test
Build/x64/Debug/Inkeys.exe --draw3-pen-width-test

git diff --ignore-submodules=all --check
git status --short
```

- 可在完整 headless 前用 `INKEYS_SETTING_CURSOR_ONLY=1` 聚焦光标页，但最终必须运行完整 `--no-window`。
- 记录四项配置重启、预览/实际像素、多 DPI、正式/guarded 资源和旧配置回落结果。
- 真实设置 GUI、不同输入设备、桌面背景和 Win7 实机视觉需另行授权/环境；未执行时不得标记为已验证。

## 6. 完成门槛

- [ ] PRD AC1–AC9 均有对应测试或明确保留的实机项，无阻塞开放问题。
- [ ] i18n、完整 Solution、完整 headless、相关 Draw3 无窗口 harness 和 diff 检查通过。
- [ ] 更新 `draw3-cursors.md`、配置/i18n 与 shader 合同中因本任务变化的稳定规则。
- [ ] 工作区只包含任务所需源码、资源、生成物与 Trellis 记录；随后按用户授权进入提交/收尾。

## 7. 实际验证结果（2026-11-05）

- 完整 `InkeysRepo.sln Debug|x64` 使用 x64 host 构建成功；正式 VS/PS 与两个 CS 均经既有 FXC 链生成，最终 EXE/headless 链接成功。
- 二十语言 i18n check 全部 739/739 通过。
- `--draw3-renderer-upload-test`、`--draw3-pen-width-test` 通过。
- `--draw3-renderer-pixel-test` 通过：新增正式/guarded 高亮圆点的黑边、白环和半透明填充读回均通过，既有 18 项 guarded 对照无差异。最终 guarded PS 为 40528 bytes，SHA256 `A5CB427C832D2CA082A86C71D194D24F48BBB6B4589F0E3B5BEB450DE593038B`。
- 光标页聚焦回归中的四项配置、滑块、重置、预览、深浅主题、多语言/多 DPI 布局及既有光标 resolver 均通过。一次完整聚焦运行 exit 0；最终复验仅 `RenderScheduler: pending idle diagnostics flush at the limit without extra client callbacks` 失败，未发现光标用例失败。
- 完整 `InkeysHeadlessTests.exe --no-window` 已运行但非绿色：除上述 RenderScheduler 定时诊断外，还包含既有共享卡片与 equal-aspect 图片 presenter 断言；这些区域不在本任务修改范围，未为其扩大变更。
- BOM/EOL 对照与 `git diff --ignore-submodules=all --check` 通过；未留下滑块临时诊断输出。
- 未执行真实设置 GUI、多输入设备、真实桌面背景或 Win7 实机视觉验证。
