# SVG独立数值计量设计与验证

本次仅补S4计量，不修改已冻结产品修复、默认视觉或后端。上一批Debug/Release EXE、raw、像素与Headless证据保持原版本有效，新版本单独记hash。

## 最小接缝

现有Ui3SvgStage只有Parse/Raster/Upload/Draw四项，无Count；固定数组长度由最后实际枚举Draw+1派生。新增独立owner线程TLS SvgStageTimingScope和固定数值Ui3SvgStageTimings（四段calls/ms、clockReads），不写SvgObservationScope/CurrentUi3SvgScope，不绑定对象、缓存或COM，不参与完整观察/forceCacheBitmap/绘制决策。默认sink=null时不读新时钟、不分配。

Ui3SvgStageTimer复用现有四段钩子。原probe启用时仍读同一对probe时钟、保持旧clockReads/elapsed语义；numeric同时开启复用该结果，不再额外读钟或NoteOperation。仅numeric开启时用steady_clock，可通过scope注入纯数值测试钟；嵌套scope（包括null屏蔽）按LIFO恢复。计数为钩子执行次数，Parse含现有内容/尺寸解析入口，Raster含lunasvg内部几何，Draw仅CPU DrawBitmap提交，不代表GetDC内全部SVG WARP执行；UTF转换/换色在parse钩子之前的工作不在四段内，不把四段之和冒充整个SVG CPU函数。

真实Scheduler Bar回调用独立scope包住状态投递和mainFrame；预分配有界32768个固定数值帧槽，owner唯一写入，按frame_ticks/ordinal/phase与raw Bar callback逐一核对。退出join后写CSV，溢出/缺配/CurrentProbe变化均使fixture失败。新svg-pilot模式仅100/200%、一轮、每方向2cycle；单独环境开关启用numeric，不改旧pilot/full默认。

## 红绿与透明性

先添加API/scope与无窗口合同、保留旧StageTimer未接numeric，运行既有--bar-eraser-offscreen-test得到真实红测。绿版再连接计数/elapsed；测试覆盖null不读注入钟、numeric-only仍无CurrentProbe、四段计数、nested/null scope恢复、原probe同时启用clockReads只增2和相同elapsed、真实同轨迹cache bitmap不变/无需parse-raster-upload/整幅BGRA一致。仅运行适用offscreen，不重复上一批5m47 Headless全套；不修改共享headless逻辑。

## 实际红绿与版本

- 原冻结产品Debug final.exe（4E9B53F6…）、隔离Release（C0BAD63F…）及全部原始矩阵保持，未覆盖历史证据。
- 红版完整InkeysRepo.sln Debug|ARM64 exit0；svg-red.exe SHA256=20C7A0C8968DC8EC4C8C8D872CB128B1A2FCA7BC19B3FD8B9E49A8ABC5A44083。--bar-eraser-offscreen-test正常用户OS exit1，恰好N02/N04/N06三个新合同失败（原timer未写numeric）。结果及完整原报告保留svg-red-test-result.json、svg-red-contract-results.log。
- 绿版同Solution/原生ARM64 MSBuild、同PATH规范化、/m:1 /nr:false、600s监督exit0；instrumented svg-green.exe SHA256=0F5BD6755DAB9E533999A5B8D922D2C3C1A1D9701724BE0BA25C11A8169635C5。
- 绿版--bar-eraser-offscreen-test可靠OS exit0、完整报告failures=0。N01–N07覆盖null无注入钟读数、numeric-only和嵌套/null恢复、双sink同一时钟对、原probe身份/计数不变、真实同cache bitmap且Parse/Raster/Upload零额外调用、整幅96×96 BGRA（36,864bytes）timing on/off逐字节一致。该像素证明范围是既有真实单SVG renderer轨迹，不能冒充新增整套workspace像素矩阵。
- N07固定4096次微控制：default-null timer对9.546ns，numeric timer对721.948ns，增量约0.712us/阶段（当前Debug一次观测，非产品延迟承诺）。按pilot真实calls折算每Bar callback约7us量级，未从测得阶段时间中扣除。默认null仍无内部读钟/分配；实际fixture内无COM、日志格式化或逐帧写盘。

不重编Release、不重跑270次矩阵或上一批5m47全Headless。本轮只扩纯计量接缝，没有改headless测试、冻结产品逻辑或原probe默认语义；共享probe的原行为与新增接缝由既有真实offscreen全部合同一起回归。原Headless OS exit0仅对应上一批明确hash，未将其冒充新instrumented版本全套结果。

## 短采样与分母

INKEYS_UI3_TRANSITION_PERF=svg-pilot，INKEYS_UI3_TRANSITION_SVG_TIMING=1；正常用户、真实Scheduler、默认动画/双光/WARP，100%/200%各一轮、六向每方向2cycle（共24次转场），OS exit0。结果目录TestResults/ui3-workspace-transition/svg-numeric-pilot，仅复制本次100-1/200-1文件，不夹带current中的旧150%/其它轮。两份raw sealed、batch/callback/数值槽掉样0，sidecar 958行全部按raw Bar callback frame_ticks/ordinal匹配，CurrentUi3SvgScope始终null且原生检查无变化。496/462个Bar回调中，真实成功提交301/272；没有用空Continue或Idle计为提交。四段所有calls都落在这573个成功帧，计时clock_reads逐行等于2×四段calls和。

下表calls/ms为各倍率全部六向两cycle的总和；四段顺序为Parse、Raster、Upload、Draw，Draw仅CPU提交。主要占比采用相同真实成功帧的Bar callback elapsed分母，另保留全回调分母，不以Idle稀释结论。

| 倍率 | 成功提交/Bar回调 | P/R/U/D calls | P/R/U/D总ms | 成功帧SVG四段占Bar elapsed | 成功帧SVG四段P95 | 成功帧Draw/GetDC P95 |
| --- | --- | --- | --- | --- | --- | --- |
| 100% | 301/496 | 550/542/542/3334 | 21.016/52.191/52.599/3.995 | 4.59% | 0.841ms | 2.788/6.384ms |
| 200% | 272/462 | 501/493/493/3013 | 21.383/45.585/42.679/3.803 | 3.03% | 0.817ms | 2.646/14.719ms |

仅Parse/Raster/Upload的成功帧占比100%为4.45%、200%为2.93%，P95分别0.833/0.792ms。全回调分母的四段占比为3.81/2.62%；主要结论不用此分母。逐方向数据如下，不能只挑总体分位隐去WhiteboardToDesktop长尾。

| 倍率/方向 | P/R/U/D calls | P/R/U/D总ms | 成功帧SVG四段P95 | Draw/GetDC P95 |
| --- | --- | --- | --- | --- |
| 100/DesktopToPpt | 113/113/113/602 | 4.812/5.123/8.466/0.718 | 0.685ms | 3.383/6.176ms |
| 100/PptToDesktop | 98/98/98/598 | 3.465/3.886/6.481/0.628 | 0.473ms | 1.842/4.629ms |
| 100/DesktopToWhiteboard | 44/42/42/566 | 1.850/4.743/7.045/0.680 | 0.456ms | 2.008/5.319ms |
| 100/WhiteboardToPpt | 126/124/124/530 | 4.278/22.608/9.009/0.638 | 1.010ms | 2.011/7.064ms |
| 100/PptToWhiteboard | 132/130/130/558 | 4.722/8.162/12.788/0.707 | 1.305ms | 3.193/5.249ms |
| 100/WhiteboardToDesktop | 37/35/35/480 | 1.889/7.668/8.811/0.625 | 0.795ms | 7.111/24.648ms |
| 200/DesktopToPpt | 96/96/96/566 | 4.148/7.109/7.506/0.673 | 0.489ms | 1.542/9.744ms |
| 200/PptToDesktop | 100/100/100/610 | 3.715/5.590/7.634/0.678 | 0.486ms | 1.984/10.095ms |
| 200/DesktopToWhiteboard | 36/34/34/445 | 1.592/4.757/4.954/0.505 | 0.459ms | 2.004/14.378ms |
| 200/WhiteboardToPpt | 120/118/118/486 | 4.532/9.813/8.834/0.584 | 0.921ms | 2.273/10.410ms |
| 200/PptToWhiteboard | 117/115/115/492 | 5.810/7.965/8.242/0.657 | 0.738ms | 2.240/11.507ms |
| 200/WhiteboardToDesktop | 32/30/30/414 | 1.586/10.350/5.509/0.706 | 0.922ms | 11.309/52.983ms |

## 慢帧、结论与后续选择门

完整逐帧与top10在svg-analysis.json，未移除长尾。100%最慢WhiteboardToDesktop batch115.399ms/GetDC51.056ms/SVG四段0.021ms，mask create=0；同倍率WhiteboardToPpt另有batch38.551ms/SVG四段17.683ms（其中Raster16.530ms、Parse0.163ms、Upload0.964ms）的离群，不能说每一帧SVG都便宜，未剔除此样本。200%WhiteboardToDesktop前三慢为batch110.120/77.682/53.224ms，GetDC86.083/52.983/46.033ms，SVG四段6.002/0.817/0.336ms，mask create均0。两个倍率全部mask create总数仅3/2，不构成这些慢帧的反复创建证据。本次短pilot时段长尾明显，单轮两cycle不能当性能稳定性验收或与上一批不同版本/时段计算收益。

本次Debug短pilot所测同步四段中，数据支持排除“同步parse/raster/upload是100%/200%这批默认六向样本的主要普遍瓶颈”，没有足够收益证据去改变SVG算法或加缓存；不能外推150%或Release。四段不覆盖前置UTF/替色，DrawBitmap延迟的WARP执行仍可能落在GetDC；不能由提交CPU低就排除所有SVG栅格执行，也不能将GetDC全部叫做纯复制。现有测量仍把主要墙钟成本指向GetDC处的flush/互操作等待，但尚未分清具体D2D命令执行与互操作成本。

后续门：先按同一成功callback/tuple细分GetDC同步成本归属，只有证明某类实际D2D命令或互操作面积主导后再选小候选；不会因现阶段不足就再加显式Flush/EndDraw、关闭双光或重挂proof。若下一步必须收紧横向storage/source对称锚点、改backing布局/后端/提交分批或默认画质，需要独立具体设计，重新列出负原点/DPI/反向/失败/像素与成功事务门禁，不能作为此次纯计量的顺手实现。未找到足以安全关闭R4差距的现成小修时，应保留当前已验证改善和R4未完成事实，等待主会话决定下一范围。本次在此交接，不再继续猜测优化。

## 最终交接

新增API与四段列已由主会话同步规范；无产品SVG/光影/后端改动。源码最新hash/编码见TestResults/ui3-workspace-transition/svg-instrumented-source-manifest.json（9份代码均CRLF、BOM保持），git diff --check exit0。历史Debug final.exe 4E9B53…与Release C0BAD63F…文件hash复核未变，instrumented版本0F5BD675…独立记录。当前11个tracked变更包含原8项及本轮Probe.h/.cpp与EraserAttribute.Test三项，不含shader/工程/依赖变更。没有再次运行完整矩阵、Release或Headless，没有commit/push。
