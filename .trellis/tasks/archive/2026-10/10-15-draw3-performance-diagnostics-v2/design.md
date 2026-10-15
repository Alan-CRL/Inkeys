# Draw3 诊断包 2 技术设计

## 边界与授权

同一用户请求的一个完整交付，沿用v1显式开关和最终进程握手。用户对上一轮三项补强+定向对照方案已回复“好的，请你完成诊断包2”，原始会话还明确授权规划实现至最终，因此无需再次审批技术细化。创建新Trellis任务记录v2，不commit/push。仓库允许无交互GUI的CLI验证，新增纯离屏D3D probe可在确认不创建HWND后测试，不启动产品主窗口。

## 真实绘制旁挂

- 只扩展PerformanceSession与inline旁挂，保持Host/RTS/输入proof/业务调用原参数和结果。正常off不得新计时/统计/分配/查询/线程/I/O。
- 新增资源角色与匿名代次、operation/site/参数。覆盖Map、Write（包括常量逐字段写入）、Unmap、Bind、Copy/Update、Clear、Draw、Present等最关键叶调用。void API标result_available=false，不能伪造HRESULT=S_OK。资源元数据只在诊断冷注册读取GetDesc，不在热路径探COM。
- 保留v1 inclusive stage；新增leaf操作是独立调用边界，导出声明accounting，未覆盖残差不冒充Unmap。frame/prior分别有有界归属/统计，跨Begin必须按真实端点拆分或明确未归属，不能写上一帧。
- 固定调用前缀+独立慢调用前缀与固定分组聚合，快速设备不会在普通调用前缀满后完全失去慢调用。seen/retained/dropped/invalid、资源/分组容量与覆盖截止均公开。聚合只保存有界count/total/max等，不把无原始全量的percentile写成精确值。
- v1基线new payload=26,390,832 B，余7,163,600 B。参考新增frame/prior≤256 B×16384、完整调用8192+独立慢调用4096×≤128 B、group2048×≤128B、冷metadata≤64KiB；最终按sizeof和AllocatedBytes实账≤32MiB，legacy合计≤64MiB。容量满不热扩容。

## GPU采样修复

owner loop只维护候选/轮询；首个实际图形工作到来时才开启query。前8个真实图形工作帧优先采集，此后每8个候选稀疏采集（最终参数由实现预算核对）。每代保持8固定slots、DONOTFLUSH单轮，不等待/Flush。输出observed marker mask、候选序号、actual draw/工作分类、CPU frame/epoch关联以及pending/disjoint/failed/缺失。空循环不能占用真实绘制分母；Map-only/失败和没有有效marker不可冒称shader执行时间。阶段首标记早于惰性begin时，只记录实际观察到的截取边界并公开窗口口径。

## 无窗口对照矩阵

新增独立普通C++ ResourceProbe.cpp/.h，CLI在配置/单实例/HWND前严格分发：

- --draw3-resource-probe-list：JSON case清单和revision；不创建图形设备。
- --draw3-resource-probe <case_id> <absolute_existing_case_dir> <session_id>：一个case、一个独立进程、离屏D3D；输出probe-result.json（schema_version=1, probe_revision=2）。
- --draw3-resource-probe-test：参数、case矩阵、格式/结果策略的纯CPU测试。
- 能力literal：INKEYS_DRAW3_DIAGNOSTICS_V2。无效probe前缀一律GUI前非零返回。

probe直接分开创建硬件与WARP，不硬件失败后自动伪装WARP成功。最小复用301/302内嵌shader和InkPoint/48B常量合同，不泛化生产renderer Init，不创建swapchain/HWND、不操作产品文档或用户配置。每个case显式comparison_path，不能声称实际Host重放。

矩阵覆盖大/小结构化buffer、48B常量buffer、DISCARD/合法cap NO_OVERWRITE、带Draw/不带Draw、常量字段写/本地POD memcpy（必要时volatile写对照）、DEFAULT+UpdateSubresource对照和代表性offscreen大小/clear/copy。支持不足显式unsupported；合法NO_OVERWRITE不覆盖GPU在用区域。禁止DONOTWAIT与DISCARD/NO_OVERWRITE非法组合。

每case冷初始化+有限warmup+少量测量迭代，记录QPC/频率、wall/ownerCPU、各API、resource大小/实际writtenBytes/flags、shader身份、adapter/driver/feature/cap、迭代、错误与不可用。不加每call强制Flush/同步污染主对照；若另有GPU-drain对照必须独立命名并明确成本。

## 一键脚本与离线数据

CMD/PS1保持CMD目录/Inkeys-Draw3-Diagnostics。manifest明确package_revision=2/capture_schema=2及各raw_schemas，EXE/脚本SHA。仅静态v2能力标记通过才发正式新argv，不运行旧EXE探参数。

launch/completion身份schema保留1；session/frames/gpu等按实际变更升2，input-events语义可保持1。新增graphics calls/slow calls/groups/resources文件版本由recorder落盘合同统一，analyzer接受v1与v2；旧包清楚标无细项证据。

应用正常退出且owner身份确认停止后，脚本顺序执行probe-list和case worker。若采集timeout/进程存活不确定则matrix skipped_product_not_confirmed_stopped。case目录唯一、结果内session/case/PID/EXE/start身份与父持有进程核对；双流有界排空。每case20秒级超时、矩阵总≤8分钟，超时只结束自建probe进程，保留阶段marker和不完整结果，其余case继续；绝不终止产品。可显式SkipResourceProbes用于定向复测，默认完整矩阵。

报告优先呈现真实render帧分布、per-call/资源热点和覆盖、最慢调用前后、CPU/wall、嵌套/未覆盖说明、输入消费比例、GPU sampled draw覆盖与证据强度；对照只在身份/配置/迭代可比且样本足够时给相对比值，失败/unsupported/timeouts不填零。保留raw，生成top-graphics-calls.csv和矩阵汇总。

## 风险

第三方驱动单个API可挂住，worker进程deadline是外部边界；实际生产采集不能被强杀。诊断clock和采样有成本，报告公开计量频率与预算，不能承诺绝对无扰动或一次就能证明驱动内部根因。新probe的无窗口验证不代表目标兆芯现场通过。外部原始诊断包只读，派生产物写Build验证目录。
