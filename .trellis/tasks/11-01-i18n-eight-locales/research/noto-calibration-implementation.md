# Noto设置字体校准实施交接

## 修改边界

- `Setting.Typography.h`：显式Legacy/Design角色；中性Legacy配置；Noto的每角色字面和主脸基线校准；源字族尺度与primary字族基线分离。
- `Setting.cpp`：所有主脸、粗体、Design和merged fallback统一使用该选择器。Harmony主脸的原参数不变。
- `setting_design_tests.cpp`：实际字体的尺寸上下界、基线、跨字族补缺、Regular/Strong层次与旧Noto负对照；独立ImGui上下文对照板。
- `setting_home_warp_tests.{h,cpp}`：复用已有无HWND WARP、真实ImGui DX11后端并用WIC导出当前对照帧；原探针默认1000×1100行为保留。

未更改主栏/DWrite、语言文本/元数据、字体二进制、图标、语义字号/行框、FontScaleDpi、构建配置或后端；不做Harmony日文候选。

## 字体profile

| family/role | ExtraSizeScale | GlyphOffset.y（参考30） |
|---|---:|---:|
| Harmony Legacy | 1 | 0 |
| Harmony Design | 1.213×0.97 | 0.75 |
| Noto Legacy | 1.448/1.213≈1.193735 | -30×(1160/1448−928/1213)≈-1.082 |
| Noto Design | 1.448×0.97=1.40456 | 0.75−30×(1160/1448−928/1213)≈-0.332 |

Extra按源字族选择；合并字体共享primary的baked.Ascent，所以所有merged source的GlyphOffset必须按primary家族/角色选，不能把每个source的独立基线补偿直接混合。真实缺字「课」(Noto→Harmony)及「한국어」(Harmony→KR)验证了该边界。

## 测试与对照板

- `InkeysHeadlessTests.exe --no-window --setting-general-only` 可先跑focused回归；最终由主会话运行完整`--no-window`。
- 新检查100/150/200% DPI，Design/Legacy、Regular/Bold、12/13/14/20/28/40号。共享汉字与大写H/数字的实际DrawData高度比保持0.90–1.10；仅额外容许一物理像素的glyph quad栅格边距差，不随DPI放大容差。并有基线与跨字族补缺上下边界检查。
- g/q等下伸部字形造型不被当作字号锚，另验证下伸部实际保留；实际Inkeys Agpqy文字仍在PNG可见。
- 旧Noto是真实字体、未补偿配置的负对照。比较跨六个字号聚合的真实高度，避免小字号两端一像素栅格/quad边距使单点负对照失真；不把产品配置常量重复成测试断言。
- 输出 `Build/setting-fonts/noto-calibration-100.png`、`150.png`、`200.png`。每图五列：CN参考、JP旧/校准、KR旧/校准，包含共同文字、混排、实际PageHeader/SectionHeader/ToggleRow以及Legacy文字。独立列child提供真实content起点/宽度，避免SectionHeader的ItemSize回到整图左侧。

## 验证状态

- 本代理未启动编译、GUI或竞争测试进程；主会话统一执行。
- 第一候选完整构建通过；focused发现字形造型/栅格造成的测试锚问题及预览夹具列原点问题，已依真实证据修正；未缩小产品已校准字面，也未放宽原UI布局断言。
- 第二候选的merge、负对照聚合及独立列检查通过，仅Legacy13/Bold大写样本有9px/10px的一像素quad量化差，已按上述物理像素规则修正；预览描述位置也跟随SectionHeader实际结束游标。第三候选源码已冻结，diff/CRLF通过；编译及focused/完整回归结果由主会话补充。
- 本轮不commit/push、不结束或归档任务。
