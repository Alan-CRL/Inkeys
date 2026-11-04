# R3 颜色动画终点修复与回归

## 根因与修改

真实 `NavItemEx` 和 `Button` 都复用 `imfluent.cpp::AnimateColorU32`。旧实现每帧把插值输出取整为 U32 再作为下一帧起点；高刷新率下单帧变化小于半个色阶时永久停滞，透明导航背景残留数个 alpha 单位，普通按钮也不能准确恢复目标色。

最小修复只改 `AnimateColorU32`：在既有窗口 StateStorage 中保存本次目标、起点与累计时间。目标改变时以当前可见 U32 色重定向，保持指数曲线和原 seconds=.083；固定目标持续从原始起点按累计时间计算，不再反复反馈每帧取整误差。目标变化先重置状态，再判断是否已在端点，防止快速变化误复用旧时钟。缺席帧重新提交仍直接归到真实目标。

原 `LerpColorU32` 完全保留；RGB 按 alpha 加权，输出仍为普通 RGBA。没有强制不透明底色、关闭悬停动画、改变目标色或增加全局状态/线程。

修改文件：

- `Inkeys/additional/imfluent/imfluent.cpp`：12 行新增、3 行替换。
- `Inkeys/additional/imfluent/UPSTREAM.md`：第10项记录累计时间补丁与回归合同。
- `InkeysHeadlessTests/setting_design_tests.cpp`：新增 `CheckAnimatedColorEndpoints`，在既有设计集的100%与187.5%两倍率调用。测试采用真实NavItemEx/Button、鼠标事件与最终DrawData采样，不读取颜色动画私有storage键。

## 验证证据

完整 Solution 构建由主代理统一执行，避免共享文件并发构建。

先完成同一回归的无 HWND 独立 ARM64 探针：从源文件提取未改写的生产 palette/ApplyPalette 与精确的新回归函数，直接编译仓库真实 ImGui 四个cpp和ImFluent实现。未复制/替换被测动画算法。探针使用默认内置字体，仅验证颜色与交互，不宣称字体或桌面背景效果已验收。

- 编译器：当前 Visual Studio 动态定位的 `HostARM64/ARM64/cl.exe`，C++20。
- 修复前：16 cases / 8,832 frames，退出码1；旧实际NavItemEx移出后残留alpha，30/60/120Hz分别可见1/2/5单位，高刷新率还可能更高。普通按钮终点断言也失败。日志 `Build/settingui-color-probe/baseline.log`。
- 修复后同一100%回放：16 cases / 8,832 frames，退出码0，日志 `Build/settingui-color-probe/fixed.log`。
- 修复后100%+187.5%：32 cases / 17,664 frames，退出码0，日志 `Build/settingui-color-probe/fixed-two-scales.log`。
- 每种倍率覆盖 Solid/半透明 × 浅/深主题 × 30/60/120/240Hz。断言实际hover/press/release/移出到准确透明端点、选中填色保留、普通按钮hover/default到准确目标、中途反向、缺席帧重开、透明黑到浅白的中间帧保持白色而不发黑。
- `git diff --check` 对本子任务文件通过。保留各文件无BOM及原有换行；没有无关格式化。

早期探针曾因直接包含整份设计测试拉入无关外部依赖而链接失败，随后改为只提取所需真实fixture/生产palette。此变化只在ignored Build目录；没有修改工程配置、依赖或产品代码绕过环境。

## 验证边界

没有 GUI/Computer Use，没有任何commit/push。独立颜色探针通过不替代主代理的完整 Debug|ARM64 solution、整套 Headless 和现有 Combo/Selectable 动画合成回归。主代理继续执行完整验证，并在任务交付中注明真桌面模糊观感未由此验证。
