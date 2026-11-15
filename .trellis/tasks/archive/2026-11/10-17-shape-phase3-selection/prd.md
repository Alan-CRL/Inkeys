# 已批准的 P3＋P4 实施范围

用户已明确批准规划并实施（2026-10-17），无需再次请求任务或执行许可。基线 d0a1dc73/1d4b5472。沿用现有P3、逐点、P4任务，不创建总任务。本轮无自动commit/push，无可见GUI/Computer Use，保留原编码BOM/CRLF。

仅创建后当前对象编辑；不重选旧对象、不圈选/多选/坐标关联/吸附。下一笔创建、切工具/页、Clear、Esc结束编辑；普通笔保持原路，多接触全部结束才编辑最后完成项。只当前对象轮廓/可见填充/控件截获输入，其余空白穿透并直接开始新图形。后续可替换自动显示策略，本轮不增加“调整图形”按钮或设置。原电脑选择/穿透语义保持。

21类启用：已有11＋Polyline/Polygon/Arc/Sector/Cuboid/Cube/Cylinder/Cone/Prism/Pyramid。15类继续禁用：Angle/Parallel/Perpendicular、4坐标工具、8函数。P4后置条目继续未完成，不冒称全阶段完成。

独立SelectionOverlay受Window Service管理，画布上/Bar下/PPT同层，原owner/显隐/最小化/置顶/反向销毁完整接入；没有第二套RTS/窗口线程/渲染循环。UI3共享RenderPipeline client及BarButton Configure/Retarget/Advance/Draw/成功呈现hit，自己的SingleRow list。颜色/样式/线型/三档粗细/必要参数仅作用于对象；不借整个Pen属性树、不改下一笔工具、不加精调/数字输入/删除复制翻转组合入口。浮条与主栏工具弹层互斥；打开主栏工具配置结束当前编辑。正常下方、不足上方，钳制工作区并避主栏。

控制圆12DIP、hit至少44DIP，旋转圆按钮32.5DIP、hit44DIP；共享动画。优先旋转→专属点→通用缩放→本体，同类最近。Line/Arrow仅端点+旋转；Polyline各点+旋转；Circle中心/半径不旋转无八点；Ellipse中心/两半轴/旋转；Arc/Sector圆心半径起止角/旋转；RegularPolygon中心首顶点/旋转/3–12边；其他闭合平面外框8点+适用顶点，重合点合并；空间外框/旋转/底面深度高度。圆/正多边形/Cube等比，梯形上下底平行、菱形和平行四边形保持约束。无效编辑Up取消恢复before。

缩放保持线宽。新Arrow头长=3*线宽，总宽同头长，仅随粗细；短线不按长度缩头，零长拒绝。v2旧箭头保持外观，不能用新规则误读旧文件。

ConstructionSession在drawing thread跨contact存活，首笔冻结tool/style/id/display；raw Up确认一步，预测只改当前步；固定阶段末步Up自动最终提交；折线/多边形Complete，至少2/3点且简单多边形。一次完成一个对象/一个撤销；Back回退；Cancel/工具页Clear失捕不留文档。构造中全局Undo=Back。

提示属于Bar场景，复用原居中提示锚点/外观/动画，独立状态；构造优先，结束恢复原提示。仅按能力显示步骤及Complete/Back/Cancel，单笔图形无无用步骤按钮，不增加新HWND。

Arc/Sector第一笔center→radiusStart，第二笔连续极角展开确定sweep（顺逆/大弧），无方向按钮。Cuboid先正面矩形再深度向量；Cube先方形再深度投影，等边与投影分离；Cylinder/Cone先椭圆底面再局部竖向height，Cone顶点在中心轴；Prism/Pyramid底面中心→首顶点，再高度；3–12边默认3，复用已有次级选项；默认底面纵向投影1/2，首顶点仍跟raw endpoint。空间仅描边；可见边所选线型、隐藏边固定虚线；同色覆盖并集防半透明接缝；一个非嵌套Compound逻辑对象。

运行期编辑同索引替换InkStroke，保ID/层序；统一journal包括普通追加/创建/编辑/Clear边界。候选文档/history/tile重放成功后发布，失败回滚；预览排除原项并在原层位回放，旧新tiles并集失效。RevisionAllocator每ID运行期不复用，预览/UndoRedo物化/新分支都新修订；安装页面清shape缓存，不能因逆操作恢复旧revision导致缓存重用。旧legacy首次编辑物化DPI圆角并升级通用Shape，Undo可恢复before表示。

UInk10不升外层，新增构造写inkeysShape v3，读v1/v2/v3；标准线/折线/多边形复合降级。白板冷历史升级兼容版，含journal/游标/redo/Clear continuation。Desktop/PPT保最终参数，不额外跨进程恢复完整编辑栈。编辑/UndoRedo贯通page mutation/autosave，恢复候选整体验证后安装。
