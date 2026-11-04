# R1/R4/R5 原生实现与验证记录

## 修改边界

原生 worker 修改 `Setting.cpp`、`Setting.Base.cppm`、`Setting.SessionState.h`、`setting_session_state_tests.cpp`。产品文件保持原BOM/CRLF，无GUI、无commit/push。Setting.cpp已交还主代理，主代理后续接入页面/条件块动画；本文描述原生部分，不代表最终完整方案已全部验收。

## R1 开屏入口

- 将既有 `ConfigureHomeMotion` 和 `CancelHomeEntryOnInput` 提前到自绘标题栏items之前，仅移动状态计算，不另造时钟。
- 使用主代理提供的 `HomeEntryChromeVisible`：Pending隐藏，Playing在遮盖层淡完前隐藏；普通开屏在SettleStart、fadeOnly在WritingEnd+Dock之后可见。取消帧继续禁点穿。
- 四项业务入口由同一if控制真正的item登记/绘制；窗口icon/title/caption按钮保留。原生HTCLIENT命中使用共享atomic可见性；真正重开时在首帧前先复位，避免旧会话的可操作区域泄漏。
- 现有source boundary回归核对生产顺序及真实门禁接线。逐帧实际HomeMotion/DrawData覆盖由主代理统一完成；不能只用source字符串声称视觉验收。

## R4 已证明的冗余工作

- 原 `QueueResize` 对同尺寸每次增加serial；现在同尺寸不新增事务，但原来尚未消费/失败的serial保持pending。
- 初建swapchain使用HWND自动尺寸，原下一帧可能对相同目标再次ResizeBuffers并重建RTV。现在CreateRenderTarget成功时记录真实backbuffer GetDesc尺寸；只有RTV存在且尺寸明确相同才复用。
- 资源实际事务为小型 `ResizeSettingPresentation` callback边界：相同有效RTV直接S_OK，否则Release→ResizeBuffers→CreateRenderTarget。失败、缺RTV、尺寸未知均不能命中快路径；CleanupPresentation归零实际尺寸，new epoch不复用旧资源。
- 原生Size消息原本已避开ImGui IO锁；只有Move暂停，Size保持真实render/Present。本次没有把Size改成Idle，也没有更改共享设备/交换链模式。

## 独立ARM64无窗口探针（2026-11-02）

位置：`Build/settingui-native-probe/`，命令 `python Build/settingui-native-probe/compile.py`。通过vswhere定位当前VS，动态建立ARM64 CL环境；没有工程/依赖配置修改。三次编译和对应执行均成功退出。

1. `mailbox-baseline` 编译并调用从当前HEAD提取的原始生产SessionState头。1000条消息、100个真正尺寸目标，每条消费一次：1000次resize事务。
2. `mailbox-current` 编译并调用修改后的生产SessionState头，相同输入：100次事务；两者全过程仍render，不重建字体/设备。
3. `resource-current` 调用**实际生产** `ResizeSettingPresentation`，回调内部创建真实D3D11 WARP Texture2D/RTV，不创建HWND。1000次请求、99次尺寸改变（首次资源已为初始目标）：旧始终重建策略为1000次Release/resize回调/CreateRTV；新策略为99次。缺RTV、未知尺寸、resize失败、RTV创建失败、同目标重试及独立新WARP device场景全部通过。材质事务回放也通过。

原始日志：`mailbox-baseline.log`、`mailbox-current.log`、`resource-current.log`和相应compile.log。采样CPU时间分别44µs、48µs；资源回放旧17998µs、新44293µs，单样本噪声较大，**不据此宣称性能加速**。计数证明消除了重复事务；测试中的resize回调是WARP纹理分配，**不是DXGI ResizeBuffers或DWM耗时**。不能据此推导Windows7实机主瓶颈或流畅性已解决。

完整Headless新增真实生产事务边界测试：`CheckSettingBackdropTransactions`、`CheckSettingPresentationTransactions`、`CheckSettingResizeWork`，覆盖上述重试与serial边界。主代理随后执行solution build与完整无窗口回归。

## R5 材质连续性

- 原生owner拥有 `SettingBackdropState`，同HWND/能力/主题的成功状态直接复用；调整已启用的accent时直接覆盖，不再先Disabled再Enable。
- 真实DWM composition、WM_THEMECHANGED及HWND销毁使缓存失效；能力/高对比度改变会改变eligible。启用失败撤销并返回Solid，不把失败缓存成成功，下一等价请求仍可重试。
- 普通主题消息不再强制重复提交相同frame margin；composition变化仍显式force，最大化/还原几何仍照常同步。
- 渲染线程保存 `settingPaletteBackdropMode`，与ApplyPalette同批更新；帧内根背景/开屏缓存键读取该冻结值，避免native先发布新backdrop、当前palette尚未应用时出现失配帧。
- callback回放验证首启E、重复请求不再调用、主题直接E而无D、失效重验、失败ED和下一次恢复E；它证明生产事务顺序，不证明系统compositor原生动画的视觉效果。

## 未验证和不能扩大结论的范围

未运行真实Win7窗口resize、Win10/11动态背景最大化/还原、系统DWM动画或实际交换链resize耗时。没有GUI授权。以上两项仍需实机视觉/性能验收；主代理可以继续以同源受控采样定位其他成本，不能把重复事务消除视为所有卡顿或跳变的完整根因。
