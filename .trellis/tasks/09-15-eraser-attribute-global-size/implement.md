# 实施与验证

- [x] 核对用户确认规格、AGENTS、HEAD/status、既有五入口与清空业务。
- [x] 建独立任务、PRD/设计；用户已授权直接实现，无待定产品选择。
- [x] 全局字段、配置恢复/事务、尺寸与灵敏度集中解析及模型测试。
- [x] 共享光标视觉规格、属性布局/状态/绘制/命中与dirty/window接入。
- [x] 工具开合、菜单、异步持久化、清空接受边界。
- [x] 完整ARM64 Solution构建、headless与隐藏窗口产品测试。
- [x] 检查diff/编码/二进制保留，记录逐项结果和未视觉验收项。

## 命令
从vswhere定位ARM64原生MSBuild，保留本进程路径内容并消除Path/PATH双键；/m:1 /nr:false /p:Configuration=Debug /p:Platform=ARM64 InkeysRepo.sln，允许至少5分钟。
Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window
Build/ARM64/Debug/Inkeys.exe --draw3-eraser-hidden-test
补充新几何与配置测试并复用既有清空/会话回归。无可见窗口测试。

## 检查
按trellis-check主Agent自查；回退仅限本轮编辑，不触及用户基线。最终不commit/push/archive。

实际命令、迭代中暴露的问题与未实机验收项见validation.md；全程不提交/推送/归档。

## 2026-09-16 追加执行
- [x] 新增Automatic配置/快照门控并更新设置页事务。
- [x] 重算非对称布局、90×70整体按钮及内部发光分割线。
- [x] 调整整个橡皮组件Z序并保留现有dirty/命中快照。
- [x] 更新纯模型、离屏和隐藏测试，完整构建并重跑四套既有命令。
- [x] 更新功能spec、检查格式/编码与无关diff；不commit/push/archive。
