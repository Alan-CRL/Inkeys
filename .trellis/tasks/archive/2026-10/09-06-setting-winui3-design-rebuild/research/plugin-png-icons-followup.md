# 插件页 PNG 图标恢复（2026-10-24）

用户要求延续设置父任务，仅恢复插件总览四个 Inkeys2 PNG 图标，其他不变。沿用 in_progress 父任务，不创建新任务、不结束/归档，不 GUI/commit/push。

- 已核对 origin/inkeys2-final 的智绘教/IdtSetting.cpp 和四张 PNG Git blob；当前资源与 Inkeys2 完全一致。
- PPT 助手：PluginFlag1 / TextureSettingSign[5]。
- SuperTop：PluginFlag4 / TextureSettingSign[10]。
- DesktopDrawpadBlocker：PluginFlag3 / TextureSettingSign[8]。
- LnkHelper：PluginFlag2 / TextureSettingSign[6]。
- 设置行增加可选纹理，复用既有图标槽位，不改变文字布局、卡片尺寸、导航与配置行为。无纹理参数的其他设置行保持原绘制。
- 复用原设置会话纹理加载、缩放重建及设备生命周期，不新增图片或资源工程项。

## 验证结果

- 四张 PNG 与 origin/inkeys2-final 对应 Git blob 一致；映射与历史代码一致。
- 三个源码文件保留原 UTF-8 无 BOM、CRLF；git diff --check 通过，20 行新增/13 行删除，无无关代码改动。
- 使用动态定位的 Visual Studio ARM64 原生 MSBuild；每次在同一 PowerShell invocation 中规范化 PATH 并禁用 node reuse。
- 完整 InkeysRepo.sln Debug|ARM64 构建约 10 分钟后 exit 1；PptCOM DLL/TLB 已生成，但没有有效错误输出，不能确认主程序编译成功。见 plugin-png-icons-build.log。
- 随后缩小到同一 Solution 的 InkeysHeadlessTests 目标，使用 /m:1；exit 1，首个有效错误为 Bar.Metrics.cppm(1,1) C1902: Program database manager mismatch; please check your installation。多个未改动 module 同样报 C1902。见 plugin-png-icons-headless-build.log。
- 这是本机/Codex 构建链的 PDB 管理器错误，不是已发现的图标源码错误；不修改工程、工具链配置或调试格式绕过。
- 新测试程序未生成，未运行旧测试二进制充当本次验证；未启动 GUI/Computer Use。
- 本轮复用已有图形资源与生命周期，没有需要新增到 spec 的通用合同。父任务继续 in_progress，不 commit/push。
