基线 `97369702`，只读核对。文件、着色器、配置键、界面、工程及断言改动均无。未构建或运行自检。下文 Draw3 文件省略 `Draw3.` 前缀。

1. **需要调整｜容量。** `kLaserParticleCapacity` 全部引用：`LaserParticles.cppm:21,343`、`LaserParticles.cpp:89,592–679`、`LaserParticleSystem.cpp:45,72–92,195–211,237–258,341–344`、`Renderer.cpp:1323–1336`。HLSL 宏见 `laserParticleCommon.hlsli:5`、`laserParticleVS.hlsl:50`。两个 `maximumSeeds` 定义见 `LaserParticles.cppm:34,69`，使用见 `LaserParticles.cpp:405,521,537`、`DrawingController.cpp:6233,6237,7768,7805`。`InkeysHeadlessTests` 无引用，原型工程有独立常量。槽、批次分别取掩码，`latestDissolve_` 哨兵取批次上限，行进预算独立校验。

2. **需要调整｜拆池。** Step、Draw、RemoveGroup、结束时刻按池，汇聚与 `KillSlots(LatestDissolveRange)` 只操作激光飘散。Prepare/WarmUp、闲置归还按池，共享时钟与着色器，Reset 清双池。脏区按行进、飘散、光波分账，可见性取并集。现有 Step 假定末尾 N 条同属激光飘散，普通批可混行进与清空。`LaserParticleSystem.cpp:136–370`，`DrawingController.cpp:20446,24632,25539–25588`。

3. **需要调整｜GPU 簿记。** 清空用 `kCanvasClearParticleGroup` 和批号 0，登记为不可汇聚批。按批标组可省逐槽 CPU 写入。回读绑定设备代次和批次身份，避免覆盖未读结果，修剪取 `min(written, reserved)`，零粒同步退簿。登记寿命、脏区后参与可见性。派发前清重叠 SRV，派发后解绑 CS UAV/SRV，再复制计数、绑定 VS SRV。`LaserParticles.cpp:551,626–665`，`LaserParticleSystem.cpp:200–227,321–348`。

4. **不成立｜历史全覆盖。** 清空前可取当前 history 与 Canvas viewport。页面像素减视口后向外取整、裁剪。白板手势改视口，PPT 可恢复存档视口，桌面默认零但支持平移。笔、荧光笔、橡皮、单点已有宽度与 AA 余量，参数化图形另用自身 bounds。背景、PPT 本体、激光在 L2 外，正常提交先入历史。冷加载期间禁清空，但待重放时历史与 L2 可错代，应按快照有效范围降级。`InkHistory.cpp:602–715`，`DrawingController.cpp:17053,20698,21745,23230–23243,23549–23572,25732,26362–26412`。

5. **需要调整｜清空事务。** 现为读回→捕获→发布可见→清页→全脏区→成功才排种子。重做被 `type == Clear` 排除。建议先捕获，成功后才提交 GPU 批并淘汰旧槽。`preserveLaser` 保池，但重置仍取消旧碎裂。命令可在 Step 后消费，粒子与碎裂须共用起始帧。`DrawingController.cpp:21474,21488–21495,23317–23371,24661,25564,26106`。

6. **成立｜工程。** 加源和 `.cso` 的 None、ASCII 转写的 Read/Write 与 Inputs/Outputs、六配置共享 FxCompile、filters 的 None/FxCompile、RC、资源 ID、renderer 字节码加载。历史同工程已有 `Compute`＋`5.0`，多遍用单入口分流或分入口产物。`Inkeys.vcxproj:1282,1565,1612–1655`，`4f81cffe^` 同文件 `:1511–1522`，`Inkeys.vcxproj.filters:2711,3227`，`Inkeys.rc:523`，`resource.h:77`，`Renderer.cpp:1214`。

7. **需要调整｜设置。** Direction 读盘走通用整数，UI/启动以 `==1` 解释。Limit 须严验 0–3。Host 左移一位，owner 右移，Get 返回原字；有效效果会滤掉档位位，热切须独立于 Inactive 锁存。`InlineStack` 必画线，增加无分隔组合即可。边界自检补缺省、四档往返及非法类型/超宽。`Other.Config.cpp:842`，`Host.cpp:1885,3286`，`LaserParticles.cppm:120`，`Setting.Controls.cpp:2269`，`IdtMain.cpp:874,3272`。

8. **成立｜自检。** `--draw3-laser-resources-test` → `RunLaserResourceLifecycleValidation`，含环、绘制、回读；Debug harness 默认开。`--draw3-laser-raster-failure-test` → `RunLaserRasterFailureProductionProbe`，含触顶、圆点、汇聚。均无窗 WARP FL11_0，可测 CS/UAV。选项另在 `--draw3-parked-desktop-exit-test`。`Renderer.cpp:1227–1684`，`HiddenWindowTest.cpp:5199–5258`，`DrawingController.cpp:13954,16324`，`Inkeys.vcxproj:84`。

9. **不成立｜分步。** S1 删常量会断遗留调用。先迁单池容量、CPU/VS 合同及全部测试，再拆池/降档，再直写、GPU、设置。低档保单批密度；独立淘汰会改变溢出结果。高档触顶样本需加长，GPU 记录无随机数字段。`implement.md:12–69`，`DrawingController.cpp:7763`，`LaserParticles.cppm:229`。

10. **需要调整｜风险。** 概率采样只保证期望和上界；触顶时 GPU 原子顺序无法复现 CPU 截断集合，2×2 对细线也有偏差。GPU 清槽可免随粒数增长的 CPU 工作。实测留给主 Agent：原子争用、满池绘制、Win7/WARP、Win32 内存与设备重建。相邻问题是碎裂可见性在帧尾可能被覆盖。`LaserParticles.cpp:494–537`，`DrawingController.cpp:24643,25588`。

**必须先改设计才能开工的问题**  
定稿槽与批次身份、独立降级、清空提交及回读归属、L2 错代退路，并修订低档溢出和概率采样的验收口径。