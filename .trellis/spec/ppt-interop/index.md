# PPT / WPS Interop Guidelines

本层只覆盖托管 `PptCOM`、类型库/activation manifest，以及 Inkeys native 端的加载、状态、控件和页级墨迹联动。证据标签含义见根 `index.md`。

## 边界文件与可确认职责

| 路径 | 证据级别与职责 |
| --- | --- |
| `PptCOM/PptCOM.cs` | `【直接确认】` 定义 COM-visible `IPptCOMServer`/`PptCOMServer`，连接 PowerPoint/WPS、读取放映状态、控制翻页和窗口 |
| `PptCOM/PptCOM.csproj` | `【直接确认】` .NET Framework 4.0/AnyCPU，Office Interop 引用，AfterBuild 运行 `TlbExp` 并复制 DLL/TLB |
| `PptCOM/PptCOM.manifest` | `【直接确认】` 被 native 启动路径作为 activation-context manifest 资源/文件使用 |
| `Inkeys/IdtMain.cpp::wWinMain` | `【直接确认】` 初始化 COM，准备 DLL/TLB/manifest，创建/激活 activation context 并加载 PptCOM DLL |
| `Inkeys/IdtPlug-in.cpp::CheckPptCom/GetPptState/PPTLinkageMain` | `【直接确认】` `#import` TLB、创建服务、传入 native 状态地址、运行服务/控件线程和命令包装 |
| `Inkeys/IdtPlug-in.h::PptInfoStateStruct/PptImgStruct` | `【直接确认】` native 放映状态及每页 `IMAGE` 映射 |
| `Inkeys/IdtDrawpad.cpp` | `【直接确认】` 页码变化时保存/恢复 `PptImg`，随后更新 `PptInfoStateBuffer` |

详细 ABI、生命周期和数据流见 [com-contract.md](com-contract.md)。

## 当前端到端数据流

~~~text
IdtMain: COM + activation context + PptCOM.dll
  → IdtPlug-in::CheckPptCom 创建 IPptCOMServer
  → Initialization(&PptInfoState.TotalPage, &CurrentPage, GetOffSignalInteropPointer())
  → PptCOM::PptComService 绑定 PowerPoint 或 WPS并写 native 页码
  → IdtDrawpad 检测页码变化，保存/恢复 PptImg[页]
  → 更新 PptInfoStateBuffer
  → Inkeys.UI.Ppt 的四个页码窗口按需显示缓冲页码
  → UI3 交互队列把翻页/结束等业务命令投递回 IdtPlug-in 的 PPT 业务线程
~~~

`【直接确认】` `PptInfoStateBuffer` 不是 managed 直接写入的第二份状态；`IdtPlug-in.cpp` 注释说明它在 `DrawpadDrawing` 完成 PPT 画布加载后才同步。不能把 COM 页码、UI 缓冲页码和 `PptImg` 当成同一个变量。

`【直接确认】` `PptImg` 保存的是 native EasyX `IMAGE` 页级墨迹，由画板换页逻辑读写；它不是 C# COM 对象提供的幻灯片图片。

## 当前实现与支持范围的边界

- `【直接确认】` `PptCOM.cs` 有 Microsoft PowerPoint 和 WPS 的 ROT/COM、进程、窗口与清理分支，并有事件/轮询及 Office-busy 重试。
- `【历史/兼容】` 代码和文案提及 PowerPoint 2007、WPS 2013+ 等兼容场景，只能证明实现意图或历史声明。
- `【待确认】` 本轮没有运行 Office/WPS；实际支持版本、位数、安装类型、Windows/架构组合和重连行为均不能由分支存在直接确认。
- `【待确认】` `CheckCOM()` 返回代码常量，native 会调用并保存结果，但未找到比较或拒绝不匹配版本的逻辑；当前不能称其为已实施的版本兼容策略。

## 实施前决策门（阻塞）

当任务涉及 PowerPoint/WPS 版本、Office 位数、安装类型、Windows/架构组合，或准备调整/删除某个兼容分支，而正式支持范围仍不明确时，必须在实施前询问开发者。不得因为代码中存在某个兼容分支就自行扩大支持声明，也不得因为分支看似历史就自行缩小支持范围。

## 修改前的证据型检查

以下是根据当前链路形成的 `【合理推断】` 清单，不是已经存在的完整发布测试政策：

1. 若改变 `IPptCOMServer`，同时核对 C# 接口顺序/GUID、TLB、native `#import` 调用、manifest/复制产物；不要只改一侧。
2. 若改变 `Initialization` 或共享状态，核对 native/C# 整数宽度、地址稳定性、读写同步和退出后解引用。
3. 若改变绑定/恢复，分别检查 PowerPoint 与 WPS、事件与轮询、busy、放映结束、文档切换和 Office 退出。
4. 若改变页码语义，追踪 `PptInfoState → IdtDrawpad → PptImg → PptInfoStateBuffer → PPT UI`，不能只验证控件文字。
5. 按 `AGENTS.md` 用完整 `InkeysRepo.sln` 验证 DLL/TLB 生成与复制；静态文档审计不能替代该构建。
