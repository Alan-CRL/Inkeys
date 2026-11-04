# 日/韩设置字体差异：调查结论与下一步方案

日期：2026-11-02。基线：e5f0574e。仅调研；未修改产品源码、字体资产或配置，未启动GUI、编译或提交，任务继续保留。

## 结论

优先修正设置页的字体度量校准，保留当前正常的主栏。以校准后的Noto JP/KR作为主方案，同时制作Harmony SC日语同页对照，验证用户偏好的可行性；当前Harmony字体无法完整替代韩语。

这属于上一轮Noto接入遗漏的设置字体校准：之前验证覆盖缺字、格式与边界，没有建立跨字体的实际字面尺寸和字重观感基准。

## 根因与量化证据

设置使用ImGui默认stb后端，按hhea的ascent-descent缩放；主栏使用DWrite的em字号。`imgui_draw.cpp:4667-4670` 先执行ScaleForPixelHeight，再乘ExtraSizeScale。当前Harmony Design字体额外放大1.213×0.97=1.17661，Noto则没有对应校准。

所有被测字体UPM=1000。Harmony SC/TC的hhea为928/-285，跨度1213；Noto JP/KR为1160/-288，跨度1448，两种字重一致。公式为：

`ImGui有效em = 名义字号 × ExtraSizeScale × UPM / (ascent - descent)`

| 使用角色（名义14作比较） | Harmony当前有效em | Noto当前有效em | Noto相对Harmony | Noto校准起点ExtraSizeScale |
|---|---:|---:|---:|---:|
| 设置Design脸：侧栏/卡片/标题等 | 13.58 | 9.67 | 71.2% | 1.40456 |
| 旧设置脸/窗口相关Legacy角色 | 11.54 | 9.67 | 83.8% | 1.193735 |

因此仅把Harmony的1.17661复制给Noto仍不足以匹配字面。1.40456与1.193735只是等em的测量起点；还必须用实际ImGui字形校准基线、视觉大小和不同脚本的观感，不能直接视为已验证最终参数。

共享字形「設」的名义14字面高度：当前Harmony Regular约12.44，Noto JP Regular约8.59；在DWrite等em模型中分别约12.82、12.43，差距明显更小，符合截图“主栏正常”的现象。

字重并未漏绑：Noto Regular/Bold实际为400/700，设置标题/分组已绑定真实Bold。同em的「設」轮廓面积，Noto Regular约0.349、Harmony约0.334；Bold分别约0.532、0.480。当前偏细主要受缩小影响，不能据此直接加粗或改成更重字重。轮廓面积只是辅助证据，不代表已经完成真实抗锯齿像素或视觉验收。

完整数据：`font-face-measurements.json`；调用与角色证据：`settings-font-pipeline-analysis.md`。

## Harmony能否直接显示

实测仓库现有Regular/Bold：

| 字体 | 当前日语679条的不同字符缺失 | 当前韩语679条的不同字符缺失 | 边界 |
|---|---:|---:|---|
| Harmony SC | 0 | 456 | 可制作日文对照；没有Hangul音节覆盖 |
| Harmony TC | 71 | 464 | 不能替代完整日/韩UI |
| Noto JP | 7 | — | 缺字是保留的中文专名，现有合并补缺覆盖 |
| Noto KR | — | 9 | 缺字由现有合并补缺覆盖 |

Harmony SC有当前所需假名和汉字，但其GSUB脚本仅DFLT/latn，未发现hani/kana及JAN地区替换；ImGui默认stb也不执行完整OpenType地区字形替换。需要独立对照「骨、直、令、返」等日文字形，才能决定是否接受SC作为日文设置字体。不能把当前文本覆盖推导为完整日语输入/未来文本的保证。

官方[OpenHarmony字体目录](https://github.com/openharmony/utils_system_resources/tree/master/fonts)也同时提供Harmony SC/TC与独立Noto JP/KR资源；其[README](https://github.com/openharmony/utils_system_resources/blob/master/README.md)列出的Harmony支持文字系统不含韩文。这里只用于核对当前发布结构，不能据此断言所有华为新包都没有日/韩字体。

另发现华为[当前官方资源页](https://developer.huawei.com/consumer/cn/design/resource)列有更新字体包。公开链接在本环境两次请求均返回HTTP200但内容为0字节，未取得有效包，无法核验其最新字形范围；本报告关于Harmony覆盖的确定结论针对仓库现有文件。

## 方案比较

| 方案 | 优点 | 代价/限制 | 建议 |
|---|---|---|---|
| 保留Noto JP/KR，按字体与角色校准 | 保持日/韩地区字形，已有资源可复用，直接处理根因 | 需要真实离屏样张和基线/字重复核 | 主方案 |
| 日文设置用Harmony SC，韩文保留校准Noto | 最贴近用户喜爱的中文风格，当前日文文本不缺字 | 日文字形需接受；韩文仍有两套字体；混合源需分别校准 | 做对照候选 |
| Harmony主字体，日/韩只缺字回退 | 拉丁/汉字风格统一 | 回退只补缺，不会替换已存在的中文地区汉字；韩文仍大量依赖Noto | 不作为默认优先项 |
| 换另一套日/韩字体或换字体后端 | 可选不同审美/栅格化效果 | 新字体仍要校准，增加资产/布局/兼容核验范围 | 先不扩展 |

## 下一步建议范围

1. **先做真实ImGui离屏样张**：中文现状为基准，比较当前Noto、按上述度量起点校准的Noto、Harmony SC日文候选。使用同一页面、窗口、DPI和折叠状态；对比侧栏、页面标题、正文、次要说明、下拉和窗口标题，不用不同页面的文本量判断字号。
2. **按字体家族和角色拆开配置**：当前`Setting.cpp:1897`会让Legacy Harmony也使用名为NotoFontConfig的默认配置，不能只把这个函数统一放大。保持Harmony Legacy=1、Harmony Design既有配置；分别校准Noto Legacy/Design，并覆盖合并源Regular/Bold。
3. **固定语义字号、行框与图标尺寸**：不放大整窗FontScaleDpi，不改全局控件高度，不修改TTF度量表，不改变正常的DWrite主栏路径。基线偏移根据实际字形测量确定，不能直接照搬Harmony的0.75参考偏移。
4. **先大小、后字重**：保持400/700，校正字面后再看正文是否需要Medium等变化；不先做伪粗体或全局提字重。
5. **验收补强**：真实ImGui同字样（共同汉字、Inkeys、数字）字面高度与基线；日/韩各自典型字形；Regular/Strong层次；100%/150%/200% DPI；窄窗口、混合语言菜单、旧页面与Design页面。复核十一语换行/控件边界、字体atlas与冷启动准备，再运行既有无窗口回归。视觉候选通过后才确定最终参数或启用Harmony日文。

本轮没有实施上述修复，也没有宣称校准样张或真实GUI验收通过。
