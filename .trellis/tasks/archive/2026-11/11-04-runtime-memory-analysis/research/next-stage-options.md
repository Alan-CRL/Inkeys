# 下一阶段候选：收益、风险与架构边界

2026-11-05；本轮为方案介绍，未批准或实施新增的中高风险改动。沿用result.md的已测基线：本机Debug启动后空闲Private约510.16MiB；已有低风险批次减少61.10MiB。

## 比较口径

以下是源码资源载荷估算，不是已测可回收Private Bytes；按完整2880×1920作示例，BGRA8一幅21.09375MiB、两幅42.1875MiB。具体画布尺寸、设备/驱动、缓存状态和重建时序会改变实际收益，各项不能直接相加。

| 候选 | 风险与作用 | 建议 |
| --- | --- | --- |
| 私有PPT请求与导出规范页去重 | 中等；降低大文稿保存、排队与导出峰值。DrawingController.cpp:1703仍复制active到legacy；uink_draw3_export.cpp:34仍按值生成规范页 | 最优先。先限制在私有producer/规范视图；保留公共schema及旧输入，验证新旧输出字节等价。不影响空白启动基线 |
| Setting删除CPU BGRA恢复副本 | 中等；倍率1像素约8.83MiB，倍率2约12.47MiB，另有纹理但不可重复计量 | 推荐保留现有字体/纹理热状态，存资源ID+尺寸+像素转换配方；只在设备恢复时重解码。不能顺手把整个Setting变成首次打开才初始化 |
| Laser两张主体层按需/热缓存 | 中高；未使用Laser时有约42.19MiB名义全幅载荷空间 | 值得做独立批次。准备时机、淡出/多contact/烘干与失败路径要明确；首用分配与反复切换抖动是风险 |
| 辅助selection ULW读回层 | 中高；staging+DIB两幅约42.19MiB，即便DComp主路径也初始化 | 值得做独立批次。延后初始化仍需覆盖resize和selection切换；GPU直接呈现属于更大的呈现路径改造 |
| Freeze有效整屏DIB、用过白板后背景target | 中高；每幅接近21.09MiB，白板为使用后的高水位 | Freeze不仅提示小块，还画全屏暗幕；应研究小提示表面+独立遮罩，保留白板共享窗口所有权。单纯首用分配可能带来卡顿 |
| GPU历史缓存页粒度与池收缩 | 中高；当前首次Undo页16MiB、composition页24MiB | 先降低少量内容时的分配放大，不先降低撤销能力。小页会增加纹理对象/绑定，频繁收缩会引起重建；64/192MiB是预算而非启动占用 |
| 不可达Undo分支回收 | 高；控制反复Undo后新起笔的长期增长，收益随点数增长 | 稳定ID/索引、Clear恢复、存储队列与retained页必须纳入可达性判断；不可见不等于可释放，不限制合法历史来换内存 |
| trusted L2快照 | 高；一幅约21.09MiB | 放后面。它保障失败/切换回退，需证明旧可见内容、快照版本与重绘时机，不能直接删除 |

Bar/PageControl backing收缩、字体解析高水位和PPT/CLR按需激活也可研究：前者可能损伤再次显示流畅度，后两者实际Private贡献尚未单独量化。字体已经使用动态Alpha8 atlas与同atlas FT_Face共享，不能重复将其列为新优化。

## 架构级选项

1. **文档/保存共享不可变笔迹数据：最推荐的中期架构方向。** 完成的每笔点数据由文档、撤销、快照共享，修改时只复制受影响的笔/小块。保持三层绘图与双device。不要对整份文稿做粗粒度COW，以免写一笔触发全量复制；异步任务必须有清晰所有权和版本退休边界，避免旧版本越留越多。收益集中于多页、大文稿与保存峰值。
2. **选择前置表面研究GPU直接呈现。** 在现代DComp路径中研究共享GPU内容，避免每次读回CPU和长期staging+DIB；保留旧系统ULW路径。仅仅主窗使用DComp不足以证明辅助窗可以替换：透明命中、窗口层级、切换退休和故障回退都要复核。此方案可保持UI/画布设备隔离，但改变呈现架构。
3. **稀疏分块画布。** 三层语义可以保留，物理存储只为有内容或活动区域分配tile。只分块L2最多改变一幅全屏载荷，不能宣称所有全屏资源都消失；同时改变L0/L1才能扩大收益，也会触及最敏感的逐帧路径。需处理边缘接缝、Retain混合、橡皮、多contact、viewport、历史和resize。当前已有历史tile缓存不等于主画布已稀疏化。保留Win7时不能把D3D11.2硬件tiled resources当通用前提，手工纹理分块仍需单独设计。
4. **PPT托管服务按需激活/独立助手。** 延迟激活可能降低未用PPT时的进程基线，但当前CLR实际贡献未单测。更大的方案是需要时启动、结束后退出的助手进程。当前native/managed初始化传native地址，分进程需要IPC重做，自动接管/断线/Office兼容风险高。必须看主程序+助手合计内存；把CLR搬到常驻助手并不自动节省系统内存。

## 推荐顺序

先私有快照/规范视图去重，再Setting图片恢复副本；争取更大的启动收益时，分别评估辅助ULW和Laser资源生命周期；随后才是Freeze呈现拆分和缓存页策略。重度长时使用单列历史回收。架构层面先考虑不可变数据共享，画布物理分块作为最后的大项目。所有建议都不自动改变原L0/L1/L2与双device约束。

## 依据

- 本任务research/draw3-memory-map.md、ui-memory-map.md、low-risk-draw3-plan.md、low-risk-ui-plan.md及当前源码。
- DirectComposition支持同一bitmap内容关联多个visual，但这不直接证明本工程的辅助窗口替换可行：[Microsoft bitmap objects](https://learn.microsoft.com/en-us/windows/win32/directcomp/bitmap-surfaces)。
- 若讨论后台GPU准备，须先核实工程的device创建flags和线程合同；context仍需单线程或同步，不能直接拿immediate context跨线程用：[Microsoft D3D11 threading](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro)。
- D3D11.2属于Windows8.1代API，硬件tiled resources仍须检查设备tier：[Microsoft D3D11.2](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-2-features)。
- CLR Stop不释放宿主运行时资源：[Microsoft CLR Stop](https://learn.microsoft.com/en-us/dotnet/framework/unmanaged-api/hosting/iclrruntimehost-stop-method)。
