# 白板惯性尺寸与设备适配：验证记录

> 最新授权（2026-11-13）：用户要求先提交当前已验证改动，但不结束任务、不push。之后屏幕笔惯性与默认光标问题先只读调查，不修改产品代码。

状态：源码与自动验证完成；任务保留in_progress，待真人设备体验验收。未启动GUI、未commit/push、未归档。

## 实现与证据

已接入可信直接Touch/屏幕Pen的毫米动作度量、无可信标尺的模式化DIP回退、五profile参数、唯一有序Down的Pending准入、逐事件canonical DIP出口、真实RTS Pen token及配对反馈。原可靠尾段、当前残余正投影、反向与Down中断保持。没有修改输入后端、橡皮、持久配置、触控板隔离、solution/vcxproj、依赖或SDK。

八个产品/测试文件与三份规范修改，保留原BOM和CRLF，尤其DrawingController原本UTF-8无BOM。独立审查见[implementation-review.md](research/implementation-review.md)，全部命令、退出码、源码/产物/日志哈希见[机器证据](research/implementation-validation-results.json)。完整原始日志在 `Build/Logs/pan-scale-device-2026-11-13/`。

## 开工基线

Git基线 `1b819d2df656f84cb9481b8e7aab1321a31c73ae`；起始仅本任务规划目录未跟踪，没有其它脏文件。原源码格式/哈希见baseline.result.json。

旧独立验证目录的Inkeys.exe已不存在，默认EXE是另一哈希。本轮保存该产物哈希并运行pan-feel/control-fence，两项exit0且输出与上次已验证基线逐字节一致；已知旧Headless的pan专项exit0。这些是开工观察，不冒充本轮新产物。

## 完整构建与环境恢复

全部用vswhere动态定位的ARM64原生MSBuild，`InkeysRepo.sln Debug|ARM64 /m:1 /nr:false`，同一PowerShell先规范Path/PATH并禁node reuse，超时1800秒。

| 构建 | exit | 秒 | 结果 |
| --- | ---: | ---: | --- |
| build-1 | 1 | 34.937 | 默认增量链接器LNK1000 |
| build-2 | 0 | 35.172 | 完整solution通过，产品源哈希冻结 |
| build-3 | 0 | 24.875 | 完整solution通过，产品源哈希冻结 |
| build-4 | 0 | 23.687 | 完整solution通过，产品源哈希冻结 |
| build-5-default | 0 | 15.875 | 完整solution通过，产品源哈希冻结 |

准备阶段曾修正运行器的多Git路径选择，并恢复Controller原本无BOM；当时未启动MSBuild，不能算源码编译失败。build-1的主程序C++编译通过，首个有意义错误是link.exe 14.44.35207在IncrBuildImage内部C0000005/LNK1000。没有修改代码/SDK/工程配置规避它。

build-2/3/4以独立OutDir完成全solution。最后确认默认EXE没有运行、源/目标均在workspace内，将本次失败生成的268435456字节 `Build/ARM64/Debug/Inkeys.ilk` 备份到日志目录，再按原配置重建默认输出。build-5-default成功，0errors/0warnings；没有递归删除缓存。先前编译中现有第三方/IdtMain转换、nodiscard与ContactRecord对齐警告未顺手修改。

## 审查与首轮专项修复

- 身份/来源选择与mailbox发布、清除原先分锁：改同一短事务和配对读取，UI/触觉/wake锁外，新增双publisher生产CPU回归。
- Pending毫米Down在Step前的EDID资格/密度变化：按真实Down source重验并Stop旧批；模式/无害更新和DIP到可信不半途升级。
- 新适配把重复/逆frame当失败而Stop：恢复Core Snapshot no-op，Controller在Drain前保留队列，下一有效帧完整准入/闭合；原128条失败断言完整保留。
- Stop死指旧断言仍要求Touch owner：改None/非held/非Active，仍检查物理HasContacts、BatchDead、位移不变和Up回收。
- Pen夹具raw身份错配可能空样本误通过：对齐真实raw IDs并验证实际发布，未放宽生产token资格。
- 异常终态即使排序在同key Down之前也可靠取消闭合，不遗留held或拒收屏障。

初轮新产物headless-pan/diagnostic/config通过，control-fence只有两次旧owner断言失败，pan-feel只有上述128次重复/逆帧失败；全部失败原始记录保留，没有只摘PASS。

## R3完整无窗口验证

| 记录名 | exit | 秒 |
| --- | ---: | ---: |
| headless-pan-r3 | 0 | 1.047 |
| control-fence-r3 | 0 | 1.312 |
| pan-feel-r3 | 0 | 2.891 |
| diagnostic-output-r3 | 0 | 1.859 |
| config-boundary-r3 | 0 | 2.140 |
| setting-general-r3 | 0 | 18.313 |
| headless-all-r3 | 0 | 325.265 |

全部运行于build-3的新产物，产品源码哈希全程一致。完整套件末尾PASS animation correctness。生产probe实际经过ContactInput→CanvasPanInput→Window，不以公式/末端helper替代真实接线。

## 最终收尾及默认产物复测

全量通过后仅修两处单位注释、source-conflict/source-not-direct-touch诊断reason及十个故障fixture的精确标签断言。其它六源文件与R3快照字节一致，resolver去除reason语句后的文本一致；无运动/profile/准入/估速/渲染逻辑变化。差分在 `closeout-vs-r3.diff`，R3完整源快照也保留。因此完整套件证据复用，最终收尾完整build-4并做对应专项；默认目录正常build-5后再次验证实际默认产物。

| 最终/默认记录名 | exit | 秒 |
| --- | ---: | ---: |
| headless-pan-final | 0 | 1.500 |
| control-fence-final | 0 | 1.500 |
| pan-feel-final | 0 | 2.922 |
| diagnostic-output-final | 0 | 2.110 |
| config-boundary-final | 0 | 1.922 |
| headless-pan-default | 0 | 1.203 |
| control-fence-default | 0 | 1.188 |
| pan-feel-default | 0 | 2.813 |

所有最终运行产品源哈希一致。原DIP基线实际2240场景、整份708090字节与开工和上次已验证输出完全一致，SHA256 `c3c3e6975bac0b8998c98a291a72f71e823bf708b8a1e01701238df01e67d9df`。程序旧汇总rows=2112未包含部分追加场景，本任务未顺手改计数格式。新物理/大屏/屏幕笔profile另有生产和纯内核回归；旧DIP等价不代表它们的最佳手感已验证。

## 最终正常产物

默认目录已正常构建，并通过pan/control-fence/feel专项：

- `Build/ARM64/Debug/Inkeys.exe`：SHA256 `7716d646564cfa73145dfbb2c35b5418e3a48681dc47755f15eda9670abdf12a`。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe`：SHA256 `23f0338c31c1628107b6c2176914e11e506218270a6daefbfaf07168851d940f`。

独立PanScaleValidationFinal及R3产物、失败ILK备份保留在Build中供追踪。没有关闭或操作用户应用。

## 真人验收与能力边界

- 未真实测试Surface Pro 11触摸、教室大屏、屏幕笔/笔尾、外置板及无EDID两模式手感。约28×19cm是用户描述，不是本轮读取的硬件标定。初值可按真实动作继续校准，不声明最优。
- EDID仍是现有业务策略级可信，基础整数厘米、override/黑边等边界保留；DPI回退保证单位闭环与有界模式响应，不承诺真实毫米一致。
- Pointer cursor/range不证明同一次RTS Down；缺token/歧义/容量不足时保守降低反馈纠正，不丢core输入，不凭旧RTSack全局清Pointer-local latch。
- Touchpad继续quarantine，未实现新后端/缩放/绘制模式漫游或显示服务增强。未做GUI/光学/硬件延迟测量。

代码与自动验证完成，任务继续in_progress；commit/push/归档未授权，真人体验验收待后续记录。
