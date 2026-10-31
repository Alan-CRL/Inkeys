# H 来源与更新元数据实现记录

## 生产改动

- `Other.Config.cppm`：H 改为 `(writeFile, writeReferer, uploadName, T, name, source)`；强类型 `ConfigReadOnlyValue<T>` 接受固定值、只读地址、带 shared_mutex 的地址描述符。单参数构造均 explicit，空地址抛出 invalid_argument，构造时不解引用实时来源。
- 实时来源只在输出时 Snapshot；H 的默认值/文档读取分支是 no-op。Config 内部复制按 schema 顶层 group 做自然成员复制，X 的合法原子/序列状态和 H 描述符均独立于输出开关，保留文档缓存簿记。
- `Other.Config.cpp`：文件/Referer 分支独立检查输出开关，关闭时不取来源快照。很小的生产共用输出函数处理已知 H 文件键删除、Referer 编码与追加，供四种组合回归直接验证。
- UserId 文件保留原花括号；仅 Referer 发送副本删除 `{}`，使用 `id`。Referer 顺序为 id/ed/pa/we/ch，ev 不再上报但 EditionVersion 继续落盘；TargetArchitecture 也仅落盘。
- UpdateChannel 绑定 setlist.UpdateChannel/setlistUpdateMutex；main.json 的 `$Info.UpdateChannel` 保存当前副本，旧 deploy.json UpdateSetting.UpdateChannel 保存路径不变。锁序为 Config -> 来源；在来源锁内只复制字符串。
- `Net.Update.Download.cpp`：版本检查显式使用 `Inkeys-Updater/3.0`，安装包下载原值相同。没有改 Net.Update.cpp、HTTP/TLS 退路或跨主机 Referer 删除逻辑。

## 无窗口回归

新增 `InkeysHeadlessTests/config_metadata_tests.cpp`，仅在现有 runner/project 登记；测试提供生产 Config 所需 setlist 与 mutex 的测试存储，合成 ID，私有临时目录。

覆盖四种文件/Referer 输出组合、关闭文件输出删除已知键且保留未知键、构造后的实时来源变化无需 ReadAll、固定/地址/加锁来源、空地址拒绝、explicit 不可由普通值隐式赋值、原始与发送 GUID 的格式区别、ev/target/channel 文件输出、ReadAll/ReadMini/Reset 保护来源、合法 X 原子与序列复制及 H 固定/实时描述符复制、并发完整通道快照。

Headless 的 deploy.json 写入器没有链接：旧通道持久化按未改动 `IdtConfiguration.cpp` 的现有加锁读取及 `UpdateSetting.UpdateChannel` 赋值静态核对；新 main.json 输出由真实生产 Config 回归执行。

## 验证结果

- ARM64 原生 MSBuild 由当前 VS 的 vswhere `-find MSBuild/Current/Bin/arm64/MSBuild.exe` 发现。各调用先 Remove-Item Env:PATH，设置 MSBUILDDISABLENODEREUSE=1，没有修改依赖、工具链或 PDB 配置。
- 首次沙箱 headless 编译：日志 `headless-build.log`，C1902 PDB manager mismatch，最终 Build FAILED。初始进程返回的 session_id 未保留，实际退出码没有捕获；一次跟进命令日志锁失败 MSB4104 的 exit1 保存于 headless-build.exit.txt，不能解释为最初编译退出码。
- 主机首次编译：`headless-host-build.log` exit1；本次初稿嵌套 generic lambda 复制生成触发 MSVC C1001。改为更小的自然 group 复制后，编译器内部错误消失。
- 主机第二次编译：`headless-host-build2.log` exit1；新测试包含 IdtConfiguration.h 引入 hashlib 的 CRT C4996，按已有 Config 编译项给该新增测试 item 配置 `_CRT_SECURE_NO_WARNINGS`。
- 主机最终 headless 编译：`headless-host-build3.log` / `headless-host-build3.exit.txt` exit0。warning 为既有第三方 nameless union/整数转换，不修改第三方。
- 沙箱 `--no-window`：`headless-tests.log` / `headless-tests.exit.txt` exit1；新与既有文件原子写入测试受限，其他新来源/上报/读/reset/copy 测试通过。
- 主机原样 `--no-window`：`headless-host-tests.log` / `headless-host-tests.exit.txt` exit0；Config metadata tests: 0 failures，最终 PASS animation correctness。
- 完整 solution：`solution-build.log` / `solution-build.exit.txt` exit0，Build succeeded，146 warnings / 0 errors，耗时 1:09.86；`InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，允许五分钟以上正常运行，无并发 product 构建。产出 Build/ARM64/Debug/Inkeys.exe。当前实际 solution 还包含 headless 与 PptCOM.Tests，旧研究“headless 未在 solution”是过期观察。
- Solution 的 Vcpkg AppLocalFromInstalled 先调用 pwsh.exe 因 PATH 规范化退出9009，随后现有构建脚本自动回退绝对路径 WindowsPowerShell，步骤正常完成；没有残留构建阻塞。warning 为既有第三方/旧代码类型转换等，未为清零扩大任务。
- `git diff --check` 范围检查 exit0；`file-format-baseline.json` 记录原 BOM/EOL，现有编辑文件均保持 CRLF，animation_tests.cpp 保留 UTF-8 BOM，新测试文件 UTF-8 LF。

## 剩余边界

- 未运行 GUI、未发送真实服务器 ID、未验证 123 日志或风控，未进行非 ARM64 构建。
- Typed copy 对合法 X 状态保持既有用途；先前 JSON 中转可拒绝/规范化非法内存 Bar entries，当前成员复制保留它们。主代理确认不为非法内存值扩大本任务，外部 JSON 读取仍经过原校验。
- 其他实时来源当前在启动阶段初始化，若将来并发改写需提供来源锁；源地址与锁须覆盖 Config 使用期。
- 没有修改 Win7 ULW/IdtMain，未 commit、push 或 stage。主代理负责 H spec 与任务/journal 收尾；已阅读其新 H 合同。
