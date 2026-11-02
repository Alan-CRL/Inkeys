# 20语国际化验证结果

本任务基于 `feature/i18n@8bf8f41d` 完成九语扩展，实现与所需无窗口验证已完成。用户已确认改动生效并授权提交、结束任务；最终提交及归档状态记录于task.json，未授权push。

## 完成内容

- 新增id-ID、vi-VN、tr-TR、it-IT、pl-PL、uk-UA、es-MX、ro-RO、en-GB，各679/679个叶子值，共6111个；英/西地区版仅做必要差异。
- 二十项菜单按locale code字典序显示，保存索引0–10不变，新增11–19；真实F4/Home/End/Down/Enter选择回归检查首行及en-GB/en-US/末行身份。
- 新增消息框默认按钮；复用Harmony SC/TC、Noto JP/KR和最新FreeType动态字体/事务热切换，没有新增或修改字体文件，没有改变生产字号、行框、基线或渲染管线。
- 实际DWrite测量后精炼26个固定槽短标签；离屏图检查再精炼UA侧栏专用key，完整页面标题保留。新九语展开导航宽度、实际越南语Caption重音裁切均有回归。

## 验证汇总

| 验证 | 最终结果 |
| --- | --- |
| i18n sync/check | PASS，19个非基准语言均679/679，100%，键序/占位符/必要换行一致，无待译标记 |
| 最终EXE内嵌资源与源文件 | 20/20逐字节一致；LoadLibraryEx DATAFILE只读，不执行程序入口 |
| EXE资源/真实格式化/索引/事务/并发快照 | --i18n-locales-test exit0，20语Language/LANGID及实际参数类型通过 |
| 生产字体生命周期 | --setting-font-lifecycle-test exit0，20语3轮、同族复用/跨族重建、过期/失败/重试通过 |
| Bar/PageControl真实离屏 | --bar-eraser-offscreen-test exit0，31.12秒，最终固定槽及四表面刷新0失败 |
| 完整InkeysRepo.sln Debug ARM64 | ARM64原生MSBuild exit0，最终增量12.99秒 |
| 完整--no-window | exit0，304.1秒，stderr为空，含默认按钮/字体/布局/真实菜单提交/实际Caption裁切 |
| 20语真实页面A/B矩阵 | exit0，2898.07秒，1664行（832FT场景+832stb对照），缺字0、预算失败0 |
| UA侧栏修正后的页面专项 | exit0，64.97秒，80行；已替换最终矩阵中UA的80行 |
| 新九语独立字体样本 | 九次exit0，180行/90FT场景；Regular/Bold、Legacy/Design及12/13/14/28字号 |
| 静态/上下文/编码 | Trellis校验、独立审查、diff与原BOM/CRLF检查通过 |

页面矩阵覆盖100/125/150/187.5/200%全部20语，另保留中英日韩400%，浅深主题、四页面及语言下拉打开/滚动到底/回顶/关闭。最终数据为 `Build/i18n-twenty-locales/page-metrics-final.csv`，并回写 `Build/setting-fonts/pages/page-metrics.csv`；原始全矩阵和UA复测分别保留于同目录的page-matrix-full.csv及page-matrix-uk-verified.csv。

九语字体样本及完整页面在相同工作负载下比较STB/FT，预算统计仍是ImGui/FreeType分配及实际字体纹理的逻辑字节，不能描述为整个进程内存或独立显存。没有为本次扩语重做性能优化或放宽原预算。

## 字体与图片核验

八个现有Regular/Bold字库SHA256与规划基线相同。Harmony SC主脸覆盖新增语种；任何当前语言下的20项母语列表继续依靠SC/TC/JP/KR同字重补缺。实际全文字形、特殊字符、共同H/数字与Noto负对照、语义字号及实际控件clip检查通过。

已查看en-US100、id-ID100、vi-VN100/125、pl-PL125、uk-UA187.5修正前后、ro-RO200代表离屏图。最终UA侧栏词完整可见，特殊字符及文字层级正常。PNG位于 `Build/setting-fonts/pages/`。

最终完整headless二进制SHA256：`432815CCC7D7E015EB67302AEE008B0D7A26EF957A962C07B2115241F28D6E5C`。

全矩阵初次运行二进制SHA256：`92CF079C841BBD645F760AF32264677BE060B554415BC7783C6440E6DD70BA01`。其后仅完善未在page-only分支执行的Caption/导航测试；生产与矩阵函数不变，UA修正以最新二进制80行复测覆盖。记录两个hash，不冒称同一二进制。

## 环境与夹具定位

- 沙箱MSBuild首轮C1902 PDB manager错误；相同solution/配置在原生环境通过，未改源码或项目配置绕过。
- 默认沙箱TEMP的MoveFileEx原子重命名返回拒绝访问5，工作区同操作成功；最终测试进程局部TEMP/TMP设到Build内，所有配置落盘回归通过。
- 新字母表曾将语义Caption行框当实际clip，产生4条假阳性。保留common样本原严格阈值，新字符单独coverage，补真实Caption完整UV/尺寸/IdxBuffer与ImDrawCmd::ClipRect检查；没有缩字或移动生产基线。
- 构建有既有第三方/历史转换等warning，未为0 warning扩改无关代码。

## 既有问题与人工审校

旧RU `SettingsUI/Design/Experimental` 在HEAD已超出展开侧栏文字区，本任务未改该文件。按最小修改约束单列记录于 `research/existing-russian-navigation.md`；新增导航宽度断言覆盖新增九语，二十语资源/格式/完整字形/既有布局专项仍执行。不能据矩阵exit0声称旧俄语导航截断已修复。

专业术语、极窄槽缩写及长权限/安全说明仍建议母语者审校，详见 `translation-review.md`。键完成率不等于母语认证。按仓库规则未启动GUI、Computer Use、触控/多显示器交互或其他系统/CPU架构实机验证；全部运行证据是命令行、无窗口或离屏。

本轮测试生成到旧09-06路径的10张未跟踪PNG已逐文件保存到Build/i18n-twenty-locales/eraser-demo-visuals，保留哈希清单，没有修改旧任务或递归删除。

## 用户验收与收尾

用户已确认改动生效，授权提交并结束。功能提交为`100fda717084aeac1e3b377c24b141d9412a118a`；本任务已归档，未push。收尾复核19语check及20个EXE资源逐字节比对均通过，未重复运行未发生源码变化的完整构建/矩阵，也未启动GUI。
