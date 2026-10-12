# Plan: Modeler 源码迁移与库内终态扩展

## Status

2026-10-12，父任务后续终态算法仍planning。用户已在最新完整阶段1方案后明确批准实施迁移，由子任务 modeler-source-build-baseline 执行；B-D必须依赖其验收，不能仅凭源码接入开始。

## A. 源码迁移基线

- [x] 三架构成员、machine、符号、CRT、namespace和hash只读核查；13 runtime对象/152 Abseil侧对象。
- [x] 13 runtime cc清单、GMock排除项、pragma/project/test ABI和旧头风险调查。
- [x] 保存旧库无窗口失败/成功轨迹与完整模型输出，作为独立控制组。
- [x] 按最新授权固定vcpkg20250512.1#0，三个static-v143 triplet给Abseil单独指定C++20，匹配头/Release库；保留原merge，核对machine/CRT/namespace及无Modeler定义。
- [x] 复用MSBuild登记13传统TU与filters；统一头来源、C++20/MT/NDEBUG/iterator/Abseil宏，仅Abseil按授权切.1，其他依赖不升级。
- [x] 排除迁移目标全部旧merge默认链接项；VERBOSE:LIB/map证明Modeler来自新obj，不由旧库补缺。
- [x] 扩展关闭时同输入比较旧/新Down/Move/Predict/Up、stylus、静止、时间异常和预算；区分源码/ABI/浮点差异。

阶段门：正常行为parity与构建归属未通过前，不加收尾算法、不调spring掩盖迁移问题。

## B. 库内模块与离线效果

- [x] 独立无状态求解器与显式TailPatch规划，不改旧Update追加合同/StrokeModeler布局。
- [ ] 定义单元无关request/outcome/scratch，以caller source id定位实际接纳窗口，输出位置/映射，不伪造Result速度。
- [ ] 验证入口/终点/范围原子合同、Reject/NoChange、空/短/重复/非finite及总预算。
- [ ] 同一真实模型输出比较平滑位移、raw约束重构、截尾参考、原路径；统一物理弧长重采样衡量转角。
- [ ] 有序匹配及圆弧、S弯、反向、自交、真实角点、稀疏样本，量化偏差、形变、窗口不足失败率、耗时/内存。
- [ ] 依据实测选择算法/权重/窗口/预算；不把玩具值投入产品。

阶段门：能写求解器和能改善用户笔迹分别验收；仅理想圆弧成功不能进入普遍修复。无收益时保留实验结论，不接产品。

## C. 产品完成与存储

- [ ] 有界消费raw尾部/来源记录和reference，不先重写RTS/mailbox。
- [ ] 只传未提交区与连接点，不用modeledResults索引推断realPoints。
- [ ] 直接完成/deferred完成统一helper；旧Up/gate作安全baseline，patch不再过旧全窗口距Up单调门禁。
- [ ] 属性从未taper基础半径/time有序映射，共享笔锋规则，物理Up显示锁不变；首次L2与Stored/重放同几何。
- [ ] Cancelled、多contact、池复用、新Down、页面命令、dirty、普通Pen各压感模式和HardPen空间边界。

阶段门：endpoint/L1/Stored一致与抬笔形变证据。若需扩大保护窗口，先明确独立合同；不静默删掉失败类别。

## D. 立即物理Up与续接

- [ ] 检查Touch物理Up与实际kUp分离，记录只final处理的改善延迟。
- [ ] 如需立即改善，preview可撤销；baseline/model/续接证据不改，等待时不固化可变区。
- [ ] 验证preview到final、reconnect、超时/淘汰，不重估真实速度/压力，不用模型未来time养粗。
- [ ] 用修正位移和恢复跳变决定过渡或运动期机制；状态能回滚不等于视觉无缝。

未验证该阶段时，不宣称所有Mouse/Touch/Pen立即Up都已解决。

## E. 构建与验证

复用InkeysRepo.sln Debug|ARM64，从当前VS定位原生ARM64 MSBuild，同一PowerShell invocation规范PATH、禁node reuse，完整build至少5分钟。三架构Debug/Release审查与编译/链接，ARM64本机成功不外推其他架构/Win7运行。

算法用ABI匹配的既有真实模块测试/明确无HWND的生产probe。InkeysHeadlessTests.exe --no-window照常回归，不为混链Abseil把其MTd改MT。关闭扩展基线、各工具、预算、完成/Stored一致性均验证。

GUI/隐藏Host/真实设备仅按用户明确授权执行。本轮只读库存和文档。实施时更新第三方provenance与固定ABI规范；默认不commit/push。

## Rollback

原merge原hash保留，vcpkg Abseil在独立installed目录。源码迁移与终态扩展可分别回退：先关闭终态走原算法；必要时整个目标回原merge+原头，不在同目标混链两种Modeler。候选失败回endpoint-safe路径并记录未改善，不能称普遍修复。
