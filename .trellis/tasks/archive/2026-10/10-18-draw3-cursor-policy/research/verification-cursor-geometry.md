# 荧光笔边缘与笔形圆角：本轮验证（2026-10-19）

需求为 [cursor-geometry-revision.md](cursor-geometry-revision.md)，补充上一轮两类型/固定设备规则。本轮代码已完成并通过本机编译和无窗口回归；Win7 真机与 GUI 像素验收仍未执行，Trellis 任务保留 in_progress。此前 verification-cursor-revision.md 仅证明上一轮代码。

## 本轮结果

- 显式高亮荧光笔左右边缘由1.25 DIP减为0.625 DIP，上下保持1.25 DIP，外框和填充0.35保持。Mouse/Digitizer/Unknown/Conflict仍高亮；ScreenPen非高亮、legacy默认编码及其它光标继续原内轮廓公式。
- PenIcon仅笔侧/笔尾四个非尖端角使用内切圆弧，24 DIP图标目标圆角半径1.5 DIP并随DPI等比缩放；尖端、方向、热点、外框/dirty描述保持。切点最多占相邻边1/4，极小边/退化半角保留旧轮廓。
- 新圆角数学只在type21专属branch内执行；避免本轮使圆形、荧光笔和橡皮光标连带运行四角计算。该结论来自编译后控制流核对，不是性能测量。
- 与本轮baseline相比，产品源文件仅改正式inkPixelShader.hlsl与同步生成的Draw3.Win7GuardedShader.h。PenCursor、WindowControl、RendererPrimitives、VS的基线SHA不变，没有新增配置/UI/公共接口，输入和墨迹/历史没有改动。

## 验证证据

| 项目 | 本轮实际结果 |
| --- | --- |
| 独立 Trellis check | check-cursor-geometry.md 已完成；核对4角圆心/半径/切点/角扇区、尖端、coverage/alpha、源码基线、编译分支与guarded身份，未发现阻断问题，checker未改源码。 |
| FXC参考与兼容生成 | 动态发现SDK ARM64 FXC，ps_5_0 /E main /O3正式参考PS、完整guarded编译和反汇编均退出0；生成器未改，正向门仍>21。正式参考/guarded的ISGN、OSGN、RDEF一致；DefaultInk原/还原尾部均84B1C4CF3F07A25ACBB5B9C081A91B2269AA62C47B67A0AA83BFB049BD2096A5，与前轮相同。25条derivative/discard最小guard深度1。 |
| 完整Solution | 动态定位VS ARM64原生MSBuild，同一PowerShell invocation规范化PATH并禁node reuse，InkeysRepo.sln Debug\|ARM64 /m:1 /nr:false，600秒上限。首次源码编译/FXC完成、增量链接器内部崩溃退出1；保留单个.ilk后相同配置完整链接退出0，最终error行0、stderr 0 bytes。主程序、PptCOM、PptCOM.Tests、headless均生成。 |
| Headless | 本轮完整构建后运行InkeysHeadlessTests.exe --no-window，300秒上限，退出0；Config metadata tests: 0 failures，最终PASS animation correctness，stderr 0 bytes。headless产物SHA与上轮一致，与本轮未改测试/CPU来源逻辑相符；本次仍实际执行，未沿用旧通过记录。 |
| 最终EXE数据核验 | 仅LoadLibraryExW AS_DATAFILE\|AS_IMAGE_RESOURCE数据映射，无入口执行。SHADER301–304逐字节等于当前项目生成的四份CSO，均DXBC；guarded36380字节在EXE恰出现一次，SHA声明匹配。实际产品PS的ISGN/OSGN与兼容体一致。 |
| 实际产品编译控制流 | 对项目实际生成的inkPixelShader.cso另行dump，已确认type21比较后进入独立if、四角运算在该分支内，排除仅参考/O3产物隔离而实际工程平铺的遗漏。证据actual-production-ps.asm。 |
| 静态/格式 | 本轮PS与guarded保持原UTF-8 BOM/CRLF；其它4份基线源SHA不变；JSON/JSONL/引用、文档空白与git diff --check检查。task.py validate退出0，implement/check各8个条目；长input spec的截断提示按主动读取完整相关Win7章节处理。 |

配置、设置、翻译和生产工具投影源码均未变化，因此没有重复i18n生成、状态CLI或pen-width CLI，不把它们上一轮的通过写成本轮执行。没有新增复制生产数学的CPU测试；FXC/代数审查与headless不构成GPU像素验收。

## 编译与验证假设的处理

本轮再次复现MSVC link.exe 14.44.35229.0的C0000005 / LNK1000（IncrBuildImage）。第一轮solution退出后核对Build/ARM64/Debug/Inkeys.ilk的绝对路径，将该单文件移入本轮verification目录保留，然后完全相同的solution命令完整链接成功。没有改源码/项目来处理环境错误，也没有关闭增量链接、改debug/PDB、升级依赖/工具链或清理其它缓存。已有pwsh.exe查找提示仍保留在后处理输出，最终MSBuild退出0。

资源核验最初误将显式/O3参考PS与项目实际输出强制同SHA而触发断言；核对fxc.command.1.tlog后确认项目命令没有/O3，不能要求不同编译选项的容器字节相同。修正的是验证假设：资源应精确等于各自当前CSO，同时单独核实际产物签名与branch。没有修改项目选项或替换产品CSO来迎合参考SHA。实际项目PS为DBBFF61A…895760E，参考/O3为D6C9BE02…656E31E，完整身份分别保存。

### Trellis break-loop复盘

1. **分类与证据**：这是已观察到的工具链/增量状态边界，不是shader或C++源码编译错误；对应隐含假设是“上次完整链接后，下一次增量状态一定可复用”。两轮均在IncrBuildImage崩溃，保留.ilk后同配置完整链接成功，但尚未证明缓存损坏或链接器内部根因。
2. **为何复现**：此前处理只恢复当次构建，没有修复工具链；不能将本轮相同现象解释为圆角源码错误，或继续盲改源码/签名/项目。
3. **处理机制**：保留第一条有意义的错误、工具版本、日志与单文件缓存；等待当前构建退出后核对路径，只处理本地生成.ilk并重链接，检查实际退出码。若完整链接也失败，应重新按首个错误定位，不能无限重复。
4. **范围**：没有据此扩大到安装/升级VS、全局配置或其它项目；本轮源码边界仍是两份shader文件。
5. **知识记录**：本报告保留本机环境复现；draw3-cursors spec补充按实际编译选项区分参考/资源身份，避免类似验证误判。

## 日志与产物身份

本轮本地ignored目录：Build/verification/cursor-geometry-20261019/。

- baseline/与baseline-hashes.json：修改前PS/guarded及相关来源文件身份。
- build.stdout.log / build.stderr.log：首次退出1的增量链接错误。
- Inkeys.failed-incremental.ilk：对应失败缓存，未递归删除。
- build-full-link.stdout.log / build-full-link.stderr.log：相同配置最终退出0。
- headless.stdout.log / headless.stderr.log：本轮完整--no-window退出0。
- actual-production-ps.asm、shader-resources.json：实际资源控制流与EXE数据映射证据。
- FXC生成证据另在Inkeys/Cache/CursorGeometryShader/generated/，源数学/签名/guard检查细节见implementation-cursor-geometry.md。

最终SHA256：

| 产物 | SHA256 |
| --- | --- |
| ARM64 Debug产品EXE | 19062359DA4F0A35F28193AC8868083E799D26265BC513253B30A5D052935AB7 |
| ARM64 Debug headless EXE | 1B506A348C917E7B7313F82836007438B8C73821C30BC4D7BBEB7B8912557522 |
| 正式PS源 | CDB41DD4DED5B230154BE2E04566365058604386551E15F3D2BB73A330AC0E0C |
| 实际项目PS CSO，36148 bytes | DBBFF61AD1FFD911F97735E52ECC3AEAC7A0EE14218E39A84EB8ECEDB895760E |
| /O3参考PS CSO | D6C9BE02F6F238B5BD6D9FC2D21A0E72C1444492C838C49E1B98DC78C656E31E |
| Win7完整guarded PS，36380 bytes | F61BFB9DE4A86157037588D3DEEB62185318BA8AEDD83C20A625EC40EFC09327 |

新兼容字节身份已变化，旧Win7现场验收不能沿用；本机ARM64 Debug EXE不是Win7运行包。没有启动应用/设置窗口、HWND或GPU harness、Computer Use，没有commit/push。高亮实际像素、圆角观感、Win7 WARP以及原双光标/首触/抬笔残留仍待目标设备视觉验收。
