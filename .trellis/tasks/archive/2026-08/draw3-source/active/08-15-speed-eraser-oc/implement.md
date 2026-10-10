# 笔速橡皮与 OC 平滑执行计划

1. 在预测/宽度模块增加 Eraser 模式类型、独立 OC 控制器、DIP 路程窗、转折保护、Touch 启动限制、非对称恢复与断触暂停/恢复；补齐纯算法单元测试。
2. 在窗口控制器接入 `C` 切换、自动重复过滤、模式读取和 Cursor 唤醒；保持 `3` 仅选择橡皮且不重置模式。
3. 在绘制控制器增加三条 Hover lane、批次模式锁定和 Runtime OC；将动态直径送入 Eraser 模型点、L1 增量和 Mouse/Pen/Touch 光标。
4. 扩展断触资格为 Touch 与倒转 Pen Eraser；重连时保留 Runtime OC 并加入桥接路程，验证多候选和普通 Pen 拒绝。
5. 补齐 DPI/采样率、往返转折、真实减速、慢速短幅最终恢复、Touch 短划、双指隔离、Hover→Down、Pen 尾重连、Stored 宽度和 dirty bounds 测试。
6. 运行 `git diff --check`，检查 C++/cppm 的 UTF-8 BOM + CRLF，使用 ARM64 MSBuild 构建完整 Debug/Release solution，并运行两套无窗口测试。

Rollback point：OC 纯算法、模式/快捷键、Runtime 接线和断触资格互相独立；任何一层失败均可回退而不影响固定橡皮和现有持久数据。

## 验证记录

- ARM64 `Debug` 与 `Release` 完整 solution Rebuild 均通过，四个 shader 构建和资源链成功；两次构建均为既有 `additional/` 第三方警告，任务源码无新增警告、无错误。
- Debug 与 Release 无窗口测试全集均通过，覆盖 DIP/DPI、60/120/240Hz、OC 时间边界、折返保护、Touch 启动限制、Hover 继承、断触续接、动态宽度存储和 dirty bounds。
- `git diff --check` 通过；七个 C++/cppm/test 文件保持 UTF-8 BOM + CRLF，Trellis Markdown 保持 UTF-8 无 BOM + LF。
- 按无可见窗口约束，实体 Pen/Touch 手感与真实断触、可见 UI/基础绘制/resize、D3D Debug Layer 未验证。
