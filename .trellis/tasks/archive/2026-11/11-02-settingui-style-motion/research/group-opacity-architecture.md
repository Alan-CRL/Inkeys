# 整块透明度的实现边界（2026-11-03）

用户明确要求整块内部正常合成后，再整体渐显/渐隐。当前产品尚未实现此合同。

## 根因与可检验数值

`Setting.Controls.cpp::TransformMotionDraw` 将块进度乘入各顶点 alpha；`Setting.Pages.cpp::ApplyContentAlpha` 在旧页淡出时递归乘每个顶点 alpha。DX11 backend `imgui_impl_dx11.cpp:700-704` 使用 `SRC_ALPHA / INV_SRC_ALPHA`。重叠图元分别与目标合成，因此无法形成一张完整块的透明度。

例如底色RGB=(24,24,24)、轨道=(96,205,255)、内部黑色thumb=(0,0,0)，组透明度a=0.2：正确结果是黑色整块覆盖目标，即 RGB=(19.2,19.2,19.2)；现有逐图元混合在thumb中心得到约(30.72,48.16,56.16)，会比底色亮且偏蓝。这是合成公式推导，非用户桌面的实测像素。浅色/半透明材质及边缘抗锯齿还需验证。

## 必须覆盖的渲染数据边界

- `PageBlockScope::Finish` 的一次块可能既包含当前父DrawList的一个顶点/索引范围，又包含后来绘制的多个child DrawLists。Dear ImGui在Render后按窗口顺序输出列表，不能在父列表末尾插入普通Begin/End callback就认为child已画完。
- `ConditionalBlock` 可嵌套，具有独立裁剪和淡出；页进入及条件显隐相遇时是嵌套opacity组。旧页整体fade应在全部块和子窗口按各自opacity合成后，再对旧页内容作为一组做fade；不得丢失或重复相乘其他层的alpha。
- 偏移、clip rect、IdxOffset/VtxOffset、font动态纹理事务、SRV身份/Alpha8像素着色器及callback排序必须沿用实际DrawData；不复制执行业务switch、不改变输入命中。Popup/Tooltip不能意外合入正文组；同排和页外元素不得改变原有Z序。
- 帧提交缓存对带callback的frame不允许复用旧baseline。设备epoch、resize、hide、失败、stop必须失效与释放中间资源；资源失败不能Present半成品，也不能永久取消已有输入恢复。

## 可行的局部DX11方案

1. 在UI构造阶段记录稳定块ID、opacity、实际draw/index范围、对应child树和嵌套关系；只保留当帧值/ID，不跨帧保存指针或旧字体UV。位移与剪裁保留原路径，但组opacity不能再逐顶点相乘。
2. ImGui::Render之后，在现有Setting renderer内按实际CmdList顺序分段。普通片段继续绘到原RT；组片段按原顺序绘进同设备可复用的透明RGBA离屏RT，再将组图像按opacity合成回原RT。不同块的错峰opacity必须独立，嵌套组需要严格栈式合成或等价的正确算法，不能假设父子范围在单个DrawList连续。
3. 组内DX11默认`SRC_ALPHA/INV_SRC_ALPHA`画到透明RT，得到预乘RGB；最终混合用`ONE/INV_SRC_ALPHA`，图像着色的RGB和A均乘opacity。用默认straight-alpha blend采样预乘RT会再次乘alpha，形成暗边。透明底及固有半透明card必须保留其原alpha，不应把其每个图元强制设为255。
4. 独立有界资源对象由Setting共享渲染线程持有，复用纹理容量；保存/恢复实际OM RTV/DSV以及blend/shader/scissor，SRV和RTV切换前解除资源绑定。失败后回到现有HRESULT/epoch仲裁；不能通过忽略backend错误来完成提交。无需新字体、持久化配置、线程或系统窗口。

## 必需的实际验证

- 真实WARP/DX11像素回放：完整开启深色toggle的轨道与黑thumb，opacity取0/.1/.2/.5/.9/1，比较输出与“先用原始alpha合成再整体fade”的参考像素；thumb中心、轨道、AA边缘分别断言。明确检测旧算法中间亮度反转，且opacity1与原始静态画面逐像素一致。
- Solid和带alpha底色；浅/深主题；100/120/187.5/200%；有clip的长页，子Card/自绘主页/文本和Alpha8字体。条件块进入退出及嵌套、旧页淡出+新页错峰；快速导航/输入/scroll/同排顺序。
- 真实生产调用及device-lost/分配失败/尺寸变化/热字体事务；资源重建不能泄漏，FrameSubmission不能跳过需重新合成的回调。复用ARM64完整solution和`--no-window`。Win7实机resize性能及DWM合成观感仍需人工实测。

## 当前结果

已确认根因与正确数学。临时原型使用RGBA离屏RT与预乘混合，ARM64 Solution编译通过；真实WARP对0、0.2、0.5、1四个opacity的深底/蓝轨/黑thumb中心像素与组混合参考误差不超过2，定向探针exit 0（Build/settingui-style-motion/refinement/group-target.log）。旧逐顶点对照仅有数学推导，新增对照测试没有编译执行。原型未处理父/child分段、嵌套、全页淡出及生产失败回退；遵主代理要求已撤销未接入原型头和临时测试，保留研究与日志。生产draw-data分组与真实完整页面WARP测试仍未实现。不要声称此需求已解决，也不要用单独改变toggle的颜色/透明度代替全块合成。
