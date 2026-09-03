# StartupBarPreview-v1.bin

该资源只能从真实 Bar 的成功 `UpdateLayeredWindowIndirect` committed frame 生成，不能由手绘图、mock renderer 或图片转换代替。

## 生成前提

- Windows 主显示器为 96 DPI；
- Debug x64 完整解决方案已构建；
- capture-only 会话在内存中强制使用简体中文、深色主题、默认按钮、默认 zoom、expanded 状态，并关闭调试覆盖层、动态灯效、mouse hook 和 Bar 交互；
- capture-only 会话不会把规范默认值写回用户配置。

## 生成与验证

在仓库根目录运行：

```powershell
Build\x64\Debug\Inkeys.exe --capture-startup-preview Inkeys\Inkeys\UI\StartupPreview\StartupBarPreview-v1.bin
Build\x64\Debug\InkeysHeadlessTests.exe --validate-startup-preview Inkeys\Inkeys\UI\StartupPreview\StartupBarPreview-v1.bin
Build\x64\Debug\InkeysHeadlessTests.exe --validate-startup-preview-resource Build\x64\Debug\Inkeys.exe Inkeys\Inkeys\UI\StartupPreview\StartupBarPreview-v1.bin
```

生成器在无动画、hover、press、capture、menu、panel 或调试覆盖层的 committed frame 后静默 750ms，再把 exact Bar crop 复制到 CPU staging。writer 使用同目录 `CREATE_NEW` 临时文件、`FlushFileBuffers` 和 write-through replace；完成标志只在生产 parser 反读成功后发布。

## 当前规范值

- 文件 SHA-256：`F9592D61CB77A27467D4847148E1BC4412398373C23056894E2D8FFB157E3285`
- magic：`IKSPRVW\0`
- format/header/layout epoch：`1 / 160 / 1`
- visual signature：`F158AF6C1B51A72A8637EF11FF0A2E986978F2902120A6E482AC7840C2766C63`
- pixel format/flags/capture revision：`1 / embedded / 0`
- bitmap：`494 x 105`，stride `1976`，payload `207480` bytes
- capture DPI：`96 x 96`
- capture monitor/work area：`1920 x 1080 / 1920 x 1032`
- window offset：`713, 939`
- anchor：`52, 53`
- progress rect：`82, 99, 411, 103`

默认按钮、主题、语言、图标/颜色/圆角/阴影、布局或像素/alpha 规则变化时，必须递增 layout epoch、重新生成并更新上述规范值。
