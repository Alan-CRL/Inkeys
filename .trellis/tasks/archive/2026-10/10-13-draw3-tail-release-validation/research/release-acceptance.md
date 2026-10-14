# 阶段四工程验收结论

2026-10-13。阶段四技术与当前人工验收通过。用户已明确确认鼠标/触控板、实体笔、触摸均未出现异常；此前偶发长斜线未复现、未定位，按用户要求暂缓，不作为当前结束前置，不宣称已修复。数字6旧小末钩也沿用原暂缓决定。

## 最终人工确认（2026-10-14）

用户明确宣布人工验证通过，并要求结束任务及提交 commit。阶段三、阶段四和父任务维持 completed/已归档状态；本次补记最终确认，不新增阶段五或代码修改。

本次确认未指定额外的程序配置或目标平台，因此不据此追加 Release GUI、真实 Win7、原生 x64/Win32 等覆盖声明。此前偶发长斜线和数字6小末钩沿用用户已批准的暂缓决定，不宣称已定位或修复。

## 最终行为

默认正常颜色、无反色元数据、无尾轨迹同步I/O。显式`INKEYS_DRAW3_TAIL_TRACE=1`保留正常颜色的有界诊断，关联初始/当前接触、消费合并、真实Up、拟合前模型、笔锋后的完成副本和Stored。每进程128次/2MiB，有限点列与create-new后缀不污染旧证据。诊断不进入求解参数、文档格式或上游Modeler API。

本阶段未改变尾段算法、阈值、参数、Modeler运行源、Abseil/vcpkg、SDK、工具集、优化/CRT或shader。新增真实完成态history/composition/UInk往返和等待/接续/Reset回归；默认off/on不会改变生产确定性几何。

## 自动验收

| 配置 | 主solution | demo/test solution | 生产 / 尾段 / 原模型 |
| --- | --- | --- | --- |
| ARM64 Debug | PASS | PASS | 334115 / 9245 / 128412，0失败 |
| ARM64 Release | PASS | PASS | 同上，0失败 |
| x64 Release | PASS | PASS | 同上，0失败 |
| Win32 Release | PASS | PASS（solution x86映射） | 同上，0失败 |

均使用当前VS原生ARM64 MSBuild、同PowerShell规范PATH、3600秒构建超时。原模型报告SHA256四配置均`0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646`，保持精确基线。完成态persistence/integration均0失败。

最终Debug程序SHA`494f645b0289a89f41607ab8b7cffe4b3a41ef204913d1226b04d538dabd2221`。最新默认生产与Headless证据已补，Headless实际退出0，98.36秒；/MTd工程未改。`--draw3-control-fence-test`退出0；最终默认/on生产两次几何一致，on记录64完整条目/1,845,792字节，默认无新增TEMP日志。

四套主产品PE machine及SHA与构建记录匹配，301–304资源非空DXBC；x64/Win32主产品及demo/test静态imports对保存基线added={}、removed={}。仅数据加载和dumpbin检查，不执行GUI。构建日志无旧merge.lib，Abseil来自各目标VcpkgInstalled Release库。

## 人工与真实记录

38916日志92完整Mouse记录：91应用、1前缀相交回退。9812日志107完整Pen记录：67应用、39求解/无需修正、1前缀相交回退。位置/输出属性finite、时间不倒退、Up对应、最终Stored映射和数组预算检查通过；没有整笔Down+Up-only或模型Up失败回退。Touch体验来自用户确认，日志中没有Touch或reconnect条目，不能将Pen数据当作触控接续现场证明。具体逐笔证据见actual-38916-input.md和actual-9812-input.md。

## 成本与清理

关闭诊断I/O，完整`FinalizeStoredStroke`预热后30轮：ARM64 Release prefix0中位/P95=0.0471/0.0493ms；prefix1000=0.1464/0.1498ms。这是完成几何生成函数成本，未包括实际GPU或端到端抬笔延迟。x64/Win32仿真成本另见release-matrix.md，不冒称原生性能或历史成对回归证明。

保留程序/PDB、原对照、输入、报告、CL/link/FXC/RC命令与当前ARM64Debug缓存；9个已验收Release项目obj释放6.500GiB，此前57项旧缓存释放约2020.6MiB。未删第三方/installed依赖或用户资料。清理清单保存在忽略的phase4-evidence目录。

## 明确未执行

真实Win7、D3D Debug Layer、GPU像素自动测试、Release实体GUI、窗口resize没有作为本轮执行项；x64/Win32可执行检查是在Windows11 ARM64仿真环境。没有添加Touch立即Up新预览，也没有猜测性过滤远距Up或恢复位移门。

原始与机器报告在`Build/modeler-baseline/phase4-evidence`；正式源码、规范及任务研究纳入签名提交，生成cso和Build不纳入。完整审查见phase4-final-check.md。后续若旧偶发事件再次出现，继续使用显式诊断捕获并建立对应回归，不影响本次按用户决定完成的阶段四记录。
