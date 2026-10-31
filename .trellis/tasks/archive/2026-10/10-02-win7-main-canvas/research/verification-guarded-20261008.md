# Win7 完整 guarded shader 单入口包验证（2026-10-08）

继续10-02-win7-main-canvas。用户明确要求 shader 效果不变，故候选保留全部数学/细笔补偿/变粗细/AA/光影/所有功能/双源输出，只有 DefaultInk 尾部正向控制块变化。正式 HLSL/CSO及301–304资源不变；旧删除组诊断只在永不显示的fixture运行，正常两轮从不选择。

## 可交付身份

- ZIP：`C:\Users\alan-\.codex\visualizations\2026\10\03\01a100c6-aa67-7cc3-9d01-6cda328f70c3\win7-guarded-20261008\Inkeys-Win7-Guarded-x64-Release.zip`，29300415 bytes，SHA256 `07B744726DE1B3BA8E04AE20F026AF01B2FAE67B03BBAFEB80573DD9D65902A4`。
- EXE：SHA256 `23D6E5F811821A4E27CFDCD18A00BDF1B996886F2DE1AAE48154D40D63F0C547`；Release|x64，baseline HEAD cdc39044bb35fca6ffbdd176e7ff51a81cc0a481+本任务未提交工作区；没有commit/push。
- candidate PS：28980 bytes，SHA256 `2171DDA6E709A3F4CD444E6281AB64635C3F969CD71722662B04DA0DD8055384`，实际header字节存在EXE；全部数学、未知type原d=0行为、ISGN/OSGN、17资源声明保持。相关DefaultInk导数/细笔/discard实际在正向if内。
- 9文件、1CMD；ZIP CRC和逐文件相等核验PASS，旧包不覆盖；diagnostic-manifest记录实际EXE/source/formalresource/verification/质量报告身份。

## 验证证据

- ARM64原生MSBuild完整InkeysRepo.sln Debug|ARM64、Release|x64均exit0，最终增量18.55s/50.61s，900s预算。首次ARM64新增测试代码std::max遇Windows宏，机械括号修正；首轮x64完整224.95s。无shader或工程配置绕过。日志pwsh不存在来自既有vcpkg探测，目标有系统PowerShell回退，未触发双方失败warning；编译退出0并实际CLI运行。
- 两架构WARP/独立Hardware各105唯一用例PASS，共420；旧matrix69、sequence5、diagnostic7、production4保持。每后端guarded18 reference可用、独立公式正确、全图逐通道max_delta=0，共72次对照；原版效果图没有变化。
- 两架构Headless --no-window exit0、stderr空；首轮系统TMP中7个atomic/config文件操作失败，root helper只重定向子进程TEMP/TMP到短可写隔离目录后通过，产品代码不改，首轮raw保留。
- 首轮cursor新增oracle错误把灰描边当全白，使原版/候选均报396formula errors。保留失败raw，仅将fixture独立CPU期望改为正式signed-distance/outer+inner/灰描边+白填充/Retain四通道公式，容差1.01，重编后全PASS。shader/candidate字节未改。
- 38个formal/旧包/原21份capture资料前后SHA相同；旧23诊断shader数组逐项字节长度/hash相同；两架构EXE正式SHADER301–304逐字节匹配当前正式CSO。
- 实际Windows PowerShell5.1最终x64EXE ProbeOnly，两后端105/18 PASS、guarded reference_unavailable0、exit0，完整双流和identity，没有启动正常产品GUI或加载普通产品配置。root verifier误把reference_unavailable=0子串当hardware不可用，按identity精确exit3修正后仅重解析已有真实raw，未重跑产品。
- collector实际WinPS mock47项+冻结3复验PASS，含身份拒绝/异常缺失重复/reference比较矛盾/失败继续/大双流/空格路径/唯一CMD/环境恢复；实际自有child900000ms截止注入，精确PID结束、等待5000ms、Dispose、后续硬件检查继续。不是实际等15min。
- minimal diff/原编码换行/git diff --check PASS。PRD/design原有LF保留；其他修改保持原BOM和CRLF。task validate通过，旧大input spec自动注入32KiB限制仍有告警，新增报告/合同已明确上下文引用。

## Win7 验收与边界

唯一入口Run-Win7-Diagnostics.cmd，同EXE三阶段：01-pixel-probe（诊断1、guard空、full空）、02-original-shader（guard空）、03-guarded-shader（guard1）；两正常应用都关闭full覆盖。wrapper最终恢复调用者全部三个环境值。创建候选失败不会回退冒充成功。正常启动LoadShaders guarded分支未在本机打开GUI运行，靠源、环境隔离、EXE字节/实际GPU输出及Win7后续初始化日志证明实际选择。

每驱动105=旧87+新增18，查询最坏约618s，collector截止900000ms/15min。Win7原版FAIL仍如实记录并继续两应用阶段；guarded独立PASS不能当采集或真实画布已恢复。105 probe验证直接创建候选和production resolve公式，不覆盖产品MAX/MIN多层累积全部历史；实际撤回重做/定格等必须人工观察。

WARP真实恢复优先验证普通笔立即出墨、亚像素细笔、粗细变化、荧光笔、橡皮/激光、撤回重做、选择穿透和定格返回。用户此前VMware硬件非零像素但切橡皮才刷新仍是另一路径，不能用WARP候选通过宣称它已修。分别返回两设备完整Win7-Diagnostics时间目录。

本机为Win11 ARM64；WinPS5.1/mock不是Win7 PS2与光学验收。Task继续in_progress，不archive；下步依据Win7原版/guarded选择日志、18项输出与实际ULW source/DIB及用户观察决定最小生产修复，不删任何效果。
