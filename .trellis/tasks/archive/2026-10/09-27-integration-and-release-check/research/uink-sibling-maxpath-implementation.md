# UInk sibling MAX_PATH 回归实施

Active task: .trellis/tasks/09-27-integration-and-release-check。2026-10-01。

## 当前授权和读入

Root指定本agent为唯一tests RED writer；只可写 inkStrokeModelerTestTests/uink_tests.cpp 与本新报告。shared uink_file.cpp、entry/contact_input_tests/工程/Main/Host/spec/父账本不写，无build/run/GUI/Git或递归。设计2AB3FD4E…F87DD99与独立设计复审0366088AC710B0CD796694FFB9FCC1EE4C9CAC605A3F93EC6B016546BCC5A57B已APPROVE；已读根AGENTS、当前task implement/check及G commit/security上下文、相关native模块/质量/资源指南和实际测试/模块/入口。

Root真实ordinary229/271/extended271/missing-parent boundary已落盘FD7E4F7A…8944762；当前合法PPT230→内部tmp271与原Win32 error3因果已按设计确认，仍不承诺任意最终长path支持。设计列既存CREATE_NEW前DeletePathOnExit归属竞态，当前测试不伪称覆盖随机GUID collision/race。

## Before identity

- uink_tests.cpp：FFDEDD5BC9BEA5336A71C1D12FB49BEF9391999C4C3FF72DF75D357CB2261ADC（UTF-8 BOM/CRLF）。
- shared uink_file.cpp：0E516BFB09D5CCCA8C11A730CD52763A8DAA38F51C0F6B6E0F946936AB3E6348；本RED不改。
- shared cppm：E4E4ED5AA5A1E8FCA5E9FD6B72105A69959D07974EA1BBA3BD9E1F375EA1AE0F；本RED不改。

## 最小测试计划（实施前）

1. 在现uink_tests.cpp追加独立TestSiblingMaxPathSave，RunUInkTests在TestFullFileSave之后实际调用。复用MakeBasicDocument/MakeInk、ReadBytes/WriteBytes、FaultScope和真实SaveUInkFile/ReadUInkFile/CreateUInkEditingSession，不复制事务算法、不预造正确目标。
2. new private repo/TestResults/release-hardening/root GUID目录保留全部新data，核既有路径组件/drive nonreparse目录lease，原目录和用户数据不读改。建立精确151 WCHAR父目录（前缀含分隔符152）及两个GUID+下划线+.uink的78字符末叶，最终普通绝对230、旧tmp271、新同父GUID-only tmp/bak192/recovery201。中文/空格在new私有父目录，当前cwd须repo；前提不足直接FAIL，不用fallback路径凑成功。
3. 第一真实CreateNewLogicalFileWithIdentity应Committed；打印actualstatus/systemError/WriteFailed diagnostic/target及各长度。旧helper预期WriteFailed/error3、target未出现，Committed断言RED；首存失败后明确SKIP update/recovery，其余不冒覆盖。
4. 首存成功后strict Read Complete/sourceRevision与saved.revision相等；真实ApplicationOwned session新增ink，SaveExisting→fresh strictRead同file/page GUID、内容/颜色/点及真实新revision/SHA。最终验证故障沿旧fault要求PartialCommitRequiresRecovery，实际返回same-parent backup严格可读、旧bytes和sourceRevision内容保持；独立未知文件在整个test保持，不按后缀扫删。

## 实际入口/构建边界

uink_tests已在tests vcxproj登记，RunUInkTests已由default main调用；default main还运行ThinStrokeGpu等，不能称整个默认目标NoGUI。Root已确认并唯一修改contact_input_tests.cpp新增两行 --uink-file-only→RunUInkTests()==0?0:1；此前不是既有CLI，本agent不代Root写入口。RunUInkTests本身只in-memory/文件持久化，不CreateWindow/ShowWindow/RTS/GPU。

现实际solution是根inkStrokeModelerTest.sln，包含demo和tests，tests OutDir=$(SolutionDir)$(Platform)/$(Configuration)，C++20/module dependencies。Root需用VS ARM64 MSBuild在同PS调用规范PATH/MSBUILDDISABLENODEREUSE，Debug|ARM64完整solution、超时≥5min，随后从repo cwd outside新compiled测试EXE --uink-file-only取得精确RED。主Inkeys需要另完整InkeysRepo.sln，不能用这个testbuild替代。当前本agent不运行，待PATCH_READY/Root独占串行槽。

## RED-only 实码冻结（PATCH_READY_UINK_RED）

实际只改uink_tests.cpp，新增private `TestSiblingMaxPathSave:2088`及`RunUInkTests:2884`一次调用，共158行新function+1调用；旧任一测试helper/断言/生产算法没有修改。移除新增function与调用后，字节精确恢复原FFDEDD5B…2261ADC。

### 实际前提与运行判据

- 仅从repo cwd已存在的TestResults/release-hardening解析普通绝对drive路径，核drive及全部component实际handle为disk/directory/nonreparse，以FILE_SHARE_READ保deny-write/delete lease；新root只CreateDirectory成功后使用，不接受already-existing。新父目录含中文/空格，151 WCHAR，最终末叶两个实际GUID36及下划线/.uink78，target230。原tmp271、新tmp/bak192及recovery201长度在运行stdout记录；最后三值是设计后的推导，不冒actual helper观测。
- 私有目录不交给旧TempDirectory析构，保全部新证据和unknown sentinel，不枚举删除/复制任何旧文件。目录lease到function返回才Close；任何前提不足CHECK失败/return，不换短目录。没有GUI/输入/外部配置/Office/系统设置。
- 核心RED精确位置`2175 UINK_CHECK(state, firstSave.status == UInkSaveStatus::Committed)`，此前直接调用原SaveUInkFile(CreateNewLogicalFileWithIdentity)生成真实UInk，没有CopyFixture/WriteBytes预造目标。旧源码应实际IoError5枚举值/system_error3，诊断WriteFailed/temp3，target仍不存在；对应条件另CHECK而非把失败当PASS。stdout明确`SKIPPED update/recovery: first save failed.`，这些后续在RED不计覆盖。
- 首存为真Committed后严格Read Complete/document/sourceRevision，sourceRevision==saved.revision，同fileGUID与single canvas；由此生产CreateUInkEditingSession(ApplicationOwned)增加真实ink并SaveExisting。再次strictRead验证同file/page GUID、2内容、style/points、sourceRevision与返回值相等、实际SHA和revision变更。更新失败显式skip recovery，不伪称覆盖。
- 最终验证故障用现FaultScope/failCommittedRevisionValidation，实际SaveExisting返回PartialCommitRequiresRecovery；只读生产返回opaque recoveryPath，核同私有parent、<MAX、strict可读、完整predecessor字节及原SHA、fileGUID/2内容。unknown bytes在first-failure及Green终态均核保持；并不声称覆盖review列GUID碰撞/PathExists→CREATE_NEW清理竞态。

### 静态检查/源身份

- uink_tests.cpp after SHA-256 `686E3436159EACB3078E19692091406FF4F85D90B4621C421A54B715D2CE99DB`，132492 bytes、UTF-8 BOM、2895 CRLF、零loneLF/尾随空白。词法balance、实际3次Save/3次Read、shape guard/skip、CWD上的230/271/192/201计算和无删除/复制/GUI新增已静态核。
- Shared uink_file.cpp仍`0E516BFB09D5CCCA8C11A730CD52763A8DAA38F51C0F6B6E0F946936AB3E6348`；cppm仍`E4E4ED5AA5A1E8FCA5E9FD6B72105A69959D07974EA1BBA3BD9E1F375EA1AE0F`。没有先修helper再伪造RED。
- Root所有contact_input_tests/CLI改动由Root独占，本agent仅核默认入口含GPU并建议纯RunUInkTests路径；未写entry/工程。新版selector不是原来已有，运行应等Root注册并冻结候选。
- 一次只读代码列印超出文件末行造成Python IndexError，已经读取至真实wmain末行3065；属于静态输出脚本边界，未跑EXE、不作为Build/产品错误。

本报告新文件UTF-8无BOM/LF，旧cpp格式保持。agent未Git/build/run/GUI/ComputerUse/递归；未称新源码通过编译或已取得动态RED。Root下一步：独立actual test/safety核新函数只触新私有data；全部writer冻后按已存在inkStrokeModelerTest.sln Debug|ARM64/ARM64 MSBuild与PATH规范、≥5min，outside repo cwd运行新`ARM64/Debug/inkStrokeModelerTestTests.exe --uink-file-only`，保存exact asserted RED和先后skip，不能默认目标夹带GPU。实际Root结果出来后方可WRITE_ALLOWED_GREEN到shared helper，旧RED及data全部保留；C10未缩目录复验、Release3arch/Win7/UNC/collision/restart/可见恢复仍另门。

停止写uink_tests.cpp与本报告，PATCH_READY_UINK_RED。

## 实际RED与GREEN写入前冻结

Root经独立475240AAB3BC0EF23DDB16A3B49DAA13B33CB37572C315D4E93835D8AB699FB1 CLEAR_RED_TEST后，原生ARM64 MSBuild既有inkStrokeModelerTest.sln /t:inkStrokeModelerTestTests Debug|ARM64实际0。outside exact --uink-file-only pid23824自然1，EXE SHADE38C9BE35E557DD29DA534E28F69BFFA3067EAF9D23B719D3ACCB79D01D8AB2；已读uink-sibling-maxpath-red-debug-arm64-test status/stdout/stderr。

实际仅line2175 firstSave.status==Committed一FAIL；first_save_status5/system_error3、target230/parent_prefix152/oldtmp271/new192/192/201，其余旧测试和前提无失败，update/recovery明确SKIPPED。此为真实有效RED，原source/数据/日志保留；不是声称这些后续在RED已覆盖。

Root现WRITE_ALLOWED_UINK_GREEN仅shared uink_file.cpp::UniqueSiblingPath、必要本次新增uink_tests及本报告。拟从已规范化target最后\\或/处取得含分隔符原parent，npos明确nullopt；候选改parent+GUID36+原suffix，32次/BCrypt RNG/PathExists/CREATE_NEW/所有3caller和原flags/锁/事务/cleanup/recoveryPath不改。保持drive root、UNC和已namespace前缀，不重规范路径或加入extended路径、不承诺任意最终长path。

本次before helper0E516BFB…E6348、tests686E3436…2CE99DB/cppmE4E4ED5A…A1AE0F。Root RED stdout里原wcout在中文父名处截断，必要测试诊断将改仅这一个full-target输出为明确UTF-8字节，以便保留完整private目标；不改首存/Read/update/recovery/unknown任何断言或长度。既存CREATE_NEW前cleanup身份race另单元，不纳本命名修补或宣称已修。

## GREEN候选最小实码冻结（PATCH_READY_UINK_GREEN）

Root授权后唯一生产源码改动是shared `UniqueSiblingPath:473`：新增4行parent提取/comment/无separator早退，将1行candidate组装从完整target重复末名改为parent+wideToken+原suffix。从已NormalizePath的target最后反斜杠/正斜杠取包括分隔符的原父前缀，保大小写/drive/UNC/namespace；没有重新规范、CWD/temp目录兜底或std::filesystem/新公开API。独立GUID生成、32次PathExists碰撞检查及所有CREATE_NEW/锁/Win7 API/flags原样保留。

### 三个真实caller和opaque recovery

| caller | 路径/现语义 | 本次处理 |
| --- | --- | --- |
| SaveUInkFile:925 | .tmp，原WriteNewFile→durable→strict self-read/SHA→atomic create/update | 只有候选末叶变GUID36+.tmp；同父同卷原flags/reads/return不改 |
| SaveUInkFile:1016 | .bak，原ReplaceFile predecessor比对与保留/清理 | 同父GUID36+.bak，实际返回路径仍由recoveryPath直接携带 |
| SaveUInkFile:1053 | .new-recovery，原predecessor不符时移出新目标/恢复旧点 | 同父GUID36+原13字符suffix，所有恢复分支和条件逐字原样 |

recoveryPath消费者沿生产返回的真实完整路径读取，测试从该locator验证predecessor bytes/SHA/document，不猜最终basename。当前FileSave所有原DeletePathOnExit/cleanup/error/status未改，尤其设计列CREATE_NEW前清理归属race未纳本单元，不能宣称其安全身份已解决。

### 边界与收益的准确范围

静态输入例覆盖 `C:\file`（parent=`C:\`）、`C:/dir/file`（保斜杠）、含空格父、`\\server\share\file`（保share）、已 `\\?\C:\dir` 和 `\\?\UNC\server\share` 前缀；无separator明确nullopt，不跨目录。这里只静态检查prefix，没有向盘根/UNC写入或把例子说成真Win7测试。

本RED相同input target230/prefix152，新.tmp/.bak实际候选长度192，最长.new-recovery201；最终path230与indices/schema/File GUID/sourceIdentity/命名mutex键不改。最长leaf49若parent prefix210总259，prefix211总260仍会受原普通API边界影响，不truncateGUID/父前缀、不承诺所有合法最终230输入或任意>260目标都可保存。没有改manifest/registry/最低Win7，也没有缩C10目录。

### 必要测试日志修补

Root真实RED stdout证明原wcout在中文parent处截断，当前只替换一个full-target输出为实际WideCharToMultiByte(CP_UTF8)及std::cout字节；新targetPrinted前提失败仍CHECK/return。首存/Read/update/recovery/unknown、root创建/lease/230路径及故障条件完全保留。去除此日志段精确恢复RED测试686E3436…2CE99DB，不以打印修补制造存储GREEN。

### 源身份、静态检查和后续验证

| 文件 | before SHA-256 | after SHA-256 | bytes/BOM/CRLF |
| --- | --- | --- | --- |
| shared uink_file.cpp | 0E516BFB09D5CCCA8C11A730CD52763A8DAA38F51C0F6B6E0F946936AB3E6348 | CAE84F1A3F9664E5BA1B7CDB085CE50A917EE05ADC247B07FCDE766EEF314F79 | 57086 / BOM / 1587 |
| uink_tests.cpp | 686E3436159EACB3078E19692091406FF4F85D90B4621C421A54B715D2CE99DB | 5A2855B2FC9A81A35E4989EA3C98A6B359E482110A894BDD67B0C963FAD5381A | 133075 / BOM / 2902 |

CPP原UTF-8 BOM/CRLF、无loneLF/尾随空白；生产去除4行+恢复单candidate行精确恢复0E516BFB原hash，测试去除仅UTF8输出段恢复686E3436原hash，说明3caller/所有其他分支和所有旧断言没有漂移。cppm仍E4E4ED5A…A1AE0F，未写entry/Main/工程/Host/AutoSave/Helper/UI/Root文档/spec。源码词法/前缀例和长度/static caller数量检查，无新runtime测试或强制GUID collision hook。

Root下一门：新standalone既有solution测试target实际Build0→outside exact --uink-file-only，预期首存真实Committed、strict Read/sourceRevision、真实ApplicationOwned Update与predecessor opaque恢复/unknown全部通过，任一失败仍FAIL。原RED日志/data不删，新PE/源另冻结。等主source全部freeze后完整InkeysRepo.sln Debug|ARM64及适用core tests，再C10 unchanged230终路径release/hold三轮/same+foreign strict fresh，按实际独立code/safety审查；不能仅test选择器0代主产品结果。

本agent未Git/build/run/GUI/ComputerUse/递归。当前生产GREEN实码尚未编译/动态核；原32碰撞API未做强制GUID碰撞、predecessor不符new-recovery路径未新增故障演示、UNC/drive-root真实I/O/Release3arch/Win7/RTS quiescence/visible恢复按原门未验证。Root实际RED已确认，下一GREEN不提前PASS。生产源、必要测试与本报告停止写入，PATCH_READY_UINK_GREEN。
