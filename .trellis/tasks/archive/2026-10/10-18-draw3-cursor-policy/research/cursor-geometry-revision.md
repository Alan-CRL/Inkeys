# 光标边缘与圆角修订（2026-10-19）

本轮是 cursor-policy-revision.md 两类型与固定设备策略上的外观增量，用户已明确要求实现，沿当前 in_progress 任务继续。不改变类型默认、配置/UI、来源分类、Touch 隐藏、穿透、活动工具投影或墨迹/历史。

## 当前需求

1. 荧光笔内部预览颜色容易被高亮盖住：左右两边高亮厚度改为目前一半，上下保持。Mouse/Digitizer/Unknown/Conflict 仍高亮，ScreenPen 仍不高亮，不新增选项。
2. PenIcon 原五边形中，笔尾和笔侧共四个折线拐角改为圆角；左上笔头保持尖角，笔尾仍向右下。保持24 DIP、热点、外框和dirty范围。

## 最小变更边界

- 真实差距位于 Assets/inkPixelShader.hlsl 的光标内轮廓和 GetPenIconDist；优先只改该正式 PS 及同步生成的 Draw3.Win7GuardedShader.h。
- type5 矩形只在显式高亮边缘时将左右内缩宽度从1.25 DIP变为0.625 DIP，上下仍1.25 DIP。非高亮屏幕笔0.5 DIP、圆光标、橡皮、Laser、legacy默认透明度公式及填充0.35保持；小尺寸内轮廓不得反向或产生负/非有限coverage。
- 笔形四个非笔尖顶点采用向内圆角，圆角随24 DIP图标等比缩放；禁止把整个多边形统一圆化导致笔尖变钝，不能外扩轮廓/改输入坐标。
- 不新增运行时参数/公共接口/线程、配置、设置或资源ID。若代码证据表明需要超出PS参数现有合同，先向root说明；不自行扩展架构。
- 兼容体必须从修改后的正式源沿现有 Generate-Win7-GuardedShader.ps1 同步生成，DefaultInk正向门仍为已确认的 >21。所有非光标数学、未知type输出、ISGN/OSGN与导数/discard正向guard合同保持。

## 执行与验证

按Trellis实施→独立check→root统一编译顺序。实施代理负责PS、guarded数组及 implementation-cursor-geometry.md，允许命令行FXC编译/反汇编，不执行产品/GUI/窗口harness/GPU新测试、不操作Git写入。root负责PRD/设计/清单/spec/任务元数据与最终构建验证。

先保存本轮源基线及策略文件SHA，检查diff仅涉及两处光标数学。沿既有ARM64 FXC /O3编译正式参考PS和guarded，核签名、DefaultInk原/还原尾部哈希及guarded字节身份。独立审查四角/尖端和小尺寸/高亮门控；完整 InkeysRepo.sln Debug|ARM64（动态ARM64 MSBuild、同invocation PATH规范化、600秒上限），既有headless --no-window。编译后只做PE资源数据映射核验，不启动窗口。保留原编码/BOM/CRLF，git diff --check。

不为这个纯shader增量重复不相关的状态CLI或i18n生成，不编造复制生产数学的测试来声称像素验证。真实可见圆角、左右高亮与Win7 WARP仍须GUI/目标设备验收；本轮没有GUI授权。前轮验证只作历史，不替代本轮字节身份与构建结果。

## 最新增量：放大圆角（2026-10-19）

用户实看反馈1.5 DIP不明显，要求加大。当前实施将24 DIP图标的四个非笔尖角目标半径加倍为3 DIP（min(halfSize)*0.25），切线长度上限由相邻短边1/4放宽至2/5，以使普通图标四角实际均达到3 DIP。相邻两角合计最多4/5边长，仍留1/5直边，尖端跳过、退化保护和内切公式保持。只改这两个常量及对应中文注释，荧光笔与设备高亮规则、type21分支、热点/外框/输入/配置保持。

实施/独立check沿本任务进行，输出implementation-cursor-radius.md及check-cursor-radius.md。参考PS/guarded在Cache/CursorRadiusShader/generated生成，核签名/尾部/>21/guard/字节身份；root按既有完整Debug|ARM64和PE资源核验。已确认本机增量链接器连续两次IncrBuildImage内部错误，本轮构建前只保留该已核对.ilk生成文件，使原配置直接完整链接，不改项目。纯shader常量变化不重复不包含shader数学的headless、状态CLI或i18n；上一轮结果仅历史，不冒称本轮执行。无GUI/Win7真机/commit/push。
