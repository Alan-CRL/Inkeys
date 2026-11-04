# 验收反馈调整边界

用户明确要求三点：移除“淡化范围”独立标题，范围直接并入闲置行；主栏标注仅底栏模式，限制普通/居中底栏，排除浮动；内容闲置上限60%，背景保持30%。本轮直接延续已实施任务，不新建任务或重复询问已明确的产品决定。

最小差距位于 SettingRow 的 inlineContent 渲染、主栏 RenderLoop 的当前 bottomDockMode 门禁和唯一 IdleContentOpacity 常量。预计修改 Setting.Pages/Setting.cpp 与对应 UI 回归、Bar.RenderLoop/ThemeMaterial 与现有像素/策略回归、20语言/生成项及任务/专题文档。不改正常透明度配置、等待时长、外部翻页、附属面板保护或输入系统。

所有修改保留上轮未提交实现。GUI/Computer Use 继续不启动，未授权 commit/push。

后续补充：用户要求关闭淡化时，内联复选区动画折叠，开启时动画展开；父行与时长下拉常驻，偏好保留。为让同一张卡片的外框高度跟随内联条件高度，将在 InlineRowContent 增加可选条件标识/可见性，并复用现有 ConditionalMotionState/ConditionalBlock 的180ms高度、裁剪和渐隐；其他未传此选项的行不改变。相关 UI 回归覆盖中间帧、反向、关闭动画与隐藏期间无事件。
