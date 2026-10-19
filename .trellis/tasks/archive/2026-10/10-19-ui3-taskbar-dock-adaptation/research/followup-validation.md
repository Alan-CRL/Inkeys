# 用户追加调整验证（2026-10-19）

## 最终行为
- 桌面顶部/左侧/右侧任务栏：普通及居中底栏主体描边外沿距本屏底部5真实屏幕DIP；与底部autoHide Hidden共用同一换算，不叠加UI倍率。底部常驻/autoHide Raised继续贴稳定任务栏top；PPT/Whiteboard原场景5DIP及双画布1px规则保持。
- UI.Bar.UseTaskbarHeightAsScaleBase缺字段、非法bool或重置时，由本次启动冻结的设备默认分类决定：大屏true，其余false。沿用READY触控且尺寸不可用或主屏面积>=1200cm²的既有判定；用户已保存的严格bool优先，当前/持久化PaintDevice覆盖不影响此缺省。
- 构造、ReadMini、ReadAll、Reset共用schema getter。缓存constinit，不在静态初始化枚举；READY触控在Mini前复用一次正式Display枚举并在原初始化点复用结果，无触控保留原枚举时机。DPI awareness只调用一次，失败仍由原tracker建立后的0xD001分支处理。
- 未来PC UI统一默认开启仅记录方向，本轮未实现。保留本任务此前所有未提交改动。

## 源码审查和静态验证
- trellis-check未发现未修复问题，见research/followup-check.md；设备判定/启动例外研究与实现说明见research/followup-device-defaults.md、followup-default-implementation.md及followup-dock-implementation.md。
- git diff --ignore-submodules=all --check通过。使用Git checkout filters与工作树比较，28个既有修改文件的BOM/换行格式保持；新增CanvasPolicy头仍CRLF。证据：.trellis/.runtime/ui3-taskbar-dock-followup-build/format-audit.json。
- task.py validate父任务通过，implement/check各13条。超出32768字节的既有spec注入截断警告已由研究/实现/check显式补读相关完整章节处理，没有为消除警告重写spec结构。

## 新产物验证
- Build：vswhere定位ARM64原生MSBuild，同一次PowerShell先Remove-Item Env:PATH并设置MSBUILDDISABLENODEREUSE=1。构建InkeysRepo.sln Debug|ARM64、PreferredToolArchitecture=arm64、/m、/nr:false；监督超时900秒。实际exit0，130.11秒，0errors/183warnings。逐诊断与本轮7个增量源码的修改行范围核对，无warning落在本轮新增/修改行；保留既有Setting/Bar/Idt及第三方数值转换等警告，未修改无关代码。
- Config：新Build/ARM64/Debug/Inkeys.exe --config-boundary-test，exit0，2.65秒；PASS明确包含device-based UI scale defaults。精确参数入口在正常启动及任何HWND之前返回，隔离配置位于忽略的TestResults目录，不打开主窗口。
- Headless：新Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window，exit0，106.19秒；Config metadata tests为0 failures，最终PASS animation correctness，stderr为空。
- 产品与Headless二进制时间戳均晚于各自最新相关源码；见artifact-warning-audit.json。日志、process.json与上述audit保存在.trellis/.runtime/ui3-taskbar-dock-followup-build/，与上一轮日志分离。测试TEMP/TMP使用该目录temp，未修改产品写盘器。

## 用例与边界
- Display生产helper固定96/120/144/192DPI对应5/6/8/10px；三方向、autoHide两值、负origin/负bottom、底部Hidden/Raised/ordinary及Unknown/无任务栏回退。
- Config生产分类覆盖无触控、未READY、只有笔、integrated/external touch、未知物理尺寸及面积低于/等于/高于1200。两seed覆盖构造、无文件/缺字段、严格非法bool、Mini选中路径、All、显式两值、Reset、复制/冻结写回、NoUpload及相反用户PaintDevice；RAII恢复全局测试环境。启动预览旧默认断言同步为冻结设备默认。
- 未执行GUI/Computer Use、真实任务栏鼠标升起、触控设备启动耗时、Win7及多屏硬件验收。提前物理枚举可能增加触控机器预览出现前等待；没有额外observer/窗口或重复全量枚举。
- 未commit/push，父子任务保留in_progress，待用户实际效果验收。
