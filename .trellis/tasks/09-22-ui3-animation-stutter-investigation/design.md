# 修复设计（第一批已于 2026-09-22 获批）

## 目标与边界

先消除已定位的无效动画目标与无效光照唤醒，为低概率长帧保留足够诊断信息。证据索引见 [findings.md](findings.md)。用户已批准本方案的第一批 S1-S4；D1-D3 仍不在本轮实现范围。

不降低光影质量、不改变动画速度、不改变共享线程/后端，不把尚未验证的容量或缓存方案一并重写。

## 第一批 S1：单一描边目标

仅在 `Bar.RenderLoop.cpp` 删除 ColorSelect1..11 选中/未选中分支中 22 个 `ft.SetTar(1.0)`。常规目标统一由末尾颜色块几何循环提交。保留勾号显隐、面板缩放、描边颜色/透明度、批次 SyncValueDuration、可见性/换边的一次性 retarget。

合同：同一布局计算中，同一属性只提交最终几何目标；关闭时 ft 与面板宽度使用同起点、同曲线和同结束时刻，`ft / (panelWidth / 370)` 保持 1。重定向/换边允许重建段，但不得每帧提交中间错误目标。不要逐帧 SetDirect。

验证：真实动画 module 的关闭、展开、反转、非均匀正 dt 和选中色切换；隐藏后不续帧。使用当前 probe 给出的曲线作为缺陷回归向量，断言可见的同步比例而非某个内部实现字段。必要时将最小布局目标提交边界与现有测试连接，避免复制生产循环作为唯一测试。

## 第一批 S2：只唤醒确实受光照变化影响的 scene

局限于 `Bar.Scene.cpp` 私有实现，将“本次局部光照变化是否贡献实际 damage”从 damage helper 逐层返回。发布者始终保存最新全局/scene 光照快照，但仅对有贡献的 scene 发 invalidate/wake hooks；回调继续在 registry/scene 锁外执行。

保持以下行为：

- 鼠标旧位置照到、新位置移出时，旧光必须被擦除。
- 同一区域内强度/颜色改变，即使矩形形状相同也必须重绘。
- primary/drawing 光变化继续保守全量 damage；暂不增加更复杂的主光贡献判定。
- 首次订阅仍由现有 `SetSharedLightingSubscribed()` 强制重绘。
- 布局、显隐/退场、按压、epoch、失败 pending 和 debug 请求仍由原入口唤醒，不由光照广播接管。

不能比较 pendingDamage 前后矩形是否相等，也不能仅看 invalidated 的旧值；要以本次光照计算是否贡献非空 damage 为依据。第一批不改 PageControl 的通用空 damage fallback，不提前移动其动画推进，不新增公共 scene API。详见 [私有实现设计复核](research/pagecontrol-fix-design-review.md)。

## 第一批 S3：只读 DC 的准确修改区域

在 Bar 和 PageControl 的 `GetDC(COPY) → ULW` 成功获取 DC 分支，把 `ReleaseDC(nullptr)` 换成指向空 `RECT` 的参数，并添加简短中文注释说明 DC 只作 hdcSrc。

保留 COPY、取得/释放配对、GetLastError 捕获和四阶段全部成功后才提交的事务。该修正依据 API 语义，不承诺本机可测的帧时间改善。

## 第一批 S4：限频诊断

复用现有 logger，在渲染线程内累计 POD 数值和 monotonic 时间，不新增独立 UI、后台上传或每输入事件日志。

- 每个共享客户端：是否被调用、耗时、FrameResult；从而分离 Bar 自身慢与其他客户端阻塞。
- Bar：原始 dt、用于动画的 dt、动画推进数、成功 present 数/间隔、退避跳过/重置次数。
- 阶段：呈现锁等待、D2D 绘制、GetDC、ULW、ReleaseDC、EndDraw。
- 状态：epoch/backend、target/capacity、displayCapacityZoom、viewport/source、当前 zoom、光源活跃状态。
- 光影：parent mask hit/miss/create、exact hit/fallback 原因、有效分片调用数；不记录 SVG/文本/鼠标轨迹明细。

常规热路径只更新计数；默认仅在长帧、提交失败或恢复事件时输出并最多每秒一条聚合记录。健康阶段的秒级汇总可由内部诊断开关启用，复用既有配置惯例，不增加产品设置界面。阈值集中定义，首次实施以 50 ms（已有慢放限幅阈值）为长帧触发参考。日志调用频率与 flush 等待分别衡量，不把日志自身变成主线程负担。

## 分开的后续事项（不在第一批批准范围）

### D1：动画时间与恢复门

先建立三种状态：真正 idle、活动推进、等待呈现恢复。Bar 从自身 idle 恢复时 Rebase，fresh epoch 在旧退避门之前观察；活动动画的推进时钟与呈现尝试分开。装饰 demand 只更新最新画面需求，不清同类失败退避。参数动画墙钟追赶与弹簧积分分步需要成组设计；不能只删除 clamp，也不能先禁止光照重置而保留退避冻结动画。

### D2：恢复 exact 整图缓存的平移兼容

候选仅扩展整数平移且设备像素边界对齐的情况，继续拒绝缩放/旋转/错切和无法保证对齐的分数位置。保留半径、尺寸、预算、预热晋升和失败回退。需要无 GUI D2D 像素对照证明 Alpha/边缘等价；若该验证无法建立，不先放宽 gate。分数 zoom 的量化半径差仍是独立问题，不能承诺 25 片全部消失。

### D3：capacity/source/viewport

最终 candidateViewport 必须被真实 target 包含。计划应先求完整 viewport/envelope，再确保足够容量，重新计算 source、mapping tuple 与全量重绘。现有源范围缺少保证已被 helper 模型证明，真实 ULW 故障仍待现场或获准 GUI 阶段验证。

容量缩回要同时处理 `displayCapacityZoom` 和 `capacitySize`，以几何稳定度而非光影是否 idle 判断，避免逐帧重建清空所有缓存。阈值需数据支持；不将缩容当第一批默认疗法。

## 兼容、验收与回滚

- 保留现有 Win7/D2D clip-stack、premultiplied alpha、WARP/device epoch 和多窗口呈现所有权。
- 验证主工程时使用原生 ARM64 MSBuild + `InkeysRepo.sln Debug|ARM64`，遵守同一 PowerShell PATH workaround，不改变 SDK/依赖。
- S1、S2、S3、S4 各作为独立可审阅 diff；发生光照漏擦可只回滚 S2，描边行为异常只回滚 S1，诊断有开销只回滚/停用 S4。不得撤销用户其他改动，不自动 commit/push。
- 第一批验收是确定缺陷被修正、无效工作被抑制、错误/长帧可判因；“用户原始低概率问题已解决”需要用户故障场景的后续记录或反馈，不能由编译成功代替。
