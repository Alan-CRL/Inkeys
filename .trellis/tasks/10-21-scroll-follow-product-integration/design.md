# 滚动批注 Draw3 与主栏接入设计

父设计：../10-21-scroll-follow-v1/design.md，作为本子任务完整契约。开工先阅读父设计和research/current-architecture.md。

## 模块边界
拥有 Business/ScrollFollow.*、必要Draw3窄接口/IdtState/Main/Bar/配置布局/资源/工程登记。禁止修改Shader/墨迹算法/导航产品门禁/UInk格式。

## 接口与失败
实现ScrollFollowService、会话绝对目标单槽与可靠结束屏障、Draw3原平移恢复、输入/双窗握手，以及主栏More按钮/SVG/i18n/console CLI。
必须实现父design相应接口/线程所有权/错误回滚和门槛。记录额外调查与实际接口到本文件，不引入未批准的替代实现。
