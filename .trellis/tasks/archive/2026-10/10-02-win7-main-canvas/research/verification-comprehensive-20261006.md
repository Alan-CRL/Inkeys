# 全面单入口候选验证与交付（2026-10-06）

用户要求修复或提供尽量少次完成定位的全面测试包。继续既有win7-main-canvas任务in_progress，本轮没有commit/push/archive。已确定WARP完整PS的Draw/readback边界，不把独立自检失败或VMware光学刷新问题宣称为唯一根因。

- 完整InkeysRepo.sln：Debug|ARM64 exit0/52.94秒；Release|x64 exit0/161.59秒，原生ARM64 MSBuild由vswhere定位、同调用PATH规范化，900秒上限。构建既有转换/第三方与LNK4075警告未作无关修补。
- 两架构WARP/Hardware各87唯一终态PASS，diagnostic7/sequence5/matrix69/production4与deviceRemoved0各独立核验；Hardware无WARP回退。两架构Headless --no-window exit0。
- 新EXE实际WinPS5.1 ProbeOnly WARP/Hardware各87 PASS；quoted/case/trailing/extra六argv场景通过。未启动普通产品GUI。最初与argv并行触发既有进程保护被拒绝，原日志保留，顺序final重试通过，不修改源码绕过保护。
- 实际WinPS mock共30场景通过：四调用/双后端独立失败和unavailable、旧schema拦未知CLI、直接ProbeOnly身份错误、manifest SHA/package/87/69错误、缺文件/部分采集/大双流/环境恢复与唯一CMD。timeout只在测试副本立即注入等待结果，精确自有child被Kill/等待，未真实等待600秒。WinPS5.1/PS2子集静态检查不替代Win7 PS2。
- 23份诊断DXBC/signature/resource/实际控制流证据通过，正式301–304在两EXE保持源码字节；8个正式/测试CSO及全部24项本轮开始存在的旧资料保持。missing-before.json列的6项旧捕获在本轮开始已不存在，未计保持成功。原6诊断数组保持。
- 独立研究FINAL和正式Trellis check FINAL限定PASS；git diff --check及本轮源UTF8/BOM/CRLF通过。变体/dummy资源仅在隔离fixture内，不替换普通生产shader。任务待Win7/光学验证。

## 交付身份

- ZIP：C:\Users\alan-\.codex\visualizations\2026\10\03\01a100c6-aa67-7cc3-9d01-6cda328f70c3\win7-comprehensive-20261006\Inkeys-Win7-Comprehensive-x64-Release.zip
- ZIP SHA256：E7E534FC1988A6702DE8C00DF17656B96596B24912B4B137A62B9D719874E018
- ZIP bytes：29294773
- EXE SHA256：5D5254AAFE523640EC4F47E4AF132E25D43060B99FF426CD34666821672E672C
- 唯一入口Run-Win7-Diagnostics.cmd；唯一身份文件diagnostic-manifest.json；probe_schema=comprehensive-20261006，expected87/matrix69。
- ZIP CRC与每文件逐字节核验PASS，旧包保留。manifest记录当前HEAD与未提交范围、源hash和本机验证身份。

## 下一现场步骤

两台Win7各解压新包到独立目录，退出旧Inkeys后运行唯一CMD。两个后端自动自检；随后普通ULW/full ULW两轮，各新建空白页普通笔5秒、先等3秒观察，再橡皮/激光各3秒、正常退出并填写观察。故障时每driver最多600秒，不故意等待；FAIL/unavailable仍继续采集。回传整个时间根目录，含summary、observations及三个阶段，注明VMware/WARP设备。

收到后按路由/16绑定组合反向复验/六功能群关联及GPU源-DIB-视觉证据定位；family关联仍可能包含编译布局/声明变化，不能仅凭名字定位某opcode。目标是少次获得可修复分界，未保证一次唯一根因，也未声明Win7恢复。
