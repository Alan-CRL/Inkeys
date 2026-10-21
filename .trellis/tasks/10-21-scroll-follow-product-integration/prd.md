# 滚动批注 Draw3 与主栏接入

## 目标
实现ScrollFollowService、会话绝对目标单槽与可靠结束屏障、Draw3原平移恢复、输入/双窗握手，以及主栏More按钮/SVG/i18n/console CLI。

## 需求来源
../10-21-scroll-follow-v1/requirements-source.md 与父prd。用户已明确批准本任务树及design。

## 验收
依赖capture真机与tracker离线通过。验证初始化任一步失败回滚，旧session/scene和重复seq拒绝，退出最终目标不丢，UI保守互斥与生命周期停止。

## 边界
拥有 Business/ScrollFollow.*、必要Draw3窄接口/IdtState/Main/Bar/配置布局/资源/工程登记。禁止修改Shader/墨迹算法/导航产品门禁/UInk格式。
遵守父任务批准的feature/move、GUI授权、保守互斥、不commit/push和编码/EOL约束。
