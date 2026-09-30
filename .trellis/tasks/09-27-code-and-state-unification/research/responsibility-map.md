# Work 1：当前职责依赖与最小拆分边界

生产编译链依据 Inkeys.vcxproj 与 native-desktop/index.md；IdtFloating、IdtDrawpad.cpp、IdtWindow.cpp 为工程 None 的历史源码。以下是当前职责，不是按行数提出重写路线。

```text
Bar.Interaction / Bar.Button / MouseHook / PPT business
  → IdtState 的条件命令、authoritative/target/echo 和短锁值快照
  → Draw3 Bridge 不可变 ProductState 与 Window Service requested owner

UI3 RenderPipeline 的唯一 Scheduler 线程
  → Bar.RenderLoop coordinator（唤醒→同帧快照→目标/layout→animation→dirty→present）
    → Bar.FramePacing / Bar.Animation（时间与曲线）
    → Bar.Layout / Bar.Scene（布局/命中/几何）
    → Bar.Rendering / Bar.UI（光影缓存、SVG/path、D2D 资源）
    → RenderPipeline device epoch 与诊断
  → Setting、PageControl、Freeze 等其他串行客户端

Draw3 独立 RTS/input → Host/controller/document/history → renderer/独立设备 → DComp/ULW；
PptCOM 只发布会话/页身份，不直接持有 UI3/Draw3 GPU 资源。
```

## 当前修改与保留例外

- 已把跨入口笔型/形状/宽色的正式写入与长手势版本判定收敛到 IdtState；Bar 一帧取值快照，Bar.Layout 的纯值重载承接激光预设和宽度，MouseHook 与配置保存按业务边界各取一次。相关细节见 state-ownership.md 和 migration-table.md。
- Bar.RenderLoop.cpp 仍较大，但当前已有 RenderLoop、Layout、Animation、Rendering、UI、State 与 diagnostics 文件分工。本轮为保证 H0 成本与外观可比，只动需要的快照/时序行；Work 2 在分段测量后才决定是否按资源/光影/调度职责拆出具体函数或文件，不把复制到 .inl 当收口。
- Bar.Interaction.cpp 的消息/hover/capture 与 FineDial 物理手势交织；本轮只统一业务命令及修旧候选跨代提交。若后续拆手势 runtime，先冻结 capture/取消/抬手合同并保留原线程，不引入第三套控件层。
- Setting 仍是 ImGui 客户端，Bar 保持现有 widget；Draw3 保持自己的设备与绘图 owner。统一的是状态/帧/资源合同，不强迁 UI 框架。

## 待 Work 2/3 证据决定的拆分点

1. UI3：先对 input/wake、snapshot、target/layout、动画、光影 mask、SVG 解析/raster/upload、GetDC/ULW/EndDraw 分段；只在同一职责有实际耦合/重复或成本证据时拆 Bar.RenderLoop/Rendering/UI。
2. Draw3：先对 RTS、ContactInput、modeler、几何、上传、Present、history/save 分段；生产 Host 当前未接 RuntimeMetricsSession，独立 demo 的 benchmark 不可替代。按 owner 拆而不跨 UI3/Draw3 合设备。
3. 需要修改工程项时同步 vcxproj/filters 与 Debug/Release 三架构；不改第三方和生成文件。
