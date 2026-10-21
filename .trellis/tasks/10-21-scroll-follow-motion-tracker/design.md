# 可信二维视觉追踪与特征接力设计

父设计：../10-21-scroll-follow-v1/design.md，作为本子任务完整契约。开工先阅读父设计和research/current-architecture.md。

## 模块边界
拥有 CV/VisualMotionTracker.*、新 tracker tests 与必要工程登记；不触碰GUI/Draw3产品。

## 接口与失败
按父design实现独立VisualMotionTracker、图像接口、可信度和接力，并加入现有headless离线图像序列测试。
必须实现父design相应接口/线程所有权/错误回滚和门槛。记录额外调查与实际接口到本文件，不引入未批准的替代实现。
