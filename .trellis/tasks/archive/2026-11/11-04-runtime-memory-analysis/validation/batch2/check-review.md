# Batch2 最终质量审查

结论：已批准的2–5完整变更通过代码、格式、编译、适用回归、规格和采样核对。主会话统一构建/运行，本代理未自行启动程序/GUI、改变系统设置或commit/push。1保存峰值去重与6历史缓存策略保持后置。

## Findings (fixed)

1. `Draw3.DrawingController.cpp`：Laser退出循环原按`selectedTool==Laser`连带取消实际已提升为Eraser的掌擦（active及ended待提交），可能丢弃已接纳内容。已以私有`RetireLaserToolRuntimes`仅退休actual Laser；Eraser继续原Move/Up及历史提交。既有`--draw3-palm-eraser-test`增加真实Coordinator、模型、Promote、CPU文档Eraser提交和Undo/Redo回归，最终退出0。
2. `Draw3.HiddenWindowTest.cpp`：命令fixture结束选择/隐藏测试后仍selectionMode=true就期待注入落笔。已通过正式Bridge切回Drawing并等待真实Primary成功帧/内容ready。冻结上一批EXE复现同一首个FAIL，属于既有fixture前提缺漏；没有放宽产品输入门。
3. 同文件Whiteboard fixture未回传当前WhiteboardStyleToken，2048(WhiteboardStylePending)阻止Down。已仅在同host/generation/page/color/serial、非loading、成功呈现的当前场景，经正式PublishProductState采用精确token，再等Host真实恢复input-ready；未改生产2048/ready门。修复后完整`--draw3-hidden-test`约23.015s退出0，旧首错和强制停止级联历史保留。
4. `PageControl.cpp`（UI5 owner修复）：真实已确认输入快路漏比较inputLocked，旧过渡锁可在视觉稳定后永久沿用。InputChanged现把锁差异关闭新准入，confirmed idle快路要求锁一致，随后经原candidate/Window owner ACK发布新输入；没有直接改ShouldLock/写false或多发业务revision。新增真实延迟旧锁、暂停owner及匹配ACK后恢复回归，最终`--page-control-hidden-test`约5.766s退出0。冻结旧版同样复现此前首错/10条级联。

## Findings (not fixed)

无未解决的已确认代码问题。以下是证据限制而非源码失败：

- WPR实际录制仍受普通Windows令牌0x80070005限制，无ETW分配栈或完整GPU驻留分解。
- 流畅度按用户要求仅静态审查；没有宣称动态FPS/延迟验收。
- 全产品采样为普通启动/静止及真实白板命令管道往返；Setting/Laser的创建/退出/恢复使用真实离屏probe证明资源归还，未包装成完整GUI交互采样。
- 本机ARM64 Debug结果不外推为Release、Win7/其他GPU或所有用户文稿收益。

## 完整静态审查

### 2 Setting

逐slot对照资源名、倍率取整、原尺寸atlas、直通道反预乘/白色alpha遮罩与原实现一致。10PNG+HICON及3空槽一致。仅HICON保留16KiB raw，PNG仅存稳定配方；正常Show/每帧不解码。必需恢复失败清半成品但保留来源可重试，12/13可选，空来源skip。独立原转换CPU参考、真实GPU读回/两epoch/OOM/可选降级实际通过。

### 3 Laser

普通Init/resize不预留大纹理，工具/Selection控制边界准备完整候选，成功后发布；退出解绑主体/live/scratch并仅结束真实Laser，不丢持久掌擦。失败锁存，不每帧重试；在当前工具上按恢复/再入重建。保持选中时Up→Hold→Fade、L0/L1/L2/像素公式/设备分工。

### 4 Auxiliary ULW

HWND/metadata与像素分离；缺缓冲的首次真正Aux视觉工作先全合成/复制/提交。Full-present谓词限定SelectionUlw，不提升普通Primary脏区；资源回收的General wake不制造视觉帧。原生隐藏证明来自IdtState成功owner显隐事务及场景守卫，Host在绘制owner重验host/product/output/content/成功提交计数/尺寸及Selection空白clean；不自行查询HWND取代owner证据，不伪造/清零ready或成功Present。真实Host 150ms idle无自激提交/重建测试通过。

### 5 Freeze / Whiteboard / Bar / PageControl

Freeze同次idle快照后continue，不让并发新信号落入空surface；启动真实透明帧保留，结束透明提交失败保留DIB重试；白板拥有HWND时只归还自有像素、不清白板，每次提交刷新HDC/size且不复用旧DC。Whiteboard真实Inactive/隐藏及pending退休后只释放client目标，helper调用位于active绘制renderLock之前，native lease先Reset；最小化使用owner快照，恢复失效prepared并完整重建。不清文稿/主题/共享WARP epoch。

PageControl在真实Hide、过渡完成且无pointer/drag/capture后回收；重显沿原全脏/输入确认门，锁状态属于confirmed输入身份。Bar仅在几何稳定、无调试等待/输入flush时收缩，超过2倍面积且可省至少1MiB，target尺寸变化走原RequireFullDirtyRetry；不反复销毁基础小表面。真实WARP大→小与远端像素/重复idle复用、实际隐藏/退休/再入测试均通过。

### 集成、范围与格式

五条新CLI均argv优先识别，坏argc及harness off早退2，不落普通配置/单实例/GUI。21个本批源码/接口维持UTF-8 BOM与CRLF，git diff --check通过。AutoSave/PresentationAutoSave、InkHistory/InkHistoryGpu、GraphicsInitialization与本批source-before逐字节一致；第一批snapshot moves保留，1/6及设备选择未扩展。新`memory-resource-lifecycle.md`完整7节和index链接已审阅，包含类型签名、代际/错误矩阵、真实测试、inputLocked与owner ACK合同。

## Verification

- Lint/static：pass，包含full-scope调用链、资源/失败/输入门、UTF8 BOM/CRLF、diff和规格检查。
- TypeCheck：pass，最终`build-input-confirmation-final.result.json`退出0；当前产品source diff SHA与`after-identity-stable.json`完全一致，PDB GUID/Age7匹配。
- Link环境：首轮MSVC ARM64 incremental linker内部C0000005/LNK1000，主会话只清本任务ILK生成缓存后同参数重试成功；未改源码/配置/依赖来绕过内部错误。
- Tests：latest-by-name 25/25 pass。原批量`tests-final/test-results.json`为24/25；受影响的PageControl及UI backing在最终Age7产物重跑，两项均0（`tests-confirmation-final/test-results-page-control-hidden-ui-idle-capacity.json`）。没有宣称所有25项在同一最终binary重跑，保留首次失败/旧版对照历史。

## 采样数字独立复核

从原始CSV各运行末20样本重算采样器采用的上中位数，并与summary逐字节比对；并非把名义resource bytes当Private Bytes。

- 第二批新鲜before两次Private为511.3359375/510.36328125MiB；最终Age7 after两次为430.56640625/428.125MiB，各239–240条、约60秒、无强制清理。均值510.849609375→429.345703125MiB，减少81.50390625MiB（15.954579%）。最终两次EXE SHA均为e7757bbabd29...。
- Working Set同法均值516.18359375→435.359375MiB，减少80.82421875MiB。它不与Private Bytes或纹理载荷相加。
- 真实白板周期before统计：desktop508.55859375MiB→returned512.84375MiB；after：desktop429.62109375MiB→returned432.0078125MiB。两次summary的phase Private/WS/GDI均从CSV末20样本独立重算一致；failure=null、forwarder退出0、无强制终止，进入/退出锚点事件与样式记录存在。
- after返回桌面约比其入前高2.38671875MiB，并非完全回到相同字节；不能把系统/堆/布局高水位包装成零剩余。GDI before32→41，after28→31，与用后可再生资源更充分归还相符；它不是内部全部ready证明的替代。
- 局部真实probe：Setting常驻CPU16384bytes、Laser退出/恢复退出0、Freeze idle0、Bar4194304→49152名义bytes、Aux隐藏回收且空闲提交数稳定；这些不额外叠加到81.50MiB进程实测收益。

## 最终结论

已批准的第二批2–5质量门通过。没有剩余确定代码缺陷；规格、源码、最终身份、适用测试与采样口径一致。后续1/6和架构级方案继续未实施，不影响当前范围结论。
