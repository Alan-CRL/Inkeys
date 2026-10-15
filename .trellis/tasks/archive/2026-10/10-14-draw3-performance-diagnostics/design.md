# Draw3 性能诊断技术设计

## 授权、边界与事实

用户已批准规划及实现至最终。唯一交付为 Draw3 现场性能诊断包，采集、启动和分析共同验收，保留一个任务而不拆成独立产品任务。主会话负责规划/接口协调/构建/规范/收尾，Trellis 子代理按文件所有权实现和检查。

现有 Draw3 唯一 Host/RTS/绘制 owner、独立 D3D 设备与 DComp→ULW 选路保持；不改变输入准入、绘制算法、timer、目标帧率或呈现行为。现有 RuntimeMetrics schema2 的 bounded Down proof、inclusive 四阶段和结束后导出合同保持。ContactSnapshot 已含 QPC/sequence/admissionRevision、ContactHandle 含 generation，不扩正常逐点载荷。

## 启动与会话

Scripts/Draw3Performance 提供 CMD、Windows PowerShell 脚本和 README。CMD 接收或查找软件 EXE，脚本创建唯一会话目录、收集硬件、启动 `--draw3-performance <绝对目录>`。显式 argv 经现有 LaunchState/SuperTop 透传，环境变量可辅助但不作为跨 UIAccess 重启的唯一依据。不绕过 SuperTop，不修改用户配置。

只有最终进程越过 SuperTop 后写 `launch.json`（schema_version/session_id/final_pid/process_start_time/status/output_directory）；脚本据此等待正确最终进程与 `completion.json`，初始 exit(0) 不代表采集成功。正常结束应包含采集/导出状态；启动失败、最终进程失踪、崩溃、导出缺失必须报告 incomplete/failed，不能产生绿色结论。脚本不杀死已有实例或任意软件进程。

实验输出字段沿现有 ConsoleOutput owner 放入 `Experimental.Inkeys3.ConsoleOutput.Performance`，默认 false，说明 Draw3/重启生效/输出位置；普通启动时本次记录目录位于软件 log/Draw3Performance 下。该字段不能隐式开启 Draw3/Cursor/TouchArea/PptCOM verbose 输出。显式启动优先本次 argv 目录，不写回性能覆盖值。

## 固定容量性能记录

新增 Draw3.Performance.h/.cpp 内部实现，由 Host 显式创建和拥有 PerformanceSession，向所属 controller/input/renderer/presenter 传可选指针。正常模式指针为空，新增记录与所有时钟/query/统计/分配/日志完全在启用分支之后；不设置全局逐点原子开关，不新增正常 ContactSnapshot/Record 字段，优先复用已存在 metrics 门。

允许开启现有 RuntimeMetrics 作为正式 Down/内容 proof 和四阶段证据来源，保持旧接口/导出含义；PerformanceSession 的新原始结果有独立 schema。两者合计固定 payload 预算不超过 64 MiB；新采集 payload 不超过 32 MiB。最大样本只是一项诊断容量，不改变业务准入。热路径只写定长数值槽；RTS producer 使用有界并发安全索引，owner 单写帧和回执，停止 RTS 且 join 绘制 owner 后才读取/导出。不覆盖前缀、不热扩容、不写盘、不逐帧格式化、不busy wait、不新增常驻采集线程。

每代 Host 在会话下使用独立 `host-NNNN` 子目录保存 `session.json`、`frames.csv`、`input-events.csv`、`gpu.csv`、`initialization.json` 或等价有明确schema的输出，避免 DComp 启动失败后 ULW 新 Host 覆盖旧证据。落盘为 create-new；主进程完成握手在全部 Host 停止及导出之后。崩溃可能失去未导出的内存采样，脚本须明确此限制，并保留硬件/启动/普通日志证据。

## 帧与 GPU 口径

帧记录包含 serial/QPC起止、活动区间/idle断链、尺寸/目标/输出身份、physical/terminal/command/recovery计数、dirty、实际Present尝试/成功/失败和结果。CPU分段至少含控制/恢复、ingress、model/预测、几何/上传/栅格提交、composite、Present及ULW Copy/Map/CPU行拷贝/ULW；标明互斥/嵌套关系，旧四成本字段不可求和。等待/idle记录与工作耗时分开，正常idle不得制造帧。

GPU 以稀疏固定slot timestamp+disjoint 观测几何/合成/读回命令流区间；GetData仅DONOTFLUSH、单次非阻塞查询，不等待不Flush。unsupported/pending/disjoint/query_failed/ring_full/owner_stopped分别报告。命令流区间可能含CPU提交空隙，不当成纯shader执行成本；CPU API阻塞不冒充GPU duration。实际device变更重建诊断query属于诊断模式，不能改变业务恢复。

## 输入关联

诊断 sidecar 记录 RTS callback入口 QPC/批次token和真实publication边界的record/generation/even sequence/sourceQpc/publishedQpc；不能在Publish*返回后再读取snapshot推断刚发布的序号。下游记录同序号consume、真实非预测几何Adopt、当前帧有效raster/composite proof及成功Present返回。

离线依据相同输入key、canvas/content/output/frame身份与有效阶段构建延迟。Down/Move/Up分别报告 callback→publish、publish→consume、consume→process/raster、raster→successful Present和总软件延迟。输入被覆盖/争用丢弃/拒绝/取消/过期/无真实几何/无成功呈现/样本容量不足分别保留。用raw input source QPC和callback入口QPC区分输入流年龄与解码开销。

不能只用lastConsumedSequence、hasContent或活动contact存在推导实际内容成功。普通墨迹Move只称真实内容software Present latency，不承诺原始坐标像素；Up用stored item与当前投影proof，不把CPU store当作屏幕完成。无法证明的工具（尤其Laser终态Hold/Fade）明确标supported=false及原因，仍记录consume延迟和帧成本。软件Present返回不能称光学可见/触笔到光子。

## 初始化和环境

脚本使用WMI/CIM可用路径记录OS/CPU/GPU/驱动/内存/显示/电源等，字段失败独立报告，硬件查询在测试前完成。软件PerformanceSession冷阶段直接记录实际graphics/presenter/RTS阶段及失败HRESULT，不依赖跨SuperTop继承stdout。不得直接启用StartupEnvironmentDiagnosticsEnabled，因为该开关还触发ULW像素扫描和GPU绑定查询；可拆出仅初始化的冷期记录入口。

## 离线分析

独立PowerShell分析器读取版本化结果，导出summary.json/中文report.txt或Markdown、top-long-frames.csv、输入延迟分布与异常计数。报告样本数、seen/retained/dropped/invalid、覆盖时间、峰值/中位数/P95、有效时P99，active interval与idle后首帧/成功Present间隔分开；采样不足不宣称稳定性通过。

报告只做有证据的归因：WARP/ULW同步Map/CPU拷贝/Modeler/命令恢复/几何复杂度/连续成功Present抖动等，不能由相关性认定CPU/GPU型号因果。离线合成测试涵盖idle、失败、掉样、缺失、长帧和输入关联。

## 验证与回滚

无可见或隐藏GUI，不用Computer Use。完整InkeysRepo.sln Debug|ARM64、原生ARM64 MSBuild、同一PowerShell规范化PATH/node reuse、至少5分钟构建期限；使用Headless --no-window与新增早期无窗口probe验证真实计量器。PowerShell AST/版本语法、离线合成测试、i18n sync/check及git diff检查。真实低端/兆芯/Win7/图形驱动行为在最终说明中明确待现场运行。回滚只移除本次诊断字段/旁挂/包，不动现有RuntimeMetrics、产品算法与渲染路线。
