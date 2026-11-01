# 实施与验证记录

日期：2026-11-01。用户批准后task.py start进入in_progress，无GUI/UAC操作，无commit/push。

## 变更
目录fileID presence、默认禁止多开、全部实例登记、固定跨架构包、验真Ready/精确父退出连续交接。更新保留原路径/哈希/备份验证，能力三态兼容旧目标。正常重启监督器、SuperTop双Ready、installer物理guard均接同一生命周期。通用安全开关在教学安全选项后，配置默认false并仅影响后续启动。

## 已完成检查
| 检查 | 结果 | 证据 |
| --- | --- | --- |
| 完整InkeysRepo.sln Debug ARM64，原生MSBuild | 退出0 | validation/final-build.log（最终build-installer退出0）；前几轮build日志在Build目录 |
| --single-instance-tests（严格等待，常规Windows） | 退出0 | validation/single-instance-protocol-installer.stderr.log（最终root/installer/root版本） |
| --shutdown-supervisor-tests --instance-lease-only | 退出0，旧父exit77，restart_count=1 | validation/single-instance-lease-verified.stdout.log |
| --config-boundary-test | 退出0，含新bool与只读Mini无迁移字节变化 | validation/single-instance-config-verified.stdout.log |
| --supertop-token-failure-test | 退出0 | validation/single-instance-token-verified.stderr.log |
| --setting-general-only --no-window | 退出0，三语真实条目/事件/位置 | validation/single-instance-general-verified.stdout.log |
| i18n.ps1 check | en-US、zh-TW均573/573 PASS | 本轮根代理工具输出 |
| git diff --check、原worktree-filtered BOM/CRLF | PASS | 原core.autocrlf=true，不能用index原始LF误判worktree CRLF |
| 独立检查 | 见research/final-review.md | 并行check未自行构建，动态证据取根代理真实工具结果 |

编译存在既有C4244/C4267等告警及applocal pwsh.exe缺失提示，MSBuild真实退出0；未改SDK/依赖/工程配置以绕过环境。

## 失败记录与修正
1. 首轮真实protocol和新增监督relay红测：Local互斥体在runner使用AppContainerNamedObjects，硬编码BaseNamedObjects误拒。改为OpenMutex reference系统实际解析完整名比较，保留类型/范围/失败closed。
2. 跨架构uintptr wire、旧handoff转发、Ready后物理guard冲突、SuperTop双Ready先后、未知cap降级均由独立检查/根代理发现并修复，关闭证据见final-review。
3. 原能力fixture在已执行/查询EXE上原位改RT_VERSION，native发生110；相同sandbox两份独立只读/私有新copy API探针证明offset/API正确，改每个变体用未执行新copy。
4. 新copy版本资源写在受限runner仍error5，其他接力/race/目录/probe通过；同一最终EXE在常规Windows严格专项全部通过，unknown/absent/current写入均1，读取enum2/0/1。不能把runner资源写限制当生产代码失败，亦未关闭校验以使其通过。
5. Inkeys.exe GUI subsystem CLI用Start-Process -Wait -PassThru -WindowStyle Hidden及双重重定向，取真实Process.ExitCode；不把直接&返回的旧LASTEXITCODE当成功。

## 覆盖与限制
- 进程专项在私有复制EXE中真实运行生产Acquire/Adopt/Publish；barrier两个进程并发准入只一个赢家，不同目录独立，allow→deny仍拒新增，坏包不通过，最后引用关闭可重开。
- 监督链回归由driver持另一SYNCHRONIZE-only引用，真实Restart接力不会被该存活引用误拦。旧完整监督套件红轮只有新增namespace相关case失败，其余原有正常/崩溃/截止/故障case均PASS；修正后新专项绿。
- 未执行真实产品GUI、UAC/UIAccess、Win7、跨32/64位包运行，以及真实安装文件替换/回滚的端到端实机验证；固定wire静态布局不等于跨架构运行已测。
- 旧V2/V3目标的兼容桥只确认OS创建/lease复制/恢复，旧版自身后续SuperTop不能转发新协议，不声称跨旧二进制全链持续新lease。

## 最终installer目录回归
最后构建InkeysRepo.sln Debug ARM64退出0；同次产物严格Start-Process常规Windows专项退出0。日志validation/single-instance-protocol-installer.stderr.log：
- 预占installer物理guard时，测试父exit1是预期负例；installer conflict retained-parent=1 ok=1，证明失败发生在Ready前、父仍持根lease且无接任marker。
- 释放阻挡后真实私有root→installer→root两跳成功，parent exit0；随后普通probe拒绝/不同dir接受/allow接受/关闭后拒绝/坏包拒绝/释放后重开均符合预期。
- 两并发进程barrier verdict为0/1，只有一个准入赢家。
- 三个未执行私有PE变体资源写均1，unknown/absent/current实际enum分别2/0/1；专项总PASS。

最终这些补充仅改TryRunTests测试区，生产准入/更新/SuperTop/配置代码相较此前已通过lease/config/token/general专项的版本保持不变。没有为达成PASS关闭任何生产检查。
