# 冷页加载计时日志（2026-11-24 加入，未提交）

用于 C1.5 的实测基线。改动只加计时与输出，不改行为。

## 改动位置

- `Draw3.WhiteboardStore.cppm/.cpp`：请求带入队时刻；完成结果带回 `queueMs`（排队）、`workMs`（后台实际工作：读盘含整文件校验与解码，或导出加重写文件）、`exportMs`（仅保存）、`fileBytes`。
- `Draw3.DrawingController.cpp`：
  - `RestoreWhiteboardHistoryUnchecked` 增加可选的 `WhiteboardRestoreTiming*`，累计逐笔解析与追加、足迹、journal（含整页可逆校验）三段耗时及规模。
  - 绘制线程上的 `pageLoadTrace` 串起一次翻页：命令开始 → 离页保存 → 请求读盘 → 读完后接手 → JSON 解析 → 历史恢复 → 驱逐与清缓存 → GPU 恢复提交 → 首次 Present 返回。
  - `saveWhiteboardPage` 输出离页在绘制线程上的截取与序列化耗时。

## 日志格式（Debug 级，每次翻页一行）

```
[Draw3Diag][page-load] result=presented action=next from=1 to=2 cold=1 totalMs= saveMs= requestMs= queueMs= readMs= fileBytes= readyMs= parseMs= historyBytes= restoreMs= strokesMs= footprintMs= journalMs= strokes= items= operations= evictMs= gpuMs= tiles= pageItems= path=
[Draw3Diag][page-save] stage=capture page= strokes= historyBytes= captureMs=
[Draw3Diag][page-save] stage=written page= success= queueMs= exportMs= workMs= fileBytes=
```

字段含义：

| 字段 | 含义 |
|---|---|
| totalMs | 翻页命令开始到首次 Present 返回 |
| saveMs | 绘制线程上的离页保存（PPT 自动保存截取 + 白板逐页截取与 JSON 序列化） |
| requestMs | 命令开始到发出读盘请求 |
| queueMs / readMs | 读盘请求在后台排队 / 后台读盘（含整文件 SHA-256 与解码） |
| readyMs | 命令开始到绘制线程接手读盘结果（减去 requestMs、queueMs、readMs 即唤醒与等帧延迟） |
| parseMs | JsonCpp 解析整页历史 |
| restoreMs | 历史恢复总计，其中 strokesMs（逐笔解析与追加）、footprintMs（足迹）、journalMs（journal 解析与整页可逆校验） |
| evictMs | 切换当前页、丢弃缓存、驱逐其他页 |
| gpuMs | 清空各层与恢复可见瓦片的命令提交（CPU 侧），tiles 为可见合成瓦片数 |
| path | 恢复路径（empty / composition_cache / composition_rebuild / …） |

## 本机初步观察（隐藏窗口自检，页面很小）

- 冷页 `totalMs` 约 13–16ms，其中 `queueMs` 约 9ms：读盘请求排在离页保存之后（保存 `workMs` 约 8ms）。真实大页面需实测。

## 实测方法

1. 日志级别设为 Debug。
2. 白板写一页较重的内容（多笔、长笔、荧光笔、图形，最好有撤回过的操作），再建一两页。
3. 来回翻页若干次（离开的页保存完成后会变冷，再翻回来就是冷加载）。
4. 把日志交回，按 `[page-load]` / `[page-save]` 行分析。

## 第一次实测（2026-11-24，日志 `2026-11-24T10-09-07.log`，Debug|ARM64 构建）

页面：第 1 页 98 笔（历史 JSON 961 KB），第 2 页 246 笔（862 KB），整个多页文件 2.7 MB。冷加载 10 次，数据稳定。

| 段 | 第 1 页（98 笔） | 第 2 页（246 笔） | 线程 |
|---|---|---|---|
| totalMs（命令到首次呈现） | 360–370（首次 569） | 290–320 | — |
| readMs（整文件 SHA-256 + 解码） | 47–61 | 41–53 | 后台 |
| parseMs（JsonCpp 解析整页历史） | 94–97 | 76–83 | 绘制 |
| restoreMs | 45–47 | 22–27 | 绘制 |
| ├ footprintMs | 36–39 | 15–19 | 绘制 |
| ├ strokesMs | 5–6 | 4 | 绘制 |
| └ journalMs（含整页可逆校验） | 2.6–2.9 | 2.6–3.4 | 绘制 |
| gpuMs（77/78 块瓦片的冷重建命令提交） | 141–184 | 121–128 | 绘制 |
| 唤醒与等帧（readyMs − readMs） | < 1 | < 1 | — |

保存：

- 离页在绘制线程上截取并序列化：第 1 页 52 ms、第 2 页 36 ms（第一次翻页的 `saveMs` 为 61 ms）。
- 后台每次保存都重写整个多页文件：`workMs` 121–152 ms，其中导出 1–22 ms，其余是复制旧页字节、写入、Flush 与校验。
- 第一次冷加载的 `queueMs` 为 129 ms：读盘排在两次整文件保存之后。

结论（Debug 构建，绝对值偏大，排序可参考）：

1. **GPU 冷重建提交最大**，约占 40–45%：按“每块瓦片 × 叶子内每一笔”逐个清除、绘制、合成，77 块瓦片就要重复很多次。
2. **历史 JSON 解析第二大**，约占 25–30%：`WhiteboardHistoryJson` 把整页所有笔画（含点列）再存一份，与 UInk 正文重复，98 笔就有 961 KB。
3. **读盘约 15%**：每次都对整个多页文件做 SHA-256，文件随页数增长。
4. **足迹 10%**：长笔的足迹逐段枚举瓦片。
5. **journal 校验很便宜**（约 3 ms），不是瓶颈。
6. 保存链路：离页序列化 36–52 ms 压在翻页上；后台整文件重写 120–150 ms，会推迟紧接着的冷加载。
