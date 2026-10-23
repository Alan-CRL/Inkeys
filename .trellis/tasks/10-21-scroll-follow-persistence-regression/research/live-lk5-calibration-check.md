# 双向 LK 五层校准：独立检查

2026-10-23；任务保持 in_progress。checker只读精确源码差异、before快照、完整构建/CLI/矩阵原始日志和独立诊断；没有重复实验、构建、产品CLI、GUI、新截图或产品源码修改。本节放行原真实short与相同128px/19200px long复验，不提前声称实屏修复成功。

## Findings (fixed)

无checker产品源码修复。实现者的生产改动严格为 `VisualMotionTracker.cpp` 两个 LK 调用的 maxLevel 3→5及一条中文小图层级限制注释。独立按before字节替换这两个数字、去除该注释后与当前文件完全相等：没有改win21、FB/残差/空间/覆盖/跳变门、出生锚点、拒绝事务、三帧Pause、自动恢复策略、接口、线程、全局OpenCV配置或捕获后端。

- Tracker before E336887415B4A0128AF4C742B1110587DED1DD2E4E83EC43991DB48BD7FFF4C0；当前 `BF3409F88F606C82B31AE4BEB30B592C105E945028C115FF06109EDB843B1978`。
- tests before FF9C0B7FB0A8D7F5AA94EF7F8855937D9A08C31C2EB537DCD70517E8DBD9EA6A；当前 `EF45B732AE3D2D9432F9FFA76E6C06C1697BE17E711F01B152F6B2BEA92FE667`。移除新增native块/两个调用及新增块的分隔空行后，旧tests逐字相等；同时核对Git HEAD按原CRLF checkout转换后与before一致。
- 两文件UTF-8、原BOM状态、纯CRLF和EOF保持，`git diff --check --ignore-submodules=all` exit0。checker初稿字节断言曾误把Git blob的LF与工作树CRLF直接比较，随后按真实before快照核验，并处理新增块自身分隔空行；没有因此修改源码。

## 测试语义及敏感性

新增固定2880×1808 BGRA、沿专用TexturePixel不可变世界公式按独立camera直接生成每帧，不递推warp、不用Tracker结果生成GT，也不给光流GT初值。小图能力不足仅为观察，不新增“必须永远拒绝128”的断言。

- native静止300帧：每帧必须Tracking且accepted、相对原点漂移≤.5，突然停止每步漂移≤.5。
- 16/32/64/128/192像素、二维四个方向，共20组，每组含移动、停止、返原点、反向、再停止/返原点，120帧；全部合法prefix误差仍≤max(3px,path×.002)，不能只要求最后位置正确。
- 128px长程往返284帧：峰值142×128=18176px，超过10个1808px ROI高度；路径36352px。accepted初点存活归零后必须继续可信、补点累计>320。它不是GUI所需的19200px峰值，不能互相替代。
- 四个**实际native大尺寸**负例：多数固定+中部4px次流、新页/无重叠、scale1.02、rotation1°。它们使用相同2880×1808图像和生产5层，非旧960×540自动clamp3路径；每次拒绝都要求total/delta保持0、reference sequence/generation不变，前两次非Paused、第三次Paused，随后原图仍锁存Paused。没有仅打印reason就称事务通过。

同一新128正例连原L3的独立敏感性执行：PID33836 exit2，日志是两条实际Expect失败（every frame accepted/prefix error），InconsistentTranslation、FB-valid43/inliers2、error128/limit3；不是工具或编译错误。新用例确实能检测旧参数局限。

## 实际完整矩阵及构建

`texture-motion-diagnostic/native-calibration/tracker-matrix-result.json`：当前headless SHA `CC3F315472FA01EAAF1DD9E00557C9099868CF1C6B77F36963CE1D4138FFF0A5`，PID4204，`--visual-motion-tracker-only`，timeout600000ms，exit0/334.743秒，stderr0B，原stdout末尾 `PASS visual motion tracker failures=0`。

checker从原stdout独立计数：旧14组6918合法帧全部accepted、rejected/paused0；新22组704帧全部accepted、rejected0。新增4负例原始最终reason依次AmbiguousMotion、ForwardBackward、NonTranslation、NonTranslation；对应冻结断言在成功进程中全部执行。旧变形、歧义、无重叠、拒绝恢复与严重无效检查仍在原调用链。

| 新native结果 | 原始观测 |
| --- | --- |
| 静止300 | 最大漂移0px |
| 20组大步/停止/反向 | 全120帧通过，最大累计误差约.00022px |
| 128px往返 | 284/284接受，最大/最终误差.00118px，最大单步误差.00021px |
| 真接力 | 第14帧initialSurvivors首次0，此后270可信帧；补点累计13853，最终初点0 |
| 新native负例 | 四类均保持最后可信参考，第三次暂停，无自动恢复 |

完整 `InkeysRepo.sln Debug|ARM64` 原日志 `motion-tracker-build-native-lk5-calibration-1023.log`：Build succeeded、0 Warning/0 Error，内部18.49秒，wrapper18.935秒。当前主EXE磁盘hash独立复算为 `8DAF6D3B9DB1368C487CEB76CEA9E8EE733F8444CCE68FD421207FD133FDED21`。

最小 `offline-7819578/result.json` 独立读原27行：25坏argv各exit2/双输出0B；protocol exit0/276ms、state exit0/118ms，原stderr各PASS。没有重复全套157秒无窗口测试，也未把protocol里的Capture/Host/ACK seams说成实屏。

## 尺寸、图案与性能边界

ignored final-source/compile.cmd实际编译生产BF3409 Tracker而非旧临时variant。三次receipt均exit0且同source hash：native28.918秒、1920×1080 1.547秒、960×540 1.114秒。**诊断执行exit0不是所有位移都成功**：原raw仍有能力拒绝，需按每条accepted解释。此诊断是合成identity像素/current geometry，历史失败Mag图像未保存，不能称历史Mag帧回放或唯一故障根因证明。

独立3/4/5比较支持选5作为当前native纹理最小成功候选：4仍有128方向失败，5使两类native纹理128/192四向及原native pose路径通过；256仍有限。当前实测OpenCV小图实际最高层960×540为3、1920×1080为4：前者128四向仍可拒绝，后者受控横向部分改善、垂直仍拒，原Canvas仍有限。不能宣称所有屏幕/应用均可靠支持128，更不能许诺固定最大位移范围。

完整矩阵只计Process的native静止P50/P95约33.295/43.828ms，long约47.795/70.574ms；20组大步P95约40.892–95.869ms。Quantile沿原 `floor(q×(n−1))`，六帧小样本的P95不是nearest-rank；未输出逐帧max，不补造该数值。334秒总测试含世界图生成和其它测试，不是捕获FPS；这些Debug离线结果也不等于线上帧率或稳定性能承诺。

## Findings (not fixed)

- 当前能力依赖实际可用层数、纹理和方向；小图大步/256px等限制如上，属于现有校准范围，不改门槛掩盖。
- 新native没有每帧单独断言点预算≤320；生产AddFeatures原硬上限及旧14组预算断言保持，不能称新增此断言。按主会话决定不为非阻断观测项打断重跑。
- 新native仅公开P50/P95，缺逐帧耗时max；如实报告，不临时增加范围。
- 实际新binary的原short及相同128px/19200px GUI长程仍须真实复验；离线校准不自动将旧失败run改判通过。外部应用、硬件笔/触控和旧系统也未因此验收。

## Verification

- 精确生产最小差异、旧tests字节保持与格式：pass。
- 完整ARM64编译/类型与链接、原始新旧完整矩阵及native安全负例：pass。
- 最小CLI和最终生产源码三尺寸诊断receipt：已核pass执行结果，能力拒绝范围另列。
- 无发现阻断原真实short→同128px/19200px long复验的问题，已通知主会话放行；checker没有运行GUI或重复构建测试。

离线复核摘要：`texture-motion-diagnostic/native-calibration/checker-calibration-verification.json`。
