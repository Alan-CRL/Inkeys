# 密度权重隔离候选实施

2026-10-23。仅ignored `Build/ScrollFollow/recorded-motion-replay/density-floor3-candidate`，生产Tracker BF3409、header813509、原测试EF45与旧replay CF56均未改。

实际 `product-live-13172-19751750` 的13帧（1Initialize/2Await/10Process）已由root以原Tracker精确回放差异0。原frame11参考10空间票24/37=.6486486，距离.65还差.05票；34个错误FB均在底2行。root独立像素比对支持实际画面已更新，不把该失败归为Mag未刷新。

候选79A1617F只改fit/anchor每点票为1/max(cellCount,3)，spatial分母为实际总fit票；每格仍至多1票。OriginalCellWeight仅给原otherRatio使用1/count与occupiedCells，.15/.35/6 interior歧义保护及.65/40/FB/NonTranslation/Coverage/锚点和参考事务不改。删除四处精确增量可还原BF3409，patch/hash在manifest。

root实际候选旧trace回放返回预期exit1/89字段差异，不能称精确回放PASS；frames11..13均accepted/None，末totalY=-199.9999389648，与独立200px GT差.000061px。唯一原矩阵exit0/202135ms，原6918+native704合法前缀与原4负例未改通过，native长程max.00114；该限定离线结果尚非生产/GUI通过。

新增4个真实像素保护case已stage未跑：12稀疏内格运动/36密集格固定；24+24相反流且交换密度；初始全覆盖后只8密集格保留。全部从原Canvas/Render生成不可变patch并平移整个patch含边界，不构造Match。独立GFTT检查输入稀疏1..2角点/密集≥6；实际Tracker统计须满足FB/内点≥40、存活≥.35、fit≥.65、affineVariation≤1，再分别明确到Ambiguous或Coverage门。已有runnerUpRatio仅暴露实际原单位格票，不声称通过公开API取得每个FB match密度；raw GFTT角点不冒充FB计数。

每case三拒绝核原lastTrusted、零total/delta、第三帧Paused和不自动恢复。稀疏少数用.15<runnerUp≤.35+Ambiguous证实零主流内部次流分支；两清晰流要求runnerUp>.35；8格局部须真正Coverage拒绝，低特征或形变失败不算通过。专用入口只跑新4cases/16Process，不重复原长矩阵。

新tests5C4A8DFA、compile-density-guards.cmd和density-tests-manifest.json已交checker；future integration patch仅提案未应用。实施者未执行编译、CLI、GUI或输入，若fixture未到目标门按测试设计失败记录，不放宽断言或改GT求通过。root独占编译和运行。

## 新保护用例V1失败与V2修正（未运行）

root实际V1候选exeF16E90F1返回exit1/1303ms/15断言失败，日志/源码5C4A均保留。稀疏少数与覆盖用例到达预期门；opposite原密度实际Ambiguous/runnerUp.351351已过目标门，额外affineVariation≤1断言却不等价原NonTranslation联合条件（原variation1.35075不能单独判形变）。交换密度时真实稀疏FB只剩12/36格、原单位格runnerUp=.3333未达到>.35，实际接受约-8，该fixture未保持两个足够可追踪流，不能当保护通过。

BF生产源码同V1独立对照exe99A9F242也exit1/15失败，swap同样接受-7.99998、runnerUp.333333，证明这个观察不是floor3新引入；仍不能据此接受该新用例。原密度BF spatial约.6486，候选约.847。

root授权的V2仅将opposite实际两流改成±4full像素（处理流±2，间距4仍大于双共识半径3），其它scene仍8；去掉错误affineVariation单项断言，严格目标reason本身证明实际NonTranslation联合门已经通过。原FB/内点/存活/fit/unitrunnerUp阈值、事务、第三暂停和不自动恢复要求不变。

V2在独立density-guards-v2目录，tests1421CCD308D8A37B7C75168C660A8004DA5E1CB5DA424FCE283A3E7C5A66244F，fixture-v1-to-v2.patch及compile.cmd/manifest已交checker，未来integration patch仍未应用。保留旧V1产物不覆盖；原EF45/候选79A未改。实施者未编译/运行，实际V2仍待root执行，失败时继续按fixture能力失败记录，不放宽保护或改预期。

## V2实际核准与最小源码推广（构建/GUI待执行）

root实际V2 exe068678A3 exit0，4cases/16Process全部通过：稀疏少数runnerUp .25；两种密度相反流runnerUp .5、fit .75；局部覆盖56FB/8格。实际lastTrusted/第三Pause/noresume与所有密度sanity、目标reason通过，checker已独立读raw核准，旧V1失败不覆盖。

在上述核准后按root授权，仅应用两文件：Tracker BF3409→7936941A08CA16A42906CB4FFB7C40E228EAF31A638B53B4A358D3623E7CAFF5；tests EF45→E72D3158E44A058D85750089A1069C60340012E68EFC61980E4A4EC5F1252FEE。生产算法与已验证79A候选代码完全相同，仅去除“隔离候选”注释并校正每格“至多一票、稀疏格降权”的说明；没有调整其余门。

V2 helpers原样进入原测试匿名namespace，Run末尾追加一次调用，不包含自身测试源或注入main。删除新helpers与单次调用即可逐字恢复EF45，所有旧case bodies、阈值、种子和GT保持不变。header/API/依赖/配置/其它产品源码未改。BOM/CRLF/EOF和scoped diffcheck通过；before/完整精确patch/前后hash/逆删证明保留于ignored production-promotion目录。

当前两文件源码已应用并冻结，等待checker源码复审后由root完整solution构建与必要检查。实施者没有构建、CLI或GUI；隔离离线结果不能替代新生产binary实屏跟随验收。
