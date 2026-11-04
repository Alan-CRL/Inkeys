# 准入状态表与连续占位方案

| 分支 | 占位持有者与时序 | 失败处理 |
|---|---|---|
| 普通启动false | 目录文件ID命名Local presence mutex；原子创建判存在 | 既有对象则拒绝，权限失败区别报错 |
| 普通启动true | 仍保持同一个presence对象HANDLE | 最后实例死亡才销毁，之后关false可正常重新准入 |
| Restart/CrashTry | 监督器通过显式继承名单持有原实例lease，等精确旧进程死亡后再接力给新进程 | 合法替换不增加实例；其他允许模式实例仍活时也不会误拦本实例重启 |
| 更新根→installer→根/回滚 | 同一逻辑根lease连续复制；每一跳child先验真/确认Ready再等父死亡 | 失败保留父lease，仅取消本次精确child |
| SuperTop三段式 | helper先复制原root lease；暂停final中显式DuplicateHandle，Ready/Accepted后才退出root并恢复final | 复用既有失败降级，helper失败root保留lease |

子进程句柄值不能在CreateProcessWithToken前预知，使用GUID命名的短寿命共享handoff包传递：精确child PID、父PID/父HANDLE、lease HANDLE、逻辑根。暂停child生成完毕后才填包/恢复；child检验所有者父镜像在逻辑根、自身目标在根或installer、根的volume/fileID对应lease对象名称，再收Ready。参数不是豁免证据。NtQueryObject仅动态读取Mutant名称，失败closed；不新增静态ntdll导入。真实Win7/UAC未运行需单列。

只读启动偏好用Config.ReadMini(paths,true)，复用解析但不执行旧布局迁移写盘。更新installer从handoff逻辑根读取，不从临时installer目录读取。

实施补充：
- Packet固定uint64句柄与版本/byteSize，布局断言确保跨Win32/x64/ARM64；转换前检查目标uintptr范围。
- 已消费handoff参数不能继续进入LaunchState::commandLine，否则下一跳会误读旧child PID。
- 当前Local对象可能解析到AppContainerNamedObjects（Codex runner实测），验真用系统解析的Local同名reference完整Nt名称比较，不硬编码BaseNamedObjects，不只比后缀。
- 新资源StartupHandoffProtocol=1。已验证的update/rollback目标：1走Ready协议；字段缺失走暂停+lease复制+Resume兼容桥；未知/损坏/读取错误拒绝，不以超时猜能力。旧V2/V3仅能确认OS创建/占位注入/恢复，不能承诺旧二进制自身之后的SuperTop也保持新lease。
- adopted installer同时持物理installer目录guard；legacy验证后rebind根lease时保留原物理guard。根准入失败直接退出，不能经fail删除另一链有效update.json。
- --single-instance-tests真实私有copied EXE覆盖两跳Ready/父退出接力、barrier竞争、不同目录、allow→deny、无效包以及私有VERSIONINFO的1/2/缺字段三态。真实UAC/GUI/Win7未运行。

SuperTop双Ready闭环：不能先通知原root退出，再让final检验包。handoff分支先Publish暂停final、Resume使其只执行Adopt预检、收到final新Ready（final仍阻塞等helper死亡）后，才ConfirmParentExit发送原Ready/等Accepted/等精确root死亡。helper返回退出后final业务初始化才继续。无handoff的原ResumeAfterParentExit路径保持原暂停恢复语义和测试，ConfirmParentExit共享原事件等待。
