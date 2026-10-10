# Technical Design

## Architecture

在 UI3 Bar 渲染循环旁新增 `BarDirtyRegionTracker`。Tracker 不负责判断是否需要渲染，也不替代 `BarPresentDecision`；它只把视觉变化转换为可事务提交的单一 damage `RECT`。

Tracker 每帧接收：窗口边界、稳定视觉键对应的当前边界、变化标记，以及必要的显式矩形 damage。它保存最近一次成功呈现的边界快照和失败/跳帧后尚未提交的累计 damage。

## Internal Contract

- `BeginFrame(windowBounds)`：开始观察当前帧，不清除尚未提交的 damage。
- `Observe(key, currentBounds)`：登记控件或功能组当前边界；不可见时登记空矩形。
- `MarkChanged(key)`：将该键的已提交旧边界与本帧当前边界并入 pending damage。
- `IncludeDamage(oldBounds, newBounds)`：用于动态光、调试覆盖层等显式影响范围。
- `ForceFullDamage()`：把窗口边界锁存为 pending damage，直到成功提交。
- `ResolveDamage(requireFallback)`：返回裁剪后的 pending damage；有呈现请求但无分类 damage 时返回全窗口。
- `CommitPresented()`：仅在完整呈现事务成功后，用本帧观察值替换 committed 快照并清空 pending damage。
- `RetainForRetry(forceFull)`：租约跳帧保持 pending；设备/呈现失败时追加全窗口 damage。

## Data Flow

1. 提交目标并推进动画；动画包装器把 `changed/active` 关联到控件键或功能组键。
2. 完成当前布局派生后观察标准控件边界，并登记自绘功能组和动态光影响范围。
3. 先解析业务 damage，再按独立开关加入 FPS 文字及红框的旧/新边界，得到最终 present dirty。
4. 最终矩形同时用于 D2D dirty clip、透明清除和 `ULW::prcDirty`。
5. 完整事务成功才 commit；跳帧保留，失败强制全窗口重试。

## Precision and Safety

- 标准 UI 对象使用对象地址作为渲染线程生命周期内的稳定键；功能组使用固定枚举键，调试文字与红框使用独立的成功呈现快照。
- 父布局移动会标记对应功能组，避免仅子对象自身值未变化时漏掉继承坐标变化。
- 鼠标/主光先分别计算光源影响矩形，再与所有实际可见 PointLight 边框的四条影响带求交；只合并存在像素贡献的交集。控件几何变化仍由控件/功能组 damage 覆盖。
- Tracker 使用稳定视觉记录和复用的变化/观察索引；普通成功帧只推进本帧实际观察记录，不清空重建两张哈希表，也不复制完整快照。
- 仅主光/鼠标光变化时走窄热路径，只扫描可能承载 PointLight 的 Shape、超椭圆和按钮边框，跳过 SVG/PNG/Word 集合。
- 未来动态缩窗所需的全可见内容收集保留为停用代码块，普通呈现帧不执行；只有变化对象、被标记功能组和光源帧才计算相应边界。
- 显式 D2D 变换内容按同一 pivot/scale 解析呈现边界；绘制时才继承父对象的子视觉在 dirty 采集前同步同一 `Inherit`，禁止读取未参与绘制的默认 `(0,0)` 快照。
- `Debug.Enable` 只控制红框；`Debug.ShowFrameRate` 仅在前者开启时控制文字。文字随真实呈现帧更新，并通过成功后关闭的 sleep latch 在活动结束时只补一帧“休眠”标记；失败/租约跳帧保留，恢复活动时重建统计桶。实际/无限制帧率共用完整 1 秒锁存桶，无限制分母只累计进入 pacing 等待前的帧工作时长。
- 未分类的 `renderOnce`/外部请求以全窗口回退保证正确性。
- 现有可见内容边界代码保留为 helper；旧的逐帧默认调用注释停用，未来可用于动态窗口尺寸，整套 UI 移动时可作为功能组边界。

## Compatibility and Rollback

- 配置 schema 在 `Experimental.Inkeys3.UI3.Debug` 下新增默认 `true` 的 `ShowFrameRate`，保留旧版开启调试时显示 FPS 的行为；窗口协议不变。
- `BarPresentDecision` 与现有失败退避保持不变。
- 新 tracker 若出现不可分类或非法矩形，退化为全窗口 damage；可通过恢复旧的可见内容边界调用快速回滚。

