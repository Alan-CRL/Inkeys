# H 字段实时绑定与更新请求元数据

## 目标

升级 Other.Config 的 H 字段，使 schema 可以声明固定值或实时变量来源，并独立控制文件输出与更新 Referer 输出；通过同一机制上报设备标识及最新更新通道。

## 需求

- R1：H 支持固定值和变量地址来源。固定值保持声明值；绑定变量在每次文件输出或 Referer 输出时读取最新值，不依赖 ReadAll/Reset 刷新缓存。
- R2：H 独立声明是否写入文件、是否写入 Referer。保留 H 不从磁盘回读覆盖运行时信息的语义；X 字段的读取、默认值和持久化行为不变。
- R3：EditionVersion 继续写入现有 Info.EditionVersion，但不输出 ev；ed、pa、we 继续写文件和上报；TargetArchitecture 继续只写文件。
- R4：UserId 继续以原格式写文件；Referer 使用 id，并仅从发送副本中去掉花括号，保留全部 GUID 字符和连字符。上报不得含原始花括号或其百分号编码。
- R5：当前更新通道在 H schema 中声明为 ch，同时写入新配置文件和 Referer；事实源仍属于 setlist.UpdateChannel，新配置文件保存实时信息副本，deploy.json 原有保存继续保留。沿用其锁协议读取最新值。
- R6：更新检查与安装包下载均显式使用 User-Agent: Inkeys-Updater/3.0。Referer 保留现有 URL 编码和分号分隔格式。
- R7：默认值处理、ReadAll/ReadMini、ResetToDefaults 和 Config 复制不得通过实时绑定写入外部变量；写文件开关不得无意影响 Config 内部复制。

## 字段输出合同

| H 字段 | 写入文件 | 写入 Referer | 上报键 |
| --- | --- | --- | --- |
| UserId | 是，保留花括号 | 是，去花括号 | id |
| EditionVersion | 是 | 否 | — |
| EditionDate | 是 | 是 | ed |
| ProgramArchitecture | 是 | 是 | pa |
| TargetArchitecture | 是 | 否 | — |
| WindowsEdition | 是 | 是 | we |
| UpdateChannel | 是，保存当前通道副本 | 是 | ch |

## 已确认事实

- Other.Config.cppm:348-349 当前把 H 声明为保存值；:379-380 的 H 与 X 仅在 canReadFromDocument 上不同。
- Other.Config.cpp:581-584 当前所有字段都写文件，:605-617 使用单一上传开关生成 Referer。
- Other.Config.cppm:397-413 当前 Config 复制经 OverlayDocument/ApplyDocument 完成，升级必须避免丢失未写文件字段或回写外部变量。
- 更新通道由 setlist.UpdateChannel 持有并使用 setlistUpdateMutex 同步；当前 GetUploadInfo/Write 已持有 Config 的 rwMutex。
- Net.Update.cpp:163-165 是两个更新请求共同的 Referer 入口；Net.Update.Download.cpp:133-140 的版本检查尚未显式设置 UA。

## 范围约束

允许本任务所必需的 H schema、遍历/输出/复制机制升级和有意义的 headless 回归测试。保持 X 配置行为、JSON 已有键名、本地 UserId 与旧更新配置归属。不修改 Win7 ULW、更新选择逻辑、TLS/HTTP 退路、跨主机重定向、依赖和构建系统；不提交、不推送、不启动 GUI，不向真实服务器发送设备 ID 测试请求。

## 验收标准

1. 地址绑定字段在来源变化后，无需重新读取配置就能在 Write/GetUploadInfo 中输出新值；固定来源保持原值（R1）。
2. 文件/Referer 四种开关组合互不影响，关闭文件输出的已知 H 字段不在最终文件中出现；内部复制遵循明确的 H 来源语义（R2、R7）。
3. 文件中的 EditionVersion 和带花括号 UserId 保持现有格式；Referer 字段集合为 id/ed/pa/we/ch，无 ev、花括号及其编码（R3、R4）。
4. 切换通道后下一次生成 Referer 得到最新 ch；新旧配置文件均保存当前通道，新配置不反向覆盖事实源，也不绕过现有来源锁（R5）。
5. 两类更新请求的初始 UA 均为 Inkeys-Updater/3.0（R6）。
6. 配置读取/重置/复制不修改外部来源；既有 X 字段行为通过回归；差异保持原编码和换行（R2、R7）。
7. 完整 InkeysRepo.sln Debug | ARM64 构建与适用 headless --no-window 检查通过；环境问题单独记录。

## 风险与延后项

实时来源必须具有足够生命周期，跨线程可变来源须遵守来源的锁协议。123 日志保留能力仍未经实测；沿用现有跨主机删除 Referer 与 HTTP 退路行为。设备 ID 生成和无效 ID 统计策略不在范围内。

## 计划状态

复杂任务的 PRD、design.md、implement.md 和最新研究已齐备，两个子代理上下文清单已切换至实时 H 研究。需求已收敛，无阻塞产品决策；此前仅追加 ch 的设计已被替代。用户已确认开始实施，并明确 UpdateChannel 同时写入新配置文件和 deploy.json；进入实施。

## 实施结果与验收

- H 已升级为固定值、变量地址或带共享锁地址的只读来源，两个输出开关独立；构造不提前解引用，输出时获取副本。
- `$Info` 保存原有六项并新增 UpdateChannel；ev 仅落盘，id 的文件副本保留花括号，发送副本删除花括号。Referer 顺序为 id/ed/pa/we/ch；两个更新 GET 的 UA 均为 Inkeys-Updater/3.0。
- UpdateChannel 来源仍是 setlist.UpdateChannel，由现有共享锁保护；main.json 在 Config::Write 时保存最新副本，deploy.json 沿用原保存入口。
- Config 直接复制组内 X 值和 H 来源描述符/固定值，避免文件输出开关影响内部复制；有效 X 标量/序列行为回归通过。旧 JSON 中转对非法内存 Bar 条目的归一化边界差异详见 research/check.md，未发现合法生产路径依赖。
- ARM64 headless build exit 0；正常主机环境 --no-window exit 0，Config metadata tests: 0 failures，全套 PASS animation correctness。
- 完整 InkeysRepo.sln Debug|ARM64 使用 ARM64-native MSBuild 构建 exit 0，146 warnings、0 errors；本次 Config/更新请求源码没有 warning 诊断。
- vcpkg AppLocal 的 pwsh 探测失败后使用既有 Windows PowerShell 绝对路径回退，目标完成。初次受限 runner 的编译 PDB/文件写入环境失败已单独保留；通过正常主机执行验证，不修改生产写入器或依赖规避。
- scoped git diff --check 与修改前磁盘 BOM/CRLF 格式核对通过。未启动 GUI，未向更新服务器发送真实设备标识，未 commit/push。

验收完成，研究、审查、构建和测试原始日志均保留在本任务 research/ 中。
