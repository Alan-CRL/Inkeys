# 剩余单帧位置跳变：原点交接诊断

> 此文保留实施前的历史判断。固定手势表面的最终实现及本轮人工验证结果见 [实现与验收](fixed-gesture-surface-implementation-2026-10-26.md)。

## 原始证据与当前结论

- HEAD 8ba46740；用户确认顶边正常、脏区外异常消失，剩余脏区内跳变。36秒45° V路线已被用户确认符合要求。
- V第1轮肉眼仍跳；第2轮用户说电脑很卡，肉眼看到但未录到问题帧；第3轮完整36.0019秒、12轮，录屏明确捕捉异常。
- 第3轮录像D:/Personal/Downloads/2026-10-26 18-29-26.mp4，24.233333正常、24.250000整组UI约左80/下40、24.266667恢复。蓝框也是D2D绘制内容，不是独立的GetWindowRect证据。
- 附近CSV尺寸1298×406不变，原点(792,1514)→(873,1473)。录像起点与QPC并非帧级同步，不能把这条CSV迁移与24.250帧强行配成同帧。

## 只读审查与测试缺口

- directWindowDragMutex覆盖GetDC/ULW/ReleaseDC/EndDraw/成功快照；未发现Bar WndProc改写WINDOWPOS。
- 绘制使用frameTransitionTranslation，最终FramePresentation可能使用presentedTranslation。模式barrier待确认时已有配对保护，普通stale fallback也有保留合法直移的用途。尚未证明整次80/40错配在此路径可达，因此不直接改fallback。
- 现有离屏测试逐输入同步跑完整frame，sink只拷贝参数/检查包含，不执行真实ULW、不验证提交瞬间源像素；不能外推真实合成帧。
- 独立Win32探针尝试延迟WM_WINDOWPOSCHANGING，但ULW没有进入该延迟回调；结果不构成系统异步交接或根因证据。该探针仅存TestResults，不进入产品/构建系统。

## 本轮唯一产品源码变化：默认关闭诊断

- Bar.LiveOriginTrace.h + Bar.RenderLoop.cpp。仅INKEYS_ENABLE_UI3_PRESENTATION_HARNESS且显式INKEYS_UI3_ORIGIN_TRACE_DIRECTORY存在时启用。未改变动画、窗口位置/大小/ULW参数、锁或调度。
- 唯一render owner有界8192条内存记录：frame/live/selected translation、serial/barrier、viewport/source/capacity、布局映射、目标top/grip、ULW前后真实HWND和API时间/结果。关闭时不分配记录缓冲、不QPC、不额外窗口采样；没有屏幕抓取/DwmFlush。
- Attempt先于几何事务构造，逆序析构在解锁后导出；每行有session/QPC，不再依赖one-shot手势归属。仅遇到非held呈现才导出，同文件可含多个session；若退出/快速再按下阻止该呈现，文件可能延后，必须按实际QPC范围检查，不能宣称必然每手势独立落盘。
- 导出失败保留内存、停止诊断并OutputDebugString一次；饱和明确记录dropped，不静默冒充完整。

## 验证和下一门

- 原生ARM64完整InkeysRepo.sln Debug|ARM64构建退出0；审查后最终增量构建退出0。只有既有hashlib++ C4267警告。
- --ui3-bottom-dock-transition-test诊断关闭/开启均退出0、failures=0。启用导出38个CSV、826行，无列错位/逆时/丢记录。同步离屏重建top/grip最大误差2.28e-13px；不表示复现或修复真实闪烁。
- 自有诊断窗口PID26492，EXE SHA256 EF7FEEC08AC9D4AC2A8C0FAF716E98F51C2C8FAAE1E36DF06B32F1BF6E57735D；私有原基线配置，启动HWND/DPI/命中已核对一致。
- 等用户准备后同一36秒V路线先无录屏。必须核对trace与replay QPC范围、session、dropped、API前后实测窗口及用户是否看到原症状。此时再决定最小修复/受控回归，不把诊断或旧测试PASS当完成。
- 没有提交、推送、归档。证据均在TestResults/ui3-origin-handoff-2026-10-26和TestResults/ui3-v-drag-2026-10-26。

## 第一轮同帧诊断已实机复现，追加源像素证据

- 2026-10-26 用户确认无录屏时跳变比录像更明显，几乎每次进入/退出都出现；标注15个系统时间点。原生V重放36.0007秒、12轮，QPC与trace session1匹配，2019行无drop，2007成功held ULW、11 defer。后续手动session3在另一文件、在replay结束约22秒后，明确排除。
- 提交世界top/grip相对同帧目标最大差3px（普通stale小步），无80/40误差；所有ULW后真实HWND与dst相符。capacityOrigin固定，5次source/size变化均full，50次大原点迁移。ULW p50=1.112ms/p99=2.327ms/max=5.359ms，大迁移max=2.993ms。
- 用户15个秒级时间点最近大迁移均在±0.3s内；这是粗时间相关，不能当具体异常帧的调用定论。完整correlation/summary在TestResults/ui3-origin-handoff-2026-10-26。
- 不直接采用仅mode切换的viewport反补偿：左右跨区重复捕获/脱离会形成累计漂移/扩窗；每帧反补偿又频繁改变source/full replacement。未证明根因前不引入该策略。
- 追加显式INKEYS_UI3_ORIGIN_TRACE_PIXELS=1：初始化预分配64×512×384×4缓冲与私有DIB，仅大原点迁移/full时从ULW源DC读取Logo邻域，松手/解锁后导出BMP；不读桌面。读回可能改变时序，必须再次确认原症状仍复现，不把未见问题当PASS。
- 完整Debug|ARM64构建退出0；启用诊断/像素开关的离屏吸附测试退出0（无真实HWND故不触发原点读回）。另独立无GUI smoke调用实际Trace.CaptureSource，验证GDI源crop、已知像素、top-down BGRA与BMP导出通过。
- 源像素窗口PID22420，SHA256 028F41E64AEF087D739CBB60FFA90A0B8E5E9ECDE134D293BA4628166DBCF120，原fixture核对一致。旧PID26492通过所属DrawpadWindow正常WM_CLOSE，exit6；第一手势证据已落盘。等待用户准备后重放，不自动开始。

## 源像素结果与最小原点完整提交候选

- 源像素第2轮36.0012秒、12轮；用户确认强度基本相同、仍明显跳变。session1覆盖0.047～36.951秒、1988行/drop0，51张源图；后续手动session另存且QPC在结束后，排除。
- 51张源DC截图目视Logo与选择按钮正常；独立灰色边框扫描Logo顶部相对同帧目标全为0px，左边为-1px描边（全一致），没有80/40错位。这是ULW调用前实际GDI源内容证据，不只数学参数。图像位置结果和contact在TestResults根下。
- 独立自动关闭的D2D/WARP/GDI-compatible target探针先呈现旧图，改画并GetDC后等待300ms但不调用ULW，旧窗颜色/位置不变；调用新ULW后也正确。此小场景未复现源表面提前可见，不支持据此宣判alias，不能代替真实UI交接。
- 原基线大迁移50次中43次仍为partial；提出仅原点变化整窗复制的单变量候选。真实缓存比较在几何锁内、WindowUpdated之前；不添加D2D全脏，不改source/viewport/动画。既有direct SetWindowPos成功同步缓存，因此普通直移后的稳定dst仍partial。
- 源/尺寸/epoch稳定时未变像素由保留bitmap维护，完整ULW复制不要求新全绘；失败ready=false时完整重试。两位只读审查未发现正确性阻断；极端每次ULW仍有pending move时可能每帧full，实际成本需采样。
- 原基线新增full预估45/2008、47/1969次（约2.3%，不外推其他场景）。候选完整Debug|ARM64构建和既有离屏吸附回归均退出0。
- 候选PID24748，SHA256 56A9A14EC941CEC2DA1E531872DD7024A0AEFA4BC82BDA81511F908ACB892CAF；轻量trace开、源像素开关关，与首轮轻量诊断相同。原fixture已核对；未开始V重放，待用户准备。候选不代表已修复。
- 备选只改native translation为raw连续输入并不保持屏幕等价：Docked bottom和Centered far端目标当前未扣rawT，会随rawT漂移，首帧seed后追错终点；不叠加这类未经设计的补偿。

## 整窗候选失败、边缘对照准备

- 第3轮候选36.0016s、12轮；用户确认基本相同、仍明显跳。trace session1=1990行/drop0、1975成功ULW，56次原点变化全full（50次大变化），条件确实生效但症状不变。候选条件已从源码撤回，不作为修复保留。
- 用户四象限观察：进入右下→异常左上；进入左下→异常右上；退出右上→异常左下；退出左上→异常右下。方向与native原点迁移反向吻合，尚不能据此确定API内部机制。
- 三轮17759/17524/17218个外部真实窗口采样，bottom全1800..1920；API前后同样无bottom>1920，所有post四边等于dst+size。超过工作区1856但没有已记录的物理越屏/系统拉回。viewport受layoutBounds0..1920裁，V轨迹T.y=-120..0；其他正T场景可理论越屏，本轮不外推。
- 回答用户上端/抓点问题的实际例：attempt262→303，dst(876,1472)→(794,1514)，size均1292×406，src均1347/685，windowBottom1878→1920；UI屏幕top1652→1654，client top180→140。初期可扩高，稳定切换则原点与内容局部位置互相抵消，非每次改高度。
- 新增仅harness显式INKEYS_UI3_DIAGNOSTIC_RAISE=1的测试夹具，Bar私有pending monitor/workArea/dockline统一y−200，宽高/DPI/UIScale/handle不变；真实系统显示、任务栏和Display全局快照不变。两位只读review未见当前固定桌面单屏路线阻断；不覆盖PPT/白板/跨屏。
- 完整Debug|ARM64构建退出0；私有PID13152启动rect[876,1476,2004,1674]与预期一致，Logo975/1575命中。新SHA256 D23C81BD5A25C532D63B0DC2A9562A9AE84401928B70FE1879D506C6B4250E04。旧PID24748正常WM_CLOSE exit6。
- 等待用户准备后V路线整体上移200（entry1535，vertex1615，左右1455）。需按相位/输入比较相同source/size/T和大位移，真实bottom<=1720，方可判断物理边缘是否必要；未复现也不能直接宣布边缘根因。

- 上移对照已完整执行36.0011s、12轮。session1=2029行/drop0，trace QPC覆盖重放0.027～36.930s，后续手动session3明确排除；2020成功ULW、50次大原点迁移仍在。外部18053采样和ULW前后native bottom均1600..1720，全程低于真实工作区1856，未触实际任务栏或屏幕底边。monitorOrigin=(0,-200)。主要viewport/layout bottom1920和height406保持；宽受包络/dt轻微变化，稳态1300而原基线1298，sourceX随2px padding差不同；不强称所有字段逐帧完全相同。held capacityOrigin(-553,829)，Up吸收尾端另计。当前等用户反馈，不能声明假设已排除或修复。

## 最新结论与诊断清理

用户已确认上移200px后仍明显跳变且强度基本相同。两轮held mode/center压缩序列长度50且相位分布一致，T范围相同，真实窗口全程远离物理边缘，因此物理任务栏/屏幕底边接触不是原症状必要前提。内部virtual layout裁剪和原点交接仍在，此结论不能直接归责Windows。

用户提出固定手势表面方案，本轮仅评估，详见gesture-fixed-surface-assessment-2026-10-26.md。诊断源码三文件与wiring patch已存TestResults/ui3-origin-handoff-2026-10-26/diagnostic-source-snapshot；确认未混入其他修改后，Initialization和RenderLoop恢复HEAD原有编码/CRLF、LiveOriginTrace临时头移除。原生候选的无效行为条件此前已撤回。PID13152经所属DrawpadWindow WM_CLOSE正常退出exit6，无遗留自有测试窗口。没有提交/推送；新方案未实现/未测试。
