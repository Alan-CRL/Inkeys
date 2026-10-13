# 本轮执行计划
1. 读现有业务 FIFO、SyncDraw3State owner/呈现同步链，确认锁循环和最小修复位置。
2. 在现有设置调度边界移出所有相关阻塞同步，增加关键中文注释及有效无窗口线程回归。
3. 恢复下拉选择；将上方两条说明放入类似PPT提示的独立信息卡片，自适应高度。
4. 更新回放生成器适配下拉/卡片，并明确同步桩回放的限制。
5. 运行 i18n check、diff/编码检查，独立 trellis-check 审查，完整solution Debug|ARM64与Headless --no-window（已知sandbox限制时在外执行，至少允许5分钟）。
6. 使用 trellis-break-loop 捕获上次修复失效原因，修正规格及验证记录，记录日记；不commit/push，保留任务。
