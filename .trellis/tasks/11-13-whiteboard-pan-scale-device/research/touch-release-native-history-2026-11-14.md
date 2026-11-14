# Touch释放历史：实际ARM64纯核回放


> 版本说明：本文repair-final的“最终/锁定”指当时green-4导出快照（核心4eb6c0bc…）。之后最终审查又加入Touch慢scroll残余/预算资格、slop内呈现0及相关回归；历史原生数据仍属于该导出版本，未冒称与最终交付字节一致。最终相同真实轨迹和新路径由交付版headless-pan/生产probe复验，版本差分见本轮validation记录。
2026-11-14。仅操作已导出的冻结模块及独立probe；没有修改产品、现有测试、规范、项目配置或当前工作区版本，没有GUI、Git checkout/reset、commit。main并行实现不参与本baseline。

## 方法与运行证据

六个版本的CanvasNavigation.cpp/.cppm均按export-manifest逐文件SHA复核，通过VS安装自带vcvarsall取得现有14.44 ARM64原生CL环境，直接编译真实C++20 interface、implementation、薄适配probe并链接console程序。每版本四条编译/链接命令exit0、六组运行均exit0；原始argv/退出码/耗时在Build/Logs/pan-touch-release-2026-11-14/history/<版本>/native/result.json，build.log保留。没有复写算法为Python后冒充native。

输入来自固定console.snapshot.log；真实QPC、三位小数像素*.5转系统DIP，按真实consume frame分批。适配层沿生产CanvasPanInput使用max(frameQpc,newestSampleQpc)，防止frame读取后刚到的真实点被内核当future point拒绝。首跑漏了这一步，key4存在偏差；首跑结果另存adapter-v1-before-frame-normalization.json，不作为结论。纠正后六版重编译重跑，当前key4对应146.093593mm，与日志146.09358相符。

每组独立新建内核，**没有前序fling残余**。因此key25只能验证本手势selected，不能用本结果宣称完整旧boost接力已复现。最后终态后按约240Hz推进最多10秒，统计到Idle的后续位移；不引入新手指提前抓停。旧版本在终态帧已有追补行为时，后续滑距不包含该帧已经发生的部分，不能将consumedDeltaDip一概当纯held距离。

## 主要结果

表中距离是终态帧之后到停止的位移，按本机每轴毫米比例作后验换算；前三个老版本实际仍使用legacy DIP profile，不能把物理数值差都归为估速回归。c2与两个本地快照使用原TouchPhysical profile(.19444445,.19791666)，w=0、1440×960 DIP视口。

| 版本 | 原生profile | key26后续滑距mm | key64 | 双指47/48 |
| --- | --- | ---: | --- | --- |
| 7b213d90 | legacy-default-DIP | 138.946 | legacy-no-reason / True | legacy-no-reason / True |
| 4b4edb91 | legacy-default-DIP | 18.940 | compressed-time / False | short-span / False |
| 4706ec0e | legacy-default-DIP | 18.940 | compressed-time / False | short-span / False |
| c2f21bd0 | TouchPhysical | 18.851 | compressed-time / False | short-span / False |
| pre-touch-terminal | TouchPhysical | 1.163 | few-samples / False | short-span / False |
| touch-terminal | TouchPhysical | 67.478 | few-samples / False | short-span / False |

可以得出的结论：

- 7b213d90的key64与47/48会coast，而4b4edb91已分别变为compressed-time/short-span、无coast，4706与c2延续。因此硬零边界并非最新Touch progress修改才出现，已有更早稳定化策略的来源。
- 不能整体回滚7b：同一key64旧核后续滑距约956mm，47/48约471mm；key26初始输出方向还与当前真实向量相反。旧算法同时带有此前要修的过大/错向风险。
- key26在pre-touch-terminal双段质量版仅约1.163mm，在touch-terminal版约67.478mm；后者明显改善旧短尾，不能把整体现象概括为最后一轮单向退化。
- touch-terminal key26释放运动年龄50.0174ms，selected约338.53mm/s，与当前日志相符；它仍存在motion-age折损，是新一轮需要修的具体分支。
- 完整正常key4/25在c2、pre-touch与touch-terminal的本手势raw/selected一致，key4后续146.094mm，key25隔离无boost约127.442mm。实际日志key25因前序残余得到约172.727mm目标，差异是本probe明确隔离了前序fling，不能据此判回归。

## 数据完整性与边界

4/25/26/64/47-48分别17/17/23/3/7个input事件，Down/Up齐全、组内ordinal无缺口。31-33组缺Down/Up且有5处ordinal缺口，未运行该组，不能补造为exact。186-188边界齐全但有3处缺口，运行仅作近似慢尾对照，不作数值保真结论。完整数据见history/fixtures.json及本报告JSON。

7b没有raw/selected诊断API；原始probe为统一输出把终态Step后的contentVelocity填在同名字段，这两个字段在该版本只是surrogate，机器结果已明确标识；不得据此声称它是原始拟合或精确曲线seed。跨版本比较优先使用是否起滑、实际后续位移、原生提供的reason及明确单位。

这证明实际冻结kernel在同一可见输入下的行为，不是完整RTS/Controller/Present链重放，也不是物理手指速度或光学测量。未给旧版强行套当前profile，没有把现有工作区代码复制进旧模块。所有源导出hash与可执行hash、运行结果见[touch-release-native-history-2026-11-14.json](touch-release-native-history-2026-11-14.json)。

## 修复候选3：带真实Apply结果确认的native补充

本节仅编译新导出repair-candidate-3，未重跑前述六版。沿用相同fixture/系统DIP/真实QPC和frame=max(frame,newestSample)归一化，使用该版本真实CanvasNavigation模块。四条编译/链接及六组运行均exit0；导出两模块SHA与export.json及主会话当前锁定源码相符。

每次Step后调用真实`ApplyCanvasContentTranslationChecked`推进模拟viewport，再将返回viewportDelta取负，作为系统DIP实际内容位移连同原motionFeedback标签传给`ConfirmAppliedMotion`；确认发生在下一Step之前。没有直接ACK requested delta、没有改tag或强行补预算。模拟viewport位于常规范围内；这仍是纯核/真实Apply函数模拟，不能代替主会话已完成的Controller生产Apply probe。

| case | mode | 响应来源 | 后续滑距mm | 备注 |
| --- | --- | --- | ---: | --- |
| 4 | measured | Measured | 146.09365 | 原正常独立组保持；9/9 ACK |
| 25 | measured | Measured | 127.44186 | 无前序fling，不能与日志实际boost后172mm混比；12/12 ACK |
| 26 | measured | Measured | 158.13319 | seed约677.65mm/s，原TouchTerminal约338.53/67.478mm；14/14 ACK |
| 64 | distance-bounded | Base | 9.03096233 | budget9.03096781mm；一次真实ACK，不加残余/不触发快甩档 |
| 47/48 | short-supported | Measured | 335.43336 | seed约1305.61mm/s，合法短证据有界估速；2/2 ACK |
| 186–188 | measured | Measured | .27322 | 原始输入有缺口，仅近似；53次尝试、52次接受，未强迫接受 |

64的预算来自先前Step已应用的约9.03mm新方向位移，末次Up没有倒灌预算；其handVelocity字段仍0、seed单独给出，清楚区分“没测得可靠手速”和“距离策略的初速”。所有组没有旧fling，因此ResidualOnly或Measured叠旧residual不在此probe证明范围；由主回归另验证。

26保留last-motion age50.0174ms作为事实，但不再机械把受支持候选减半；原有support仍可限制raw，故boostSuppressed=true并不表示这组出了年龄错误。47/48的335mm是高速短证据在当前基础Touch曲线上的后续自由距离，不是距离降级预算；不能声称所有短拖都只额外走原拖距。

数值距离按约240Hz推进且应用float viewport后求和，4/25与旧probe的极微差为实际Apply舍入；不冒称跨适配器逐字节相同。31–33仍缺边界未执行，所有缺行/无旧残余局限沿用前节。

独立记录：[候选3机器证据](touch-release-native-candidate-3-2026-11-14.json)。原始文件为history/repair-candidate-3/native/result.json、verified-result.json及build.log。

## 最终冻结版本：repair-final

仅编译/运行最终新导出repair-final，未重跑旧六版或candidate3。该导出相对candidate3只有同帧释放诊断清零helper变化；cppm hash相同。四条native编译/链接和六组运行均exit0，六组完整result对象逐字段等于candidate3，包括实际Apply ACK接受数、decision、预算及coast数值。

最终模块SHA：

- cpp：`4eb6c0bcf2ff6f56c91f95632e3ff94170040e8f8325df36e096f53c9cd29037`
- cppm：`97a4893e61ed811640977d062578d5ac046b063d278a7940bb615dea3498c97c`

已分别与export.json及主会话锁定产品源码核对一致。实际ApplyChecked后取负viewportDelta、标签原样确认、下一Step前ACK、Up当次预算不倒灌，以及独立组无旧fling/186缺行近似/31缺边界略过等边界均保持。此结果仍不是Controller生产Apply或GUI证明。

交付应引用[最终native机器证据](touch-release-native-final-2026-11-14.json)；candidate3仅保留为先前阶段证据。最终原始日志/result位于history/repair-final/native/。
