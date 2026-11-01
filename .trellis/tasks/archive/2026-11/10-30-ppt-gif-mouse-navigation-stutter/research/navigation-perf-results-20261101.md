# 2026-11-01 人工分段采样结果：长等待定位到 owner 维护排队

## 样本有效性

用户按交接入口完成测试。采集目录 Build/Validation/ppt-navigation-20261101-115104-217-f6c4701c，PID78812，进程正常退出0，capture_errors为空；EXE SHA 0C3D0906D9F08AB29D755517EA03B1F1FA58666BEE35D7F88960A28451474D3C，内嵌/随附PptCOM均为52465C7A0779F6E8CBF4AB72E083ED69DB5F9382893C02021BBCAC663A921526，native_perf_marker_present=true。匹配本轮诊断产物，不是先前Inkeys-draw旧版本。

分析 summary.json：1577条事件、749个span、19条native导航请求，status=complete，0issues；中位请求总耗时150.343ms。按QPC/源/请求与线程关联，无parent/child重复累加。用户实际输入方式及光学页面切换不由这些数值自动推断。

## 核心样本（毫秒）

| 请求 | 总耗时 | native队列 | managed邮箱驻留 | owner执行 | focus |
| --- | ---: | ---: | ---: | ---: | ---: |
| Previous epoch14 | 1355.671 | 0.142 | 1198.671 | 156.192 | 0.086 |
| Next epoch18 | 1144.459 | 0.278 | 703.529 | 440.080 | 0.095 |

### Previous epoch14 / native span79 / managed request14

- 邮箱等待1198.6715ms，被同一owner线程78204上的maintain-binding385重叠345.2583ms + observe390重叠853.3597ms覆盖，合计1198.6180ms，未覆盖约0.0535ms。
- owner执行156.1920ms，其中view-get393为62.052ms，invoke394为93.819ms。
- COM进入managed前0.0839ms、managed返回到native后0.0161ms；owner dequeue到execute0.0562ms；focus0.0864ms。
- 本轮没有精确重复上轮1228ms的旧事件，但重现了同类且更长1355.671ms样本，并分清了这一次的主要空档。

### Next epoch18 / native span103 / managed request18

- 邮箱等待703.5293ms，被maintain-binding569重叠204.5858ms + observe574重叠498.8634ms覆盖，合计703.4492ms。
- owner执行440.0802ms，其中command-precheck577为298.125ms，view-get578为61.717ms，invoke579为79.826ms；不能把440ms全部记为View.Next。
- focus0.0951ms、native队列0.2779ms；与上条共同排除本次长尾主要来自native队列/焦点/跨层封送的解释。

### 其它复核

- 5/19条导航的managed邮箱驻留超过100ms。
- 本轮native队列最大0.3931ms，focus最大0.1853ms。
- Previous epoch10等待127.472ms对应annotation维护重叠127.426ms；维护占用不仅有ROT/观察。

## 归因边界

已证实：本轮长导航等待主要是同一Office owner正执行维护，排队命令无法立即进入；慢维护内仍是Office COM调用，但不能再将整体长尾简单归为PPT文件自身、focus或实际Previous/Next方法。未发现本轮又恢复普通翻页全文稿拓扑扫描。

尚未证实：旧1228ms事件缺少分段记录，不能把新样本的各段回填到旧样本；observe内部哪个COM成员最慢尚无成员级日志；已进入的外部COM不可本地取消，因此调度优化不保证所有长尾消失。

## 建议后续修复范围（尚未实施）

针对唯一owner的维护/请求调度做小范围设计：非必要ROT/批注/descriptor等维护开始前及阶段之间检查请求；拆分命令执行所需的最小fresh场次/窗口校验与较重页面/拓扑维护，避免让已排队请求连续等待整轮维护。必须保留旧场次拒绝、busy/轮询恢复、动态访问与逐笔释放；不能直接把命令搬到ObserveBoundShow之前或取消校验，也不并行访问Office/模拟键盘。

本轮仅分析并记录，不修改产品行为、不操控桌面、不commit/push。是否实施以上调度修复应以单独明确的范围/设计继续；原GUI像素/退出验收不补勾。

## 独立原始日志复核

trellis-check独立重算两条原始记录，PID78812、QPC10MHz，native调用线程85392、owner78204；native COM时间区间唯一包含相应managed请求，并非依赖两侧编号相同。ROT是maintain-binding子项，未重复累计。两条focus结果都是already-foreground，未调用SetForegroundWindow。无剩余数据关联问题。

需保留的重要限制：两条请求都在ROT已运行时到达，仅在开始扫描前加一次队列检查不能消除该样本。下一步应先细化Observe的原有成员读取（身份/窗口校验、当前页/总数、View/State、HWND），再设计维护安全让出与最小新鲜校验；只能在已进入COM调用返回后让出，不取消调用、不跳过安全校验。
