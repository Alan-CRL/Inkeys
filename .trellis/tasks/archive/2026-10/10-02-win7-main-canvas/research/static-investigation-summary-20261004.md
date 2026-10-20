# Win7 晚间实测前静态定位汇总（2026-10-04）

## 结论

本次静态调查没有找到可以证明当前故障唯一根因的生产代码错误。已将 USER32 ULW 参数和普通窗口显示路径进一步降为次要解释，重点仍是Win7 WARP的GPU数据/实际执行/输出，以及swapchain backbuffer→staging的拷贝读回。RTS/controller/document成功不是GPU非透明像素写入的证明。

这不是重述“还需要日志”：本轮完成了原独立仓库对照、编译shader执行字节比较、资源与owner时序审计，以及FLIP声明/实际创建/ULW实际呈现路径的分层核验。所有新的结论都有可追溯报告，晚上使用已经交付的同一个修正包即可，不增加包版本。

## 新得到的静态正反证据

1. **原Demo核心GPU→ULW并没有因产品接入被换成另一实现。** 找到原仓库D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3，main ref8d045298。graphics_initialization、transparent_presentation、renderer、renderer_primitives及main.cpp与当前本仓Demo逐字节一致。产品虽有外部双窗Host、两套CPU staging/DIB和更多诊断，但FLIP描述、GetBuffer(0)/RTV、dirty CopySubresourceRegion→blocking Map、Primary alpha底层和ULW blend/dirty合同沿用原实现。产品未将UI3共享device/context混进Draw3。
2. **纯USER32最后一步不能独自解释现在的观测。** source统计在CPU访问mapped staging时累计，Unmap后才调用ULW；日志虽然在ULW返回后打印，值早已保存。因此USER32接受却不显示、DIB刷新或窗口Z序不会倒过来把这些源统计变零。
3. **缺少Present1/显式Flush不等于读到了尚未提交的Draw。** 当前ULW从backbuffer复制/阻塞Map，不执行DXGI flip present；Microsoft说明Map是runtime自动提交命令的时机，Flush本身还是异步。不能凭缺少Flush就静态宣称修复，源RTV未解绑也没有被官方Copy合同判为非法。
4. **实际故障EXE的shader身份已直接核对，未发现profile或执行指令升级导致的共同回归。** 用datafile方式只读旧06CD58和新2D468B EXE的SHADER301–304，四个嵌入shader与当前CSO全字节相同；核验VS/PS/两个CS均为5.0profiles，产品与当前本仓Demo的DXBC执行/输入输出签名chunk全部相同。部分metadata/原PS极细笔覆盖差异不能扩展成橡皮32px和Laser也失效的证据。此证明限shader字节，不宣称旧故障EXE全部C++与当前源码相同。
5. **GPU资源基本合同与同线程owner没有发现新的破坏。** 最终源和staging创建为BGRA8、single sample；源目标canonical identity现场匹配，dirty/copy坐标在界内。Graphics、renderer、两套ULW均使用同一独立immediate context；窗口线程只发布resize/full请求。现场稳定1920×1080并有首次full提交，未出现与透明采样对应的恢复/resize失败。
6. **消失的两类瞬态visual绕过history/L1/L0，进一步指向公共绘制路径。** LaserTip直接使用与Cursor相同的t0/b0、VS/PS、resolve blend和目标RTV；它不依赖document烘干。因此仅CPU历史或笔画层级的故障无法单独解释橡皮与激光tip一起没有像素。绑定正常尚未证明GPU读取的t0/b0字节正确。
7. **FLIP问题须区分文档声明与本项目事实。** 官方D3D11 DXGI FLIP_SEQUENTIAL声明Windows8起支持；Win7平台更新提供部分DXGI1.2。现场却是WARP普通创建成功且实际GetDesc1返回FLIP。当前ULW不调用Present1，因此这些证据证明“该交换链对象已创建”，还没有回答其backbuffer的Clear/Draw/Copy是否正确，更没有证明DXGI FLIP→DWM直接呈现链。保留用户旧Win7 ULW实测事实，不以OS标签改BLT。

## 为什么原Demo成功不能直接排除当前Win7 WARP边界

当前现场明确Hardware失败后WARP/FL11_0。找到的原仓TestResults日志是Qualcomm Hardware/FL11_1及DComp/DWM，不能对应用户当年Win7 ULW绿色测试的EXE/driver/模式。没有证据否定用户实测，也没有证据把本次读取的原源文件、已有EXE文件名或时间等同于当年那个已测版本。

原Demo同路径加上Win11 ULW正常，降低“产品迁移把通用ULW参数写错”的优先级；它们不会证明不同runtime/driver下GPU数据加载或backbuffer读回都等价。

## 当前假设优先级和可证伪条件

| 范围 | 当前证据及优先级 | 现有晚间自检能如何分界 |
| --- | --- | --- |
| Win7 WARP绘制数据/执行/输出/正式混合 | 高：上传/bind正常但Draw(void)不证明有效VS/PS输出；编译指令未漂移 | 两Clear成功而正式Draw失败；看pipeline计数与两个关闭blend比较 |
| Win7 swapchain-backed资源/Copy/readback | 高：只有staging零，尚未直接建立已知色读回正证据 | 普通offscreen Clear正常但backbuffer Clear异常将直接提升此项 |
| 生产shape6/尺寸/dirty拷贝和同步相对独立基准的差异 | 次一级：当前自检是Circle128×128/fullCopy/显式event，实际是Grip1920×1080/dirtyCopy/blockingMap | 六例全部成功后才做上述单变量对照，不先改生产行为 |
| DIB/USER32 dirty/桌面可见性 | 低于上述边界：它发生在已保存源统计之后 | 只有先取得非零源/DIB而桌面仍不可见，才直接验证最后一步 |
| 交换链销毁/窗口恰好resize/通用CPU_MISC描述歧义 | 单列观察，不是当前根因 | 无现场触发或当前失败正证据；不为猜测改今晚候选 |

动态structuredbuffer CPU_WRITE与MISC_BUFFER_STRUCTURED的泛化文档描述有歧义；当前创建/Map成功、原Demo相同。规范/硬件实际行为没有建立当前红→绿，不能把它称为已定位的缓冲区缺陷。swapchain释放后Flush、局部提交时DIB重建的生命周期风险同样缺少本次触发证据，只保留在专项报告。

## 关联报告与验证

- [原Demo→产品对照](demo-product-ulw-static-20261004.md)
- [Copy/Map、线程与ULW生命周期](copy-ulw-lifecycle-static-20261004.md)
- [shader与Win7合同](shader-win7-contracts-static-20261004.md)
- [DXGI FLIP证据边界](dxgi-flip-boundary-static-20261004.md)
- [独立结论审查](check-static-investigation-20261004.md)

本轮为静态读源/字节码/API合同调查，不运行编译或GPU/GUI；既有源码/脚本和候选包按baseline hash核对保留。此前修正版的两配置build/Headless/本机WinPS真实自检证据仍归前一轮，不冒充本轮新测，也不能代替Win7。

任务仍in_progress，未commit/push/archive。晚间只运行已有新包的Run-Win7-PixelTest.cmd，取同一Win7设备上的Clear/Draw/混合分界。当前不能保证它一定给出唯一原因；若六例均PASS，已明确下一层生产差异，不重复查输入或猜测更换后端。
