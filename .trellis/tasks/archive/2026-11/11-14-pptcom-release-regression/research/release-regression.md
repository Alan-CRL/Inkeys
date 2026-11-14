# PptCOM 释放回归调查

> 最新修正：用户同机 Inkeys2 20261019a 和完全不运行 Inkeys 的 PowerPoint 都有退出驻留。下文只证明历史释放策略差异，不能再作为当前延迟是 Inkeys3 特有回归的结论。第三次日志显示 CP 原释放调用等待33.458秒后正常配平，见 third-user-log-and-baseline.md。

日期：2026-11-14。当前 HEAD：`e73673f7b1c91fd13cc5a4a946960d7b9934c646`，分支 `bugfix/pptcom`。对照 tag：`20261019a` → `7a2e07837813566069f7e0c922c7806acc4c82a2`。

## 结论与证据等级

**已确认释放策略的回归位置和引入提交；尚未在真实 Office 上证明唯一运行时根因。**

- Inkeys2 1019a 与 Inkeys3 的该修改之前，`FullCleanup(final:true)` 使用 `SafeFinalRelease`，且完整清理末尾执行 `GC.Collect → GC.WaitForPendingFinalizers → GC.Collect`。
- `0eb1c7c5` 在同一提交中删除 `SafeFinalRelease`/final 分支和上述 GC 序列。当前代码仍是这个策略：三个根对象仅单次 `ReleaseComObject`，之后清空字段；`final` 参数现在仅用于清理计时标签。
- 这使原来“最终清空已停用 root RCW + GC 收尾”的保证消失。单次 Release 返回值大于零时，清空托管变量不等于释放底层 COM；当前适配器既不使用返回值，也不报告异常。
- **条件性因果推断**：若现场存在剩余 RCW 计数或等待终结的互操作引用，上述策略变化可解释用户报告。不能从删除 GC 本身推断已经找到了某一笔具体漏取得，也不能断言恢复 GC 单独足够。
- 未发现证据支持为了修复此问题删除整套 accessor 或 raw 事件包装。当前 accessor 的常规读取使用真正 dynamic；对象返回有明确 finally。C# 的手动 COM 释放是 `Marshal.ReleaseComObject` / 在独占最终清理时的 `FinalReleaseComObject`，不存在 C++ 式 `delete dynamic`。

## 当前调用链与代码位置

1. `Inkeys/IdtPlug-in.cpp:507` 的 `GetPptState` 调用 `PptComService`（`:536`）；服务结束后重置服务槽，随后重试创建。它不是主动关闭用户 PowerPoint 的入口。
2. `PptCOM/PptCOM.cs:865` 的关闭事件与 `:873` 的 raw 事件分派只发提示，由 owner 重新观测，不在 COM 回调中销毁 root。
3. `ObserveBoundShow`（`:1299`）在 `ActivePresentation` 失败时独立读取 `Presentations.Count`（`:1323`）；Count=0 或明确断连使服务退出。当前已存在最后文稿关闭检测，不能笼统断言“没有清理路径”。busy 保留为 Unknown，不能等同退出。
4. `PptComService` 的 finally（`:1508` 起）先停止 mailbox，`:1513` 调用 `FullCleanup(true)`，`:1516` 起最多三轮重试失败事件解绑。
5. `FullCleanup`（`:501`）先使旧 binding 无效、清空批注缓存、解绑事件；`:525–527` 按 window → presentation → application 单次 `SafeRelease`，`:531–533` 清空字段，`:545` 直接输出完成。**当前缺失最终 root 清空与强制 GC 的位置就在这里。**
6. `SafeRelease`（`:460`）委托 `presentationAccessor.Release`；`PresentationDescriptor.cs:182–191` 仅调用一次 `Marshal.ReleaseComObject` 并吞异常，未检查其返回的 RCW 计数。临时对象按取得单次释放本身应保留，不能把此 helper 全局改成 FinalRelease。

## 已核对的所有权边界

| 路径 | 当前事实 | 本次判断 |
| --- | --- | --- |
| ROT 返回与去重 | `PptCOM.cs:1147–1187` finally 释放临时项；`:1214–1228` 未转移 bestApp 总在 finally 释放，转移后置空 | 旧的同 identity 漏释放已修，不再作为当前已确认漏点 |
| Observe 窗口/文稿 | `:1413–1416` 转移窗口后置空；`:1445–1450` 释放剩余局部 | 未发现本轮可证明的遗漏 |
| 属性与命令 | `PresentationDescriptor.cs:121–149` 常规分派为 dynamic；`PptCOM.cs:1738/1748/1760/1772/1785/1910` 等具名临时对象 finally 释放 | 不重写这些访问方式 |
| HWND 标量 | `PresentationDescriptor.cs:83–87` 与 `PptCOM.cs:1666–1668` 类型转换只借用已有 RCW | 不能因多一个别名就额外 Release/delete |
| raw 事件连接点 | `ComEventConnection.cs:103–111` 独立持有 FindConnectionPoint 的取得，解绑后单次释放 | 不是没有释放方法的泛用包装器 |
| 失败 Unadvise | `PptCOM.cs:490–491` 将 CP 转入静态 registry；`ComEventConnection.cs:136–158` 失败保留，成功/断连后 Release | 活引用确会保留；属另一个可能残留源，必须检查结果，GC 无法替代解绑 |
| 最终根对象清理 | `FullCleanup(true)` 与普通重绑相同的单次 Release | 旧最终清理策略被移除，重点修复边界 |

## 引入时间与历史对照

| 提交/版本 | 时间（+08:00） | 相关变化 |
| --- | --- | --- |
| `657f0954` Add PPT presentation autosave & bridge support | 2026-09-05 10:37:21 | 引入 descriptor/accessor，但仍保留 final FinalRelease 与 GC；不是这两项删除的提交 |
| `0eb1c7c5` restore dynamic compatibility and deterministic COM cleanup | 2026-10-18 21:53:26 | 删除 SafeFinalRelease、final 分支、Collect/Wait/Collect；引入 raw 事件连接点及 retirement registry；是本次释放策略退化的明确起点 |
| `20261019a` / `7a2e0783`（Inkeys2） | 2026-10-19 15:12:18 | 保留 final FinalRelease 与 GC；这条分支没有包含上述 Inkeys3 修改 |
| `21d3de47` / PR #238 合入 dev | 2026-10-19 16:20:57 | 将上述 PptCOM 改动合入 Inkeys3 开发线 |
| `97e61925`、`f1267e12` | 2026-10-30 / 10-31 | 加快观测、维护文稿身份，未恢复最终 root 清空及 GC |
| `555ba5ce` | 2026-11-01 15:45:39 | 为清理添加分段计时，仍只有单次 Release。当前 blame 大量显示此提交，是包裹/行号变化，不是本次策略删除的起点 |

注意：tag 的日期晚于 `0eb1c7c5` 并不矛盾，两者属于不同历史分支。不能用 tag 名称或当前 blame 单独判断回归引入时间。

复查命令：

```text
git show 20261019a:PptCOM/PptCOM.cs
git show 0eb1c7c5^:PptCOM/PptCOM.cs
git diff 0eb1c7c5^ 0eb1c7c5 -- PptCOM/PptCOM.cs
git log --all -S GC.WaitForPendingFinalizers -- PptCOM/PptCOM.cs
git log --all -S FinalReleaseComObject -- PptCOM/PptCOM.cs
git blame -L 501,546 -- PptCOM/PptCOM.cs
```

## 为什么已有验证没有挡住它

- 旧任务 `10-18-pptcom-dynamic-compatibility/design.md:63–64` 明确改成“不依赖 FinalRelease/GC”，这不是无意漏掉三行，而是当时的设计决定。本次用户已明确要求恢复“先手动释放，再强制 GC”，优先于旧 spec。
- 旧任务 `research/verification.md:58–64` 和最终补记明确：真实 PowerPoint/WPS 进程退出和下一文稿验收均 NOT VERIFIED。
- 当前 `DispatchFixtureTests.cs:193–225` 的测试在 Scope.Dispose 中自行对根对象调用 ReleaseComObject；该文件没有调用生产 PptCOMServer.FullCleanup/PptComService。它证明了夹具图和 accessor 的已枚举取得配平，不能证明真实生产最终清理完整。
- `ManagedCompatibilityTests` 虽调用生产 FullCleanup，但根对象是 FakeNode，不能观察真实 RCW 剩余计数或跨进程 Office 退出。

## 官方 API 依据

- [Marshal.ReleaseComObject](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.marshal.releasecomobject?view=netframework-4.8.1)：返回的是释放后的 RCW 计数；重复入站可能增加计数。对在途/共享 RCW 强行释放可能破坏其他调用。
- [Office application does not exit after automation](https://support.microsoft.com/en-us/servicing/visual-studio/troubleshooting/office-application-does-not-exit-after-automation-from-visual-studio-net-client)：说明未释放互操作引用与 Office 残留的关系，以及显式清理后 GC 的补充作用。本文示例的 Quit 不适用于接管用户已有 PowerPoint 的本项目，不据此增加 Quit。

## 首轮调查验证与限制

- 已进行源码、git show/diff/log/blame、Inkeys2 tag 和旧任务记录交叉核对。
- 首轮没有修改业务代码、构建/运行测试或激活 Office/GUI。后续补充调查已运行现有构建与原生夹具，结果见 ownership-audit.md；仍不能标为用户故障已复现或修复通过。
- git status 普通扫描被 HiMsg 子模块 gitdir 的读取权限阻断；使用 `--ignore-submodules=all` 检查主仓库改动。没有变更子模块。
- Python 的 WindowsApps 别名不可执行，uv 发现缓存也遇到权限；改用本机已安装 KiCad 的 Python 3.11.5 成功运行 Trellis 脚本，没有修改工具链/环境全局配置。
