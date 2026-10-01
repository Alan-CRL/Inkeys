# UInk sibling MAX_PATH 独立设计复审

Active task: `.trellis/tasks/09-27-integration-and-release-check`

日期：2026-10-01。范围：只读真实 shared UInk 保存/恢复源码、生产调用者、已有测试与 Root 取证，只写本报告。未改源码、工程、spec、账本或其他报告，未 build/run/GUI/Git，未递归派发。

## 判决

**APPROVE / STATIC_DESIGN_GREEN：采用 choice A，仅将 UniqueSiblingPath 候选改为原同父目录内的 GUID36 + 原 suffix 短叶名。** 没有发现消费者依赖旧 target-basename 拼接合同。此判决允许 Root 指定唯一 writer 实施下述窄范围；不表示实现已完成、编译通过或任何 C10 动态 PASS，不增加 GUI 运行授权。

既存按路径清理的并发身份边界见后文；本命名修复不能被记为该边界已解决。

## 精确审查输入

| 输入 | SHA256 |
| --- | --- |
| research/uink-sibling-maxpath-design.md | `2AB3FD4E67D5008C800F1F7E493460DC3DEDEC909E373238E10CDDE48F87DD99` |
| inkStrokeModelerTest/draw3/uink_file.cpp | `0E516BFB09D5CCCA8C11A730CD52763A8DAA38F51C0F6B6E0F946936AB3E6348` |
| inkStrokeModelerTest/draw3/uink_file.cppm | `E4E4ED5AA5A1E8FCA5E9FD6B72105A69959D07974EA1BBA3BD9E1F375EA1AE0F` |
| inkStrokeModelerTestTests/uink_tests.cpp | `FFDEDD5BC9BEA5336A71C1D12FB49BEF9391999C4C3FF72DF75D357CB2261ADC` |
| TestResults/release-hardening/uink-path-boundary-8a3a5b885c174579bf0654cc975944a2/result.json | `FD7E4F7A7C036C87058D467A41854C04F04F7F3C0EEC5AA07263C6B608944762` |

## 真实证据与原因范围

Root 的上述 JSON 记录同一 pwsh host：ordinary229 父存在且成功；ordinary271 父存在但 error3；extended271（API 字符数275）成功；missing-parent229 也 error3。本 reviewer 读取证据，未执行该实验。它支持本轮普通路径长度边界的归因，不能把 error3 单独当作长路径证明。首个未进入 API 的 UInt32 签名错误、旧无效 2d8da JSON 不计有效证据。

生产 `Inkeys/Inkeys.vcxproj:997–998` 直接编译此 shared cpp/cppm。实际 `UniqueSiblingPath:473–485` 将完整 target 再追加 `.`、GUID36 和 `.tmp`，因此230字符最终路径变成271字符内部路径；`WriteNewFile:491–493` 使用原 GENERIC_READ|GENERIC_WRITE、share0、CREATE_NEW、NORMAL|SEQUENTIAL_SCAN。

`Draw3.PresentationAutoSave.cpp:465–492` 的真实 `SaveVersionedUInk` 生成 `files/<fileGuid36>_<transactionGuid36>.uink`，末叶78字符。该已报告最终230案例的父前缀为152；choice A 的 `.tmp/.bak` 完整路径192、`.new-recovery` 为201。这里只推导这个输入的内部长度，尚未实测修补后的保存。

## 调用链与命名兼容

| 真实站点 | 路径用途与判定 |
| --- | --- |
| SaveUInkFile:858、921、928–954 | target 先由 NormalizePath/GetFullPathNameW 得到绝对路径；生成 temp，按实际路径写入、严格自读和校验 SHA/长度/GUID/object count；不从叶名推导身份。 |
| SaveUInkFile:965–991 | create-new 发布通过同目录 MoveFileExW；目标存在返回 SourceChanged，未新增覆盖。 |
| SaveUInkFile:995–1068 | update 对真实 target/sourceRevision 再读比对；backup 实际路径直接传 ReplaceFileW；predecessor 比对失败时 `.new-recovery` 实际路径直接用于移出新目标及恢复旧目标。 |
| SaveUInkFile:1071–1103 | 最终目标 SHA/长度复核，必要时返回保留的 backup 或 target；成功清 backup 失败仍给真实 recoveryPath 和 warning。 |
| uink_file.cppm::UInkSaveResult:64–70、现测试:1862–1864/1879 | recoveryPath 是完整实际路径；测试读取返回路径，检查 predecessor 字节或 create-new 最终路径，没有旧 basename 猜测。 |
| Desktop AutoSave.cpp:769–779；PPT PresentationAutoSave.cpp:478–492 | 消费 status、revision 和各自预定最终路径；未解析 UInk 临时/备份名称。两者 index.json(.bak) 自有事务不属于本 helper，不能顺带改动。 |

全仓源码搜索未发现对 `.uink.<GUID>.tmp/.bak/.new-recovery` 进行重建或按该模式扫描的产品消费者。更改内部叶名不改变最终 UInk 名称、索引相对 locator、文件内 GUID、media 资源规则或 named mutex 的 target key。

## 实施冻结条件

1. **只替换 helper 的候选组装。** 从已规范化 target 的最后一个路径分隔符截取包括分隔符的原父前缀，再拼 GUID36+suffix；保留原大小写和 namespace，不重新规范化，不用 CWD、全局 temp 目录或 target 的 drive 名替代父目录。保留 `C:\`、`\\server\share\` 及调用者已传入的 namespace 前缀；同时处理反斜杠与正斜杠分隔符。没有可用父前缀时应显式失败，不能跨目录兜底。不要新增 std::filesystem 异常路径或公开 API。
2. 保留 `CreateUInkGuid` 的既有 BCrypt RNG、version/variant、FormatUInkGuid36 格式、32次 PathExists 冲突候选和调用者 CREATE_NEW。不把随机名写入 UInk 身份 metadata，不复用固定 basename。GUID-only 使同父不同 target 共用候选命名空间；已有文件必须仍被跳过，不能为了碰撞成功而删除或覆盖。
3. 原 temp/backup/new-recovery 同目录事务和所有 flags、读/写 API 次数、access/retry、锁、SHA/sourceRevision、status、recoveryPath 与 recovery/cleanup分支原样保留。不修改 target NormalizePath/比较键，不缩夹具路径，不升级 manifest、registry、API 或最低 Win7。
4. 长度收益是消除重复末名。它不保证每个合法230/接近260的最终路径都可保存：若父前缀本身太长，GUID36+最长suffix13仍可超限；验收输入必须记录完整 target、父前缀和三种 sibling 实际长度。最终 >260 路径支持不在本设计内。
5. 删除与保留仍只针对事务记录的确切路径，不能引入按 `.tmp/.bak` 后缀枚举清理、启动回收或未知 UInk 删除。已有 recovery 材料必须通过返回路径可读；不要要求恢复叶名包含原 target 名。

## Findings (fixed)

无。本次仅设计复审，没有实施或自修。

## Findings (not fixed)

- **既存 CREATE_NEW 前的 cleanup 归属竞态，未由命名设计解决。** `SaveUInkFile:928` 在 `WriteNewFile` 成功创建前就构造 `DeletePathOnExit`；其析构 `:99–101` 无条件按该路径 DeleteFileW。若 `PathExists` 检查后另一个 actor 占用同候选，CREATE_NEW 失败后仍会进入该清理。`UniqueSiblingPath/PathExists` 不等于 file identity/所有权证明。旧 target+GUID 方案已存在此分支；choice A 不新增清理算法，但也不能宣称所有并发未知文件永不被删。此项单列交 Root 判断另行窄修/验收，不扩大当前命名 writer 范围，不伪装动态复现。
- **碰撞、路径 namespace 与新回归尚未验证。** 当前没有可强制指定 sibling GUID 的测试注入接口；既有目标重存 SourceChanged 测试不等于确定性候选碰撞覆盖。不要为了本命名修复扩公开 DTO/生产故障注入。drive root/UNC 的纯父前缀构造可以静态验证；真实写入只能在获得授权且可写的私有目录进行，不能向盘根或外部共享写数据以凑覆盖。

## 必要回归与验收门

- 先仅在既有 `uink_tests.cpp` 添加实际 API 回归并保存 RED 证据：普通最终路径精确230、合法末叶与已存在私有父目录、旧 temp 实际271；断言 SaveUInkFile 应 Committed，旧代码实际 WriteFailed/error3，不能预复制“正确”目标掩盖首存失败。
- GREEN 对同一输入实际首存→ReadUInkFile Complete/sourceRevision；由读结果建立真实 ApplicationOwned editing session，再 SaveExisting update→fresh strict Read，核同 GUID、内容与末有效点，以及实际 revision/SHA。首存失败时不能把未进入的 update 记作已覆盖。
- 复用 `TestFullFileSave:1813–1896` 的写/flush/selfvalidation/commit 故障、最终验证失败 predecessor recovery、目标冲突保字节；在长末名 update 至少验证一次最终验证故障返回的短 backup 可读且字节仍为 predecessor。保留多个最终文件、不同父、中文/空格、不受影响的未知文件；不要只检查路径长度或 CreateFile 成功。
- **既有测试注册已存在**：`inkStrokeModelerTestTests.vcxproj:117` 编译 uink_tests.cpp，`contact_input_tests.cpp:3056` 调用 RunUInkTests，后者 `:2725` 调用 TestFullFileSave。不需要凭空新增 CLI。本事实不授权整个测试目标为 NoGUI；Root 仍须核实际入口、配置和运行效果。
- 唯一 writer 冻结修补精确 hash 后，Root 按真实依赖完成完整 InkeysRepo.sln Debug|ARM64 与已有测试配置，再由独立代码复审核此命名增量。旧 Build0/取证不得冒充新 helper 编译通过。
- 实际长路径 PPT C10 release、hold三轮、same/foreign fresh reader分别按现合同/授权门执行，严格保存和恢复 receipt 不降级；纯 Win32 控制实验不替代生产 worker 保存。Desktop runner Error5 另属已分类环境边界，不顺改 flags。Release各架构、Win7 SP1仅KB2670838、可见恢复/自动重启、RTS成功callback静止分别待原矩阵，不由本复审升级。

## 必要 shared spec 同步（由 Root 后续实施）

在现有 `.trellis/spec/native-desktop/draw3-integration.md` 的 Desktop/PPT 文件事务合同补一条共享 SaveUInkFile 规则即可：主产品直接编译 shared helper；内部三个 sibling 同父 GUID-only 短叶、实际路径作为 opaque recovery locator、不能根据最终 basename 猜测恢复名；短内部名称不承诺最终长路径支持，也不提供按后缀清理授权。原最终文件/index schema、最小平台、锁和身份合同不变，不另造新 package/spec。

## Verification

- 静态：已读真实 shared helper 全部三个调用点、rollback/predecessor/final-validation/cleanup分支、生产两类保存 caller、实际测试注册及 Root JSON；本窄设计 APPROVE。
- Lint/TypeCheck/Build：未执行（设计-only dispatch 禁止 build/run）。
- Tests：未执行；仅核 Root 已执行且落盘的路径控制证据，所有新保存/更新/恢复回归待实施后运行。