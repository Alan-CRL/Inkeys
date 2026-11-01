# 调试页 EndChild 游标边界错误定位

日期：2026-11-01。角色：trellis-research。只读检查本地ImGui、ImFluent和相关设置控件/现有测试；未修改产品、未构建/运行测试、未启动GUI。本记录与实现代理并行，引用为定位时的行号。

## 1. 结论与触发链

用户截图中的“SetCursorPos()/SetCursorScreenPos() extend window/parent boundaries”与新增CopyButton的末尾游标恢复直接对应。主假设经本地源码确认，不是字体、复制字符串、诊断枚举或EndChild本身的故障。

生产路径：

1. 调试软件页在 `Setting.cpp:4375` 绘制EDID块，`:4376` 调CopyButton，它是本页最后一个item。
2. `Setting.Controls.cpp:696`先SetCursorScreenPos把按钮靠右；真实ImFluent Button通过ItemSize登记按钮，消费这个定位状态。
3. 按钮返回后，`:702`再次SetCursorScreenPos，只想把x恢复为origin.x，却把包括尾部ItemSpacing的当前y一并写回，重新设置IsSetPos。
4. 之后只有PopID/break等非item操作，没有登记新布局项；`Setting.cpp:4381`直接EndChild。
5. ImGui::EndChild调用End；End检查最后SetPos后是否以未提交item的坐标扩展了内容边界，于是报告截图中的错误。

画布块的复制按钮同样有问题，但后面紧接“显示器与EDID”标题，其真实Text/Dummy会消费最后SetPos，因此通常只有EDID末项暴露。

## 2. 本地ImGui的精确合同

文件：`Inkeys/additional/imgui/imgui.cpp`。

- `:11428`—`:11434`，SetCursorScreenPos只赋值DC.CursorPos，并设 `DC.IsSetPos=true`；**不更新CursorMaxPos**。
- `:11354`—`:11383`，ItemSize在登记按钮时：
  - `CursorPos.y = trunc(line_y1 + line_height + ItemSpacing.y)`，这是下一行起点；
  - `CursorMaxPos.y = max(oldMax, CursorPos.y - ItemSpacing.y)`，明确不把尾部行距算进内容高度；
  - 清除IsSetPos。
- `:10827`—`:10837`，ErrorCheckUsingSetCursorPosToExtendParentBoundaries在IsSetPos为true时先清标记；若CursorPos.x/y均不超过CursorMaxPos则返回，否则在非SkipItems状态报告用户错误。
- `:8226`—`:8227`，End在弹出当前窗口前执行该检查。
- `:6533`—`:6548`，EndChild先End当前child，再给外层父窗口登记child尺寸。因此外层随后ItemSize不能修复已经发生于child内部的末项错误。
- `:11691`—`:11692`，EndGroup也执行同样检查，不能靠在按钮外随便加BeginGroup/EndGroup绕过。

设按钮为最远item、行距S>0：ItemSize后的CursorPos.y包含S，Max.y排除S；CopyButton再次写入相同y并设IsSetPos=true。最后x恢复到左边通常不越界，但y仍大于Max.y，满足报错条件。不是“只改x就不会影响纵向”的API语义。

ImGui本地说明 `:10816`—`:10825` 专门指出，哪怕 `SetCursorScreenPos(GetCursorScreenPos())` 也可能在末项之后产生这种问题。当前1.92路径不再依赖旧版本偷偷扩展父边界的行为，不能关闭断言或改第三方来接受它。

实际按钮使用的是 `ImFluent::Button`（imfluent.cpp:2509），转入ButtonExStyled并最终ImGui::ItemSize；不是未经登记的自绘文本。定位问题发生在正确登记按钮之后的尾部SetPos。

## 3. 为什么旧复制专项漏掉

`CheckDiagnosticCopyButtons`定位时结构：

- 每次CopyButton后都 `Design::Text("后续标题")`（`setting_design_tests.cpp:2020`）。Text会提交Dummy/ItemSize，清除IsSetPos并登记更远的内容。
- 两块之后又加 `ImGui::Dummy({0,200*scale})`（`:2027`），用于制造滚动空间；它再次登记了尾部。
- 专项确实验证了按钮尺寸、右对齐、独立ID、恢复左cursor、完整UTF-8复制与长块滚动，但**没有让CopyButton成为窗口/child真正的最后item**。

因此前轮源码/测试复核遗漏了关闭容器这个独立后置条件；不能把“随后标题对齐正确”视为“末项直接EndChild也合法”。新回归必须同时覆盖有后续内容和无后续内容两种情形，不能再加一个尾部Dummy掩盖。

## 4. 最小修复建议（交给实现代理）

优先使用ImGui正式水平布局API临时缩进：根据当前可用宽度和标准按钮宽度计算非负右移量；Indent右移 → Button → Unindent还原。它只改变x，不需要在Button后SetCursorScreenPos，并保留按钮LastItemData/ID与正常ItemSpacing推进。

本地实现 `imgui.cpp:11486`—`:11499`：Indent/Unindent只调整DC.Indent.x和CursorPos.x，不设置IsSetPos、不改变CursorPos.y。注意 `Indent(0)` / `Unindent(0)` **不是no-op**，会使用默认IndentSpacing；偏移为0时必须不调用这一对。

其他方案不是绝对禁止，但有边界：

- 在SetPos后补Dummy确实满足ImGui合同，却会更改LastItemData、间距或内容高度；如果采用必须证明没有破坏按钮点击/ID/标准推进和现有测试。不要给生产末尾随意加一个大占位。
- SetCursorPosX/SetCursorPos等定位函数同样会设IsSetPos，不能换个函数名当修复。
- 不直接改 `DC.IsSetPos=false`、CursorMaxPos或第三方ErrorCheck来压制报错；这会掩盖真实布局合同。
- 不改诊断查询、复制内容、粒子或其他业务，本问题只需调整CopyButton的水平排版方式和相关回归。

已将以上精确条件和最小方案发给根代理与拥有Controls.cpp/test的implement_settings；本代理没有并发修改源码。

## 5. 同一控件路径的必要审计

### SettingRow

SettingRow也以SetCursorScreenPos结束，但与CopyButton不同：它在绘制之前先 `Dummy({width, Pixels(row.height+RowGap)})` 注册完整行和尾隙，结束定位为 `floor(bottom.y+Pixels(RowGap))`，落在这份已登记边界内。

当前ImFluent ApplyStyle的ItemSpacing经过ImTrunc（imfluent.cpp:2164），因此SettingRow的最终floor与ItemSize当前取整合同配合，不依赖未登记的尾部spacing。已有真实SettingRow scope测试（setting_design_tests.cpp约:920）在EndChild前检查Cursor/Max并直接结束，覆盖此类行，而复制专项没有同样覆盖。

没有证据需要把所有SettingRow末尾改成Dummy或重构其action定位；应保留目前完整行注册机制，在修复中定向确认这一已有末项场景继续通过。

### NavigationRow / ButtonGroupRow

- NavigationRow的next来自已注册完整高度的SettingRow；覆盖按钮后恢复next仍位于那份预登记行边界之内。它和CopyButton“Button自然推进后直接把尾spacing SetPos写回”并不相同。
- ButtonGroupRow在SettingRow已登记的动作区内定位并提交真实按钮，最后仍由SettingRow恢复到整行已登记末端。
- 不因存在SetCursorScreenPos就认定这些路径有同一错误；需要核对是否已提交覆盖最终坐标的item。

### Text / InfoText / AdvanceGap / RenderEdidCard

- Text、InfoText和AdvanceGap均用Dummy登记实际内容/空白收尾，没有Button后再SetPos的同类尾部操作。
- RenderEdidCard按完整文本设置child尺寸并正常EndCard，CopyButton在其外层调用；不是把按钮塞进已关闭卡片导致。

本轮审计止于上述相关helper，没有泛扫无关UI或扩大修复范围。

## 6. 必须补充的确定性验收

1. 真实ImGui child：RenderEdidCard → CopyButton → 立即EndChild；后面不能有Text/Dummy/Spacing/SameLine来封口。
2. 复制按钮单独作为窗口、已有group内的最后item，也不能留下会扩展边界的IsSetPos。
3. 保留现有尺寸/右齐/LastItemData/独立ID/当前帧完整文本复制/左cursor后续标题测试；修复不能用改变高度或末尾fake item绕过旧断言。
4. 多DPI、窄/宽、明暗主题、窗口有滚动且按钮部分或全部离屏，当前window SkipItems早退等必要边界。
5. 若为红绿测试捕获ImGui用户错误，可用本地ErrorCallback与临时关闭recoverable assert，记录精确边界消息后恢复所有配置。不得触发需要人关闭的断言GUI，不永久修改产品错误选项。ErrorCountCurrentFrame的计数受配置影响，明确ErrorCallback消息计数比只看计数字段可靠。

最终红绿、完整ARM64构建和focused无窗口结果由根代理/实施代理执行并记录；本调查仅提供源码因果与测试缺口证据，不冒称已复现运行通过。
