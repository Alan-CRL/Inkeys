# Research: 大帧间位移的 LK 收敛与最小修正候选

- Query: 当前追踪器对单帧 128px 等大位移的初始化、质量门与既有覆盖；比较金字塔校准和 phaseCorrelate 初值方案。
- Scope: mixed（只读源码与 OpenCV 4.10 官方文档，不运行合成回放、构建、GUI 或修改实现）
- Date: 2026-10-23

## Findings

### 当前事实及失败含义

主会话提供的本次真实长滚动首个 128px 步骤出现连续 3 次 InconsistentTranslation，inliers=2、fb_p95≈.878、capture API error=0。失败帧像素未留存，且该步骤结束后的 SourceGT 未到达；这些日志不能证明 Mag 帧正确，也不能唯一归因于 LK。下文为源码可支持的收敛解释和条件建议。

`VisualMotionTracker.cpp:171` 固定半分辨率灰度；`:379` 正向 LK 和 `:395` 反向 LK 均为 21×21、maxLevel=3、30 次/.01、minEig=1e-4、flags=0。没有运动预测或全局粗位移初值。正向之后 cornerSubPix 再细化，细化偏差≤1，反向 FB≤1（均为处理像素），`Inside` 的边界为 12。两次 LK 均须同时考虑校准；仅增强正向仍可能被反向拒绝。

`VisualMotionTracker.cpp:414` 在 FB-valid≥40 后才拟合。因此，若日志确实来自 `:468` 的 InconsistentTranslation 且内点仅 2，说明已有至少 40 个通过双向门的候选，却没有形成足够大的同一平移集合；并非“只检测到了 2 个角点”。`:411–412` 的 FB P95 仅对 **已筛到 FB≤1 的 matches** 计算；.878 是有截断的幸存样本统计，不证明匹配到了真实物体位置。`:483` 还有另一种同名拒绝（锚点累计与局部共识不符），需要用 inliers/空间比率区分。

128 原图像素在半分辨率是 64；假设最高层确实存在，level3 上约为 8 像素，level4/5 上约为 4/2。这个算术不是可保证捕获范围：LK 是局部迭代，细节尺度、粗层纹理、混合运动、边界与初值均会影响收敛。不能用 21×2³×2 推导“336px 必定成功”。本机测试纹理 `Draw3.ProductLiveTest.inl:81–98` 含 4px 散列细节及分散 96px 大格 L 角；粗层会平均掉许多细节，错误局部极值是合理待验证解释，更深层也可能进一步损失信息。

`VisualMotionTracker.cpp:467–471` 仍要求 ≥40 内点、空间票≥.65、存活≥.35、覆盖门及归一化二维跳变≤.25；`:472–504` 用不变出生锚点求绝对 total，可信后才一次提交参考/点/累计。`:308–312` 普通拒绝 3 次锁存 Paused，不自动恢复。以上均不应为大位移放宽。

### 已有测试覆盖缺口

`InkeysHeadlessTests/visual_motion_tracker_tests.cpp:189–217` 的 14 组合法序列覆盖长路程，但最大常规单步约为水平 (14.875,.125) 原图像素，垂直为 12。12 屏累计长度不等于支持单帧跳过大距离。`:219–254` 的拒绝后恢复只覆盖 10/15px 左右累计间隔。`:338–373` 已有变形、重排、无重叠负例，不能因新初值而退化。

960×540 图像中，垂直 192/256px 本来就超过 .25 跳变门，不能把这两个用例当作必须接受的合法校准例；128px 垂直约 .237，接近现有门。建议真实尺寸 2880×1920（以及实际 workArea 裁剪尺寸）测 16/32/64/128/192/256，再按各分辨率分别标记合法范围。对角步按 hypot(dx/W,dy/H) 判断，不能仅检查单轴。Business `ScrollFollow.cpp:187–215` 固定工作区 ROI 后直接交 Tracker，另按 capture sourcePixelsPerPixel 还原；纯 Tracker 测试应匹配 ROI 尺寸，不改变 DPI 或世界锚点。

### 有序候选与选择门槛

1. **首选最小参数校准**：先在主会话已有回放上只比较双向 maxLevel=3/4/5，保持 win21、全部门槛/参考事务/角点策略不动。若 level4（或5）跨纹理、方向、尺寸、负例可靠解决，再选最小成功值。大窗口31仅在证据表明需要更大局部支持时另做单变量校准；不能先同时改层数、窗口和门槛以掩盖原因。这类修正只涉及当前两个 LK 调用和定向回归。
2. **若参数扩大仍不稳定，再考虑粗位移初值**：对最后可信 reference 和当前 gray 生成固定更小尺寸的 float 灰度（例如最长边≤512，具体由测试决定），显式复用同尺寸 Hanning window，`phaseCorrelate(referenceSmall,currentSmall,window,&response)` 得粗位移。按各轴实际尺寸比换回当前处理像素，填 `q[i]=p[i]+guess`，使用 `OPTFLOW_USE_INITIAL_FLOW`；反向也需要相符的估计（如 `refined[j]-guess`，对应索引和坐标系必须正确）。不得把估计值直接写入 total、Viewport 或替代反向结果；仍重新实际求 LK/细化/FB/空间共识/歧义/非平移/覆盖/跳变/锚点一致性。
3. 初值必须 finite、在现有归一化范围内，response 门槛需用正负矩阵校准并记录，不能把高 response 当可靠平移的充分条件。全图相位峰可能被固定工具栏、重复文字、动态区域或低重叠主导；它只是搜索起点。可保持原先无初值路径，只有非严重图像错误且未提交的 LK/拟合失败时，在**同一个 Process 事务**作一次有界辅助尝试。两次都失败仅计一次拒绝，不能先调用 Reject 进入 Paused 再“恢复”，更不能跨帧补点重置参考。此 fallback 比单参数变更改动大，需证明收益后再实施。

### 建议实现者/检查者的定向矩阵

| 维度 | 必须覆盖及断言 |
| --- | --- |
| 大单步 | ±16/32/64/128/192/256，水平/垂直/合法对角；静止→突跳→静止、连续同向、反向、突然停止；按分辨率门标记应接受/应拒绝 |
| 内容 | 本机 TexturePixel 精确公式、既有不可变 Canvas、稀疏 DocumentCanvas、固定 UI、动态 patch、重复/周期纹理；GT 均从固定世界独立采样 |
| 重试事务 | 第1/2次低可信后，下一可信图包含整个间隔大位移；拒绝 total/reference/anchor 不变；第3次 Paused 且后续不自动恢复；不将失败尝试多计一次 |
| 原安全门 | 黑/纯色、重排/新页/无重叠、缩放旋转、固定多数与少数移动歧义，仍拒绝；大于 .25 仍拒绝 |
| 累计与接力 | 原6918帧及300帧静止门不变，新增长程大步合法prefix误差≤max(3px,path*.002)，全初点离屏后接力，点数≤320 |
| 性能 | 真实ROI及960×540，Process 总耗时P50/P95/max、光流与粗估阶段、分配/缓冲上限；测无运动常态及辅助尝试最坏情况，不能用合成离线耗时宣称真实Mag FPS |
| 实屏复验 | 新构建先原真实墨迹短例，再同一128px失败步骤与十屏/水平往返；保留失败Capture像素/ROI/序号/时间及对应独立GT，结束后再核对墨迹/owner/存储 |

当前由主会话单独执行 TexturePixel 的 16/32/64/128/192/256 回放，本研究未重复实验，也不填入尚未获得的成功率。不可用缩小 GUI 步长来把追踪范围问题标为修复；小步可作为隔离诊断，不能替代失败大步验收。

### 性能与影响文件

- `Inkeys/Inkeys/CV/VisualMotionTracker.cpp`：两个 LK 参数或条件初值 helper/参考缓存；保留纯图像模块和事务边界。
- `InkeysHeadlessTests/visual_motion_tracker_tests.cpp`：新增大步/拒绝间隔/歧义负例；现有严格 CLI 与原矩阵保留。
- `Inkeys/Inkeys/CV/VisualMotionTracker.h`：仅在确需暴露新阶段诊断时增加统计；参数试验不需要改接口。
- `Draw3.ProductLiveTest.inl`：后续仅为失败像素和独立 GT 证据增加受控诊断，当前失败本身不要求变更产品 Host/Draw3/Mag。
- 不需要改 manifest/triplet/features、calib3d/highgui、捕获服务、会话暂停政策或世界坐标锚点。

增加 maxLevel 的粗层图面积增量通常小，但每点每层迭代与额外梯度/边界存储仍有成本；window31 相对21的单次 patch 面积约 2.18 倍，不能把该比率当总耗时倍率。phaseCorrelate 增加 float转换、FFT/padding和临时复数缓冲，若失败后双跑还增加一次 LK，可能使实际帧率降低、反过来扩大下一帧间位移；必须设固定尺寸/尝试次数上限和实测预算。保持原 OpenCV 运行策略，不引入全局线程修改来“优化”校准结果。

## External references

均为 OpenCV **4.10.0** 官方文档，2026-10-23 查询：

- [Object Tracking / calcOpticalFlowPyrLK、buildOpticalFlowPyramid](https://docs.opencv.org/4.10.0/dc/d6b/group__video__track.html)：maxLevel 为从0计的最高层；实际构建层可更少；未设 INITIAL_FLOW 时从 prevPts 开始，设时 nextPts 须同数量。文档不保证某个像素数的收敛范围。
- [imgproc Motion Analysis / phaseCorrelate](https://docs.opencv.org/4.10.0/d7/df3/group__imgproc__motion.html)：需要单通道32/64位浮点；可选window；输出亚像素平移及可选0–1峰响应。其响应不代替现有追踪门。

## Related specs

- `.trellis/spec/native-desktop/scroll-follow-tracking.md`：可信提交、原图坐标、终身锚点、暂停、纯图像和现有依赖合同。
- `.trellis/spec/native-desktop/scroll-follow-product.md`：真实 owner 绝对目标、旧会话隔离、结束屏障。
- `.trellis/tasks/10-21-scroll-follow-motion-tracker/research/implementation-current.md` 与 `check-current.md`：既有6918合法帧数据和独立审查证据。

## Caveats / Not Found

- 本研究不包含新实验、实际失败图像或唯一根因证明；未用 capture API零错误排除 Mag 内容/时序问题。
- level3/4/5 与窗口/相位初值的最终取舍须依主会话回放及实屏证据，不能把条件建议直接记录为已实施参数。
- 未改变任何源文件、测试、构建、配置或 spec，未运行 Git 操作；只写本任务 research 文件。不提交、不结束任务。
