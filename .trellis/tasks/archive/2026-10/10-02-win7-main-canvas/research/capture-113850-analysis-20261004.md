# Research: 新 Win7 采集 113850：WARP 透明 Draw 与硬件实际刷新分界

- Query: 核验新候选包；完整分析正常 stdout/stderr/IDT 的输入、GPU adapter、源像素、错误与生命周期，结合用户“几乎不能出现内容，都是切换到橡皮，画布才会刷新”寻找下一段判别证据。
- Scope: internal；六个采集文件只读及当前日志口径源码。未运行 EXE、GUI、构建或 Git 操作。
- Date: 2026-10-04

## Findings

### 原始文件与身份

源目录 `D:/Personal/Downloads/Win7-Diagnostics/20261004-113850-937/`。所有文件严格 UTF-8 验证成功；终端中文显示乱码不代表原始文件编码错误。

| 文件 | Bytes / Lines | SHA-256 |
| --- | --- | --- |
| console.stderr.txt | 35788 / 117 | `252F97A3EE598DEBA52436D411EB9CFD5A0C2B5979618623956D661D90E45111` |
| console.stdout.txt | 954746 / 5201 | `3A41B05F6E4D775844B10CBAA7AEFCF80B39E5A3B2F6DEBCEC12137A9A1C5D2E` |
| identity.txt | 333 / 8 | `D1836289B362E0B2D1F0FEDB5399D999E367A7AD5C20AE9DAE7415C7CF540DC9` |
| idt1791085132325.log | 88823 / 40 | `4754526496D582F1372EE19B7499D4C2AC6A62290456A65EC8D1664DEB6BAE9A` |
| pixel-test.stderr.txt | 5463 / 59 | `EC29BF9F1B345353B6E8412B3C425FBA6317B2BCD3A68C3E8D18B7322078FCC3` |
| pixel-test.stdout.txt | 1295 / 3 | `4EB080F64B99C339BCA2ABFE3928850F4CEC3D56E0054810523FB9E9171309D2` |

`identity.txt:1-8`：EXE 为桌面 `Inkeys-Win7-Gpu-Probe-PS2-x64-Release/Inkeys.exe`；SHA-256 `2D468B91156333C5CBD9E1E3C82D5713961DD94647ECCF4B44285FFC8EDDD72C`，与主会话交付候选一致。OS NT6.1.7601 SP1、PowerShell2.0。开始11:38:51.4988662、结束11:40:27.3116780，独立 `pixel-test-exit=1`，正常应用 `exit=0`；正常应用退出0不能代替自检PASS。

`console.stdout.txt:1` build=`draw3-Oct 3 2026T16:15:40 arch=x64 msvc=1944`。包字节身份得到确认，build不含源码Git SHA；没有补丁清单，不能直接断言这台设备仅安装KB2670838。

### 独立 WARP 自检失败发生在不使用 ULW 的路径

`pixel-test.stderr.txt:1-13`：128×128 HWND visible=0；WARP/FL11_0、普通FLIP_SEQUENTIAL、backbuffer/offscreen RTV初始化成功。`options_hr=0 srv_no_overwrite=1`，不能预设这台Win7不支持SRV NO_OVERWRITE。

| 用例（stderr行） | alpha / RGB 非零 | 中心 BGRA | Pipeline | 结果 |
| --- | ---: | --- | --- | --- |
| backbuffer.clear（14-18） | 16384/16384 | (64,32,16,128) | 已知色Clear | PASS，mismatch=0 |
| offscreen.clear（19-23） | 16384/16384 | (64,32,16,128) | 已知色Clear | PASS，mismatch=0 |
| backbuffer.cursor.production（27-33） | 0/0 | (0,0,0,0) | IA6、VS6、PS1368 | FAIL |
| offscreen.cursor.production（35-41） | 0/0 | (0,0,0,0) | IA6、VS6、PS1368 | FAIL |
| backbuffer.cursor.blend_disabled（43-49） | 0/0 | (0,0,0,0) | IA6、VS6、PS1368 | FAIL |
| offscreen.cursor.blend_disabled（51-57） | 0/0 | (0,0,0,0) | IA6、VS6、PS1368 | FAIL |

四次Draw的资源identity=1、viewport正确、MapReadback与pipeline statistics HR均0。`:58` deviceRemovedReason=0，`:59` 正式production_cases=4/failures=2/FAIL；两项关闭混合为对照，不计正式分母。`pixel-test.stdout:1-3` 上传成功，Draw前后RTV/backbuffer、VS/PS、b0/t0绑定匹配。upload-success中的bindings=0/targetMatch=0只是该阶段未查询，不能当作绑定错误。

自检没有ULW调用；两个Clear可正确读回，offscreen与关闭混合Draw也全零，降低纯swapchain/readback失效、生产混合单独出错或最终USER32的解释力度。PS1368说明实际执行，仍不证明PS输出非零或CB/SRV/interpolant数据正确；尚不能唯一归因shader/上传/discard/目标写入。

### 正常应用使用 VMware 硬件，源与 DIB 已有非零笔迹和橡皮光标

**本次 normal stdout 不是全零；不能沿用旧设备结论。非零源仍不等于桌面正常刷新。**

- `console.stdout:7-12`：Hardware首个D3D初始化HR=0、FL11_0；VMware SVGA3D，Vendor0x15ad、Device0x405、SubSys0x40515ad，dedicated134217728/shared1878781952，UMD raw high0x90011/low0x40001。正常主画布没有走WARP。
- `:15-19`：waitable失败0x887a0001后普通FLIP成功，2318×1360/BGRA8/buffers2/flags0。
- `:43-47`：Primary初次全帧为透明源+alpha1命中底层，visible=1/layered且没有WS_EX_TRANSPARENT。初次空白不能当作操作后没有出墨。
- 完整stdout共12条ULW-pixels：7条源alpha/RGB同时非零；5条零源是三个启动/模式首次空白、一个Clear后、一个空白selection。12条api=success/error=0/sourcePremul=1。

| stdout行 | Tick | Dirty / full | source alpha / RGB | 最大源 BGRA | 观测 |
| ---: | ---: | --- | ---: | --- | --- |
| 28 | 370798 | 全帧 / 1 | 0/0 | 0 | 初始辅助selection空白 |
| 39 | 377304 | 全帧 / 1 | 0/0 | 0 | 辅助selection空白 |
| 43 | 412326 | 全帧 / 1 | 0/0 | 0 | Primary初次空白，final DIB alpha1 |
| 1073 | 418176 | (651,783,987,950) / 0 | **122/122** | (0,16,255,255) | 红色内容进入源与DIB，早于首个已记录橡皮帧 |
| 1075 | 421967 | (651,783,1174,1056) / 0 | **5980/5980** | (0,16,255,255) | 更多红色内容进入源与DIB，仍早于橡皮帧 |
| 1320 | 425633 | (997,1129,1049,1181) / 0 | **1876/1876** | (127,127,127,127) | 白色48px/.5橡皮，final alpha最大128 |
| 1948 | 426647 | (866,980,912,1026) / 0 | **1340/1340** | (127,127,127,127) | .5橡皮光标 |
| 2478 | 427661 | (1000,947,1073,1015) / 0 | **1897/1897** | (255,255,255,255) | contact中opacity1橡皮 |
| 3122 | 428690 | (668,648,1087,1098) / 0 | **4955/4955** | (127,127,255,255) | 笔迹/光标混合区域 |
| 3200 | 430656 | (640,640,1152,1152) / 0 | **3984/3984** | (0,16,255,255) | Undo后保留内容 |
| 3206 | 432169 | 全帧 / 1 | 0/0 | 0 | 3204-3205 Clear之后，预期空白 |
| 4173 | 437785 | 全帧 / 1 | 0/0 | 0 | 切回空白selection |

非零条目finalDibRgbNonzero与source一致；红色采样在final仍红色，.5白色alpha127变成final128，符合alpha1命中底层合成，并没有在DIB中消失。

### 普通笔 -> 非零源 -> 橡皮切换的证据时序

1. 已记录三次普通笔阶段RTS Down/Up ticks为415134/415337、415539/415789、415914/416148，对应鼠标Down/Up均accepted。
2. 首次非零源tick418176距第一条已记录Down为**3042ms**，第二次tick421967距该Down为**6833ms**。
3. stdout1072绘制线程tick417801记录present success=1/visuals0/dirty全帧；紧随的1073为dirty小区域，不能仅按文件相邻当作同帧。
4. stdout1224-1299 tool0普通笔hover、primary0/空dirty的present success0；普通笔本来使用系统光标，不能由此推出输入contact被拒或ULW API失败。
5. 首个已记录tool3橡皮帧为stdout1300/tick425367；首个Cursor GPU Draw为1317-1319/tick425617。两次红色源分别早于橡皮帧**7.191s / 3.400s**，不能说“日志证明切橡皮之后GPU才首次产生笔迹”。
6. 1317-1320的48px/.5 Cursor、1876源像素与1321-1322的visual及1353的present dirty一致；异步队列导致frame文本位于ULW行之后。有tick/尺寸/dirty连贯证据，但没有唯一frame-id，不升级为跨线程原子快照。
7. 四组Cursor GPU三联ticks425617/426631/427645/428659均成功绑定，随后源均非零；没有新的“有效白色Cursor请求且匹配区域源零”的硬件正例。
8. 用户光学现象“几乎不能出现内容，都是切换到橡皮，画布才会刷新”与非零源/API success并存，不能宣称硬件正常。需保留source/DIB/USER32/桌面实际刷新的分界。

3042ms/6833ms是已记录事件跨度，**不是首像素延迟测量**：源统计限频、trace丢弃、无唯一frame-id。不能推断这几秒所有源一直零、渲染链完全停止、或确切阻塞发生在Map/ULW中。stdout40-41的selection frame375416到present382093跨度6.677s也显示长帧信号，却没有分阶段耗时定位。

### 输入、错误与生命周期完整计数

- 鼠标822条：812Move、5Down、5Up，全accepted1；mouse-source821条api0（查询API缺失），mouse-filter822条无sourceReject/systemReject/positionReject。Pointer API缺失不能解释已接受事件。
- RTS23条：5Down arrival+5published、5Up arrival+5published、3Packets published；published均deviceKnown1/device2/decoded1/published1。device2是MouseLeft，不证明真Pen/Touch硬件。
- Down/Up RTS行149-150/271-272、381-382/569-570、661-663/856-857、1390-1404/1931-1932、2426-2434/3111-3112。对应Windows Down/Up行141/269、387/575、668/854、1402/1939、2432/3117。
- Controller runtime16条，tool3/MouseLeft，generation2七条、generation3九条；普通笔runtime日志未保留，诊断受光标视觉采样影响，不能等价为普通笔未消费。红色源、4次Undo和Clear前document strokes5是后续内容正证据。
- Visual32条均primary/shape2（EraserGripCircle）；frame40/present41：31个visuals1/success1、4个visuals0/success1；6个success0均dirty0（其中一个visuals1）。laserTips/laserPrimary/laserTouch均未见非零，不能验收激光笔。
- stdout48和4177 dropped1864/331，共2195；保留5125条seq，编号1..7320，缺口恰等于丢弃。initial raw-registration active0不等于没物理鼠标。
- stderr11-12 Primary可见/layered/不穿透/2318×1360；107-108隐藏、110-117销毁发生在退出阶段。初始stderr1空HWND之后stderr3真实创建成功，不支持窗口始终缺失。没有可见几何resize。
- 正常应用唯一FAILED HRESULT为已回退的waitable0x887a0001；stderr/IDT无FAILED HRESULT，未见Raster submission failure、staging Map error或非零deviceRemoved；无日志不覆盖未诊断API/性能阶段。
- IDT共40行，info20/warning20；UI3Diag20条为耗时聚合，recovery均0/0/0，43个deviceLost字段均0。Bar/Setting等UI WARP不是Draw3硬件设备，不能混用。
- IDT31-32：11:40:16.123等待selection-output、pageContent=false，11:40:21.384 ready/selection-ulw@3，跨度5.261s。属于Clear后空白selection转换，不是最初普通笔消失的直接证据。
- stderr EDID ReadFailed仅说明物理尺寸未知并回退DIP/px，不直接解释透明GPU或USER32刷新。

### 可形成的定位与下一段证据

WARP自检现已复现两种目标Draw全零且VS/PS实际执行，不依赖ULW。新VM正常硬件则有笔迹与橡皮像素达到源和DIB，却仍有用户可见刷新故障，不能统一解释成GPU全零。两个红色源先于首个已记录橡皮帧；连续source与光学时序缺失，因此还不能唯一分辨实时提交而桌面不更新、CPU/GPU迟滞或工具切换触发重绘。

下一段最低证据应分别保留WARP shader输入/输出/写入分界，以及硬件第一笔Down→CPU消费→raster/compose→Copy/Map→DIB→ULW的同帧编号/dirty/阶段耗时，重点采样工具切换前后。不能直接把关混合、换BLT、强制全帧或替换shader当作已证明修复。

### Files found / code patterns / related specs

- 六文件：identity为包/进程身份；pixel-test stderr/stdout为六用例结果及绑定；normal stdout为Draw3输入/目标/源；stderr为窗口与eraser摘要；IDT为启动退出、UI聚合与gate。
- `Draw3.HiddenWindowTest.cpp:2334-2425`：Clear、renderer、Cursor、关闭混合、pipeline query；2322-2332判定读回；正式分母4。
- `Draw3.ContactInput.cppm:26-31`：device2=MouseLeft；`Draw3.PenCursor.cppm:17-21`：shape2=EraserGripCircle。
- `Draw3.DrawingController.cpp:13731-13774`：presentSucceeded由条件PresentFrame设置，视觉采样输出；success0+空dirty不能等同实际ULW调用失败。
- `.trellis/spec/native-desktop/input-and-ink.md` 的“Win7 GPU透明读回分界诊断”：Draw/API成功不证明像素/桌面可见；source在Map后CPU底层前取值；独立WARP与硬件分别记录；未解任务保持in_progress。
- 相关`prd.md`/`design.md`：保留FLIP/质量/CSO/原始采集，真实Win7可见验收。
- External references: 本次未新增互联网引用；不以公开支持声明扩张现场结论。

## Caveats / Not Found

- 用户可见现象不能被API success推翻；报告只确认数据阶段，未宣称恢复。
- 两设备/两driver、普通EraserGrip和独立Circle、full CopyResource和生产dirty Copy路径不等价。
- 无补丁清单、VM配置、现场视频、桌面像素采样、唯一frame-id或Copy/Map/ULW阶段耗时。
- 限频及2195条trace丢弃，缺少日志不能当作动作不存在；跨QPC/frameSeconds/GetTick/wallclock不直接相减。
- 没有真Pen/Touch或激光笔正例；系统光标普通tool0可见、tool3隐藏并请求自绘符合当前策略，自绘光学显示仍未验收。
- 原始六文件只读，未修改代码/构建/GUI/Git；研究保存不结束任务。
