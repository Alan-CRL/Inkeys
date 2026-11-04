# 切页柔和度细化验证（2026-11-03）

基线为阶段提交925d8b2b。用户要求减速、旧页先淡出、新页透明度晚于位置到位。沿用原任务，无GUI/新commit/归档。

## 第一轮

- 完整原生ARM64 MSBuild `InkeysRepo.sln Debug|ARM64` exit0，121.06s，日志 `Build/settingui-style-motion/refinement/build.log`。
- 带MotionOnly及MotionSnapshots的回放：28个真实WARP场景渲染均0失败，包含旧Appearance淡退及新Feedback接续；三倍率CPU回放各有2个断言失败，整套exit1，不算通过。
- 减速断言原先把卡片AA描边最小Y与布局顶边直接比较，250ms时剩余位移小于AA边缘偏移；改为与同一张卡的真实终态DrawData最小Y比较，仍需证明旧220ms后存在实际位移，未放宽布局/锚点误差。
- 新增键盘可达断言假设从root仅Tab即可进入非NavFlattened的两层child，正在以真实Tab/Enter及旧页输入屏障对照复核；最终以修正后的完整回放为准。
- 已查看appearance-light-100-5.png（旧页仍为“外观与动画”，仅内容渐隐，配色不先变灰）及appearance-light-100-6.png（接续“问题反馈”渐入）；底色一致、文字大小和对齐保持。为无HWND离屏，不是桌面GUI验证。

## 需最终覆盖

三倍率位置先于alpha终态、旧页pose冻结、旧控件mouse/key/scroll不能误操作、侧栏/标题栏输入不受影响、新页键盘可达、快速目标替换/返回反向、字标和Hide/Show旁路、既有条件显隐与滚动锚点，以及完整Headless。

## 最终结果

- 独立checker确认键盘断言为夹具调用时序/路径问题：在真实root Begin内初始化Nav，真实Tab并在child锚点按Enter进入，不手工指定业务按钮ID。无屏障基线与经过旧页输入撤销后的新页均可达firstAction；未为错误测试假设重构产品导航。
- 最终完整ARM64 Solution构建exit0，checker日志`Build/settingui-motion-review/build-transition-keyboard.log`；保留已有第三方warning，没有调整SDK/依赖或禁用签名。
- 最终MotionOnly+MotionSnapshots回放exit0，58.262s，stderr为空；100/120/187.5%各613帧，共1839帧，业务动作各1次。28个DX11/WARP样张（2倍率×2主题×7场景）全部failures=0。日志`Build/settingui-style-motion/refinement/motion-final.log`及同名stderr文件。
- 最终完整Headless `--no-window` exit0，315.859s，stderr为空，末尾PASS；日志`refinement/headless-final.log`/`.stderr.log`。测试子进程TEMP/TMP仍仅指向仓库内scratch。
- 同一最终主程序既有非交互`--i18n-locales-test`、`--setting-font-lifecycle-test`均通过Hidden+Wait得到exit0，日志`refinement/i18n.log`及`fonts.log`，不启动主窗口。
- 旧测试路径生成的10张橡皮PNG逐文件安全移动至ignored `refinement/eraser-demo-visuals`，没有移动跟踪文件或递归删除/移动目录。

本轮代码、自动验证及规范同步完成；任务保持in_progress，不新建commit、不push、不归档。实际桌面观感与原Win7/DWM人工验收边界保持。
