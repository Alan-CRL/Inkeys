# 笔迹文档结构与抬笔接入设计

## Object Model

```cpp
enum class StoredInkType : uint8_t { Pen, Highlighter, Eraser };

struct StoredInkPoint {
    float x;
    float y;
    float width;
};

struct StoredInkStyle {
    StoredInkType inkType;
    uint32_t fallbackRgb;
    float opacity;
    uint16_t texture;
};
```

- `InkStroke` 按值拥有 style 与 point vector，并只提供 const 读取。
- `InkCanvas` 保存 viewport 和按合成顺序排列的 Stroke vector；追加返回当前索引。
- `InkPage` 保存 Page GUID 和按 `DeviceKey` 排列的 Canvas；使用小 vector 线性查找，保持稳定顺序并避免为少量显示器引入哈希开销。
- `InkCanvasCollection` 保存 Workspace GUID 和 Page vector。Page 索引由容器位置派生，不重复存储。
- GUID 使用独立 16-byte value type；Windows GUID 生成只在平台适配边界完成。Stroke 没有 UUID。
- 当前页索引由 `DrawingController` 持有；模型本身不把视图选择当成文档内容。

## Finalization And Drawing

```text
confirmed ActiveStroke
  -> FinalizeStoredStroke
  -> current InkCanvas::AppendStroke
  -> DrawStoredStroke(the appended const InkStroke)
  -> per-Stroke operator-layer resolve to L2
```

- Pen finalizer 复制已提交稳定前缀，再拼接 `BuildCompletedPenTail(..., false, ...)` 的真实 taper 尾段；相同连接点只保留一次。每个运行时 `r` 转为 `width = 2*r`。
- Highlighter/Eraser 使用完整 `realPoints`，没有建模点时回退 `inputStartPoint`。
- `DrawStoredStroke` 临时把 Stored point 还原为 renderer `InkPoint{x,y,width/2,time=0}`，再调用现有 Pen/Highlighter/Eraser 几何与 operator。
- 同帧 ended contact 按现有 active 遍历顺序追加和绘制；每条 Stroke 独立清理 operator scratch、绘制并 resolve，随后才处理下一条。
- CPU append 先于 GPU 绘制，使设备失败时文档真值仍保留；Canceled 与 Laser 跳过 append。

## Page Lifecycle

- `DrawingController::Run` 启动时创建 Collection、第一页和默认 Device Canvas；模型仅由绘制线程修改，不加 mutex。
- 窗口 clear request 改名/解释为 new-page request。存在 active contact 时沿用当前 deferred 行为。
- 请求兑现时先成功创建 Page/GUID 和默认 Canvas，再切换 current page index，最后执行既有 GPU/Laser 清理及全量 present。
- `main.cpp` 启动前调用的直接 `ClearCanvas()` 仍只负责透明初始化，不走 new-page request。
- 旧页没有独立 L2；本轮只能通过模型查询，不能切回显示。

## Compatibility And Performance

- 不引入第三方库、序列化依赖、全画布快照、per-Stroke texture 或空间索引。
- Stroke/Canvas/Page 使用移动语义；追加只产生最终 point buffer 的一次拥有型分配。
- Canvas 不裁剪坐标；模型只要求 x/y/width 为有限值且 width 非负。viewport scale 必须有限且大于零。
- 后续 history 可在 Canvas vector 外增加 index/generation handle，不需要修改 Stored point/style 格式。
- 后续 UInk adapter 可直接映射 Workspace/Page GUID、Device Canvas、viewport、Stroke order 与基础字段。

## Rollback

- 文档模块与 finalizer 可独立移除；DrawingController 接入点集中在初始化、completed Stroke 提交和 deferred new-page 三处。
- 如果逐 Stroke GPU resolve 暂时失败，可保留 CPU append 并回退现有单笔完成绘制；不得重新引入 prediction 作为 Stored 数据。
