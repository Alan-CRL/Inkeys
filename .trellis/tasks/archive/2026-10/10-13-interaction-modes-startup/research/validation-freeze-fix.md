# 本轮卡死修复最终验证

## 修改与根因
设置绘制持有ImGui锁调用SyncDraw3State，owner事务经Window::Submit同步等待Setting HWND线程，而HWND WndProc也需ImGui锁，形成循环等待，影响共享RenderPipeline。上轮只更换控件及计数式Sync桩没有覆盖此链。
当前全局Sync入口以DeferDraw3SyncIfRendering检测设置会话的thread_local RAII作用域（两处Resume），在已有FIFO业务worker执行原Sync。直接/间接设置调用均覆盖，其他线程同步语义和owner版本/失败重试保持。
审查补齐paintDevice为已有IdtAtomic<int>，每次Sync单次load，JSON显式load保持整数键。恢复原Widgets::combo，下拉读取实时值；两条三语说明进入蓝色信息图标/圆角边框卡片，自适应文字高度。全部自动启动判定保留。

## 验证
- 最终InkeysRepo.sln Debug|ARM64，vswhere动态定位ARM64原生MSBuild，同一PowerShell规范化PATH并禁node reuse；受限环境外构建退出0。日志build-freeze-fix-final.log。没有修改项目/工具链；既有类型转换等warning不作无关清理。
- 最终Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window受限环境外退出0，PASS animation correctness。日志headless-freeze-fix-final.log。
- 增强生产调度回归：提取生产FIFO/作用域/guard/Sync设备更新块，使用真实IdtAtomic；有界模拟HWND线程争锁。覆盖锁内提交返回、同步/冻结写盘FIFO、嵌套/异常恢复、停止排空/拒绝后无锁内回退、非render同步、最终模式收敛、原子快照复制及10000次并发读写应用。ARM64独立编译/运行退出0。
- 下拉框/说明卡片回放提取实际ComboClass及样式，三语×0.6/1/1.5/2共12组退出0，双向切换与重复选择正常，几何裁切0。
- i18n check两种翻译334/334，100%；git diff --check --ignore-submodules通过。
- trellis-check独立审查：唯一并发模式字段发现已修复，无剩余产品/规格问题。
- 修改产品文件原编码/BOM状态/CRLF保持；未修改启动自动判定、项目配置或共享Combo实现。

## 限制
没有启动GUI或Computer Use。线程回归执行生产调度代码，但真实HWND事务由有界模型替代；控件回放默认字体/业务桩不证明现场窗口或字体像素效果。真实应用重启/点击仍未执行。
新程序位于Build/ARM64/Debug/Inkeys.exe。任务、规格和日记已记录；未commit/push，当前任务保留供产品模式/启动预设后续需求。

## 用户验收

2026-10-13 用户确认已经修复，并授权提交本轮 commit；明确要求任务继续进行，后续还将修改启动预设，不执行归档或任务结束。
