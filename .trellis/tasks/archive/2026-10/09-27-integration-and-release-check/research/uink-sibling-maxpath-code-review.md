# UInk sibling MAX_PATH RED 测试独立代码与 safety 复审

Active task: `.trellis/tasks/09-27-integration-and-release-check`

日期：2026-10-01。本次唯一写本报告；相关源码/工程/规范/既有报告只读。未修改源码、未 build/run/Git/GUI/递归。已有设计复审 `0366088AC710B0CD796694FFB9FCC1EE4C9CAC605A3F93EC6B016546BCC5A57B` APPROVE 保留，不重做设计；既有H1 GREEN报告保持不动。

## 当前判决

**CLEAR_RED_TEST / SCOPED_STATIC_GREEN。** 允许 Root 在源码冻结、新测试solution实际Build0、指定本repo cwd及当前compiled测试EXE身份前提下，运行精确 `--uink-file-only` 取得有限RED证据。新测试实际进入共享旧生产 helper；测试断言因果、私有新树与纯CLI入口没有本范围待修阻断。

尚未编译或运行本RED候选，不记测试PASS/RED_VALID；只有 Root 后续观察到本报告规定的唯一首存红及全部真实前提成立，才可给实际RED有效结论。生产 GREEN helper 未实施，不能由本报告提前放行。

## 精确当前身份

| 输入 | bytes | SHA256 |
| --- | ---: | --- |
| inkStrokeModelerTestTests/uink_tests.cpp | 132492 | `686E3436159EACB3078E19692091406FF4F85D90B4621C421A54B715D2CE99DB` |
| inkStrokeModelerTestTests/contact_input_tests.cpp | 142500 | `D9CBCC5EAAD903BEFA7999BD871E9D5DF0F6D2050A1FC7D28438E804AE460D48` |
| inkStrokeModelerTest/draw3/uink_file.cpp | 56809 | `0E516BFB09D5CCCA8C11A730CD52763A8DAA38F51C0F6B6E0F946936AB3E6348` |
| inkStrokeModelerTest/draw3/uink_file.cppm | 4094 | `E4E4ED5AA5A1E8FCA5E9FD6B72105A69959D07974EA1BBA3BD9E1F375EA1AE0F` |
| research/uink-sibling-maxpath-implementation.md | — | `18E4D9A8B717C277330B6726D436B627375E3B6FDDE2AA8B46C90AA30D632EC9` |

在内存中只移除新增 `TestSiblingMaxPathSave` 158行和一次 RunUInkTests 注册，完整uink_tests.cpp精确恢复123105bytes、原 `FFDEDD5BC9BEA5336A71C1D12FB49BEF9391999C4C3FF72DF75D357CB2261ADC`。旧helper/测试断言逐字不动。Root entry新增三行的实际文本已核；只在内存移除所得prior hash为 `94F6CE2CD308E0B14396CEFEEC1754351A483413CDA2E8870021D999089D7BF7`（推导值，未将未提供的entry baseline冒作已匹配）。两个cpp当前UTF-8 BOM/CRLF、bareLF0。

## 实际路径与 safety

- **新函数 `uink_tests.cpp:2088–2244`。** `GetFullPathNameW` 只将当前cwd下既有 `TestResults/release-hardening` 解析为普通drive绝对路径；长度/drive前提不满足即Check失败并返回，不使用fallback目录。函数本身没有证明任意cwd为本repo，故本CLEAR明确要求 Root 的 process working directory 是本repo，实际EXE也在本repo既有 `ARM64/Debug`。它不是任意cwd或OS sandbox授权。
- **所有层 lease `:2099–2130`。** 先drive root，再逐层到既有base；实际HANDLE须DISK、directory、nonreparse，使用 FILE_READ_ATTRIBUTES/OPEN_EXISTING/OPEN_REPARSE_POINT|BACKUP_SEMANTICS及FILE_SHARE_READ，拒write/delete共享。每次成功HANDLE进入RAII vector，push异常关闭当前HANDLE后再抛，原HANDLE由析构统一关闭；任何前提失败不会继续向新目标保存。
- **真实create-new私有目录 `:2132–2154`。** GUID root只接受CreateDirectory成功，拒已存在；对新root和含中文/空格的父目录再持同型lease。全部既有祖先和新目录lease保持到函数退出；三次Save/严格读均同步完成后才释放。没有线程、borrowed context或异步I/O被带出本函数。异常同样释放lease；只遗留已经创建的私有证据。
- **长度 `:2145–2163`。** 父路径按wstring精确151 WCHAR，含末分隔符的prefix152；末叶为两次实际FormatUInkGuid36、下划线与`.uink`共78，target230。旧 `.GUID.tmp` 附加41→271；新设计 `.tmp/.bak`40→192、`.new-recovery`49→201。最后三项是推导，stdout没有冒充它们已由当前旧helper创建。
- **未知sentinel `:2164–2166/2183/2242`。** 只在本轮新私有父目录建立固定3bytes的unrelated文件，不预建正确target；旧WriteBytes使用CREATE_ALWAYS，但路径位于本轮create-new私有树。RED返回和完整GREEN终态均核unknown bytes保持。新函数不枚举、不删除、不复用旧TempDirectory；保留本轮新root及全部证据。纯RunUInkTests中的旧测试仍在其自有GUID临时目录按原TempDirectory析构规则清理，不能把整个CLI描述为从不删除文件。
- 保持原设计的既存PathExists→CREATE_NEW前cleanup身份竞态边界；本测试没有强制GUID碰撞或并发异物插入，不宣称它解决所有未知文件身份竞态，也不把目录lease称为OS sandbox。

## RED 因果断言

1. `:2167–2172` 使用原MakeBasicDocument/实际fileGuid与CreateNewLogicalFileWithIdentity，直接调用真实SaveUInkFile；没有CopyFixture/WriteBytes预造正确target。原MakeBasicDocument已在同旧套件使用；新树所有父目录已实际存在并leased，target末叶合法，未知文件写入/Flush成功另有Check。
2. **唯一预期红是 `:2175 firstSave.status == Committed`。** 旧helper `uink_file.cpp:473–483` 实际将target+dot+GUID36+suffix传原WriteNewFile；`:491–497` CREATE_NEW失败当场捕获error，`:929–935` 返回IoError并WriteFailed/temp诊断。本范围预计IoError/systemError3，但尚未实测，不用一个退出1推导原因。
3. 首存失败分支 `:2178–2183` 另外要求IoError且ERROR_PATH_NOT_FOUND、真实WriteFailed且fieldPath=temp且diagnostic error3、target仍缺失、unknown bytes仍一致。ACL/runner错误5、非法模型/路径、不存在父目录、lease不足、其它旧测试失败都不能算此RED。`GetFileAttributes==INVALID`本身不单独证明缺失，必须与私有新树/目录lease、正确结果诊断和完整前提组合读取。
4. `:2184–2185` 首存失败明确打印SKIPPED update/recovery并return；update/backup/recovery未进入，不给覆盖。若旧helper意外成功，应按实际结果调查，不能先记“红已确认”。
5. Root保留stdout/stderr/退出码和当前helper/test/EXE身份；必须看到全部旧断言无新失败且唯一失败表达式为首存Committed。输入/目录/编译/runner前提失败应记分类失败，不能冒作产品RED。

## GREEN 后续路径已静态核，尚未运行

- `:2187–2195` 首存实际Committed后严格ReadUInkFile Complete/document/sourceRevision，核returned revision、file GUID、single canvas/一笔；只从该真实读结果建ApplicationOwned editing session。
- `:2196–2220` 增加MakeInk真实颜色/点，默认SaveExistingLogicalFile update，再fresh strict Read；核同file/page GUID、两笔、returned revision匹配、sourceRevision及SHA实际改变，色和末点保留。MakeInk的points长度固定两项；访问points[1]之前短路要求实际size与该值相等。更新失败显式SKIP recovery。
- `:2221–2242` 先读完整predecessor bytes，再由真实updated read建recovery session。FaultScope只包第三次Save且failCommittedRevisionValidation=true；退出block即Reset原故障。要求PartialCommitRequiresRecovery并实际opaque recoveryPath；同prefix/<MAX、完整旧bytes、strict Complete、旧SHA、fileGUID/两笔均由返回路径真实读校验，不根据旧basename重建备份名字。
- `RunUInkTests:2877/2891` 起止仍ResetUInkFileTestFaultInjection；新函数放在原TestFullFileSave之后、TestAppendTransactions之前。旧FullFileSave自有TempDirectory/FaultScope也沿原析构Reset。新首存无新增注入或预seed，故障不会故意污染RED或后续旧测试。

## 纯CLI入口与实际构建关系

`contact_input_tests.cpp:3010–3012` 新三行是中文注释、`argc==2 && wcscmp(argv[1],L"--uink-file-only")==0` 与立即return RunUInkTests结果归一0/1，位置在默认套件和GPU之前。准确调用不会落入RunThinStrokeGpuTests等默认路径。RunUInkTests实际只有codec/文件/CPU snapshot导出类测试，无CreateWindow/ShowWindow/RTS/GPU调用；新函数也无外部配置、Office、子进程或系统设置写点。多余/错误参数不获得此NoGUI CLEAR；Root只使用精确一个selector，默认测试入口不在本许可内。

根 `inkStrokeModelerTest.sln` 已含demo与tests；tests.vcxproj:117登记uink_tests、:156–157编译相同shared cpp/cppm、:212–214 ProjectReference指demo。既有OutDir为SolutionDir/Platform/Configuration，SubSystem Console，C++20/module dependency扫描；未新增工程配置。Root按此既有完整solution Debug|ARM64、ARM64原生MSBuild、同PowerShell规范PATH/MSBUILDDISABLENODEREUSE、至少5分钟构建后，实际测试路径应是 `ARM64/Debug/inkStrokeModelerTestTests.exe --uink-file-only`。不要默认运行demo或整个无参数套件。

此测试直接使用主Inkeys.vcxproj:997–998也编译的同一shared文件；编译/测试demo solution不能替代主InkeysRepo.sln的新完整Build，后续生产GREEN/caller/C10与fresh reader仍按既有独立门。

## Findings (fixed)

本reviewer没有自修。新测试和Root纯CLI实码符合本窄RED scope；旧断言、故障复位与生产helper hash已保留。

## Findings (not fixed) / 未验证

本次没有must-fix阻断。尚未编译/运行新RED候选；GREEN helper、新更新/恢复路径、候选碰撞/外界并发身份竞态、UNC/drive-root真实写入、主产品C10长路径及三轮fresh reader、Release/Win7/自动重启/可见恢复保持未验证。原设计/H1报告和历史失败数据不被本报告改写。

## Verification

- 静态与safety：有限CLEAR_RED_TEST，条件为上述准确EXE/selector/cwd、新实际Build0及RED因果前提。
- Lint/TypeCheck/Build：未执行（当前dispatch禁止run/build）；仅核源码声明/调用/注册、既有工程依赖、BOM/CRLF及内存精确旧hash恢复。
- Tests：未运行；不预填PASS或RED_VALID。Root完成真实RED后才可授权唯一writer实施GREEN helper，并另独立代码复审。
---

## 2026-10-01 GREEN 命名增量独立复审

本节仅追加，原RED测试scope与历史判据保留。追加前本报告SHA256为 `475240AAB3BC0EF23DDB16A3B49DAA13B33CB37572C315D4E93835D8AB699FB1`。相关源码/日志只读，未自修/build/run/Git/GUI/递归。

### 判决与精确当前源

**SCOPED_STATIC_GREEN；共享 helper 有限首存→strict Read→update→strict Read→opaque predecessor 回归实际 GREEN。** 命名增量没有新must-fix阻断；cleanup归属竞态仍独立未修，不由此结论升级为安全全PASS。主产品完整构建与C10原长fixture尚待独立门。

| 当前文件 | bytes | SHA256 |
| --- | ---: | --- |
| shared uink_file.cpp | 57086 | `CAE84F1A3F9664E5BA1B7CDB085CE50A917EE05ADC247B07FCDE766EEF314F79` |
| shared uink_file.cppm（未改） | 4094 | `E4E4ED5AA5A1E8FCA5E9FD6B72105A69959D07974EA1BBA3BD9E1F375EA1AE0F` |
| uink_tests.cpp | 133075 | `5A2855B2FC9A81A35E4989EA3C98A6B359E482110A894BDD67B0C963FAD5381A` |
| Root contact_input_tests.cpp（未再改） | 142500 | `D9CBCC5EAAD903BEFA7999BD871E9D5DF0F6D2050A1FC7D28438E804AE460D48` |

本reviewer在内存移除UniqueSiblingPath四行新增comment/parent提取/无separator早退，恢复原candidate表达式，完整BOM文件精确恢复原 `0E516BFB09D5CCCA8C11A730CD52763A8DAA38F51C0F6B6E0F946936AB3E6348`。只将测试UTF-8输出段反向还原为RED原wcout，完整测试精确恢复 `686E3436159EACB3078E19692091406FF4F85D90B4621C421A54B715D2CE99DB`。未写该重建结果；证明所有事务/删除/恢复分支及全部原断言字节未变。当前两cpp保留UTF-8 BOM/CRLF、bareLF0。

### 实际 UniqueSiblingPath 与三 caller

- `uink_file.cpp:476–486` 从已经NormalizePath的target取最后 `\\` 或 `/`，含该分隔符保留原parent；无separator返回nullopt。原32次BCrypt GUID候选、FormatUInkGuid36、PathExists保留，candidate仅改为parent+GUID+原suffix。保原namespace与大小写，不重规范路径、跨目录兜底或拼extended前缀。
- `.tmp` `:925` 仍进入原WriteNewFile/CREATE_NEW/share0/durable/strict自读与SHA；`.bak` `:1016` 仍直接传原ReplaceFileW；`.new-recovery` `:1053` 仍用于原移出新目标/恢复predecessor。三者保持同父同卷，最终target/index locator/file GUID未改变。所有Move/Replace flags、namedMutex键、access/retry/status/recoveryPath与原清理完全保持。
- 实际 `recoveryPath:1041/1066/1081–1094/1102` 仍携带真实完整路径，不由消费者复原旧basename。新增测试仅从生产返回的opaque locator strict读predecessor；现生产Desktop/PPT保存caller也没有解析这些内部名称。
- `C:\file`保留`C:\`；`C:/dir/file`和混合separator保留最后分隔符以前的原prefix；UNC `\\server\share\file`不丢share；已传入的`\\?\C:\…`与`\\?\UNC\…`前缀不改。这些是实码字符串边界检查，未称盘根/UNC真实I/O已通过。实际普通target230/prefix152的新temp/backup候选192，new-recovery201；最长49叶名加过长parent仍可触原API边界，不截断GUID/parent，不承诺任意最终230或>260都支持。
- 无manifest/registry/API最低版本/Win7 SP1仅KB2670838变动，无新module依赖/public DTO/通用I/O框架；NormalizePath、sourceChanged、锁和所有事务API均由精确旧hash恢复确认未改。

### 必要UTF-8日志修补与断言

RED真实stdout原wcout在中文父名处截断，ASCII长度/status与stderr唯一失败仍保留。新测试 `:2160–2167` 只在测试scope用两次WideCharToMultiByte(CP_UTF8)输出完整target，编码失败Check/return；没有改路径构造、存储API、正常产品print/clock或任何旧断言。首存Committed断言因七行诊断增量移到 `:2182`，其表达式及RED失败分支、update/strict/fault/recovery/unknown条件完全保持。

私有目录lease与unknown sentinel、同步Save/Read和FaultScope Reset仍按前文。首次成功后实际ApplicationOwned update及最终验证故障返回的短opaque backup严格可读、旧完整bytes/SHA保留；新GUID名称没有混入UInk元数据。测试未预造正确目标、复制正确事务算法或缩C10目录。

### 本轮已直接读取的 Root 动态证据

所有实际执行由Root进行；本reviewer只读 `TestResults/release-hardening/` 文件与当前测试PE。

| 产物 | 实际结果与有效范围 |
| --- | --- |
| uink-sibling-maxpath-red-debug-arm64-build.status.txt | 原既有inkStrokeModelerTest.sln Debug|ARM64、target inkStrokeModelerTestTests，exit0。 |
| 同RED test.status/stdout/stderr；pid23824 | outside native file-I/O、准确--uink-file-only、自然exit1；actual status5/error3，target230/parent152/oldtmp271，stderr精确只line2175 Committed一FAIL；其它输入/WriteFailed(temp)3/unknown前提没有FAIL，update/recovery明确SKIP。原中文目标日志有截断，不隐去该取证缺口。RED PE `DE38C9BE35E557DD29DA534E28F69BFFA3067EAF9D23B719D3ACCB79D01D8AB2`。 |
| uink-sibling-maxpath-green-debug-arm64-build.status.txt | 同既有solution/config/target新Build exit0，不是主InkeysRepo.sln构建。 |
| 同GREEN test.status/stdout/stderr；pid33324 | outside native file-I/O、准确--uink-file-only、自然exit0；完整中文私有target230/prefix152，old271/new192/192/201；actual firstSave status0/error0、update/recovery completed、All UInk persistence tests passed；stderr0bytes。 |
| 当前ARM64/Debug/inkStrokeModelerTestTests.exe | 直接SHA256 `F97A3801B4E463E9EA6A7A3225A759880AF7CAE8C3E12C6BFC845E6379F4DF60`，与GREEN status/Root提供身份一致。 |

因此实际有限GREEN包括真实首存、strict read/sourceRevision、existing update、新strict读与opaque predecessor/unknown保留，并包括同RunUInkTests原套件断言。新-recovery的predecessor不符rollback分支、强制GUID碰撞/cleanup身份、UNC/盘根、mainFullBuild/C10unchanged长fixture、Release三架构/Win7/可见恢复/自动重启仍未由此测试证明。H1及所有历史报告/数据保持原记录。

### Findings / Verification（本增量）

- Fixed by writer / reviewed：内部basename重复导致合法最终路径对应过长temp，及测试中文日志截断；没有本reviewer源码自修。
- Not fixed：既存CREATE_NEW前及其它按路径cleanup归属竞态，详见下述独立附录；它不阻止本命名单元的独立验收，也不被本单元记为已解决。
- 静态PASS于当前命名/测试日志窄增量；Root测试solution Build/有限UInk回归PASS。本reviewer未lint/build/run，主产品及剩余矩阵继续待原门。

### 极小独立附录：cleanup归属竞态的后续正式修补建议

此附录仅交Root冻结另一个缺陷/回归单元，不修改任何源，不扩大本次命名判决。

1. **确切既存站点与actor能力。** `DeletePathOnExit:95–107` 默认active；Save在 `:932` 先构造guard，`:933`才CREATE_NEW。能写该目录的另一actor可在PathExists返回后占用候选，CreateFile失败后本guard仍按路径删除它。另三个直接清理点为失败Replace后的backup `:1036`、成功rollback后new-recovery `:1058`、最终复核后的committedBackup `:1099`；路径存在/一次内容验证不等于清理时仍是同一对象。权限/目录可信程度由实际调用者决定；本模块并未要求所有生产autosave root都是fixture私有树。
2. **先闭合最小preCreate分支。** guard默认未armed；只由私有WriteNewFile在真实CREATE_NEW成功后反馈created（必要时同时反馈从创建HANDLE获取的身份）才arm。CreateFile失败时永不清理该候选；部分write/flush失败仍须清自己的创建物。这个created bool只解决“未创建却清理”，不能独自证明后续同名替换、三个其它删除点或文件ID重用安全。
3. **正式身份删除模式已有本仓证据。** `Ui3PresentationFixtureAuth.cpp:625–630` 在CREATE_NEW HANDLE取volume/fileIndex；`:681–698 LinkOwnedEmptyFile` 对新私有empty文件再开FILE_READ_ATTRIBUTES|DELETE、OPEN_REPARSE_POINT、拒directory/reparse并匹配身份，最后在同一HANDLE上SetFileInformationByHandle(FileDispositionInfo)，没有compare后再DeleteFile(path)的换名窗口。可在UInk匿名private范围复用这个Win7能力/模式；不要直接调用Auth私有函数或引入Fixture依赖/新全仓框架。该Auth例有private-tree/empty文件前提，不是任意生产路径的现成完整所有权证明。
4. **各artifact必须有独立真实归属。** temp来自本次成功create；backup应绑定真实predecessor/成功替换及原strong revision；new-recovery应绑定真实成功move出去的对象。失败Replace不能只因backup存在就删；获取/验证身份失败、reparse/对象不一致、DELETE权限不足时保留artifact与原恢复/诊断语义，不降权删路径、不提权、不删除新target或未知数据。同HANDLE删除前的归属凭据/存活期还需审；单独记录fileIndex后关闭再重开不是对无限对象重用的证明，保活方式也必须验证不破坏原share0/strictRead/ReplaceFile。
5. **下一单元取证门。** 只在独立私有root中稳定安排candidate被异物占用，先真实红“CREATE_NEW失败后sentinel被删”再绿不删；另核自己创建后的write/flush/self-validation/commit失败可正确清理，以及三种cleanup前替换/拒绝权限时保留原数据与recovery。不得扩公开fault DTO或用户目录权限，不用概率GUID碰撞当可重复证据。Root先冻结实际缺陷/权限/最小private seam，再授权唯一writer与独立审查/运行；当前附录不预授权源修或记这些结果PASS。