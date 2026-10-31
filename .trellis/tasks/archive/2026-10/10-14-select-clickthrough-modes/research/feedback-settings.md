# 设置说明反馈修复（2026-10-14）

## 修改
- Setting.cpp 三段说明的分隔从双换行改为单换行，减少空白行。实际绘制和 CalcTextSize 仍使用同一字符串、0.5 倍字体及同一 678 DIP 换行宽度，信息卡依实际文字高度加上下留白计算，不引入固定高度。
- zh-CN 使用「选择」和「穿透」；zh-TW 的分离式说明改为「選擇」。两种中文均不再为选择添加（清空）括注；英文分离式说明同步使用 Select。
- 仅改变设置说明，主栏选择(清空)、選擇(清空)、Select (Clear) 原有短标签不变。
- 运行 i18n sync 更新基准快照/生成链，生成键名未变化。所有已有文件保持原 UTF-8、无 BOM 和 CRLF。

## 无窗口验证
- Scripts/i18n.ps1 sync 和 check 最终均退出 0；en-US / zh-TW 各 349/349，100%。基准变动首次 sync 按正常流程生成翻译标记，补齐本轮两项翻译后再次 sync/check 全通过。
- 复用 research/replay-select-clickthrough-settings.py，提取本次实际 Setting 块、生产 ComboClass 与颜色栈实现，以实际仓库资源字体运行 ImGui 回放；没有创建 HWND、GPU renderer 或交互式 GUI。
- 添加说明间距回归：实际文字项高度必须等于生产测量高度；三段仅含两个单换行；与相同文字的旧双换行布局比较，必须恰好减少两个文字行。信息卡高度同步验证，无文字边界/字形裁切、缺字及标题/下拉重叠。
- 动态 vswhere 查当前 VS 安装，在 vcvarsall.bat arm64 环境用 ARM64 cl.exe 编译现有回放；编译退出 0，执行退出 0。
- 三语言 × 0.6 / 1.0 / 1.5 / 2.0 共 12 组全部通过，每组 writes=2、setters=2，冻结配置序列 [1,0]，重复选择不新增调用。clipped / glyphClipped / missing / overlaps / spacingFailures / failures 均为 0。

| 语言（1.0 比例） | 新文字高度 | 相同文字旧双换行高度 | 新卡片高度 |
| --- | --- | --- | --- |
| zh-CN | 45 DIP | 75 DIP | 85 DIP |
| zh-TW | 45 DIP | 75 DIP | 85 DIP |
| en-US | 75 DIP | 105 DIP | 115 DIP |

日志：Build/SelectClickThroughValidation/settings-feedback.log；编译入口：同目录 settings-feedback-compile.cmd；生成回放源码/二进制均位于忽略的 Build 目录，不进入提交。

## 验证边界
- 回放的运行时 setter / 写盘队列仍为计数桩；不以此代替生产状态机、FIFO、真实窗口和实际系统 DPI 渲染验证。
- 主会话统一执行完整 Debug|ARM64 solution 与相关无窗口测试。本代理未启动 GUI、未使用 Computer Use、未 commit/push，未结束或归档任务。
