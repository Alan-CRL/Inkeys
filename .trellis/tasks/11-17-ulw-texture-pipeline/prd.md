# 第2步-B ULW 独立纹理与 CPU/GPU 重叠

> 状态：待阶段 A 完成后规划。父任务 `11-17-win7-ulw-presentation`；分支 `refactoring/draw`。

## Goal

ULW 回退路径（Win7 / DComp 不可用或失败）改用独立 `ID3D11Texture2D` 作为最终渲染目标，并把 GPU 读回与 ULW 提交改为不阻塞绘制线程的流水线：CPU 最多领先 GPU 一帧，恢复与 DComp 相同的“CPU 不等 GPU”关系。

## 初步范围（待规划时细化）

- ULW 模式不再创建 HWND 交换链；Renderer 以无交换链方式初始化，`Resize` 补无交换链分支。
- 读回改为多 Staging 槽 + 非阻塞完成检查（不自旋）；超时回退阻塞 `Map(READ)`，连续异常熔断回同步路径。
- ULW 提交与“已上屏”回执语义重新定义；失败后从最后成功提交版本累计脏区。
- 保持画布调试、主窗命中底层 alpha、辅助 DrawpadPresentation 回收语义、Win7 SP1 + KB2670838 兼容（FLIP 实测可用，不以文档为由改动）。

## 现状依据

父任务 `research/presentation-pipeline-current.md` §1–§6、§8.3。
