# 实施计划

1. 建立 `Inkeys.Display` 接口与实现，迁移 EDID/枚举逻辑，登记工程文件并补充纯算法测试入口。
2. 将旧显示器消费者改为一次读取一个快照；用窄桥接保持 DrawingScale/StopTimingError 行为，随后删除旧管理文件与工程项。
3. 将 PPT 主屏来源切到显示快照，加入运行时约束、显示/DPI 目标状态、拖动延迟和 0.4 秒尺寸位置过渡。
4. 为 Bar 加入显示目标状态、主按钮局部中心保持、越界夹取、拖动延迟和 0.4 秒尺寸位置过渡。
5. 添加显示、PPT、Bar headless 测试；确认配置/COM ABI 未变、旧符号没有编译引用。
6. 运行 `git diff --check`、ARM64 Debug headless 测试和 ARM64 host MSBuild 完整 Solution 构建；可见窗口场景只记录人工验收步骤。

## 完成状态（2026-08-15）

- [x] 建立 `Inkeys.Display` 并迁移旧消费者，删除旧显示管理全局与源码。
- [x] PPT UI3 使用主屏快照重排，支持 DPI/位置/尺寸过渡、运行时纠偏和拖动延迟。
- [x] Bar UI3 使用同一显示 tuple；后续底栏任务继续复用 bounds/workArea/DPI serial，并保留悬浮模式显示过渡。
- [x] 首次订阅与刷新共用 publication 队列；generation 去重、嵌套订阅、并发 Reset/Shutdown drain 已有 headless 回归。
- [x] 旧符号搜索、工程登记、CRLF/BOM、`git diff --check`、headless 与完整 ARM64 Debug Solution 构建通过。

## 最终验证结果

- `InkeysHeadlessTests.exe --no-window`：通过，输出 `PASS animation correctness`。
- ARM64 host MSBuild 构建 `InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64`：通过；仅有仓库既有数值窄化警告。
- `IdtDisplayManagement|MainMonitor|DisplaysNumber|DisplaysInfo` 静态搜索：无结果。
- 未启动可见窗口；真实显示器切换与 PowerPoint/WPS 场景尚未自动验证。

## 验证命令

```powershell
git diff --check
.\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' InkeysRepo.sln /m:1 /p:Configuration=Debug /p:Platform=ARM64
```

## 审查门

- 新 module 不引入 `IdtMain.h`，工程和 filters 项一致。
- UI 订阅回调不操作渲染资源，资源变更只发生在共享渲染线程。
- 自动纠偏不持久化，Display 不定义未来 Draw3/手掌/直线业务策略。
- 保留用户已有的 `Inkeys/PptCOM.dll` 修改，不提交 commit。
