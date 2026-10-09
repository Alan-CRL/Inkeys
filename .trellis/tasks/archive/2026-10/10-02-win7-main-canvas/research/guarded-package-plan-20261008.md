# Win7 原版/守卫候选单CMD验证包（2026-10-08）

继续原任务。用户要求下一步可实机验证，并明确 shader 效果必须保持。用户授权本次实现/打包；不 commit/push/archive，不启动本机交互 GUI。

## 行为缺口与最小边界

真实87项已经证明 DefaultInk 程序群触发旧 Win7 WARP 的共享PS透明输出。需把数学完整的正向守卫候选用于实际画布，比较同EXE原版与候选，并补真实ink和非零Destination验证。不是删除功能、细笔、光影或更换后端。

生产HLSL/正式8CSO/SHADER301–304不变；增加一个从正式源可追溯生成的guarded PS字节数组，仅诊断环境明确开启时供InkRenderer选择。未开时仍加载正式资源；不按OS自动开启，不持久化配置。原始日志与所有旧包保持。

候选从完整正式源生成：所有helper、功能block、距离/AA/thin数学、SV_Position真实中心及双源公式保留。将DefaultInk输出尾部移入正向block。相对于上轮临时候选，应一并保持未定义未知type的旧d=0输出：正向条件覆盖0、3及type<0/type>20，其他已处理0..20类型均已提前返回。不要以透明fallback静默改变未知type行为。若编译器将尾部再次合并到无guard，不能仅凭源文本视为达成。

## 运行合同

- `INKEYS_DRAW3_WIN7_DIAGNOSTICS=1` 与 `INKEYS_DRAW3_WIN7_GUARDED_SHADER=1` 均严格为单字符1时才选择新PS。变量仅子进程局部，wrapper finally恢复调用者原值。
- 初始化日志记录实际 original/guarded及字节哈希；CreatePixelShader失败按原初始化失败处理，不静默回退冒充guarded成功。
- 同一EXE三stage：01-pixel-probe（明确清除guarded/full变量）；02-original-shader（原版、full unset）；03-guarded-shader（guarded=1、full unset）。ULW、input、swapchain和scheduler不变。
- 独立pixel probe保留既有87项原版和69matrix，追加数学完整guarded对照，不嵌入NoThin/FullWithoutDefaultInk作为正常应用替代。旧自检中的删除群变体仍只在隐藏诊断fixture执行，记录用途，不成为候选应用。
- 追加真实type0/3、细笔/变半径、known nonzero Destination cursor/eraseRetain；Win11同fixture比较原版/候选像素及覆盖，Win7原版零输出时只能记reference unavailable，不能把不可比图像误报equal。精简控制/原版反向复验保留。
- 新probe_schema、capture schema、manifest packageid和明确case计数同步；Hardware初始化unavailable保持严格分类，PS2 API保持。原版FAIL预期仍诚实记录，候选结果独立摘要；不得把完整性当视觉通过。
- 包只有一个CMD。两轮新测试页先普通笔/细笔/荧光笔，再橡皮/激光、撤回/重做、选择穿透、定格返回；观察明确区分原版/候选，回传完整目录。

## 所有权

1. trellis-implement GPU：Draw3.Renderer.cpp、Draw3.HiddenWindowTest.cpp，以及新guarded生成器/头/研究报告。可增加最小必要fixture辅助；不改工程构建系统、正式HLSL/CSO或IdtMain argv入口。选择probe计数后及时告知collector implementer。
2. trellis-implement collector：research/Collect-Win7.ps1、Run-Win7-Diagnostics.ps1、现有唯一CMD（仅必要时），新README/collector报告；不改C++/shader。
3. 主会话：任务/spec/context、构建/运行无窗口测试/打包与身份、原资料保存、最终交付。
4. trellis-check：全改动独立审核、生成数学不变、实际DXBC/signature、环境开关/阶段/采集PS2合同及质量门；发现机械问题可在当前所有权协调后修正。

## 验证与交付

- 读取原格式并保持BOM/CRLF，检查minimal diff。Formal HLSL/8CSO/旧header arrays/旧ZIP/原21files记录SHA前后一致。
- 定位现有VS ARM64 MSBuild；同PowerShell规范PATH，完整InkeysRepo.sln Debug|ARM64（900s），候选Release|x64（900s）。不单独构建Inkeys.vcxproj。
- 两架构独立WARP/Hardware像素CLI，不显示fixture窗口、不加载正常应用配置；Headless --no-window。
- 检查正式EXE资源字节保持；候选sameexe原/guard选择日志、候选ASMdcl/signature、所有原数学块文本/token身份；Win11图像回归不能代替Win7验收。
- 实际Windows PowerShell新EXE ProbeOnly和mock wrapper（原版/候选变量、父环境恢复、失败继续、完整raw、身份拒绝）验证；禁止真实本机普通GUI。
- 新独立输出目录/ZIP/diagnostic-manifest，版本来源、EXE/ZIP/source SHA、预期case数、唯一CMD、CRC/逐文件核验。Win7恢复与效果最终待用户实测。
