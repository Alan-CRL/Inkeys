# 按功能持有的图形资源生命周期

## 1. Scope / Trigger

覆盖 Setting 图片恢复、Draw3 Laser、辅助 Selection ULW、Freeze/Whiteboard/Bar/PageControl 的可重建像素资源，以及保存/加载CPU快照的释放判定。目标是普通静止与功能退出后的稳定占用下降；可自行释放的初始化/解码峰值允许存在。L0/L1/L2、UI共享WARP与Draw3独立hardware-first/fallback分工、正常画质和合法文档/历史不改变。

## 2. Signatures

- `InkRenderer::EnsureLaserResources(UINT width, UINT height)`、`LaserResourcesAvailable() const noexcept`、`ReleaseLaserResources() noexcept`、`LaserResourceBytes() const noexcept`。
- `TransparentPresentationController::AuxiliaryReadbackPayloadBytes() const noexcept`、`AuxiliaryReadbackNeedsFullPresent() const noexcept`。
- `ReleaseAuxiliaryReadbackResources(TransparentOutputTarget target, std::uint64_t outputRevision, std::uint64_t contentRevision) noexcept`。
- `Host::AcknowledgeAuxiliarySurfaceHidden(const HostRuntimeSnapshot& expected) noexcept`。
- `HostRuntimeSnapshot::auxiliaryReadbackPayloadBytes` 是名义纹理+DIB载荷，不是OS提交量。
- 内部 `AuxiliaryReleaseRequest` 含 `hostGeneration/productRevision/outputRevision/contentRevision/successfulPresents/target/width/height`，按值投递，不跨线程借用表面指针。
- Setting内部`SettingTextureRecipe`记录稳定`resourceName`、请求与实际宽高、`StraightBgra/WhiteAlphaMask`转换，不存长期解码PNG像素。
- 共享测试进程 `inkStrokeModelerTestTests.exe --save-snapshot-lifetime-only` 仅运行CPU保存/加载释放探针，不进入默认窗口/GPU套件；不新增产品命令行入口。

## 3. Contracts

### Setting

普通显示使用已上传纹理，只有首次图片准备与device epoch恢复执行解码。10个PNG槽只存配方；运行时HICON槽0保留64×64 BGRA小备份，未使用的1/2/4槽仍空。原资源、缩放取整、反预乘和白色alpha遮罩公式不变。恢复仅对有来源的槽工作，index<12失败沿必需资源失败处理，12/13保持可选降级；来源须能支撑失败后的重试。Shutdown归还vector容量并清配方，不仅clear大小。

### Laser

有效需求是选中Laser且不在Selection透传模式。在绘制owner的控制边界准备大纹理组，候选全部成功再发布；普通初始化、非Laser resize与新device不预分配主体。退出结束临时Laser层/尾迹/烘干，解绑SRV/RTV并释放主体/live/scratch；持续选中时保留原Up→Hold→Fade。失败不每帧反复分配，重入/图形恢复按既定控制边界重试。

退出只退休实际`runtime.tool==Laser`。`selectedTool`是接触接受时的身份，可能记忆Laser而当前已提升为Eraser；不能因此丢弃活动掌擦或已Up、等待提交的Eraser。后者继续原Move/Up、模型、存储和Undo/Redo路径，不依赖Laser纹理。

### Auxiliary ULW

HWND、device/context、尺寸和呈现证明与staging/DIB像素分离；首次需要时分配。资源重建后第一次实际辅助视觉帧必须全合成、全复制、全提交。`AuxiliaryReadbackNeedsFullPresent()`仅对SelectionUlw生效，只提升已有视觉帧；普通Primary不得因辅助资源缺失每帧全屏，General回收wake不能制造视觉帧。

原生隐藏证明由IdtState的成功成对显隐事务及最新场景守卫产生。Host在绘制owner消费回收请求，再核对host/product/output/content/成功提交计数/尺寸与ready；Selection目标还要求无内容和已有真实完整透明帧。Host不跨线程查询HWND来替代Window owner证明。回收不清零/伪造ready、clean或成功Present，也不销毁共享窗口；bytes=0后不重复投递回收wake。

### Freeze / Whiteboard / UI

Freeze真实启动透明帧成功后可归还DIB；没有像素的idle循环直接跳过。功能退出时仍持有DIB则真实提交透明，成功后归还，失败保留重试。白板占有共享HWND时只归还自有像素，不清白板；重入新建像素须更新ULW的DC/size，不用旧HDC。

白板inactive/真实隐藏及pending提交退休后释放该client目标，保留文稿、主题、业务状态和共享epoch；最小化须依赖已发布owner隐藏事实，恢复先完整重建提交再ready。PageControl仅在真实Hide、动画及捕获结束后退役，重显走原完整重画和输入门。Bar只在几何稳定门完成后收缩明显过大backing：面积超过需求2倍且可省至少1MiB；基础小表面保持，target尺寸变化沿原全脏重画。

PageControl的confirmed输入身份必须同时比较`inputLocked`；视觉/几何静止也不能沿用旧锁。锁变化先关闭nativeInputBarrier，沿正式candidate与Window owner ACK发布新输入快照，ACK前保留旧锁/nativePending，ACK后才收敛Idle；不能靠额外业务revision或直接写false替代。

### 保存/加载CPU快照

Desktop保存终态把大snapshot搬入本轮局部，在下次等待前销毁；PPT发布完成前将save/load请求赋空；Whiteboard每轮request离开作用域析构。失败与异常同样退休保存payload。不要将这些路径改成仅clear嵌套points，留下整个外层高水位。

`pending/queuedSnapshotBytes==0`、Whiteboard `Drain()`返回均可能先于本轮request析构，不是严格释放完成门。验证须在worker仍活着、Save回执尚未领取时观察登记分配真正经过delete；全局new/delete观察器只属于测试进程，固定容量、默认关闭、不自行分配，不修改既有分配失败注入或malloc/free行为。地址观察只覆盖已登记缓冲，中间副本需另审计，不能据此宣称全堆无泄漏或OS提交量归零。

成功Load完成回执合法持有shared/value快照；`CloseAndDrain()`只排空join worker，不丢弃未领取结果。消费端释放最后owner后才应销毁，失败Clear恢复点、Redo与retained业务页不能按idle无条件删除。Desktop会话幂等记录、PPT失败索引修复账/拓扑与文件权限账也可保留元数据，不能与已退休的大笔迹数组混计。

## 4. Validation & Error Matrix

| 条件 | 行为 |
| --- | --- |
| PNG/Texture/View准备失败 | 不发布半套纹理；必需恢复失败沿原重试，可选atlas降级且保留可恢复来源 |
| Laser候选中途失败 | 回滚候选，不破坏已有有效组/普通笔，不制造每帧重试 |
| 回收请求身份过期或隐藏事务未确认 | 拒绝回收，保留原资源与ready/clean，不强制推进 |
| 辅助缓冲不存在但无新视觉工作 | 保持idle，不能反复创建/释放 |
| 辅助重入仅带小dirty | 先完整合成/提交，不能将未更新backbuffer外区复制成“完整”DIB |
| Freeze透明提交失败 | 保留像素，不留悬空DC，不假报清洁；白板占用时不提交 |
| UI隐藏/动画/捕获尚未完成 | 保留仍需资源，不能提前断开输入/提交证明 |

## 5. Good / Base / Bad Cases

- Good：普通Pen启动没有Laser主体；进入Laser建立，离开后名义bytes归零；提升为掌擦的Eraser仍提交且可撤销。
- Base：空白Selection真实透明帧→owner隐藏→归还像素→无新视觉工作保持零→再入有色内容完整呈现。
- Bad：只清RTV当作释放；按`selectedTool`取消实际Eraser；释放后每次General wake重新分配；用UI调用成功代替真实隐藏/成功帧。

## 6. Tests Required

- `--setting-texture-recovery-test`：独立原转换参考、真实GPU读回、两epoch、11活跃/3空槽、必需/可选失败及清理，PNG常驻CPU像素为零。
- `--draw3-laser-resources-test`：无主体启动、创建/解绑/退出/重入、普通笔和尺寸交集像素、第二device恢复及原子失败。
- `--draw3-palm-eraser-test`：真实Coordinator路线，Laser退出后活动及已Up的掌擦仍Move/Up→CPU文档Eraser→Undo/Redo。
- `--draw3-auxiliary-resource-lifecycle-test`：真实透明帧、精确隐藏票据、过期/非空拒绝、远端像素完整再入、resize及空闲提交数稳定。
- `--freeze-resource-lifecycle-test`、`--ui3-idle-capacity-test`：真实DIB/原生layered提交和WARP大→小target及像素，失败/再入/重复idle复用。
- `--page-control-hidden-test`、`--draw3-hidden-test`与既有像素/故障回归：保持正式输入/ready/页/背景合同。fixture必须先发布Drawing并等真实Primary；模拟Whiteboard owner只回传精确匹配、已呈现的style token，再等Host恢复准入，不能为测试放宽产品门。
- 产品CLI先按argv识别，坏argc或harness关闭均早退2，不能落普通GUI启动。执行无窗口/离屏/隐藏测试时分别记录其范围。
- `--save-snapshot-lifetime-only`：真实worker与独有临时目录；Desktop覆盖成功/写或索引失败/重试/停止，PPT覆盖成功/索引失败/异常/替换/停止，两者Load检查最后owner释放；白板地址探针仅成功/文件系统失败。登记points与外层缓冲的实际释放，对clear保留容量作负控；保持既有CPU保存/文件回归通过。共享测试坏argc沿测试入口错误码返回，不套用产品入口的2。

## 7. Wrong vs Correct

```cpp
// Wrong：清空像素仍长期持有大纹理，接触身份也不能替代实际工具。
ClearLaserCoverage(laserCompositedColor);
if (runtime.selectedTool == DrawingTool::Laser) Cancel(runtime);

// Correct：绘制owner在切换边界退休实际Laser，保留Eraser等合法笔画。
if (runtime.tool == DrawingTool::Laser) Retire(runtime);
ReleaseLaserResources(); // 内部解除绑定，再释放views/textures。
```

像素载荷、Private Bytes、Working Set和GPU/映射分类不可直接相加；功能退出后的稳定值与短时重建峰值分开记录。
