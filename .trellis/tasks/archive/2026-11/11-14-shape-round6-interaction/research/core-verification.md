# 第六轮共享核心实现与验证

## 变更
FlipShape 和 TransformShapeGesture 覆盖全部预设；EditSession::PreviewTransform 保持一个 Begin/Commit 事务。屏幕反射使用外层 transform，立体只开放有符号等比对角变换。手势缩放补偿描边宽度，模型约束和 v7 结构不变。

测试新增单次镜像坐标、双次还原、反射后顶点编辑、无效缩放拒绝、3D 忽略旋转、真实 GPU halfWidth 恒定；对翻转/缩放立体执行完整 ExportDraw3SnapshotToUInk/ImportDraw3UInkDocument 与私有元数据往返。

## 自动验证
日志位于 Build/shape-round6：
- core-build-final.log：完整 inkStrokeModelerTest.sln Debug|ARM64，退出 0。
- core-shape-final.log：--shape-core-only，0 failure(s)，退出 0。
- core-uink-final.log：--uink-file-only，退出 0（完整 UInk 权限及事务测试一起运行）。
- core-results-final.json：{"shape": 0, "uink": 0}。
使用 vswhere 定位 ARM64 原生 MSBuild，同一 PowerShell 删除重复 PATH 并设置 MSBUILDDISABLENODEREUSE=1；没有改动工具链或项目配置。最终测试通过 CREATE_NO_WINDOW 子进程，900 秒超时，不启动交互 GUI。

## 环境重试边界
第一次受限构建 core-build.log 在模块依赖扫描后超过五分钟没有输出，正常及提升权限进程检查均已无 MSBuild/cl/Tracker，但 runner 会话仍显示运行；停止孤立会话后，同一命令在正常权限下 core-build-retry.log 成功。初次受限 UInk core-uink.log 的失败集中于 NTFS ACL/硬链接清理探针；正常权限最终运行全部通过。以上不能归因为形状源码错误，没有通过修改代码绕过环境。
保留既有 C4819 头文件编码警告及 PATH workaround 下附属步骤 pwsh.exe 不可见提示；完整构建退出 0。共享测试文件保持原编码/BOM/CRLF，diff --check 通过。真实设备预测延迟和多指手感留给用户验收。
