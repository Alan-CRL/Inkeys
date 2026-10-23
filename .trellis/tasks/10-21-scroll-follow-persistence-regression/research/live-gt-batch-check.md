# 测试底图真值的批量读屏审查

2026-10-23。checker只读审查，无生产源码修改、构建、产品CLI、GUI、新截图或测试重试。当前为方案预审，实际差异和新运行须后续追加。

## 当前取消 run 的独立事实

L5原生long PID30628、round `product-ink-lk5-long-1023-current`、root `product-live-30628-7972109`，8DAF6D3B产物，exit1/143.324秒。root对核实自身Source发送WM_CLOSE取消，原日志 `source_cancel message=16` 及cleanup全归还一致。

16/151个down完成，累计1952px，含前序净16后Source(16,1968)；tracker/owner仍Tracking、latest/applied661、viewport(15.9999,1968)、clamp0，未Pause。frame609（session1/capture1、accepted1/None）初始点存活0、added45，610及最终661继续可信，最终inliers318/confidence1。这是有限实屏接力证据；十屏峰值、返程墨迹、B/成功文件链尚未完成。

第16步elapsed93672ms，约5.855秒/步。ScreenSourceGt原每次99个screen GetPixel、每步前后两次是源码事实；未作逐调用计时，尚不能断言它是唯一瓶颈，更不是产品跟随耗时/FPS。完整取消记录见live-long-runtime-check.md本日追加。

## test-only 改动约束及判定

无原则阻断：一次主屏DIB BitBlt(SRCCOPY)+GdiFlush，随后内存逐点检查原99坐标及原256×192 ROI每像素。完整screen buffer仅内存，任何路径都不save；只有全验后复制/保存专用crop。原颜色精度、99/99要求、paint一致性、安全取消、失败少量RGB/窗口元数据、128步长、19200距离、480秒监督均不变。

DC、resize、BitBlt、GdiFlush任一失败即失败，DC及时归还；不得用零值补读失败、跳过点或在未知全屏落盘。批量快照与原99个不同时刻GetPixel有采样时刻差异，需新short及相同长程证明实际画面检查仍可用；更快本身不等于通过。

只优化测试oracle，不动生产Mag/PW/Tracker/owner或显示策略；诊断应报告实际GT读屏/检查耗时，而非将测试提速写作产品FPS提升。等待实现精确diff和构建/最小CLI receipt。

## 05D40CCE 精确实现预审

当前 inl SHA256 `05D40CCEA79548A11E087697695F41F4BDA3616BD7AE3722478F01493E33DEF6`，before18F4311C；原文件/patch/manifest在 `Build/ScrollFollow/gt-batch-capture`。checker独立剔除ScreenSourceGt函数后比对其余字节完全相同，UTF-8无BOM、纯CRLF及原EOF保持；未动Tracker、业务日志、其它oracle、调用顺序、步长/距离或监督。

DibSurface既有实现是top-down32位BI_RGB、span连续width×height；新索引 y×width+x 与原COLORREF的RGB通道转换正确，ROI索引(64+x,64+y)和49152个期望值不变。DC/resize/BitBlt/GdiFlush短路失败false；screen DC在检查/任何throw之前释放，局部snapshot自动释放。read后新增CheckDesktop更严格。原3个失败点诊断、paint/source元数据保持。

snapshot变量从未调用save；只有99点与全部ROI通过后，才创建256×192 crop逐像素复制并补alpha255，再沿原savePng路径。无imageName则不创建crop。失败或未知fullbuffer不落盘。更改采样时刻为一次完整快照的边界如上，没有掩盖它与原分时GetPixel的差别。

新增计时read_us包括GetDC/创建DIB/copy/flush/释放DC，verify_us包括read后安全检查及内存验证，均不包含后续PNG编码；不是纯BitBlt或产品取帧耗时。记录bytes为完整瞬时buffer字节，不能当保存图片大小。

**静态/格式预审PASS，已通知root和impl放行唯一完整ARM64及最小CLI构建。** 本段尚无新binary或batch实屏结果；新short及原long门槛待实际运行。

## 构建与最小CLI交接

checker独立核当前主EXE SHA256 `D58E04E15D3195388E47B0F5D8D01E7653503D5BE6E423C92DE31019D284BC76`，inl05D40CCE、Tracker BF3409未变。完整ARM64原日志 `motion-tracker-build-gt-batch-capture-1023.log` Build succeeded、0Error、3条既有hashlib C4267；内部15.24秒、wrapper15.571秒。

`offline-8464812/result.json` 25坏argv均exit2/双输出0B，protocol exit0/233ms、state exit0/92ms，原stderr明确PASS；未执行精确live参数。没有因本次纯harness改动重复334秒Tracker矩阵，原Tracker源码保持不变。

静态/构建/CLI预审通过，已放行root真实short及原128/19200long。当前节不宣称新batch实屏或性能通过，待实际GT/PNG/文件/资源结果补充。

## 新批量GT真实short（PID17260）

root执行，checker事后离线独立复算。round `product-ink-gt-batch-short-1023-current`，root `product-live-17260-8498359`，启动前D58E04E1产物，PID17260/exit0/6.227秒。

独立世界纹理/每通道覆盖率区间/核心平移及实际MessagePack检查通过，8张已存PNG仅为原5A+1B+2专用Source，没有fullbuffer图片。A四次核心集合严格位移相同；B18876 core、unknown0、10浅边缘。真实A/B经RTS、非空辅助ACK→正常Off主窗ACK、实际Exit两Pen229/225点、viewport(0,16,1)、raw首点与受限计算界/唯一索引GUID及原C++Read/Import/cleanup均成功。输出该root/checker-complete-verification.json。

15次GT均batch_copy1、99/99与精确ROI1，记录内存22118400B/次（2880×1920×4），不是保存文件大小；新PNG又经独立公式核查，因此批量oracle不是只因更快就放行。root/checker-batch-timing.json独立按原日志计算：

| 本测试GT阶段 | 实测 |
| --- | --- |
| 完整read阶段 | 最小36.112ms、均56.777ms、中位58.483ms、nearest-rank P95/最大74.124ms |
| verify阶段 | 均1.561ms、最大2.620ms |

read含GetDC/创建DIB/copy/flush/释放DC，verify含安全检查与内存核验；未输出纯BitBlt耗时。6.227秒总时长较前一short37.844秒降低，支持本次夹具提速收益；不是严格隔离GetPixel单调用测量，也不是产品FPS提升。产品捕获仍沿原Mag/PW与相同Tracker，不能改写为截图后端被优化。

此run Capture12条、Tracker接受10（启动闸门另计），完整backend Read均65.236ms、nearest-rank P95/最大86.800ms；混有输入/工具/保存等待，不推导产品FPS。原128px、19200px长程正在单独验收，本节不提前称完成。
