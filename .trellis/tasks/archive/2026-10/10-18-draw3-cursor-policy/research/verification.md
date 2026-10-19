> 历史记录：本文对应首次三场景可选高亮的需求/实现。当前两类型选择、固定设备边缘规则以 cursor-policy-revision.md 和 verification-cursor-revision.md 为准；本文验证不作为修订代码的通过证据。

# 实施与本机验证记录（2026-10-19）

用户已明确批准开始实现；本次完成代码与本机无窗口验证。任务保持 `in_progress`，待 Win7 设备视觉验收。没有启动产品主窗口、设置窗口、hidden HWND harness、浏览器或 Computer Use，没有 commit/push。

## 最终实现

- 鼠标 Pen/HardPen 默认 24 DIP 笔形，左上笔尖为原输入热点、笔尾向右下；Highlighter 默认真实工具宽度矩形，填充 alpha 保持 0.35。
- Settings→Drawing→Cursor：`MouseStyle` 原版/绘制，三场景边缘独立保存，默认 Mouse=true、ScreenPen=false、Digitizer=true。Unknown/Conflict 仅视觉回退 Digitizer，真实身份和橡皮模型保持原样。
- 新配置经 IdtState→Product merge→Bridge→Host owner 消费；设置继续 deferred Sync/FIFO 与冻结写盘。新鼠标样式只覆盖 Pen/HardPen/Highlighter；其他图形与 Eraser/Laser 专用策略维持原范围。
- Touch 系统光标始终隐藏，没有开关/新隐藏字段/Beta 迁移。补首触前 FORCEOFF 同源窗口策略、SetProp 失败与属性借用恢复、Touch End/重复 Pen Clear/工具和模式/显示边界刷新。白板选择态仍对 Pen/Touch 隐藏；跨窗口命中保护保留。
- 系统 before/after flags/handle 和命中线程仅在诊断开启时有界观察；没有修改未知 Mouse 的现有来源过滤，没有延时屏蔽或全局 hook。
- 正式 VS/PS 与 Win7 guarded 完整 PS 同步；新 type21 必须从 DefaultInk 门排除，生成器门 `>20→>21`。保留原数学、未知类型和原 OS/driver 选择。

## 验证结果

| 检查 | 实际结果与证据 |
| --- | --- |
| 完整 Solution | 动态定位当前 VS 的 ARM64 原生 MSBuild；同一 PowerShell invocation 规范化 PATH、禁 node reuse；`InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false`，600 秒上限，最终退出 **0**，最终 stdout 中 error 行为 **0**。主程序、PptCOM、PptCOM.Tests 和 headless 目标成功。 |
| Headless | 新生成 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`，300 秒上限，退出 **0**；`Config metadata tests: 0 failures`、最终 `PASS animation correctness`，stderr 0 bytes。生产 Config 往返/非法类型/冻结副本、不上传、Bridge cursor-only revision、实际 resolver 的 hotspot/DPI/edge alpha/三来源/Touch/Eraser 隔离在本集合内执行。 |
| 生产状态 CLI | 新生成 `Inkeys.exe --state-mode-production-test`，已核对 IdtMain 在配置和窗口启动前 return；120 秒上限，退出 **0**。两条 StateModeProduction PASS 覆盖跨入口、快速切换、迟到回调、模式/穿透冻结与 FineDial 取消/过期提交；PASS 信息按既有方式输出到 stderr，无 HWND。 |
| i18n | 既有 `Scripts/i18n.ps1 sync` 与 `check` 退出 **0**；en-US/zh-TW 均 372/372，100%。只增本次光标键和生成物。 |
| Shader 生成 | ARM64 FXC 正式参考 VS/PS 与 guarded 编译退出 **0**；ISGN/OSGN 一致、DefaultInk 尾部还原 hash 一致，25 条 derivative/discard 最小 guard 深度 1。生成器当前副本与归档只有门限一处差异；源、ASM、数组与 CSO 由独立 checker 复核。 |
| 最终 PE 数据核验 | 仅 `LoadLibraryExW(..., LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)` 数据映射，无入口执行；SHADER301–304 四资源逐字节等于当前四份 CSO，均 DXBC 且产物时间晚于源码。guarded 31160 字节在最终 EXE 恰出现一次，SHA 字符串亦匹配。 |
| 静态与格式 | 独立 Trellis checker 完成总范围审查，并复核 C2248 修正；Git checkout filters 基准下 29 个既有 tracked 文件编码/BOM/EOL 身份保持一致，task JSON/JSONL/末尾换行/空白检查与 `git diff --ignore-submodules=all --check` 通过；staged diff 空。 |

本地未提交原始日志位于 `Build/verification/cursor-policy-20261019/`（ignored）：

- `solution-Debug-ARM64.out.log/.err.log`：允许 PDB 服务后的第一轮，退出 1，唯一编译错误 C2248。
- `solution-Debug-ARM64-fixed.out.log/.err.log`：最小接口修复后的完整 solution，退出 0；stderr 0 bytes。
- `headless-no-window.out.log/.err.log`：退出 0，stderr 0 bytes。
- `state-mode-production.out.log/.err.log`：退出 0，两条 PASS 位于 stderr，属于该 CLI 既有输出方式。
- `shader-resources.json`：最终 EXE 与四 shader 资源的数据核验结果。

## 构建诊断与修正的区分

1. 首轮受限 runner 构建退出 1，可见 `C1902: Program database manager mismatch`，涉及未改动模块；仓库历史已保存同安装 `/Zi` 最小探针在受限环境失败、允许 PDB 服务环境成功的对照。同源码改在允许 PDB 的执行环境后不再报 C1902，未升级/修复 VS 或改源码、debug/PDB/项目设置来绕过。
2. 其后的实际源码错误为 `DrawingController.cpp:8023 C2248`：新设置更新调用了 WindowController 既有 private 重绘请求。仅将 `RequestDrawingCursorRender() noexcept` 声明移动到 public 的请求/消费区域，加中文注释；原函数仍只设置合并请求位并唤醒 owner，不用全画布请求绕行。独立 checker 记录了前次静态漏检及此修复。重建退出 0。
3. 构建仍有第三方/现有类型转换、对齐、nodiscard 等 warning，以及已有 `pwsh.exe` 查找提示；最终 MSBuild 退出 0，既有后处理路径完成。没有为零 warning 扩大修改范围或改工具链。

## 产物身份

- ARM64 Debug 产品 EXE SHA256：`1120F082F46B8C49974A978C6FF1843C904C0A91C1A20EBC8193BCB9FBC2C6A3`。
- ARM64 Debug headless EXE SHA256：`AE62E5C68C4705EF03D3B6FD5EB965B5C1C3E89D6788A15B6B4893F59E94DF03`。
- Win7 guarded PS：31160 bytes，SHA256 `43C7E21FE9AA531271E2036D44998A5DCE6EAC9C032B63965A5417AE293C8D57`。
- 当前生成器见同目录 `Generate-Win7-GuardedShader.ps1`；详细 FXC/签名/尾部哈希见 `implementation-cursor.md`。ARM64 Debug EXE 是本机验证产物，不是 Win7 运行包。

## 尚未验收的硬件项

- Win7 的第二枚小光标究竟来自错误 primary、系统 HCURSOR 还是独立 Tablet feedback。
- Win7 首次落指零闪现、单指/多指擦除、最后 Up/Cancel、Pen Up/OutOfRange 以及真实 Mouse/Pen 即时接管。
- 静止绘制→穿透→绘制时，下层自定义系统光标交接；设置卡片实际视觉、亮/暗背景、多 DPI、跨屏与负屏坐标的可见热点/残影。
- Win7 WARP 与硬件后端上的新 PenIcon/边缘 shader 像素。guarded 字节身份已变化，旧版本 Win7 验收不能沿用。本机 FXC、资源一致性、headless 通过均不能替代这些真机结论。

后续按 implement.md 的矩阵采集与本次 EXE 身份对应的完整 Cursor 日志，分别观察 primary/touch/system。当前保留未知来源接管，不在缺乏现场证据时猜测性收紧。GUI/Computer Use 没有本轮授权，因此未执行。
