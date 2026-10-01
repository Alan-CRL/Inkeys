# F069 UInk cleanup 归属与恢复材料独立设计复审

日期：2026-10-01。Active task：`.trellis/tasks/09-27-integration-and-release-check`。唯一写入本报告；源码/其它研究/spec/工程只读，未Git、build、运行EXE或测试、GUI、递归派发。F067/A10和上一Root F的2BB scoped GREEN保持，不重新审查其已闭合工作。

## 判决与冻结输入

**APPROVE（合并冻结合同），准许Root授权唯一writer先实施有限seam与确定性RED；不提供源码GREEN或运行安全放行。** 原B320设计的live metadata pin、NTFS能力门与同HANDLE验证删除方向成立，原Partial/异常保留权限缺口由Root独立补充闭合。Root另已选择下面的有限test action接口，实施者必须同时遵循；不能只按原稿实现。

| 输入/前像 | SHA256 |
| --- | --- |
| uink-cleanup-ownership-design.md，19991B | B320CBE66EE2047A8B68280F35CB4DF33210C0B59CBC46C94EB30C87213B3FDC |
| uink-cleanup-ownership-root-amendment.md | 7ABFCBA976CDF240DDF7397307F7E857074FD93FA30C6205082FE4C604965423 |
| shared uink_file.cpp | CAE84F1A3F9664E5BA1B7CDB085CE50A917EE05ADC247B07FCDE766EEF314F79 |
| shared uink_file.cppm | E4E4ED5AA5A1E8FCA5E9FD6B72105A69959D07974EA1BBA3BD9E1F375EA1AE0F |
| inkStrokeModelerTestTests/uink_tests.cpp | 5A2855B2FC9A81A35E4989EA3C98A6B359E482110A894BDD67B0C963FAD5381A |
| Root native metadata result.json | AFA1986AFDAE7FF0BF1D0663D1AA5BE906DA8DDE7323C318EF679323472AB235 |

已合读上述两份合同、A10 cleanup附录与实际Save/WriteNewFile/ReadRevisionAtPath/RevisionFromHandle/HashHandle/四个删除点，核native模块/资源/Win7/质量与现测试注册，并查询微软一手API文档。当前仍无F069源修改或动态结果。

## 核到的真实问题与最小修复边界

| 现站点 | 真实问题 | 批准的最小变化 |
| --- | --- | --- |
| cpp:932先建DeletePathOnExit，:933才CREATE_NEW | guard默认active；候选被actor占用、CreateNew失败仍会删异物 | 默认inactive；只由实际CREATE_NEW HANDLE建立归属/content witness后arm |
| :1036 Replace失败且target仍旧revision | PathExists(backup)不表示backup由此Save创建/仍为predecessor | 仅验证predecessor live pin的backup record；不匹配/缺证明保留 |
| :1058两次恢复Move后 | 删除newRecovery的name不证明它仍为本轮replacement | 来自原Temp对象pin的独立record，同HANDLE复核后才删 |
| :1099最终验证后backup cleanup | 强读关闭后path可被替换；普通DeleteFile没有对象约束 | 活predecessor pin+同DELETE HANDLE身份/强内容+Disposition |
| :1040–1042 Partial且locator为backup | temp仍armed，可删本轮有效staging，尤其backup已foreign/不可读时 | Root补充：所有Partial均disarm temp自动删除，pin保活；不因选择backup恢复自动清理权限 |

只改这些归属/arm/调用点与私有资源寿命；F067 sibling、源revision比较、named mutex、编码/索引schema、原read/write/flush/Move/Replace flags与rollback次序保留。底层Replace/恢复本身的并发覆盖不因此取得全面线性化或零数据丢失证明。

## 活对象与内容见证

1. **Temp来源连续。** 原share0 writer尚活时，在原HANDLE取实际disk/普通文件/无reparse/完整identity，并打开仅FILE_READ_ATTRIBUTES、share READ|WRITE|DELETE的metadata pin比对同对象；CREATE_NEW失败根本无权限。缺pin/FS能力/实际SHA任一前提只保留，不能退回DeleteFileW。
2. **SHA从实际排他HANDLE来。** 成功、partial-write及flush失败分别取该HANDLE当时的真实length/SHA；不能拿encoded digest冒充部分写入。原HashHandle会seek并恢复位置、分配两个有界工作buffer；新增证据捕获必须用独立cleanupError且保留原write/flush/error/status。捕获异常/失败不得阻断原有效保存，也不能arm半份证据。
3. **Predecessor来源连续。** ReadRevisionAtPath可选私有pin输出只用于SaveExisting的源读；原GENERIC_READ/share0 HANDLE仍活时完成强revision与pin比对。caller原currentRevision==session.sourceRevision成立之后才授权该record。不得在原strong reader关闭后重新按path取得pin并追认。
4. **三个来源不混用。** backup绑定predecessor；newRecovery绑定Temp实际replacement对象。Temp发布后Release只撤该name自动清理，pin保持整个Save；进入newRecovery时转移或借用同一活pin，不能关后重开。正常Replace在NTFS的结果沿replacement对象ID，能支持这条Temp→target→newRecovery链。[Microsoft BY_HANDLE_FILE_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/ns-fileapi-by_handle_file_information)
5. **删除同HANDLE完成。** metadata pin继续活，record path以OPEN_EXISTING/OPEN_REPARSE_POINT、DELETE|GENERIC_READ|READ_ATTRIBUTES、shareREAD打开；核普通单link、当前身份与pin/原记录一致，随后同HANDLE再核length/SHA，最后同HANDLE FileDispositionInfo。数据写/rename/delete的冲突打开不能穿过该删除HANDLE的分享门；错对象、内容变更、权限/metadata/hash失败一律只Close/Retain。元数据分享门本身不证明原地内容不变。[Microsoft CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)、[SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)

根metadata experiment的八bool、metadataError/disposeError=0已读取；它只证Win11 ARM64上share0 writer与attribute pin/rename/对象区分/简化DELETE HANDLE的组合。没有原strict reader、真实Replace和GENERIC_READ+SHA删除组合的production证据，不把这个实验写成F069或Win7通过。Root另只读确认D盘NTFS，本报告没有自行运行文件实验。

## 文件系统与平台能力门

**仅NTFS自动cleanup是合理的保守能力门，未过度改变保存支持范围。** 旧64-bit ID在ReFS不保证唯一，FAT改名可变，关闭后的ID也可复用；按原对象HANDLE查询NTFS、完整非零identity/单link，再持live pin到删除，使本方案的身份合同有明确边界。非NTFS/query失败不改原保存API结果，只失去自动删除资格，不扩大为所有FS的永久唯一身份证明。[Microsoft BY_HANDLE_FILE_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/ns-fileapi-by_handle_file_information)

GetVolumeInformationByHandleW与普通SetFileInformationByHandle最低Vista，静态API基线满足项目Win7 SP1仅KB2670838；不需FileIdInfo/FileDispositionInfoEx、POSIX flags、新SDK策略或额外KB。按原文件HANDLE查询卷，无需管理员级裸卷打开或提权。SMB不支持该卷管理查询，失败保持artifact即可；不要用pathname卷查询、FS名字猜测或扩UNC路由作清理回退。[GetVolumeInformationByHandleW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getvolumeinformationbyhandlew)、[SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)

保存继续但cleanup能力缺失时，正常首次Move仍不产生temp孤儿；更新的backup、失败/不确定提交的temp/newRecovery可留存。每次Save资源和HANDLE数固定，跨多次保存artifact数量可能累积；这是安全保留成本，需报告，不擅自加启动扫描/后缀删除/GC。额外全文件SHA与系统查询属于冷保存/失败清理成本；预算和大文件等待需实测，不能声称零成本。Win7/SMB/ReFS/FAT真实组合及数据可见恢复仍待验证。

## 状态、异常和析构必须落实的顺序

| 事务事实 | temp自动清理 | backup/newRecovery | 主返回/定位信息 |
| --- | --- | --- | --- |
| 未CREATE_NEW或任何归属/内容前提缺失 | 不arm/retain | 无资格则retain | 原I/O错误，不捏造cleanup API错误 |
| 已创建，pre-commit partial-write/flush/self-validation失败 | 有完整实际证据才清ours | 不涉及提交恢复材料 | 原IoError/SelfValidationFailed不被cleanup覆盖 |
| Move/Replace可能已触target，尚未获得确定结果 | **先默认retain/disarm**，pin保持 | 默认retain | 不能等异常catch之后才试图修已运行的guard析构 |
| Replace失败且原target强revision仍未变 | 可在该明确safe-abandon支路重新授权清ours | 只删真正匹配来源的对象，foreign保持 | 原IoError/error值保持 |
| 任意PartialCommitRequiresRecovery，任何locator选择 | 全部分支disarm自动删除 | 保留已有恢复材料与活pin至退出 | 原Partial；locator为opaque实际路径，不自动声称其仍有效 |
| post-mutation异常/不能强证旧target仍在 | retain，不能默认析构删 | retain | 原异常/catch语义，不伪造Committed或有效恢复 |
| 原最终目标strong validation通过 | temp已发布disarm | 仅原明确cleanup站点有完整证明才清 | 原Committed；backup cleanup失败保warning/recoveryPath |

强制Root补充覆盖原稿含糊的“异常清理”。最小实现是在进入可能改变target的原Move/Replace前暂停temp自动删除，pin仍持有；只在原已证明安全abandon出口重新授权。这个标记不改变原API调用/返回/rollback。单纯created bool、对象ID或“这个文件是ours”都不能代替是否仍需恢复材料的判断。

guard/所有新helper noexcept并catch内部失败，只Close/retain；writer/strong-reader必须先结束，pin再由guard/Save owner清理。成功Disposition表示marked-delete；DELETE HANDLE仍持同对象，pin可释放，剩余引用/FS决定最终物理回收。无DELETE_ON_CLOSE、无权限降低/ACL修复、无path fallback。临时容器/路径先准备、move新HANDLE不得有泄漏；全部早退/异常保留主错误，GetLastError先锁存再hook/metadata/hash/Close。

## Root已选定的最小测试接口与所有权

保持只有一条 `#if defined(DRAW3_TESTING)` setter及其必要有限类型声明。callback **noexcept返回None/ThrowBadAlloc**；只接受production产生的六stage/实际path与当前同步scope context，不接受任意cleanup path。ThrowBadAlloc只在shared cpp既定post-mutation stage、真实Move/Replace及原error已锁存后由cpp内部抛std::bad_alloc，走原Save catch和真实guard析构。callback自己throw会terminate，不能冒作异常展开验证；不改全局operator-new、正式FaultInjection DTO、配置、环境或CLI。

其余stage只记录/操作本轮新私有数据，返回None。setter安装/调用/复位使用同同步测试线程；callback/context作为一对，同scope内不并发改。若有actor线程，先释放其门并真join，再撤context/父目录leases；不detach或把actor仍可能写文件时的超时当PASS。测试callback内部捕获异常并显式前提失败，不跨noexcept边界。

| 唯一writer边界 | 批准职责 |
| --- | --- |
| shared uink_file.cpp | 私有pin/guard/同HANDLE cleanup，WriteNewFile、ReadRevisionAtPath可选私有证明，原四点及Partial/异常arm策略，test-only六阶段调用/固定异常动作 |
| shared uink_file.cppm | 仅DRAW3_TESTING限定setter/callback返回action和必要有限stage标识；所有正式DTO、status、入口逐字不变 |
| uink_tests.cpp | 新create-new私有非reparse根/租约、真实Save actor/字节身份/状态断言、scope复位；保留旧F067/FullSave/Append断言 |
| Root | 后续上下文/工程需要项、源码/运行安全门、串行构建/实际运行、spec/账本；不自行新增selector |

现tests.vcxproj确有DRAW3_TESTING并直接编译同shared cpp/cppm；主产品未定义该宏。实现后仍要核宏同时覆盖声明/实现/调用，普通产品不能残留hook状态或异常动作。named module内私有函数不由tests非法forward-declare，不引入Auth/GUI helper依赖。

## 确定性验收与不能借用的证据

必须先在现DeleteFileW行为下加seam/tests，拿真实特定RED；捕获实际candidate而非指定RNG/GUID或预造正确保存文件。成功环境前提、真实stage、actor动作成功、原status/error、byte/identity和foreign保留分别记录；缺stage/权限/FS能力只记未满足/未验证。

| 必需用例 | 必须证到的实际结果 |
| --- | --- |
| pre-create sentinel占名 | 原CREATE_NEW真实失败；RED foreign被删；GREEN从未arm、sentinel全部字节/ID和target缺失保持，主IoError一致 |
| temp替换 / same-ID改内容 | 原writer已关闭且pin保持；不同对象或相同ID不同SHA均retain，原held对象不按其它path追删 |
| backup失败/成功cleanup前异物 | failCommit仅代表固定模拟原Replace失败；真正成功Replace另一case独立；foreign/内容改变保留，原status/locator如实，不混成真实OS故障复现 |
| newRecovery | 真进入两Move与原cleanup点；Temp活pin与predecessor不同；异物/改内容拒删，数据/原状态与剩余材料分别核 |
| DELETE或Disposition拒绝 | 只对本轮自建文件与父目录DACL；准确拒DELETE与FILE_DELETE_CHILD，两者设置/恢复前提失败就停该case；readonly/sharing冲突是另一个原因，不能冒ACL PASS |
| pre-commit ours失败 | partial-write/flush/self-validation各有实际created/pin/currentSHA；能力可得且未变才清ours，未知/target完整字节不动 |
| Partial选foreign/不可读backup | 真实Partial分支且locator为backup，仍自验证temp存在；GREEN必须保留该temp，不能因它“是ours”删掉；不用副本替原真实temp |
| post-mutation bad_alloc | callback返回有限动作，cpp在真实mutation之后抛；原catch/guard真实展开；所有可能恢复材料/foreign保持，无Committed假结果 |
| 正常/能力缺失 | 原F067 230路径首存→强读→更新→实际opaque恢复仍成立；有能力Committed backup才清，无能力仍Committed并报告retention/warning；严格Read/Move/Replace不被pin挡住 |

NTFS metadata pin/完整GENERIC_READ+DELETE/shareREAD、当前强SHA删除与所有早退/异常须真实helper新GREEN验证。非NTFS或query/hash/metadata失败只能证明retain，不冒称已清ours。原Win11八bool与旧F067 PASS不能代替此RED/GREEN。

Root复用已存在 `inkStrokeModelerTestTests.exe --uink-file-only` 精确NoGUI入口（实际既有输出路径按构建配置核），不默认跑demo/无参数GPU套件。按真实项目依赖完整既有standalone solution和主 `InkeysRepo.sln Debug|ARM64`分别取证，原生ARM64 MSBuild、同PowerShell PATH修正、至少5分钟。实现/safety复审前不运行新actor/ACL/异常case。生产C09/C10、三轮fresh、Release/Win7与可见恢复另按原合同，不重写失败历史或降断言。

## Findings (fixed)

- File：原Save:1040–1042及F069设计的temp arm策略。Issue：Partial返回backup时temp自动清理仍可删唯一有效staging；异常发生后再处理也可能晚于析构。Fix：Root的7ABFCBA9强制补充规定所有Partial disarm、post-mutation先默认retain，仅强证safe-abandon重新授权。源码尚未修改，本reviewer只核合并合同。
- File：test-only异常接口选择。Issue：noexcept callback直接throw不能验证Save的实际异常展开。Fix：Root明确采用有限None/ThrowBadAlloc返回、shared cpp固定post-mutation点内部throw；不扩正式DTO或全局分配器。

## Findings (not fixed)

合并设计无剩余must-fix。所有pin/guard/arm/同HANDLE删除、宏限定seam、真实actor/权限/exception与错误保留尚无源码/新构建/运行证据，必须审最终actualdiff及真实RED→GREEN。底层Replace/rollback并发行为、映射/外部metadata/FS不兑现分享或身份规则、未durable数据及真正跨进程可见恢复不由本合同包办；不增加未授权回收政策。额外HANDLE/hash成本和非NTFS正常artifact积累需实际报告。

## Verification

- 静态设计：**APPROVE于B320+7ABFCBA9+Root选定有限action合同**；真实四删除点、Partial状态、活句柄来源、原强SHA/排他分享/已存测试和API最低平台已核。
- Lint / TypeCheck / Build：未运行；本dispatch仅设计，只读源码及写报告。
- Tests / GUI：未运行；根native result仅Win11 ARM64前提实验，所有新F069门NOT VERIFIED。报告不授予运行、提交、发版或归档许可。
