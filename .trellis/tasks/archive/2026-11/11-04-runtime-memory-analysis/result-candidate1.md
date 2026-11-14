# 候选1：保存快照能否正常释放

2026-11-05；调查起点 `e225c210`，候选6保持不动。

## 当前结论

源码审计与真实worker释放探针一致：保存任务结束后，Desktop、PPT和白板观察到的原始大保存缓冲可以正常销毁；Save完成回执尚未领取时也会释放。未发现上一份完整保存快照永久留在普通空闲的确定缺陷。active/legacy镜像、规范页和编码缓冲主要影响操作期间峰值；中间副本的释放以RAII静态审计为依据，本次地址探针不覆盖整个堆。

## 所有权与释放边界

| 对象 | 正常释放边界 | 必须保留的情况 |
| --- | --- | --- |
| Desktop保存snapshot | 成败终态都move到本次worker迭代局部，在下次等待前析构；不依赖领取Save回执 | 实际排队或正在写出时 |
| PPT保存snapshot | 发布完成回执前将save/load请求赋空，包含外层和嵌套容器；失败/异常共用此路径 | 实际排队/处理中；Clear屏障限制latest-wins替换 |
| 白板保存snapshot及JSON字符串 | 每次worker循环的request和exported局部析构；同页pending被替换时旧值析构 | 有界在途保存；当前页、保存失败/新修改页仍保留业务内容 |
| 规范页、导出校验bytes、MessagePack工作缓冲 | 所属函数返回或异常展开时析构；成功返回的document/bytes由调用者继续拥有 | 当前仍在使用的返回值 |
| Load完成回执 | 正常消费、移交后最后owner释放；重启/服务析构也清理 | 未领取、Host交付槽等待或Controller活动接触尚未结束 |
| Clear恢复点、Redo、retained页 | 遵循保存成功/版本匹配、Undo/Redo与页淘汰规则 | 保存失败时继续保留，保障恢复和撤销 |

具体源码行号与失败/停止矩阵见 [保存worker审计](research/candidate1-save-ownership.md) 和 [导出与白板审计](research/candidate1-export-and-probes.md)。生产路径未修改。

## 容易误判的现象

- `pending/queuedSnapshotBytes==0`、白板`Drain()`返回都可能略早于本次局部request的析构。需观察随后真实销毁，不能把这个短暂间隔当泄漏。
- `CloseAndDrain()`会排空并join worker，仍保留尚未领取的Load结果。它不代表丢弃所有完成数据。
- `vector.clear()`会销毁元素及嵌套点数组，可能保留外层capacity。个别区间投影存在此情况；其调用链中的再次copy/move决定容量是否进入长期owner。
- 对象销毁不保证Private Bytes或Working Set立即回到操作前精确值。分配器、模块和合法业务数据须另行区分，不能靠强制修剪工作集伪造回落。

## 保留的元数据

Desktop幂等records在会话内随唯一accepted保存请求增长，到Start/服务析构清理。其终态不含原大snapshot，仍有ID、日期、路径和空容器控制对象。当前格式可见文本内容为每成功记录`202+2L`字节（L为完整路径字符数），另有节点/字符串capacity/固定对象等开销；这是静态字段账，不是实测堆大小。

PPT的失败索引修复账可能按不同generation积累元数据；Save完成回执仍带目标拓扑，最多10000个int32 slide ID的元素载荷为40000字节。正常消费者会领取回执。它们不含原保存点数组；若以后调查长时会话增长，可独立测量这些账，不应直接删除幂等或失败修复责任。

## 运行证据

- 已提交产品二进制SHA-256仍为`e7757bbabd297cd5f75e6401d99d531d3539ac91e4c1bbd880df05e3d22f4dc4`。
- 现有产品离屏/CPU回归4项通过：parked desktop exit、parked PPT retained、loaded retained install、100页whiteboard storage。
- 现有共享CPU保存专项2项通过：Desktop AutoSave、Presentation AutoSave。
- 新增纯CPU `--save-snapshot-lifetime-only` 退出0，32.047秒：16个服务阶段和1个容量负控，登记的缓冲数量及字节全部实际经过free，overflow=0。各保存请求登记约3MiB的点数组及外层向量，不将累计测试字节当节省量。
- Desktop用真实wake、PPT用既有WorkerOperation测试事件门确认原缓冲先被worker持有，再放行观察终态释放；未领取Save回执。涵盖成功、写/索引失败、索引重试、worker异常、latest-wins被替换/最新/首个请求、停止排空；Whiteboard成功/文件系统失败在Store仍存活时观察释放。停止场景按CloseAndDrain之后断言，其余保存释放不依赖worker停止。
- Desktop/PPT Load使用真实返回快照和weak_ptr，consumer持有时不提前释放，最后consumer reset后快照及登记缓冲销毁。白板Load完成文档、同页替换、满队列拒绝及停止边界本轮仅静态审计，未新增这些路径的地址探针。
- 容量负控在vector.clear后要求观察结果仍为“未释放”，直到容器销毁才记录free，避免把size=0误判为容量归还。
- 共享`inkStrokeModelerTest.sln Debug|ARM64`构建最终退出0；首次新增PPT helper插入作用域错误C2601已修正，首次失败日志保留。四个shader在该次solution构建链成功，最终增量链接成功。既有头文件编码/窄化warning未做无关修改。
- 同一最终测试二进制的5项受影响CPU回归全部退出0：Desktop/PPT AutoSave、UInk file、UInk presentation、release-tail；多余参数按90早退，不进入默认窗口/GPU套件。
- 测试EXE/PDB Age2匹配，EXE SHA-256为`2fdcd9e2ac37ea8201598672657e8196c9d568206fad6ae23d1429ca586af797`，4份测试源码hash与BOM/CRLF检查已保存。生产源码/工程配置均无diff，因此本轮按最小范围构建共享测试solution，没有重复构建未变更的产品solution。

独立[质量审查](validation/candidate1/check-review.md)已通过。原始退出码、时间及二进制身份保存在 [validation/candidate1](validation/candidate1)。未取得全堆存活快照或ETW分配栈，不把局部测试外推成整个进程的泄漏排除结论。

## 对后续优化的建议

以普通静止内存为目标，当前大保存缓冲如期销毁，候选1去重/COW主要降低保存峰值，建议继续后置，不为这项调查改变数据表示或保存架构。未消费Load、失败Clear恢复点和元数据增长应按各自owner调查，不能无条件清掉来降低内存。

本轮仅调查与测试，未修改生产保存逻辑、候选6、L0/L1/L2或双device分工；未commit/push。
