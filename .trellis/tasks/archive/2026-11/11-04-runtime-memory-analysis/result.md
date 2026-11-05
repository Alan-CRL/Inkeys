# 首批低风险内存优化结果

日期：2026-11-05；分支 `diagnostic/memery`；基线 HEAD `ddfe8a200fea7694ff066cc10114a729a9027885`。本批次代码、采样、相关回归和静态审查均已完成；不 commit/push。后续有首用/恢复/历史语义风险的候选保持未实施。

## 实测结果

本机 Windows ARM64、主屏2880×1920、`Debug|ARM64`。正常启动后不交互，前后各启动两个独立进程，每个约60秒、250ms采一次。每次取末20个样本（约5秒）的中位数，再对两次结果取均值。所有MiB均为2^20字节。

| 正常启动稳态指标 | 修改前 | 修改后 | 下降 |
| --- | ---: | ---: | ---: |
| Private Bytes / PrivateUsage | 571.26 MiB | 510.16 MiB | **61.10 MiB / 10.70%** |
| Working Set | 581.57 MiB | 517.25 MiB | 64.33 MiB / 11.06% |
| GDI对象（每次末尾） | 38 | 32 | 6个 |

Private Bytes是私有提交，Working Set是当时驻留物理页，两者不相加；像素资源/虚拟映射也不能再加到这些总量上。上述百分比仅代表本机Debug启动后桌面空闲条件，不外推为Release、所有分辨率或长期使用的收益。

| 单次采样 | 稳态Private MiB | 稳态WS MiB | 观测Private峰值 MiB | GDI对象 |
| --- | ---: | ---: | ---: | ---: |
| before-startup | 570.793 | 585.707 | 586.609 | 38 |
| before-startup-repeat | 571.727 | 577.438 | 578.223 | 38 |
| after-startup | 510.609 | 519.941 | 513.746 | 32 |
| after-startup-repeat | 509.715 | 514.551 | 514.246 | 32 |

第一次before包含首次部署，其他运行复用生成配置；after目录复制before配置与资源并替换EXE/PDB。两次单独值保留，以便识别缓存和进程噪声。4次正常启动均由本任务对自有窗口发送WM_CLOSE结束，进程返回6，无强制终止；这些是采样结束状态，不当作退出码0的正确性测试。

### 资源账交叉确认

- `VirtualQueryEx`在约46秒的快照中，大小恰为 `2880×1920×4 = 22,118,400 B = 21.09375 MiB` 的私有已提交区域，before两次均4块，after两次均1块；与去掉三张全屏DIB的源码变化相符。没有ETW分配栈，不单凭区域大小断言其调用栈。
- 三张像素名义合计63.28125MiB；实际进程Private平均下降61.10MiB，差额含其他分配高水位和运行噪声，不将理论载荷包装成实测值。
- 同一时点MEM_MAPPED约88.82MiB、MEM_IMAGE约471.59MiB，前后相同。它们是虚拟区域分类，映像提交大小不代表全部物理驻留，也不是可直接删除的私有内存。
- 纯CPU `--draw3-history-benchmark` 前后各正常退出0。66条measured记录的digest、stored/retained/visible/redo、tile及pieces计数全部一致；各场景的内部Private基线中位数下降约15.60–15.88MiB。该入口早于产品DPI/窗口初始化，不能用它的下降量替代正常GUI启动的61.10MiB。

## 实际代码变更

仅6个源码文件，14行新增、15行删除；另在现有资源规格增加一个小节。

1. `Inkeys/IdtImage.cpp`：`alpha_drawpad`、`tester`、`pptdrawpad`保留为空对象；生产ClCompile无消费者，停用静态整屏分配。
2. `Inkeys/IdtMain.cpp`：删除这三张旧表面的启动resize；DPI日志、`LegacySurfaceReady`与28处启动里程碑引用顺序保持一致。
3. `inkStrokeModelerTest/draw3/uink_draw3_export.cpp`：局部规范页只读访问改为const引用，免一页点/operation深复制。
4. `Draw3.AutoSave.cpp`：导入结果去掉const，让已有std::move真正移动snapshot。
5. `Draw3.PresentationAutoSave.cpp`：校验后移动独占导入snapshot到局部投影；来源不再使用。
6. `Draw3.DrawingController.cpp`：捕获完成的独占临时页移动进activeCanvases；legacy镜像完整保留。

空闲下降主要来自旧DIB。四处所有权优化在保存/导入/导出时减少复制，本轮没有单独量化大文稿峰值，不把它们额外加到61.10MiB上。生产源文件的UTF-8 BOM与工作区CRLF全部保留，Git的LF对象规范化不改变这一点。

## 架构与流畅度静态确认

- L0/L1/L2的结构、合成/提交/清空语义、shader均未修改。
- UI继续共享既有WARP device；Draw3继续使用独立device及原hardware-first/fallback策略，未合并或改变后端。
- 没有新增首用/首笔分配，没有在绘制、输入、Undo/切页或显示路径新增解码、锁、同步或资源重建。
- 三张空对象没有生产读写方；引用owner在循环期间不变，move均在校验后且来源不再使用；队列、持久化格式、active/retained/legacy及历史索引不变。
- 按用户要求，流畅度只作上述静态确认；没有宣称动态FPS/延迟性能验证。

## 构建与回归

- `InkeysRepo.sln Debug|ARM64`：before和after均退出0；after日志证明6个修改TU实际重编译。使用动态定位的ARM64 MSBuild、同次PATH规范化、单节点直接调用。
- 共享TU对应的 `inkStrokeModelerTest.sln Debug|ARM64`：退出0；4个shader、共享导出器与测试目标均成功生成。两套solution没有改工程、依赖或工具链。
- 10项相关回归最终全部通过：UInk文件/Presentation往返、parked桌面退出保存、parked PPT retained保存、loaded retained安装、白板存储、设备策略、WARP像素、hardware像素、启动进度。
- UInk文件测试首次有2处目录lease断言失败。与测试相同Win32标志复核显示 `TestResults`/`release-hardening`不存在（错误2/3）；只创建其要求的空目录后，该项重跑退出0。没有改测试或产品来消除失败，首次失败日志完整保留。
- 构建出现既有第三方/历史警告，包括hashlib++转换、modeler数值转换、独立solution部分UTF-8头文件的C4819；本次未为清零warning修改无关源码。

## 环境与未测范围

WPR实际 `VirtualAllocation.Verbose.File` 开启失败：当前Windows令牌不是管理员，返回0x80070005。没有弹UAC、绕过锁屏或改变IFEO/系统诊断设置，录制状态仍为not recording。故没有ETW堆/VirtualAlloc调用栈、按模块精确百分比或GPU驻留归因。

首个沙箱内MSBuild多节点调用反复无法通信，尚未源码编译即停止本任务进程树；随后沙箱外单节点直接调用正常通过。这是Codex构建环境问题，未改变源码/工程来绕过。

本轮真实运行覆盖正常启动/桌面空闲；历史/存储/像素为无窗口或离屏入口。没有Computer Use，没有设置/白板/PPT真实用户交互阶梯，没有打开Office/WPS或用户文档，也没有Release或长时间压力内存测量。

## 需确认后才实施的候选

| 候选 | 源码显示的潜力 | 留待确认的原因 |
| --- | --- | --- |
| Laser主体、辅助selection ULW、trusted快照延迟分配/回收 | 每8WH在4K名义约63.28MiB；trusted为4WH约31.64MiB | 可能影响首选工具、首笔/选择显示或视觉失败回退 |
| 有效Freeze DIB按需或小表面化 | 4K CPU像素约31.63MiB | 当前有恢复提示与白板共享窗口合同，可能带来首用/交接代价 |
| Setting按页加载、删除CPU BGRA恢复副本 | CPU图片约8.83MiB（倍率1），另有纹理/字体 | 会改变首次显示或device epoch恢复时的解码/分配路径 |
| GPU缓存页粒度/收缩、CPU不可达redo分支回收 | 16/24MiB纹理页及长期历史数据 | 可能增加Undo/切页重建，或影响稳定索引/Clear/持久化语义 |
| 私有PPT请求进一步取消重复legacy载荷 | 每个请求的一份活动页点/operations数据 | 需额外审阅兼容消费者和新旧表示编码一致性，当前完整保留 |

这些都没有实施。后续方案仍须保持三层画布、双device/后端隔离与正常流畅度约束。

## 可复核产物

- [原始采样与汇总JSON](validation/memory-results.json)、[四次启动CSV](validation/startup-comparison.csv)。每次另有samples.csv、summary.json和VM区域快照。
- [before身份](validation/before-identity.json)、[after身份](validation/after-identity.json)：EXE SHA256、PDB GUID/Age和匹配结果。
- [原始回归结果](validation/test-results.json)、[目录补齐后的UInk重跑](validation/uink-file-retry.result.json)。
- [产品after构建](validation/build-after.result.json)、[共享solution构建](validation/build-uink.result.json)、[审查记录](validation/check-review.md)。
- [源代码内存结构](research/memory-overview.md)、[首批范围](research/first-batch-boundary.md)、[UI选择依据](research/low-risk-ui-plan.md)、[快照选择依据](research/low-risk-draw3-plan.md)。
