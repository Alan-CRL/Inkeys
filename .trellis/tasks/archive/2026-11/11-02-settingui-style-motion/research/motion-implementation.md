# R2 / R6 / R7 共享动效基础设施

## 修改边界

- `Setting.Controls.h/.cpp`：局部正文轨道策略、会话入口、块级绘制scope、条件块布局；未改导航、Combo、flyout默认策略。
- 新 `Setting.Motion.h`：值状态和稳定ID，作用域借用当前帧window；没有设备、字体、配置、线程或timer。
- 根代理负责页面/子页路由、实际页面条件调用及原生入口接入，测试也由根代理完成。此工作者未改Setting.cpp、Pages/Design、项目文件或测试入口。

## 接入顺序

```cpp
ConfigureRowAnimations(delta, enabled, rate); // 现有入口
ConfigurePageMotion(pageMotion, routeKey, opening, reset);
BeginScrollChild(id, size, flags, windowFlags, resetToTop, settleLayout,
    true, PageScrollbarAlpha(pageMotion));
BeginPageMotion(pageMotion);
// 真实页面和nested child在这里提交
EndPageMotion();
ImGui::EndChild();
```

`BeginPageMotion`须包住真实正文内容，`EndPageMotion`在outer body EndChild之前。不得包住标题栏或导航。root应不再对正文重复ApplyContentAlpha。`ConfigurePageMotion`使用现有ConfigureRowAnimations提供的delta/enabled/rate，没有第二套时钟。

## 正文轨道

stableBodyLayout默认false，既有测试/其他宿主不变。正文开启后，native始终使用完整8DIP轨道；thumb三态alpha独立乘visualAlpha。Begin之后、任何真实item之前，按实际ScrollbarY将native扣除的轨道宽度补回WorkRect/ParentWorkRect/ContentRegionRect，补偿不超过既有水平padding。轨道取既有右gutter，内容到右边仍保留至少gutter-8DIP间隔；无实际溢出不造ScrollbarY或留新增空白列。未在帧末平移正文来隐藏布局错误。

无需重新执行页面action做预排布。ImGui首帧尚未知道垂直溢出时，正文可用宽本来就相同；后续native实际出现条仍不会改变排版宽度。

## 页面块

- PageMotionState保存route/generation/elapsed和条件状态，Configure时检测context身份改变并清值；普通字体/语言/viewport变化并未当作route导航。
- PageHeader/Text/InfoText/SectionHeader/SettingRow/NavigationRow/Card自动接入；外层scope抑制内层重复。Notice通过SettingRow接入。
- `PageBlockScope("##stable")`供自绘首页hero/网格等使用；`Finish()`可在函数中间结束，其析构幂等。
- 终态布局不动，只改该块新增的父顶点和本帧新增child的顶点/clip，不扫全页顶点。Native item终态注册。正文操作在items提交之前直接使elapsed=.46，原始输入不被清空或重放。
- 同Y（1 DIP以内）块共享顺序；分区标题不推进order，使标题与首行同节奏。只对实际视口重叠块计算order，最长delay120ms，220ms浮入，340ms以后120ms thumb渐显。
- 首次route变化忽略引发导航的同一pointer事件；下帧起正文点击/滚轮/释放及相应键盘操作立刻落定。opening及动画关闭即时终态，中途ConfigureRowAnimations(false)同样结束当前页并让条件tween到target。

## 条件块

```cpp
if (ConditionalBlock block{"##stable", visible}; block) {
    const char* title = block.RetainText("title", dynamicTitle.c_str());
    // 只放纯UI，不能在这里无条件运行业务或Shell副作用
}
```

- 稳定window ID+route key索引；navigation generation变化时按当前条件初始化，不做新增动画。target变化复用NavigationTween从当前值反向，180ms。
- 完整内容仅提交一次；退出/未完全展开时禁输入，DisabledAlpha=1，最后以reveal alpha渐隐并裁剪到当前高度；真实DC占位改为当前高度，下项自然移动。
- 命令可能被PopClipRect合并，裁剪不依赖原command数量；按实际IdxOffset起点必要时拆出tail，防止裁到本块之前的图元。子window裁剪也限制在当前高度。
- reveal归零而operator bool=false时仍把occupied精确归零并处理最后高度差。嵌套条件只由最外层条件累计owner高度差，避免双算。
- RetainText在visible时冻结文案，退出时外部字符串清空仍可绘制旧文字；map节点地址稳定，不保留外部c_str。
- 变化完全在视口上方且用户未滚动时，直接同步调整owner scroll和当前祖先DC坐标，后续项同帧保持位置；原来确实贴底且无主动滚动则向新末端投递scroll。用户滚轮/拖动/键盘滚动优先。

## 验证状态与重点

本工作者未启动GUI、未构建（按根代理避免并行修改期间构建的要求），也没有声称已完成视觉/性能验收。`git diff --check`对所属文件通过，保持原no-BOM UTF8+CRLF。根代理应进行真实ImGui回放与solution build。

优先回归：短→长首帧正文宽度、native rail三态alpha和原导航/Combo默认不变；Card父/child网格裁剪；同排自绘卡同delay；操作取消同次action仅一次；页外触发导航不立即取消新页；动态提示清空文字的退出；条件半途中反向/精确归零/关闭全局动画；同一frame嵌套条件、上方增高及底部的owner scroll。

原生开屏boolean的边界由根代理处理：正常字标结束/输入取消不补播；若Pending准备后来因几何不安全直接Skipped且从未播放，未播放开屏的普通入场需由路由接入显式判定，基础设施只有opening boolean无法推断停止原因。
