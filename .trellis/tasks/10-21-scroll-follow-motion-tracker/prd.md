# 可信二维视觉追踪与特征接力

## 目标
按父design实现独立VisualMotionTracker、图像接口、可信度和接力，并加入现有headless离线图像序列测试。

## 需求来源
../10-21-scroll-follow-v1/requirements-source.md 与父prd。用户已明确批准本任务树及design。

## 验收
依赖 capture 真机门槛成功。至少十屏/反向/静止300帧/新旧特征接力/干扰/黑帧/缩放旋转/特征不足暂停及累计误差符合父implement目标。

## 边界
拥有 CV/VisualMotionTracker.*、新 tracker tests 与必要工程登记；不触碰GUI/Draw3产品。
遵守父任务批准的feature/move、GUI授权、保守互斥、不commit/push和编码/EOL约束。
