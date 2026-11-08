# Research: 首批低风险 UI / 图片内存优化选择

- Query: 在不改变首用流畅度、设备恢复、L0/L1/L2 与 UI/画布设备分工的前提下，选择最小可实施内存优化。
- Scope: internal；只读产品源码，唯一输出为本文件。
- Date: 2026-11-05
- Task: `.trellis/tasks/11-04-runtime-memory-analysis`；依据最新 prd/design/implement 的授权边界。

## Findings

### 1. 首选：停用三张没有生产消费者的 legacy 全屏 DIB 的主动分配

**结论：当前源码证据足以支持首批最小修改。** 这是删除生产路径无读写消费者的主动分配，不是把它们改为首用/绘制时再分配。

#### 分配和触碰证据

| 证据位置（修改前） | 已确认行为 |
| --- | --- |
| `Inkeys/Inkeys.vcxproj:957` | `IdtImage.cpp` 为 ClCompile，整个TU参与产品链接。 |
| `Inkeys/IdtImage.cpp:4` | 全局 `alpha_drawpad(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN))`。 |
| `Inkeys/IdtImage.cpp:6` | 全局 `tester(...)`，相同整屏尺寸。 |
| `Inkeys/IdtImage.cpp:7` | 全局 `pptdrawpad(...)`，相同整屏尺寸。 |
| `Inkeys/IdtImage.h:6`、`:8`、`:9` | 对应 extern 声明，保留这些声明可避免触及不编译的历史源码。 |
| `Inkeys/Inkeys/Graphics/Surface.cpp:108`—`:111` | 带宽高构造立即调用create，失败会抛出；不是reserve或空占位。 |
| `Inkeys/Inkeys/Graphics/Surface.cpp:151`—`:161` | create构造32-bit top-down `CreateDIBSection`。 |
| `Inkeys/Inkeys/Graphics/Surface.cpp:178`—`:184` | 保存HDC/HBITMAP/像素地址后调用clear。 |
| `Inkeys/Inkeys/Graphics/Surface.cpp:236`—`:238` | clear对整个像素span执行std::fill，实际触碰整幅像素存储。 |
| `Inkeys/IdtMain.cpp:3098`—`:3102` | 正常启动在DPI阶段再次对三张surface做整屏resize；尺寸未变则复用，尺寸变化才重新分配，不能把构造和resize重复计作六张稳态图。 |
| `Inkeys/Inkeys/Graphics/Surface.cpp:188`—`:196` | resize同尺寸且非空时直接返回，否则创建replacement并swap。 |

#### 消费者扫描方法及结果

1. 解析 `Inkeys/Inkeys.vcxproj` 的MSBuild XML，遍历每个含Include的ClCompile项，解析为项目目录内路径，对现存`.cpp/.cppm`内容逐行执行词边界匹配：`\b(alpha_drawpad|tester|pptdrawpad)\b`。本次遍历不先按Debug/Release排除条目，因此覆盖其配置并集。
2. 再对整个 `Inkeys` 下 `.cpp/.cppm/.h/.hpp` 做同名扫描，排除第三方目录，检查声明和不编译的历史文件。
3. 补查 `Inkeys/additional` 和仓库其余自有代码，未找到这些名字的额外引用；没有源码生成器或字符串消费者证据。

生产ClCompile扫描的完整命中仅为：

```text
Inkeys/IdtImage.cpp:4   alpha_drawpad 全屏构造
Inkeys/IdtImage.cpp:6   tester 全屏构造
Inkeys/IdtImage.cpp:7   pptdrawpad 全屏构造
Inkeys/IdtMain.cpp:3100 alpha_drawpad.resize(...)
Inkeys/IdtMain.cpp:3101 tester.resize(...)
Inkeys/IdtMain.cpp:3102 pptdrawpad.resize(...)
```

全仓额外使用仅是 `Inkeys/IdtDrawpad.cpp:2205` 的 `alpha_drawpad.clear()`；该文件在 `Inkeys.vcxproj:951` 为None、不参与生产编译。`tester/pptdrawpad`没有额外读写消费者。其他同名局部`background/graphics`与这三个全局变量无关。

扫描脚本可复核核心：

```python
root = Path('Inkeys')
ns = {'m': 'http://schemas.microsoft.com/developer/msbuild/2003'}
project = ET.parse(root / 'Inkeys.vcxproj')
pattern = re.compile(r'\b(alpha_drawpad|tester|pptdrawpad)\b')
for node in project.findall('.//m:ClCompile', ns):
    include = node.get('Include')
    if not include:
        continue
    source = root / Path(include.replace('\\', '/'))
    if source.is_file():
        for number, line in enumerate(source.read_text(encoding='utf-8-sig', errors='replace').splitlines(), 1):
            if pattern.search(line):
                print(f'{source}:{number}:{line.strip()}')
```

#### 建议的最小补丁边界

- 仅在 `IdtImage.cpp:4/6/7` 将三者改为默认空构造；`Surface.hpp:7`将DibSurface别名指向 `DibSurfaceImpl`，其无参构造在 `Surface.Core.hpp:19` 为noexcept default，HDC/HBITMAP/bits默认空。保留名称、类型、extern声明及历史源码。加一条简短中文注释说明当前Draw3没有消费者，避免legacy全屏缓存继续常驻。
- 在 `IdtMain.cpp:3098`—`:3103` 删除仅服务这三者的resize代码，修改附近过时注释。
- **保留** `IdtMain.cpp:3105` 的现有阶段日志及 `:3106`—`:3107` 的 `LegacySurfaceReady` milestone；其枚举定义在 `Startup.Progress.cppm:21`，本次不调整进度合同或其他阶段。
- 不改有效的 `FreezeFrameWindow`、Setting、Draw3、白板、PPT文档、L0/L1/L2和设备策略；不把大分配转移到后续任意路径。
- `background(576,386)`和对应全局GDI+ Graphics另有旧生命周期耦合，本轮无需顺手删除；直接限制为三张全屏DIB即可。

**逻辑内存影响估算：** 三张合计`3 × width × height × 4`字节，1080p约23.73MiB，4K约94.92MiB。实际分配取决于全局构造/启动时GetSystemMetrics看到的DPI尺寸；对before/after应以相同机器/会话实测，不能直接承诺上述Private Bytes差值。每张还对应HDC/HBITMAP，预期去掉这些对象的创建，但系统句柄统计需分类型核验。

#### 验证建议

- 主会话保存相同构建配置的before EXE/PDB与基线后再实施，完整`InkeysRepo.sln Debug|ARM64`构建。
- 对照ClCompile扫描确认三者不再有主动分配和生产消费；确认三个默认构造对象均empty，主启动无resize。
- 运行已有真实产品CLI模式前后对照计数器：它们仍经过C++全局初始化，故可观察这三张DIB的移除，而不必启动主GUI；完整冷启动数据另列。
- `InkeysHeadlessTests.exe`不应假设自动链接`IdtImage.cpp`或拥有这三张全局DIB，本次只把它用于相关现有功能回归，不用它的进程差值冒充产品收益。
- 保留原文件编码/BOM/CRLF；验证startup进度、纯CPU历史与离屏字体/绘图入口仍能正常退出，必要时再扩展真实窗口验收。

### 2. Setting CPU BGRA cache：技术上可改为资源配方，但不属于本轮无需确认的最小低风险项

#### 当前所有权

- `Setting.Base.cppm:24`：14槽 `std::vector<unsigned char>`持久保存解码后的CPU BGRA；`LoadTextureFromMemory`在`:165`复制数据，在`:186`创建immutable BGRA texture，SRV持有texture。
- `Setting.cpp:2173`、`:2207`、`:2241`、`:2275`、`:2310`、`:2344`、`:2371`、`:2383`—`:2400`、`:2403`加载PNG资源并进行反预乘或白色alpha遮罩转换。
- `Setting.cpp:687`—`:737`的槽0来自运行时窗口HICON，非PNG资源，64×64原始缓存仅16KiB；不能统一改为PNG资源ID。
- 唯一重建消费者是 `Setting.Base.cppm:204`—`:215` 的 `RecreateSettingTextures`；由 `Setting.cpp:5397`—`:5411` 在epoch变化时调用。普通Show/Hide不重解码，渲染/输入不读CPU vector。
- `Setting.cpp:5343`—`:5351`的Hide仅释放presentation，字体/SRV/CPUcache仍resident；本次不改此策略。

#### 是否能够保留压缩源而删除解码副本

**技术上能，但要为每槽保存完整、稳定的重建配方：** module/resource类型与名称或数值ID、初始化时实际解码目标宽高、像素变换类型（反预乘/白色alpha遮罩），以及槽0的独立raw恢复。当前PNG资源本就在进程映像里，可以保存ID/指针/长度，通常无需另复制压缩字节。两张atlas的原始文件共108,308 B（49,450 + 58,858），而CPU BGRA共7,962,624 B，说明压缩源保活有实质潜力。

但现在恢复路径只是上传已转换BGRA；改配方后将增加GDI+解码、DIB分配/触碰、像素转换和临时存储。`Surface.cpp:308`—`:355`的现有资源加载还会复制压缩源到HGLOBAL并创建流；`IdtImage.cpp:95`—`:99`经全局`loadImageSm`串行化。若重建仍在shared render owner，会影响恢复完成时间；如果错把配方执行放进普通Show/帧渲染，则直接违背当前R5。

此外必须保留相同GDI+解码/缩放/预乘精度，不能仅换stb解码后假定逐字节一致；仍须覆盖资源读取失败、OOM、optional槽12/13失败降级以及必需槽恢复失败重试。`vcpkg.json`未直接声明zlib/libpng，`imgui_draw.cpp:4063`的STB_IMAGE_WRITE_IMPLEMENTATION被`#if 0`禁用，不能把搜索命中当作已有可链接压缩器。不要为本轮临时加入新依赖或Win8+压缩API而改变Win7兼容。

**本轮建议：保留当前CPUcache及恢复流程，记录为需用户确认的下一批候选。** 不能仅删除`assign()`，否则device epoch恢复将丢失图片来源。

#### 更小的可选峰值优化

初始化中先造转换buffer、再由LoadTextureFromMemory复制到cache，会短暂多留一幅BGRA。两张atlas现有代码（`Setting.cpp:2390`—`:2400`）使用临时vector，理论上可直接填最终cache后调用原函数（指针相同已有跳过assign分支），或设计移交vector所有权的窄接口，以消除一份临时副本而不改变恢复路径。它只降低初始化局部峰值，最大单幅atlas约3.80MiB，**不降低常驻cache**，收益远小于三张死DIB；本轮无需为了凑优化数量扩展到这类改动。

### 3. Freeze：有效消费者存在，不能与死DIB一起处理

`IdtFreezeFrame.cpp:62`的`freeze_background`确实启动即创建主屏`W×(H-1)`，但`:127`、`:165`、`:195`、`:231`等为Recall/PPT提示清屏绘制及ULW提交，属于有效生产路径。减小表面需要同步调整HWND bounds、文本/图标坐标、全屏暗幕和白板共享所有权，超出本次最小diff。按需分配又会把一整屏的分配/触碰移到首次通知/冻结，用户已要求这类首用代价留待确认。

**本轮保留Freeze DIB，不与alpha_drawpad/tester/pptdrawpad混淆。** 白板背景target、Bar/PageControl高水位释放、Setting延迟初始化、PPT托管延迟激活同样保留下一轮确认。

### 4. 已有无需桌面交互的相关入口

| 入口 | 当前证据/适用范围 |
| --- | --- |
| `Inkeys.exe --draw3-history-benchmark` | `IdtMain.cpp:1960`：进入CPU历史probe，不需主GUI；全局初始化仍会执行，适合观察本次死DIB移除的进程差值，历史规模和保存噪声须单独记录。 |
| `Inkeys.exe --setting-font-lifecycle-test` | `IdtMain.cpp:1879`、`Setting.cpp:5749`—`:5875`：离屏WARP target与真实字体生命周期；能检查改动未破坏通用初始化，不能测试Setting全部图片恢复。 |
| `InkeysHeadlessTests.exe --no-window` | `animation_tests.cpp:1761`、`:1799`：禁用窗口测试并先设置CRT/系统错误对话框屏障。 |
| `InkeysHeadlessTests.exe --no-window --setting-font-benchmark-only` | `animation_tests.cpp:1721`—`:1723`：已有离屏字体基准，支持locale/scale过滤；不是全产品memory baseline。该早期入口在通用参数解析/CRT错误对话框屏障之前返回，`--no-window`并不在此入口额外设置屏障，应由runner记录异常/超时。 |
| `InkeysHeadlessTests.exe --no-window --setting-eraser-demo-only` | `animation_tests.cpp:1757`、`setting_eraser_demo_tests.cpp:390`：演示图像/离屏WARP目标专项；不能替代整个resident图片cache恢复测试。 |

没有发现现成可完整采样Setting所有14槽cache和device恢复的独立入口；若下一批做压缩/配方，需真正的离屏恢复+像素等价覆盖，不能把字体/演示局部通过当作整体图片恢复通过。

## Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md`
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`
- `.trellis/spec/native-desktop/startup-preview.md`
- `.trellis/spec/native-desktop/build-and-compatibility.md`
- 本任务`prd.md`的R3/R4/R5/R6/R8及`research/ui-memory-map.md`。

## External References / Versions

- 未使用外部文章推断当前行为；结论来自本地MSBuild项目、C++源码与资源文件stat。
- 当前分析基于2026-11-05执行前文件，主会话正在采before；实现后行号可能变化。

## Caveats / Not Found

- 本子任务不改产品源码、不构建、不运行测试、不启动窗口、不操作Git；before/after实测由主会话完成。
- 主建议是三个默认构造+移除三行resize，其他有首用/设备恢复代价的方向均不应自动并入首批。
- 逻辑DIB像素量不是已实测进程Private/WS下降值；需要保留同构建、同DPI会话的实际计数器证据。
