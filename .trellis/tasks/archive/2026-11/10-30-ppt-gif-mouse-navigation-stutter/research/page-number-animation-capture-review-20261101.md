# 2026-11-01 页码动画现场policy采样独立复核

## 结论与范围

用户确认数字缩小/淡出发生在3→4，且基本每次都有。本次现场记录确实出现 **Numeric→Recovering对应Status→Numeric**，两次转换的主、次文字setter都返回true；结合原TransitionToString的true路径调用timeline.Start，可以确认两槽内容动画都开始并在回到数字时再次Start。它不再只是离屏可达候选。

但本记录不证明Status文字已实际显现，不给出精确中点，也不是窗口receipt或光学可见证据。造成Recovering出现的来源由另一调查处理；本报告不解释其故障原因、不提出或实施产品修复。

只读复核 `Build/Validation/ppt-navigation-20261101-163049-968-6df25be7` 与实际枚举/策略源代码，仅新增本文件。未改产品、测试或其它文档，未构建、GUI、Office或提交操作。

## 采集与枚举核实

- PID92024，manifest status=exited，exit_code=0，capture_errors=[]。
- EXE SHA256=`1EB8AEBA88055BE9F5F3EFC3E5BA99C07B7B200AF04CAEA3870C1BE41A5E0750`。
- DLL与内嵌DLL同为 `1061DDF51BD7CF646030AC74830DCD67CE74508448D879D70BBEE3EAA4D850E3`。
- PptWait summary（stderr第2311行）sealed=1，seen=retained=912，dropped=invalid=output_errors=0。
- content-policy共19条：7条old_kind0→new_kind1、7条1→0，均content_policy3、setter_changes3；另5条0→0、content_policy2。
- 枚举以源码为准：BarPageContentKind Numeric=0、Status=1；ContentPolicy Initial=1、Immediate=2、Animated=3；**PptRecovery::Kind None=0、Recovering=1**，随后才是Conflict/ReadFailed/PresentFailed/Failed。不能把recovery_kind1解释为Failed。
- 19条均为session1、epoch0/direction0。content-policy是内容观察，不能拿epoch0套某个Next请求。

QPC频率10MHz，锚为QPC1196070923406对应UTC08:30:50.0651775；下述均按锚换算北京时间UTC+8，而非最终flush文本出现的时刻。

## 按场次和surface配对的七次短Status往返

按同一session/surface上的先0→1再1→0配对，publication/model各自单调推进，7对全部有入口和出口，没有跨surface拼接。入口recovery_kind均1，出口均0；入口/出口content_total_page均56。

| 阶段 | surface | 入口publication/model | 出口publication/model | 所记录PPT页码入口→出口 | 两次配置间隔 ms |
| --- | ---: | --- | --- | --- | ---: |
| 初始场次准备 | 0 | 28/8 | 30/10 | 3→3 | 101.9734 |
| 初始场次准备 | 1 | 28/8 | 30/10 | 3→3 | 93.1133 |
| 初始场次准备 | 2 | 28/8 | 30/10 | 3→3 | 35.6560 |
| target2的3→4窗口 | 0 | 50/20 | 53/23 | 3→4 | 89.2630 |
| target2的3→4窗口 | 1 | 51/21 | 53/23 | 4→4 | 70.8269 |
| target2的3→4窗口 | 2 | 51/21 | 53/23 | 4→4 | 89.8985 |
| target2的3→4窗口 | 3 | 51/21 | 53/23 | 4→4 | 88.1891 |

表中页码是配置上下文已有PptState数值，不是现场读取的Word.content.val；4→4不能解释成进入Status之前屏幕已显示4。配置间隔也不是Status实际可见时长或timeline进度。

初始场次的3对发生在16:31:02.595602至02.703430，早于第一个Next16:31:05.355；不得将7对都写成7次翻页异常。surface3在初始阶段直接走Numeric→Numeric，不能用其它surface的Status补它。

## 3→4、Next与Gate target2的时序

native日志 `native/idt1793521851415.log` 与QPC换算时刻相容：

| 边界 | 北京时间 |
| --- | --- |
| Next epoch1 | 16:31:05.355418 → 05.400505，45.0866ms |
| Next epoch2 | 16:31:05.603134 → 05.952036，348.9022ms |
| Next epoch3 | 16:31:06.148499 → 06.489538，341.0392ms |
| raw3/descriptor4不匹配 | native第124行，16:31:06.908 |
| Next epoch4 | 16:31:07.102901 → 08.058468，955.5674ms |
| raw4/descriptor4一致 | native第131行，16:31:07.666 |
| Gate target revision2接受pageIndex3（第4页） | native第132行，16:31:07.667 |
| document_ready revision2 | native第134行，16:31:07.736 |
| page_ui_ready revision2 | native第136行，16:31:07.782 |

四surface进入/退出Status落在target2准备和ready的窗口内；Next4当时仍在执行。raw/descriptor失配在Next4之前已出现，因此不能把这段Status唯一归给Next4，也不能从epoch0推断是哪次Next制造了Recovering。PptWait model_revision与Gate target revision属于不同编号领域，不能要求20/21/23与target2相等。

### 右下surface1的直接证据

- 16:31:07.668283，QPC1196246954464，ordinal465，stderr第1863行：session1/surface1、publication51/model21、Numeric0→Status1、recovery_kind1、PPT数值4/56、policy3、setter_changes3。
- 16:31:07.739110，QPC1196247662733，ordinal537，stderr第1935行：同session/surface、publication53/model23、Status1→Numeric0、recovery_kind0、PPT数值4/56、policy3、setter_changes3。
- 两次配置相差70.8269ms；出口记录的主/次scale均997千分、pct均922千分，前后采样相同。它说明回到数字的setter调用当刻两槽数值已偏离1，**不能据此判定精确中点、Status是否已显示或动画已经完成**。

左下surface0为16:31:07.667962进入（pub50/model20，3/56）、07.757225返回（pub53/model23，4/56），出口两槽scale992/pct861；中间surface2/3也有各自完整往返，未借右下记录代替。

## 总数不变但两槽重新Start，普通数字分支仍在

所有7个Status出口的content_total_page仍56，而content_target_changes=7（主/次逻辑目标变化加kind变化），setter_changes=3（原主、次setter均true）。这两个mask含义不同：前者比较Scene已有的旧/新目标内容，后者是原API实际返回值，不是“用户看见变化”的计数。

真实Status的次槽目标会由恢复文案变为Wait/Retry，再回到Numeric的/56；底层页数56不变，不代表中间目标字符串也始终/56。当前采样不读取或保存这些wstring，策略含义由BuildPageContent源码核实。

源码TransitionToString只有通过原幂等检查后才调用timeline.Start并返回true。因此 **policy3本身只证明选择Animated；本次再结合setter_changes3，才能确认两个槽位的Start真实发生**。所有7次Status→Numeric都再次调用Start，解释了不必假设Numeric直更分支失效也能发生数字缩小/淡出重现的机制。它仍不证明之前的Status文字已显示。

5条Numeric0→Numeric0记录全部policy2、setter_changes3，符合原两槽SetStringImmediate分支。但必须限定样本口径：一条为初始3/56配置更新（surface3，第1465行），其余四条发生于退出并把数值改为-1/-1（第2244-2247行）。**这不是5次普通数字翻页的运行样本**；普通数字仍Immediate由现有代码分支、这些Numeric→Numeric记录及此前离屏纯Numeric控制组共同支持，不能虚构本采样中存在一条无Status的3→4对照。

## 证据边界

此次现场已支持“恢复类型进入Status、退出时两个槽位再次Start”的具体软件机制，与用户在3→4基本都看到缩小/淡出相符。尚不能仅凭policy或scale/pct声称：

- 短Status文字已经显示或一定没有显示；
- 精确中点、timeline generation/progress或实际可见时长；
- 任一policy记录已完成D2D/ULW/native receipt或光学呈现；
- Recovering由何种加载、呈现或其它来源触发。

本次policy记录无窗口receipt的同attempt身份；本报告没有把content-policy当作pixels/receipt。故障/准备来源继续独立研究，不以这份证据直接修改动画、恢复文字、COM或ready规则。
