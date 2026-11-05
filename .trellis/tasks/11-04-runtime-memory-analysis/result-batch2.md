# 第二批结果：2–5按功能资源回收

2026-11-05；当前分支diagnostic/memery；用户已批准2–5，1保存峰值与6历史缓存页策略留待后续。代码、完整构建、相关回归、普通静止与白板往返采样已完成；未commit/push。

## 普通静止实测

同一Windows ARM64设备、2880×1920、Debug|ARM64。before为上一批已优化的冻结EXE（SHA9b16dcc4…），after为最终EXE（SHAe7757bba…）；两者均核对PDB GUID/Age，before Age3、after Age7。前后各两个独立进程运行约60秒，每250ms一采，取末20样本约5秒中位数，再取两次均值。

| 稳态指标 | 本批前 | 本批后 | 本批下降 |
| --- | ---: | ---: | ---: |
| Private Bytes / PrivateUsage | 510.85 MiB | **429.35 MiB** | **81.50 MiB / 15.95%** |
| Working Set | 516.18 MiB | 435.36 MiB | 80.82 MiB / 15.66% |
| GDI对象（两次末尾） | 32 / 34 | 28 / 28 | 当前会话下更低 |

对照第一轮最初约571.26MiB，当前约429.35MiB，历史累计约减少141.91MiB（24.84%）。这是同机Debug场景的历史比较，不外推Release或所有用户场景。

Private提交、驻留Working Set、图形资源payload互相不能直接加总。短时初始化峰值本轮宽松处理，重点是普通静止与功能退出后的稳定值。原始四次值、峰值与身份见[汇总JSON](validation/batch2/memory-results.json)、[启动CSV](validation/batch2/startup-comparison.csv)。

## 白板实际往返

使用已有同目录`Inkeys.exe -Whiteboard`命令管道进入，对原PID的正式Freeze锚点投递WM_CLOSE退出。转发进程均退出0，主PID在returned观察窗口仍存活；没有Computer Use、键盘注入、解除锁屏或打开Office/用户文档。按唯一PID/HWND、APPWINDOW/可见状态、生产日志与稳定时间窗观察；不把管道Accepted或样式本身当作全部内部ready证明。

| 阶段 | 前Private MiB | 后Private MiB | 前WS MiB | 后WS MiB | 前/后GDI |
| --- | ---: | ---: | ---: | ---: | ---: |
| 初始桌面 | 508.56 | 429.62 | 515.65 | 434.70 | 32/28 |
| 白板稳定 | 513.35 | 390.43 | 544.49 | 421.62 | 41/35 |
| 返回桌面 | 512.84 | 432.01 | 545.77 | 442.71 | 41/31 |

回到桌面后相对本次初始桌面的WS增量，由约30.12MiB降至8.01MiB；GDI增量9→3。仍保留小型字体/业务缓存，未宣称每一项完全回到冷启动零增量。白板活跃时使用的输出/缓冲集不同，因此不要求进入功能后的Private一定高于桌面。

## 2–5实现内容

- **2 Setting**：10个PNG改为资源ID、请求/实际尺寸、原像素转换配方；CPU只保留64×64 HICON小备份。普通显示使用热纹理，epoch恢复临时解码/上传后释放；原槽位、取整、像素、必需/可选降级与重试保持。真实两倍率probe常驻CPU像素均为16384字节。
- **3 Laser**：普通Init不建两张大主体；有效Laser工具边界原子准备，退出结束临时会话并解绑/释放主体、live、scratch。普通resize/恢复不偷建；持续选中时Up/Hold/Fade保留。真实probe退出/恢复退出resource_bytes均0。退休按实际tool判定，已提升为Eraser的活动/已Up掌擦可继续提交与Undo/Redo。
- **4 Auxiliary ULW**：HWND/元数据与staging/DIB分离；真实需要时建，原生成功隐藏回执带完整身份传至Host绘制owner重验后释放；不清零/伪造ready或clean。重入先完整合成/读回/提交，Primary不被无谓升级全屏，General回收wake不制造视觉帧或自激重建。
- **5 Freeze/UI**：Freeze启动真实透明帧后归还，idle不重复分配；活动前准备、结束成功清透明后归还，白板占用仅释放自有像素。白板/分页按真实隐藏、动画/捕获/提交退休门回收，保留文稿和共享device。Bar仅在几何稳定、面积超需求2倍且可省≥1MiB时缩小，走原全脏重画。

## 输入可靠性修复与测试前提

综合验证暴露的首个隐藏Host失败可在冻结旧EXE复现：fixture仍是Selection状态就注入Down。现已先发布Drawing并等真实Primary成功帧；Whiteboard fixture还补了精确匹配、已呈现style token的正式回传，等待Host真实开放输入；没有放宽生产门。

分页原生拖动旧版也同样10错。诊断确认是既有产品快路漏比较confirmed inputLocked，导致过渡锁沿用：本批在inputChanged与confirmed idle两处纳入锁状态，变化时关barrier，经原candidate/owner ACK后才更新。新增确定性旧锁候选、暂停owner、ACK前保锁与ACK后解锁回归；未用额外业务revision或直接改false掩盖。

## 验证

- 最终完整InkeysRepo.sln Debug|ARM64退出0，使用动态定位ARM64 MSBuild、同次Path/PATH规范化、单节点与20分钟预算；没有改sln/vcxproj/SDK/依赖/工具链。
- MSVC14.44 ARM64增量链接两次在IncrBuildImage内部崩溃LNK1000/C0000005；只精确清理生成Inkeys.ilk并完成独占链接，失败日志保留。不是通过源码或工程配置绕过。
- **25项适用验证最终全部通过**。包括新5个资源probe、5个坏argc早退、掌擦内容、WARP/hardware像素/上传/映射/失败、隐藏Host、分页/白板原生隐藏退役/再入、Desktop/PPT存储及启动进度。完整扫的24项0，加分页锁修复后只重跑受影响的page-control-hidden与ui-idle-capacity，均0；不宣称所有项目均在同一binary重新跑过。
- [最终按名称合并结果](validation/batch2/test-results-latest.json)、[完整扫](validation/batch2/tests-final/test-results.json)、[分页确认重跑](validation/batch2/tests-confirmation-final/test-results-page-control-hidden-ui-idle-capacity.json)。首次失败、冻结基线复现和已确认级联等待的停止原因均保留。
- 新资源测试为真实无窗口/离屏或自动关闭的原生隐藏窗口；Setting/Laser无可靠普通产品外部控制接口，未伪造快捷键。其局部资源/像素/控制路径通过，不冒充全产品交互内存曲线。
- 原编码/BOM/工作区CRLF、最小相关diff与静态资源/线程/代际合同复核；[审查](validation/batch2/check-review.md)与[资源规格](../../spec/native-desktop/memory-resource-lifecycle.md)。

## 保持与限制

L0/L1/L2架构、UI共享WARP与Draw3独立hardware-first/fallback、画质、正常帧率策略、文档/合法历史及兼容范围未改变。流畅度按用户要求作静态审查，没有要求或宣称FPS实测。候选1与6的代码保持本批前字节一致，留待后续评估。

WPR仍受本机Windows管理员令牌权限限制，本批未重试或改系统配置，未获得ETW调用栈/按模块精确贡献。各项纹理payload只作生命周期账，不能作为额外实测节省相加。

所有采样最终向任务自有窗口发送关闭消息，Drawpad关闭会触发Host停止的既有开发退出码6；这仅是主动测量清理结果，不是普通业务正常退出或正确性测试通过。采样发生在清理前。原始EXE/PDB与源码字节备份仅保留为本地验证产物，已用本任务validation/.gitignore排除大文件，未修改项目全局忽略配置。
