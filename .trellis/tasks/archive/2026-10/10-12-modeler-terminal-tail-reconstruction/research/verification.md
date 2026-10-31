# 阶段2：独立终态模块与离线验收（PASS）

2026-10-12用户审阅范围后批准创建并开始，依赖阶段1c34d3a56。完成范围是库内opt-in几何模块与可复现离线证据，尚未接Draw3，不宣称产品末钩已修复。无本阶段commit/push。

## 实现与旧模型隔离

新增terminal_tail_reconstructor.h/.cc、本地来源清单、共享14TU登记、尾段CLI及测试。13个上游运行源/18原头31项SHA未变；阶段1旧新EXE/报告等共47项冻结artifact重算变化0。Abseil20250512.1#0、C++20、MT、NDEBUG、iterator0、工具链与原API/参数/SaveRestore均未改变。

输出显式Apply/NoChange/Rejected、范围/token、位置/属性来源与独立raw对应；首两点与trueUp精确、点数保持，失败空patch不改输入。scratch复用；有序弧长匹配、位移正则、weak pin、局部/物理转角、长度/位移、相邻重叠及窗内自交安全门，工作/点数/迭代预算封顶。

## 构建与回放

当前VS通过vswhere定位，ARM64原生MSBuild、v14314.44.35207/SDK10.0.26100.0；同PowerShell保存并恢复唯一Path、禁node reuse，监督timeout3600秒。使用既有solution、独立stage2-source/stage2-main输出。

| 目标/配置 | 完整Rebuild | 最后必要增量 | 原API replay | 尾段 replay |
|---|---:|---:|---|---|
| InkeysRepo ARM64 Debug | exit0 /328.125s | exit0 /13.078s | 不启动产品 | Headless exit0 |
| demo/tests ARM64 Debug | exit0 /276.594s | exit0 /19.578s | exit0 | exit0 |
| demo/tests ARM64 Release | exit0 /244.344s | exit0 /32.172s | exit0 | exit0 |
| demo/tests x64 Release | exit0 /223.266s | exit0 /30.953s | exit0 | exit0 |
| demo/tests Win32 Release | exit0 /178.062s | 无须增量 | exit0 | exit0 |

全部原API报告SHA=0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646，与同架构阶段1源码报告严格相等，19case/128412assert不变。没有任何容差放宽。

全部尾段回放96窗口/116105assert/failures=[]；两预测器16类输入、三窗口、9组预声明权重扫描，默认18Apply/12独立物理改善记录。另有26非法/预算输入×两路径、24重复raw等价比较、未pin endpoint repair正向路径、成功后scratch拒绝及大来源ID。参数与适用限制见algorithm-selection.md。

## 跨架构精确比较

原API按同架构阶段1对照全部bit exact。新几何报告也先逐叶精确比较，未修改任何阈值或浮点flags。

- ARM64 Debug/Release尾段报告SHA相同95795208c95f557927489cf6288bb9ca34725bcb6db57665e4e3cf89bc9ebed3。
- x64仅136个独立走廊误差诊断叶有1ULP双精度差异，最大绝对1.1102230246251565e-16。输入/原Result/新patch位置、来源与raw对应均精确相同。
- Win32跨架构比较中，原Modeler的pressure/tilt/orientation存在float舍入差异；实际原utils.obj汇编显示x87乘加后最终float store，而ARM64对应逐步float乘加，已复现具体值。新公开raw_fraction差异最大4.107825191113079e-15；诊断double有微小差异。全部1920次默认/扫描evaluation的状态/原因/计数、16566发布位置及caller来源/raw段编号仍精确相同。
- 上述是跨平台数学路径差异，不是同架构旧新实现差异；原31源/头与浮点合同保留。详细定位和不能证明的历史范围见cross-architecture-numerics.md。

## 成本

全部编译与探针完成后再顺序复测三架构。每架构24case、每项16次预热+256采样，8504检查/0失败；测量只包含独立求解，排除forward/report/诊断指标生成。初次受探针竞争的报告单独保留，不作为最终成本表。

| 主候选 | 普通4类窗口median(ms) | 普通p95(ms) | 512点预算拒绝median(ms) | 512点p95(ms) |
|---|---|---|---|---|
| ARM64原生 | .173–.192 | .231–.297 | 4.115–4.263 | 5.176–6.268 |
| x64，ARM64机器运行 | .259–.445 | .327–.620 | 7.843–11.613 | 8.855–14.283 |
| Win32，ARM64机器运行 | 1.245–2.349 | 1.736–4.226 | 51.978–58.420 | 66.475–67.890 |

普通窗口94参考/28raw附近；512压力样本明确为插值模型窗而非真实设备，主候选达到match_work262144后预算拒绝。提前point budget拒绝约.1–.5us（计时量化明显）。所有计数未超预算。表中x64/Win32不能外推原生目标性能；两预测器当前几何相同，顺序计时差异不能解释为预测器收益。CPU调度可出现长尾，工作预算不是OS硬实时deadline。后续产品必须另选更小窗口/工作预算；本阶段没有测GUI整帧或新增Up调用后的生产成本。

## 工程与平台边界

9个consumer/config组合14源对象，当前installed Abseil头/Release库，无旧merge/.0头混用；新TU实际/O1 /Ob1 /MT /DNDEBUG /D_ITERATOR_DEBUG_LEVEL=0 /U_DEBUG /EHsc /GR /fp:precise /std:c++20，无PCH/模块扫描/GL/RTC。业务flags未改。Headless MTd未改，--no-window pen-width/codec/EraserAttribute216/animation PASS。

当前产品PE data-only核验SHADER301-304均有效DXBC，未执行入口；x64/Win32相对阶段1测试EXE imports新增0/删除0。diff/XML/manifest/context与独立checker通过。唯一剩余不是代码缺陷，是明确未执行的GUI/D3D运行期/实体笔/真实Win7，以及产品L1/Stored/taper/续接接入。

## 修正过程与证据

保留首轮编译Distance ADL歧义日志；仅新测试私有函数命名修复。监督增量目标project:Build错误改为solution project目标，不改产品工程。参数扫描发现两条短loop新增物理急弯，增加固定弧长硬门拒绝，不放宽测试。重复末端Move/Up因Project upper_bound闭边界丢失有效段而误拒绝，最小改lower_bound，新增等价公开回归。

本任务所有原始报告/EXE/map/tlog/关键sourceobj保留于Build/modeler-baseline/stage2-*与stage2-evidence。已清理本任务私有obj内2514个可重建文件及4个demo生成shader（合计2518项记录），释放3.092GiB；保留14个Modeler/扩展obj、tlog、map、EXE/PDB与全部报告。旧baseline/provider/source资料未删，清理后D盘约6.61GiB可用。清单在stage2-evidence/cleanup.json。
