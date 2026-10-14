# 主栏布局实施记录

- Bar.MainLayoutPolicy.h 新增内部四排序、双方向枚举、归一化与纯空间策略缓存。列表和 Y 不变，X 根据固定策略或展开侧镜像。
- RenderLoop 在完整稳定目标网格宽可用后判左右方向，使用 activeMonitorBounds 和 frame zoom、固定主按钮半宽40、gap10以及目标描边；不读脉冲实际宽或Back超调。当前侧不足且对侧fit才翻，both不足保持。缓存只受稳定几何/配置/锁变化影响。首次/PPT/白板放置不抢根位置，折叠仍计算未来展开全宽。
- PositionUpdate 在mode1保持左右方向，在mode0保留中轴；上下沿原逻辑。底栏Centered、白板专用锁优先。
- 排序变化即使width/side不变也计入统一布局批次，正常坐标、隐藏锚点、动画/damage/继承/hit沿已有流水线。
- 有限观察签名保持sizeof72和所有wire字段/协议，使用flags bit8..9表示IconOrder、bit10表示Direction，bit7继续非法。更新唯一mask消费者和冻结比较；production producer/frame一致，不改变DTO尺寸。
- Headless增加4排序×2方向、多屏负原点/zoom、四空间组合、精准像素边界、重复缓存、无效几何与锁；finite验证8组合与semantic变化。
- 真实Bar离屏增加生产PositionUpdate门禁及两行按钮X动画/隐藏Pierce/稳定身份检查。此测试直接执行共享策略与真实位置动画，不声称执行整个私有RenderLoop或GUI拖动；Render接线由审查/完整构建覆盖。
- 未改项目/新构建系统、未启动GUI或运行窗口harness、未commit/push。git diff --check通过，7个已有文件BOM/CRLF保持。完整编译与动态CLI由主会话统一执行。
