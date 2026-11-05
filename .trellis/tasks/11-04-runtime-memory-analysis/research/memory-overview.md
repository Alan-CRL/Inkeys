# 内存结构与优先调查方向

状态：源码调查已完成，未启动产品/GUI，尚无真实运行基线；分支 `diagnostic/memery`。以下数字均为指定条件下的名义载荷或已存在文件大小，不是优化收益承诺。

详细证据见 [工具盘点](tooling-inventory.md)、[Draw3 内存结构](draw3-memory-map.md)、[UI/字体/互操作内存结构](ui-memory-map.md)。

## 当前结构

| 内存所有者 | 内容与生命周期 | 需要区分的统计 |
| --- | --- | --- |
| Draw3 独立 D3D11 device | L0/L1/L2、可信快照、Laser层、双缓冲、透明呈现读回。主体随画布初始化/resize创建，不随停止写字释放 | 硬件/UMA/WARP、DComp/ULW路径；纹理payload、driver commit/residency与进程Private不能混算 |
| Draw3 GPU历史缓存 | Undo默认64MiB、composition默认192MiB预算；按需分配16/24MiB纹理页，淘汰slice通常保留池，切slot/Clear显式归还 | 预算、已分配页、有效slice、OS驻留四种量不同；不是每页预分配256MiB |
| CPU墨迹与文档历史 | Page/Canvas/Stroke点、undo/redo和tile拓扑、桌面/白板/PPT slot、恢复与保存快照 | 可见内容、可撤销历史、不可达旧分支、冷页和排队快照；保存峰值与稳态分开 |
| UI共享WARP device | Bar、分页、白板背景target；Setting独立session/图片/动态字体atlas；隐藏后不同客户端回收策略不同 | WARP使用软件栅格化，不按独立GPU显存描述；target逻辑大小并不额外叠加到已覆盖它的Private Bytes |
| CPU像素与资源映像 | Freeze整屏DIB、Setting解码图片副本、嵌入字体及图片映像 | DIB/解码副本属于实际像素存储；`.rsrc`映像、映像工作集、FreeType解析结构和glyph atlas分别计量 |
| 运行时、COM、线程和系统图形服务 | 可能启动的CLR/COM/JIT、原生堆、线程栈、DWM/ULW系统侧资源 | Office外进程不算Inkeys进程；栈reserve不等于commit；外部图形服务成本另列 |

## 优先级

1. **先量启动和空闲时的全屏资源。** Draw3 两张无条件Laser主体层是8WH；DComp仍初始化辅助selection ULW，staging+DIB又是8WH；可信L2快照4WH。Freeze另外在启动时创建并clear一张W×(H-1)×4 DIB。前三项属于图形/CPU混合账，Freeze是CPU像素；优先评估按需创建和闲置释放的可行性，保持视觉回退及选择/白板交接合同。
2. **设置从未打开时的resident成本。** 已在启动时建立ImGui、字体和图片。仅已确认的CPU图片名义载荷约8.83MiB（缩放倍率1），另有WARP纹理和字体解析；关闭设置会释放presentation，但保留resident资源。按页加载/可重建CPU副本是候选，首次显示与设备恢复成本必须一起量。
3. **历史缓存粒度与高水位。** 首个Undo/Composition页分别16/24MiB；看分配页数与有效内容比例，以及Clear/切slot后是否实际回落。不能把降低预算或撤销能力当成默认优化方案。
4. **重复操作的CPU增长。** `Undo → 另起新笔`只丢redo栈，旧stroke/history/tile拓扑仍追加持有；这是源码确认的保留行为，不直接定性泄漏。优先用既有无窗口history benchmark建立增长曲线，再判断安全回收边界。
5. **保存瞬时峰值和隐藏UI高水位。** PPT保存请求把activeCanvases深复制到legacy投影；白板保存有JSON中间态；白板背景、Bar/PageControl和设置预览在使用后可能保留backing。分别测峰值、回落和重新显示代价。
6. **字体和CLR按实际占比排序。** 8个字体资源文件共46.72MiB，但通过资源映像直接供读取，不是全部复制进private heap。Alpha8动态图集和同atlas FT_Face共享已存在。PPT尚未使用时CLR是否已启动及占多少，需模块/堆实测。

以上各项源码行号和生命周期见两份专题报告。候选优先级依据可定位的大块资源与触发时机，尚未按真实节省量排序。

## 分辨率估算的使用方式

一张3840×2160 BGRA8名义31.640625MiB；两张63.28125MiB。Draw3资源描述合计DComp约44WH、ULW约52WH，在4K示例中是348.05/411.33MiB。这里已经包括了报告列出的图层/交换链/读回资源，不能再把上述子项加一次，也不能再直接加到Private Bytes。

该公式以实际画布W/H为准，不默认整个虚拟桌面等于某一块屏幕；可选快照失败、尚未选用Laser、驱动放置、行对齐和资源延迟驻留都会改变实际记账。没有真实采样前，不对外宣称进程当前占用348MiB或能节省某个比例。

## 下一步交付标准

- 不带profiler的冷启动/空闲基线，与带追踪的归因数据分开。
- 设置、绘图/历史、白板三个基本场景的峰值、稳定值和关闭/清空后回落；PPT/Office另列后续场景。
- 原生堆/VirtualAlloc存活栈和图形资源账能解释主要阶梯，仍不能归因的部分明确标记。
- 每个候选带测量证据、功能/延迟风险和最小改动建议，再进入优化实现规划。
