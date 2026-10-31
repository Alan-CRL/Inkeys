## 2026-10-04 最新阶段状态

用户确认GUI/Office人工验收通过，授权阶段commit且任务继续。10-03其后已完成配对中文UI3/硬笔5自动采样和真实状态/FineDial CLI两配置验证；最新实际记录见父closeout-0811-20261003.md及代码补证review，不再把本文件下方历史“仍待执行”当当前未做清单。最终Release为F017237F29F68758ADEC5A255F3B1D623DC2FF2AF6407AA25DE8A7FE576809FE，已完整Debug/Release ARM64构建及相关检查通过；本轮只补人工确认和commit，不重建。Win7根因/目标机及量化可见帧不退化仍未获新结论。

# 原首发准备续接：当前候选验证（2026-10-03）

用户要求提交当前 Win7 诊断而不结束任务，再继续原 Inkeys3 首发准备。已生成现有 SSH 签名 commit `21a37239336864b2b334abedec4e88d484d10473`（Add Win7 input and GPU pixel diagnostics），未 push。Win7 与首发父/子任务均为 in_progress、未归档。签名对象已存在；本机未配置 allowedSignersFile，未将签名验证能力视为已通过，也未改 Git/SSH 全局设置。

## 续接边界

最新 scope 以父 `closeout-0811-20261003.md` 和代码规范 `closeout-20261003.md` 为准：暂停新性能优化，补当前中文硬笔5与旧 Canary 已有三轮的同条件对照，再补实际状态/revision/取消的窄验证。旧手工软笔样本和更早 UI3-FIRST 未完成场景不冒充本范围通过。本轮先完成当前 Release 构建与有限直接复审，未新增产品源码。

## 本次实际验证

完整 InkeysRepo.sln Release|ARM64，vswhere 定位 ARM64 原生 MSBuild，/m:1 /nr:false，隔离 OutDir/ZhjOutputDir。相同 PowerShell invocation 清理 PATH 重复项并设置 MSBUILDDISABLENODEREUSE=1，允许900秒。构建包含 PptCOM 与 TLB依赖，退出0；既有数值转换警告保留，未改工具链或工程。

| 检查 | 实际结果与边界 |
| --- | --- |
| 当前完整 Release ARM64 solution | exit0；日志 ARM64-Release/build.stdout.txt、build.stderr.txt |
| 当前 Release InkeysHeadlessTests --no-window | exit0，PASS animation correctness；不覆盖实际 IdtState 全入口 |
| 当前 PptCOM.Tests | exit0，PASS descriptor ownership and session/owner contracts；测试假 COM 合同，不启动Office，不代表真实放映退出验收 |
| 0a19182c/21a37239 与直接依赖复审 | 无确认新回归；报告 [direct review](research/release-resume-direct-review-20261003.md)，非全量发布认证 |
| 工作区保护 | 原 .gitignore、八份 cso 哈希不变；四Demo、原raw日志和research索引未提交/删除 |
| 本次交互 GUI/Computer Use | 未运行 |
| Win7 和正式发布就绪 | 待实测，不声明通过 |

此前同一生产源码的 Debug ARM64、Release x64、两架构 GPU probe 已在 Win7 任务提交前执行且通过；这轮没有无意义重跑。当前候选只冻结一个新的 Release ARM64 程序身份，不将旧其它配置结果混作新的运行。

## 产物

输出根 `C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/release-resume`；[完整身份清单](C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/release-resume/manifest.json)。Inkeys.exe SHA256 `515AB004C1285AB44EA4A8C9AC6D4688607DBF1C669B19586E989E8C5EFE434C`，43060224字节，PE machine 0xaa64。PptCOM.dll/.config/.tlb 及测试程序身份均记manifest。未覆盖此前 Win7 x64 测试包或先前性能对照程序。

## 原任务仍待执行

1. 当前新程序与旧 Canary 中文/硬笔宽度5、动画光影等共同条件的书写/橡皮/撤销自动对照；旧样本曝光与采样口径保持，不能从累计CPU或WGC回调推断可见帧率/完整不退化。
2. 实际 IdtState/bridge 的跨入口等价、重复/快速切换、旧revision拒绝；FineDial跨代/cancel单独按实际session与隔离保存边界验证。有限静态复审不代替这些动态检查。
3. Win7唯一GPU根因仍等用户明天日志；其它真实设备/Office、最终HF全量review/矩阵各自保留。

当前会话 active task 为G集成子任务，父首发任务仍继续；后续没有新的 commit/push 授权。此次只提交Win7检查点，首发新文档/续接记录保持可审阅未提交。
