# H 字段实时绑定设计

## 变更边界

当前 H 是缓存值且一律写文件，只有上报开关。本次把 H 升级为只读信息来源，支持每次输出取实时值和独立输出策略；X 仍承担可从文件读入的普通配置。更新通道同时保存到新配置和 deploy.json，运行时事实源仍为 setlist.UpdateChannel，不修改 Net.Update 的更新策略。

## Schema 接口

拟定签名：

```cpp
H(writeFile, writeReferer, refererName, valueType, valueName, source)
```

典型声明：

```cpp
H(true, true, "id", std::wstring, UserId, &::userId)
H(true, false, "ev", std::wstring, EditionVersion, &::editionVersion)
H(true, true, "ch", std::string, UpdateChannel,
    BindConfigSource(&::setlist.UpdateChannel, &::setlistUpdateMutex))
H(true, false, "NaN", std::wstring, FixedExample, L"fixed") // 仅文档示例
```

固定值、`const T*`、带来源锁的地址描述符是同一强类型只读来源的三种构造方式。固定值由描述符持有；地址绑定只保留地址，不在构造时解引用；带锁绑定在取值时短暂持共享锁复制 T，随后释放。无需通用回调框架。绑定地址和来源锁必须至少与 Config 使用周期一样长；生产来源是现有全局变量。

## 输出与状态语义

- H 字段使用与 X 不同的成员声明和遍历分支，隐藏字段的值通过快照按需读取，输出层独立判断 writeFile/writeReferer。
- ReadAll/ReadMini、ApplyDefaults、ResetToDefaults 不写 H 来源，也不从文档回读覆盖 H；固定 H 始终使用声明的常量。保持现有 X 行为。
- 文件输出保留现有 `$Info` 组和已有六个 H 的键名与值类型。writeFile=false 的已知 H 键须从最终文档移除，以免 AutoClean=false 时遗留旧值；当前 UpdateChannel 为 true，写入 $Info.UpdateChannel 副本。
- Referer 输出先取得值副本，再做现有换行清理及 URL 编码；仅 UserId 的上报副本删除花括号，不修改原来源或文件输出。
- ch 在 schema 中定义，由 Config::GetUploadInfo 直接生成；Net.Update::GetRefererInfo 不再需要额外拼接字段。
- Config::operator= 必须区分 X 存储状态与 H 来源描述符：X 沿用现有复制语义，H 直接保留/复制来源绑定与固定值，不经文件输出开关过滤的 JSON 中转，不写入被绑定的全局变量。configOnce 的 H 信息仍遵循所声明的实时来源，普通 X 保持启动快照用途。

## 同步边界

现有 Config 方法持 rwMutex；带锁 H 取值时再短暂取得 setlistUpdateMutex。审查所有来源锁持有区，确认不存在反向取得 Config rwMutex 的路径。取值快照后，编码、JSON 处理和文件/网络 I/O 不持来源锁。来源更新不得因为输出而被回写、重置或改成原子字段。

## 请求合同

Config::GetUploadInfo 输出顺序为 id、ed、pa、we、ch；不输出 ev。GetEditionInformation 显式补 User-Agent: Inkeys-Updater/3.0；DownloadEdition 已有同值。保持现有 HTTPS/HTTP 退路与跨主机删 Referer 的逻辑。

## 预期改动文件

- Other.Config.cppm：H 来源类型、schema、遍历/声明/复制与维护说明。
- Other.Config.cpp：H 读写/输出分支与 id 副本规范化。
- Net.Update.Download.cpp：版本检查请求 UA。
- 现有 headless Config 测试文件及必要的现有测试入口：真实模块回归与新增来源 globals；仅新增文件登记必要时修改测试项目条目。
- configuration-i18n-and-assets.md：在实施完成后记录新的 H 合同。

不预计修改 Net.Update.cpp，因 ch 已由 schema 输出。确需调整上述边界时先说明原因。

## 验证和回退

升级涉及通用配置遍历和复制，需要真实 Config 模块 headless 回归，不能仅用复制的编码脚本代替。使用任务私有临时配置目录检查文件；来源变量修改后验证下一次输出，不调用 ReadAll 来刷新；覆盖固定值、外部来源保护、输出开关、原始 GUID 和编码通道，以及既有 X 读取/重置/复制。保留原文件编码/EOL，审查最小 diff。回退单位为 H 支持、关联 schema 与对应测试的本任务改动，不回退其他任务。
