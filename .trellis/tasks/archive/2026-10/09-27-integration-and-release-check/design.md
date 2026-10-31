# 集成与发布检查设计

- 以现有 InkeysRepo.sln 为唯一主程序构建入口，按 PptCOM→Inkeys 项目依赖；PowerShell 同调用中规范 PATH，定位 VS ARM64 原生 MSBuild。构建与测试串行占用输出目录。
- Debug|ARM64 是基本门禁；Release 与 Win32/x64/ARM64 按实际项目矩阵扩大。项目/ABI/HLSL/资源变动使用合适 clean Rebuild，检查 DLL/TLB、shader、资源和实际产物。
- headless --no-window、Draw3、PptCOM 测试先查存在性/参数/真实覆盖；GUI 隐藏不等于 no-window，运行结论与编译结论分开。
- 功能矩阵覆盖 UI/输入/PPT/保存/重启/显示/fallback/冻结/设置 owner/关闭 gate，Win7 和 Office/GPU 组合按实际证据标记。
- Win7 SP1 仅 KB2670838 单列 FL11.0 硬件有/无两类设备；每类记录实际 HARDWARE/WARP、feature level、DComp 不可用后 ULW、透明/resize/device-lost/输入。微软 Platform Update 仅支持 D3D11.0 feature level，DComp 不可用；官方 API 与编译只证明静态可达性，不能代替目标系统运行。
- HF 指纹包含 HEAD 加改动/新文件内容；最终 reviewer 看所有 diff 和审计覆盖，后续修补导致旧测试失效时重跑。

## 2026-10-01 续执行冻结的局部合同

- PPT session已实际RED：保旧数据、ProcessSessionId新轨 sessions/<GUID>/<slide-id|page-index>，新增enum4/5并仅扩Controller mode匹配，selected strict失败仍IoError，未选坏侧轨警告且保bytes；不绕ready/未知为空。
- F069仅在合法两个RED后实施原B320+Rootamend+49C（连续pin、same-handle strong deletion、所有Partial/postmutation异常retain）；不重立事务框架。
- CAP01用户明确批准本进程owned旧版回收：current index/bak/worker pending引用均保护；跨进程旧会话全部保留。共享API冻结为move-only opaque UInkOwnedVersion，析构只close/retain；SaveUInkFileWithOwnedVersion(path,session,ownedVersion,options={}) companion不改原Save/result/options，只有真实本次created+Committed最终strong验证才从原writer连续pin转交；TryRemoveOwnedUInkVersion(ownedVersion,uint32_t& systemError)仅内部原locator，失败留文件，调用者可释放authority以免handles无界。无事后by-path追认/全目录猜测清理。F069单元先停写可验，再串行接口和PPT CAP小批。
- CAP回收在worker串行所有权内、只有成功index commit之后：持严格读取且deny-write/delete的current/bak实际句柄到回收结束，未知/缺失/损坏索引保留；合并两索引和pending强引用，才选本worker创建authority。不能清其它session、未知文件或未决partial artifact。补实际replacement/sameID mutation/失败事务/重复Save与coldload回归；共享接口和行为需独立实际diff检查。
- START01最小门：先OpenVerifiedEmbeddedResourceFile对比内嵌全部bytes并持原read HANDLE；仅不符/不可读时原子publish后再verify。readonly正确DLL无需强制改写，wrong readonly仍failclosed，不改属性/目录信任/加载path/失败清理。用同private production helper作既有CLI两readonly回归。
- UI3只给capture-on有界lastmeaningful原候选诊断；canonical/完成条件/节拍/质量不变，先取actual原因后修观察资格或产品缺陷，不用lastAbort空row归因。

### Draw3 N1最小四源执行冻结（原范围）

Root已明确WRITE_N1：Metrics cpp/cppm+Controller cpp/cppm唯一writer，PPT不再写Controller，旧modehelper保持。constructor尾aux=0+MaximumSamplesForBudget(aux=0)先减共同32MiB/actualsizeof再allocate，保U1 SIZE_MAX有界行为；N2固定群体必须effective=request，Host容量另小批按真实budget同步，Laserparticles-on不能降fps/删样。四大stage wall/同span threadCPU及Laser子段分别available masks，4Reset/5Update/4Predict实际调用，父子inclusive不合加；Run真实prewarm另列，default无新clock/GPU/IO/分配。Cold要实际透明clear+full composite成功，独立Up final在CaptureStored前冻结真terminal/ordinal/owner/投影，并由同L2/output/dirty/成功return封，不加FullPresent。Warm16/Measured216真实final与prefix后切phase，失败不删；Laser natural Up/Hold/Fade/expiry成功清除与独立ClearQuiet分群，GPUemitted不可得就是false/null，不加读回。Frame≤384/progress≤2048/Controller≤64KiB为actual编译门，遇budget矛盾消息给Root，不默削字段/样本。Host/Contact/Main/入口后续独立批，不混N1。

CAP两测试站点仅DRAW3_TESTING：successful IndexCommit后 BeforeReferenceLeases / BeforeOwnedRemoval，默认null callback(ctx,stage,root,candidate)仅观测私有actor，不授通用删除权。namespace chain beforeSave连续pin+GC重核sameID/noReparse，current/bak真实denywrite/delete readHANDLE strict解析并合pending引用，只有当前worker创建authority可删，异常/unknown保disk、GC不改Committed；过期失败token可release防句柄增长，不byname补认，不清crossprocess旧session。

### ASYNC01 用户外部条件性交错（确定性验证先于修）

编号非仓库旧ID，用户2026-10-01提供9cce406–445/951–977和Bridge145–157定位。当前Contact cppE1FE未改；消费者fallback空检查与实体dequeue之间，producer发C1失败→fallback1、C2实体成功，旧静态marker无ordinal，消费C2信号++consumed1却BridgeFIFO取C1，fallback1随后无法等于2，C2可遗留。尚无确定性运行/自然负载证据，不能写confirmed自然故障；256非动态queue硬容量。TEST_RED_ONLY唯一新worker接Contact两源+Headless tests，用default-off test屏障两线程严格控制，不sleep；两个真实Command wake与Bridge按C1→C2、两边无剩与预约释放为门。现F045先预发后消费回归不覆盖此窗口。实际RED后最小合流修，禁止全consumer-producer无界锁/GPU/IO/丢必要输入。Bridge/Main/工程由Root协调，不重审全仓。
