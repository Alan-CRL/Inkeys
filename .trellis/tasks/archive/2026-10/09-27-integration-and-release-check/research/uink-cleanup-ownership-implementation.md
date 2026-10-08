# F069 UInk cleanup 归属实施

Active task: .trellis/tasks/09-27-integration-and-release-check。2026-10-01。

## RED写入前冻结

Root明确WRITE_ALLOWED_F069_RED，唯一shared uink_file.cpp/cppm的DRAW3_TESTING seam、uink_tests.cpp和本报告可写。原生产cleanup/DeleteFileW/arm/Partial/异常保持旧实现取RED；F067同父短名与230 strict回归不回退。没有Build/run/Git/GUI/递归或其它source/entry/工程/spec/Root账本写权限。

已完整读B320CBE6…3FDC设计、Root7ABFCBA9…5423强制Partial/异常补充及49C0100AFE8786043C8629C74FC2B1B8F190DB3CB3061FC0A95E0C37000CEF5D合并APPROVE。普通production DTO/导出/CLI不增；test action为None/ThrowBadAlloc，callback noexcept，只有cpp固定postmutationstage自行throw，原Save catch/析构实际运行，不能callback throw/global new。

Before：shared cpp CAE84F1A3F9664E5BA1B7CDB085CE50A917EE05ADC247B07FCDE766EEF314F79，cppm E4E4ED5AA5A1E8FCA5E9FD6B72105A69959D07974EA1BBA3BD9E1F375EA1AE0F，tests5A2855B2FC9A81A35E4989EA3C98A6B359E482110A894BDD67B0C963FAD5381A；全UTF-8 BOM/CRLF。

## 本批最小范围和精确接口

仅#if DRAW3_TESTING：UInkCleanupTestStage六值TempBeforeCreate/TempWriterClosed/BackupBeforeReplace/ReplaceFinished/RecoveryMovesFinished/BeforeCleanup；UInkCleanupTestAction None/ThrowBadAlloc；UInkCleanupTestHook为action(stage,const std::wstring& productionPath,void* scopeContext) noexcept；唯一setter SetUInkCleanupTestHook(hook,context)。完整callbacks不接受caller cleanup路径，安装/调用/reset同同步测试线程。

所有新声明/静态状态/调用完全宏门内；普通编译移除这些blocks后精确恢复before bytes。callback默认null/None无Native行为，有hook时原LastError先锁存、调用后恢复；有限ThrowBadAlloc只真实Replace或两恢复Move成功后允许cpp内部throw，原error已锁存。当前两个actor用None，异常回归待Green扩。

本批先实施Root允许的两个实码RED：生产temp候选已选→CREATE_NEW前FOREIGN sentinel占名，真实创建失败旧guard删foreign；writer真Close→同父把ours Move到held并CREATE_NEW foreign不同ID，真self-read失败旧guard删foreign。actualstage/path/普通disk/NTFS/CreateNew动作/ID、原status与temp诊断/targetmissing/held strictRead均强核；预期唯独foreign保留断言红。补四precommit ours write/flush/selfvalidation/commit failure的实际cleanup基线应绿（用原faults，不复制cleanup）。

same-ID修改、backup/source/newRecovery、Partial选foreign/unreadablebackup、postmutation bad_alloc、ACL/缺NTFS/pin内容能力保留尚未本批实现/运行，明确欠账；Green阶段扩，不冒已有C10或F067 PASS。原所有存储断言不弱化。

## F069 RED候选冻结（PATCH_READY_F069_RED_A）

已实施test-only六阶段接口和Root允许的首批两actor+四precommit基线。三个source所有新增均在DRAW3_TESTING内；原guard仍默认active、原四path DeleteFileW与Partial/异常旧行为保留，没有提前写pin/同HANDLE绿色清理。F067候选短名和230存储/strict回归逐字保持。

### 实際seam/default语义

cppm只有必要stage/action/callback类型和一setter，正式FaultInjection/status/API不动。cpp static hook/context只在测试宏存在，安装/调用/复位同同步scope；Notify默认null立即None，无原Get/SetLastError或其它Native行为。安装hook后原Win32 LastError先锁存，callback noexcept返回后恢复，callback内异常被测试自己catch并记actor前提失败。

TempBeforeCreate在production WriteNewFile真正CREATE_NEW之前（旧guard已建/已选实际candidate）；TempWriterClosed在真实WriteNewFile成功返回后/原strict selfread之前，原writer RAII已Close。BackupBeforeReplace/ReplaceFinished/RecoveryMovesFinished/BeforeCleanup旁挂原调用点，BeforeCleanup覆盖旧四delete站点。ThrowBadAlloc只在真实Replace成功或两个recovery Move成功后由cpp内部执行，callback不能throw；其它stage只None动作。当前两actor始终None，postmutation/Partial/ACL等完整验收待下一批，不以接口已存在冒覆盖。

### 本批真实NoGUI路径/预期RED

新TestCleanupOwnership从repo cwd既有release-hardening核drive/全部component非reparse目录lease，再CreateNew独立`uink-cleanup-<actualGUID>`根及六固定case子目录，全部句柄到同步Save/断言后释放，无actor线程/detach。stdout记private_root_token、mode、actualstage计数、NTFS/动作/失败、原status/error；目录/data保留，不枚举删除。所有path由production Save回调提供，必须startswith当前私有case prefix；未提供caller arbitrary cleanup路径，callback设置/IO/NTFS前提失败不计有效RED。

| case | 实际动作/原返回 | 首批检查 |
| --- | --- | --- |
| precreate-foreign | 真实callback CREATE_NEW+Write/Flush FOREIGN3bytes占actualtemp，原CREATE_NEW应失败IoError/error80(temp)，writerClosed0，旧guard会按name删除 | 真实stage/NTFS/actor成功、targetmissing、unknown保持都应绿；唯line2432 sameForeign+完整foreignBytes保留应RED |
| temp-replaced | 原writer真Close后，test metadata pin作ID对照，真实Move ours→held-original，再同name CREATE_NEW FOREIGN不同ID；原selfread应SelfValidationFailed/error0 | writerClosed1、same-object orig pin/actual foreign不同、held original strict Complete/sourceRevision/GUID应绿；同line2432 foreign保留应RED |
| ours-write | 原failWriteAfterBytes1，实际一字节后errorWRITE_FAULT，原guard清ours | originalstatus/error、actualNTFS/stage、target缺失、ours temp缺失/unknown保持应绿 |
| ours-flush | 原failFlush，errorWRITE_FAULT | 同实际已创建的清理基线应绿 |
| ours-selfvalidate | 真created/write/strictread后原failSelfValidation，SelfValidationFailed0 | ours清理基线绿，不称Partial |
| ours-commit | 原CreateNew模式failCommit，仅固定模拟原Move失败IoError/ACCESS_DENIED5 | 原status/targetmissing/ours清理基线绿，不冒真实OS失败 |

foreign identity从callback原CREATE_NEW HANDLE实际取；temp-replaced的原metadata pin持到此case结束，读held原UInk证明它确是production内容。只在本轮新NTFS普通单link对象动作；不是生产pin已实现。未创建的Green未来不应尝试cleanup，因此precreate只要求beforeCleanup<=1，不把旧active guard的一事件当绿色语义；其余已创建场景应实际经过cleanup。foreign保留失败是明确安全断言，不将Save失败本身当安全PASS。

所有旧FullFileSave/Append/F067断言不弱化。Root outside exact --uink-file-only应看到这两actor各一次line2432保留失败，全部前提/四own基线/旧suite绿才算本批RED_VALID；任何其它错误（NTFS/query/路径/动作/参数/build/runtime）必须分类失败，不冒产品RED。

### 明确欠账

本批尚未实施same-ID改内容、backup/source/newRecovery actor、Partial选foreign/unreadable backup仍保selfvalidatedtemp、postmutation有限bad_alloc实际case、DELETE/Disposition DACL拒绝与非NTFS/metadata/hash资格失败retain。六stage/action存在不替这些动态证据。Root已允许首两个RED先冻结，Green阶段扩；合并合同B320+7ABF+49C仍必须最终完整满足，不把主Debug/core4/C10旧cleanup baseline或F067实码GREEN转为F069通过。

### 静态核对和身份

去除cpp/cppm所有新DRAW3_TESTING blocks后精确恢复CAE84/E4E4；tests去除新宏block/注册及该block边界新空行后精确恢复5A285。原四DeleteFileW、三个MoveFileExW、一个ReplaceFileW、读/写/flush与源revision条件的原文本/count保持。BOM/CRLF、zero loneLF/尾随空白、词法balance、closed6stage/2actions、同期callback/context/回调catch/私有paths、两actor和4baselines已静态核。一次自查脚本猜DeleteFileW数量为9导致assert1，实际原source是4；改为与macro-off前像API count比较后检查0，未因此改源码。

| source | after SHA-256 | Bytes / BOM / CRLF |
| --- | --- | --- |
| shared uink_file.cpp | C580FBCD5AFE1CA067214539E118AFE26E34734AF0B32843AB402659DCF47C83 | 59371 / BOM / 1640 |
| shared uink_file.cppm | D59550F2679D4343A2EFFD719C6298B6807D2744430C815F9F6D341B34AFB136 | 4662 / BOM / 175 |
| uink_tests.cpp | 9C830EF95854F50BEEF4DBD038C556CB8930769F8E6A83F7C37587FF92532566 | 145495 / BOM / 3102 |

本报告UTF-8无BOM/LF；未Git/build/run/GUI/ComputerUse/递归或新selector/工程。当前不是编译PASS/实际RED/测试safety许可。Root需独立actualdiff/有限actor安全核当前hash，再停全writer串行existing standalone ARM64测试target Build0→outside已NoGUI exact --uink-file-only，记录两精准foreign保留红与所有前提、原错误/bytes/paths。原production macro-off内容证据不替新主Solution Build；Green pin/Retain strategy未开始。所有三源和本报告停止写，PATCH_READY_F069_RED_A。

补充冻结：两个actor另强核实际temp diagnostic code/fieldPath/systemError（precreate WriteFailed/temp80，replace SelfValidationFailed/temp0），不放宽原status；foreign唯一RED位置因4行新增诊断核移至2432。最终tests9C830E…532566宏off仍精确匹配5A285 before，原F067和全部旧test/assert逐字不变。实际动态结果仍未运行。
