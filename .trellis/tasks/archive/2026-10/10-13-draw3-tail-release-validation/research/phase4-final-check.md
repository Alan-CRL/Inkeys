# 阶段四最终完整范围审查

2026-10-13。Active task: `10-13-draw3-tail-release-validation`，基准阶段三签名提交 `71858b0b`。本轮重新读取 package index/Quality Check 与最新 PRD/design/implement，以用户最新人工确认及“旧偶发长斜线暂缓、完成阶段四”为验收范围。原 `phantom-trace-check.md` 的源码审查继续有效，其人工失败结论是当时状态，已由本次后续确认替代；旧问题没有被宣称已修复。

本代理仅审查、核对证据和写本报告，未重新编译、未运行 GUI、未提交或归档，未修改算法或参数。

## Findings (fixed)

本轮最终复核未发现新的产品源码阻塞问题，无新增源码修改。此前诊断审查已修复两项：接续的初始/当前 contact 归属及旧终态残留；PID 复用混入旧日志。具体代码和回归见 `phantom-trace-check.md`。

本轮发现验证脚本仅检查固定PID主文件，可能误判旧PID文件或漏过新后缀，已通知主会话修正忽略目录的 `run-phase4-checks.py`：显式移除TRACE环境变量，以新文件集合和当前PID主名/后缀匹配检查默认无I/O。后续ARM64 Release结果已用新判据通过；不会污染产品代码。

## Findings (not fixed)

- **旧偶发事件延期（用户明确范围决定）**：此前抬笔突然出现跨屏长斜线仍未定位，没有异常现场日志。本次鼠标/触控板、笔、触摸体验均由用户明确确认正常，并要求旧事件暂缓，不再作为阶段四完成前置条件。不得写成“已修复”或“已排除全部 GPU/驱动问题”。
- **实体与日志证据边界（有意保留）**：9812 的107条日志全为 Pen，没有 Touch 或 reconnect>0。Touch 体验确认独立来自用户，不以 Pen 日志冒充触控接续回放。窗口 resize、D3D Debug Layer、GPU 像素和真实 Win7 未由本代理执行，也不由无窗口 CLI 或资源读取推断通过。
- **有界诊断与数值限制（有意保留）**：model/completed/stored各最多512点，有限窗口摘要不能证明未序列化部分不存在异常；completed为拟合+taper/公切线后的副本。create-new碰撞后缀分支有静态核对，当前自动CLI走主名称，不将碰撞分支写成已实际触发。

## 完整代码/任务范围核对

- 全部阶段四业务delta限于产品 `Draw3.InkPrediction.cppm`、`Draw3.DrawingController.cpp`、`Draw3.StrokeGeometry.cpp`，以及既有测试 `contact_input_tests.cpp`、`uink_tests.cpp`。没有 upstream Modeler/本地求解器、依赖版本、vcpkg、SDK、工具集、CRT、优化、shader或文档格式diff。
- 产品默认正常颜色、无TEMP尾轨迹I/O；精确进程环境变量显式开启有界诊断，与算法Apply和反色独立。Down锁存启用值，Move只维护常数尺寸数值，记录失败不回写或影响真实 Up/完成几何。
- 初始 contact 身份与最新 contact Down对应，接续序列基准和旧终态清除；Reset清空诊断，纯数值身份不延长ContactRecord寿命。即时/延后实际Up统计位于真实模型调用和接纳路径，没有诊断合成运动。
- 正常颜色生产probe实际比较修正位置、完整属性和样式；计时外验证每轮结果与已证实Apply的expected一致，计时区关闭诊断I/O。不是用反色marker替代几何验收。
- 新测试使用实际 Finalize 结果，经history Undo/Redo、composition footprint、UInk capture/export/file/read/import往返；等待接续预览不改活动realPoints或有界raw/model窗口，Reset之后的新contact不继承旧数据。未创建新测试框架或复制求解器。
- 资源核验工具以LoadLibraryExW数据标志读取PE的301—304，禁止执行入口；新产品hash与本轮build.json核对，imports与迁移基线对照。完整生成链与实际退出码由矩阵证据补充，不能只凭EXE存在判通过。
- 磁盘清理只针对核对过的本任务生成obj/调试缓存；六个旧对照EXE、输入和报告保留。4个未跟踪cso仍为构建产物，排除提交。
- 最新PRD/design/implement和task.json已记录人工accepted及旧事件deferred。主会话已同步spec中的首次失败与后续人工确认，当前合同明确旧事件未修复且用户暂缓。历史研究/检查点保留当时结论，本报告记当前最终验收，不能把历史失败当当前状态。

## 实际38916/9812日志独立核对

| 运行 | 完整记录 | 分类 | 最终发布 | 文件SHA256 |
| --- | ---: | --- | --- | --- |
| 38916 | 92 | 92 Mouse / Mouse source | 91 applied，1 prefix拒绝 | ce221213e26b1295c2bd2381737eac169720b37b57be28c2b31dd008574c966c |
| 9812 | 107 | 107 Pen / IntegratedPen | 67 applied，39 solver/nochange，1 prefix拒绝 | 8093d4ebd8ddb06e31996f3ff4d002eab981963e14845ec440acbff615205f39 |

两次均检查全部完整记录，不按一部分样本推断：initial/current身份对应；Down/contactDown相同；lastNonterminal/terminalPrior相同；physicalTerminal/modelUp相同；Up status=0、fallback=0、pinned=1；snapshots=moves+2、reconnects=0；位置与所输出属性finite，各数组/字节量不超硬预算。38916为1,852,209字节，9812为1,861,465字节。机器证据保存在忽略的 `Build/modeler-baseline/phase4-evidence/final-review-actual-logs.json`。

上述不是旧异常的失败fixture，也不是所有输入正确性的证明。人工确认来自用户；日志只补有限完成过程和分类数值证据。

## Verification

- Lint / 静态：本轮 `git -c diff.ignoreSubmodules=all diff --check` 退出0；5个修改源码与HEAD BOM一致、CRLF保持，无裸LF或双CR。仓库没有独立C++ lint命令。此前所有诊断字段/接口/提前返回和数据不变量审查继续有效。
- TypeCheck / Debug：最终原生ARM64完整 `InkeysRepo.sln Debug|ARM64`已退出0，诊断产品SHA256 `494f645b0289a89f41607ab8b7cffe4b3a41ef204913d1226b04d538dabd2221`。Debug独立模块solution退出0，基线/自适应/集成/文件结果通过，Headless --no-window本阶段退出0。未由本代理重复构建。
- Tests / Debug：最终默认/off与显式/on生产probe均334115项、0失败；默认无新TEMP记录，on解析64完整记录、1,845,792字节；control-fence退出0。独立算法9245项及原模型128412项通过，原baseline SHA256仍 `0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646`。
- 最终Release矩阵已由验证代理执行并由本代理独立读取实际退出码、重新计算EXE hash及PE machine：主产品/模块 ARM64 Release、x64 Release、Win32 Release 六个构建全部退出0，四配置的模型基线、尾段及集成回放通过。具体证据与成本如下。


## 最终构建、兼容与成本证据

| 配置 | 主product构建/生产回放 | 模块solution/回放 | PE machine |
| --- | --- | --- | --- |
| ARM64 Debug | 0 / 334115项0失败 | 0 / 9245+128412项0失败 / integration0 | 0xaa64 |
| ARM64 Release | 0 / 334115项0失败 | 0 / 9245+128412项0失败 / integration0 | 0xaa64 |
| x64 Release | 0 / 334115项0失败 | 0 / 9245+128412项0失败 / integration0 | 0x8664 |
| Win32 Release | 0 / 334115项0失败 | 0 / 9245+128412项0失败 / integration0 | 0x14c |

六个剩余Release构建复用原solutions，动态定位原生ARM64 MSBuild，timeout3600秒；Win32模块solution使用原有x86映射，主product仍为Win32。读取每个build.json实际exit与EXE hash并重算匹配；测试results.json的最终exit全部为0。四配置baseline报告字节SHA均为0119fd2a…646。新的生命周期/UInk断言均通过。x64/Win32程序在本机Windows11 ARM64环境执行，不能称为真实Win7或原生x64/x86设备运行验证。

Debug关联说明：最终494f程序已重新执行完整默认检查组合，`main-ARM64-Debug/production.log`现为334115项0失败，`results.json`含默认无TEMP记录及最新Headless --no-window退出0/98.36秒。此前334114旧生产日志不再作为最终证据；原phantom-trace/default.log和verification.json是同SHA独立诊断开关验证。

四主product PE都含301—304非空DXBC；资源hash跨配置一致，文件hash均与当前build.json匹配。`compatibility/products.json`的x64/Win32主product以及`compatibility/auxiliary-imports.json`的四个demo/tests条目，相对六个迁移基线EXE的added/removed均为空。六个保存基线文件已重新hash与preserved manifest逐项一致。资源仅作为数据读取，没有执行GUI；静态imports保持不是Win7真实运行证明。

完整Finalize计时均预热3轮、30轮、diagnostics_io=off；计时外断言实际Apply、真实Up、属性/样式和稳定前缀保持。最新完整Debug默认回放和各Release production.log的样本成本为：

| 配置 | 前缀0 median / P95 ms | 前缀1000 median / P95 ms |
| --- | ---: | ---: |
| ARM64 Debug（最新默认off） | 0.064400 / 0.074100 | 0.747450 / 0.937800 |
| ARM64 Release | 0.047100 / 0.049300 | 0.146400 / 0.149800 |
| x64 Release（本机兼容运行） | 0.080800 / 0.087100 | 0.236400 / 0.278400 |
| Win32 Release（本机兼容运行） | 0.392200 / 0.958200 | 0.560750 / 0.696600 |

这里是当前完成路径的有限样本实测，不是架构性能排名、任意设备上限或所有用户笔划的延迟保证。x64/Win32兼容执行成本不能与原生ARM64成本混作算法退化判定。主要门是实际修正仍生效、无默认轨迹I/O、数值/属性不退化及当前成本可见；没有为降低数字修改算法或验收容差。

本代理机器审计保存于忽略的 `Build/modeler-baseline/phase4-evidence/final-review-matrix.json`，包含当前8个EXE的hash、machine、退出状态及production计时出处。

## 最终结论

**PASS：按用户最新确认范围，阶段四技术与人工验收门均满足，可完成任务并按授权收尾。** 此结论包含正常颜色、默认无诊断I/O、实际几何修正、完成笔划生命周期、四配置构建/回放、资源与导入、有限成本以及用户Mouse/Pen/Touch体验确认。

最后再次读取清理过解析器的兼容报告：主product与auxiliary imports均没有Summary节名伪条目，added/removed仍为空。主会话仅修复忽略目录检验工具，没有修改源码或依赖；不需要再构建。最终diffcheck退出0，源码保持原BOM/CRLF；阶段三task.json的Git autocrlf提示不作为业务失败或无理由全文件换行修改。

旧长斜线仍是未定位且用户明确暂缓事项，6的偶发小末钩按此前决定延期；无此根因修复声明。真实Win7、D3D Debug Layer、独立GPU像素和额外resize矩阵未执行，不扩大验收结论。主会话可更新父/子任务、日志、按当前授权提交并归档；须排除4个生成cso，遵守SSH签名失败不得绕过、不push。
