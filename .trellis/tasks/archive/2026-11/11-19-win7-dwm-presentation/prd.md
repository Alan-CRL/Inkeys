# 第 2 步 D · Win7Dwm 透明呈现

## 目标

让 Win7 SP1 + 仅 KB2670838 的 x86、x64 用户能在 Aero 合成正常时手动启用主画布 GPU 上屏，停笔后仍能看到下层 PPT 或视频实时更新。

## 性能页与启动行为

| 设置 | 默认值 | 选项 |
| --- | --- | --- |
| 桌面画布透明呈现方式 | 自动（推荐） | 自动（推荐）、DirectComposition、Windows 7 Desktop Window Manager、UpdateLayeredWindow |
| 桌面画布交换链 | DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL | DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL、DXGI_SWAP_EFFECT_SEQUENTIAL |

两个下拉框均在重启后生效。自动维持 DComp → ULW；初始化、运行期恢复和首次启动均只有显式选择 Win7Dwm 才可进入它。DirectComposition 与 Win7Dwm 均按交换链偏好尝试创建。ULW 使用阶段 B 离屏纹理。

## 验收

1. 新装获得表中默认值。旧 EnableDirectComposition 为 true 映射到自动，为 false 映射到 ULW，新配置优先，非法值回到该项默认。
2. Win7 Aero 满足透明条件时可手选 Win7Dwm。停笔时下层 PPT/视频继续更新，实心黑笔、半透明荧光笔、擦除与指针移动正确。
3. 主画布常规 Win7Dwm 提交无需 staging/Map/DIB/ULW，设备帧队列申请上限 1。保留输入命中、辅助呈现窗口及其隐藏回执。
4. FLIP 创建失败后可一次尝试 SEQUENTIAL；合成关闭、平台不支持或可用链均失败则用 ULW。软件调试页的画布初始化信息块只显示当前状态。标题下方以独立黄色信息条显示手选呈现方式的回退原因和交换链降级原因。自动选路回退不产生呈现方式警告。保留阶段 C 旧统计。
5. 本轮无视觉探针、首次启动试用、OOBE 询问或测试，也不写视觉确认 Memory。代码无法判定的黑底、冻结、拖影由用户手动切换设置；Win7 实机验收安排在实现之后。

用户已批准实施；Win7Dwm 下是否禁用 FLIP，等待用户实机反馈。
