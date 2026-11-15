// 不创建HWND：真实Coordinator、Run共用阶段封口、CPU事务与UI身份合同。
int RunShapeInteractionProductionTest()
{
	using namespace draw3::shapes;
	int failures = 0;
	const auto check = [&](bool ok,const char* label) { if (!ok) { ++failures; std::fprintf(stderr,"[Shape.Interaction] FAIL %s\n",label); } };
	ContactInputCoordinator input; WindowController window;
	InkGuid workspace,pageGuid,otherPage;
	if (!TryCreateInkGuid(workspace) || !TryCreateInkGuid(pageGuid) || !TryCreateInkGuid(otherPage)) return 1;
	InkCanvasCollection document(workspace); document.AppendPage(pageGuid); document.AppendPage(otherPage);
	auto* canvas = document.PageAt(0)->GetOrCreateCanvas(kDefaultDeviceKey,{25,-18,1});
	document.PageAt(1)->GetOrCreateCanvas(kDefaultDeviceKey);
	if (!canvas) return 1;
	std::vector<CanvasPageRuntimeState> pages(2); EnsureOperationJournal(pages[0]);
	uint64_t token = 1000; uint32_t contactId = 0; const auto allocate = [&] { return ++token; };
	const auto configuration = CreateStrokeModelConfiguration(144);
	Inkeys::Business::PenWidth::Context context; context.effectiveDpi = 144;
	ShapeStyle style; style.width = 3; style.strokeOpacity = .5f; style.strokeRgb = style.fillRgb = 0x123456;
	const auto begin = [&](ToolSpec tool,Vec2 point,std::shared_ptr<ShapeConstructionRuntime> state = {}, InputDeviceType deviceType = InputDeviceType::Pen)
	{
		window.SetActiveTool(DrawingTool::Shape); window.SetProductVisualStyle(0x12345680,3,context,tool,style);
		ContactSnapshot down; down.position = {point.x,point.y}; down.qpc = 100000 + 1000 * ++contactId;
		if (!input.PublishDown(0x5A91,contactId,deviceType,down)) throw std::runtime_error("shape interaction Down");
		ContactRecord* record = nullptr; while (input.TryDequeue(record)) if (record) break;
		if (!record) throw std::runtime_error("shape interaction ingress");
		auto runtime = std::make_unique<RuntimeStroke>(configuration.expectedSpeed);
		runtime->handle = {record,record->Generation()}; runtime->inUse = true;
		runtime->tool = runtime->selectedTool = DrawingTool::Shape; runtime->ownerWorkspaceGuid = workspace;
		runtime->ownerPageGuid = pageGuid; runtime->ownerPageIndex = 0; runtime->viewport = canvas->Viewport();
		runtime->lastInputSnapshot = record->DownSnapshot(); runtime->lastConsumedSequence = runtime->lastInputSnapshot.sequence;
		CaptureProductStrokeStyle(*runtime,window);
		const float diameter = CanvasDiameterForTool(runtime->tool,runtime->visualStyle,9);
		ResetNominalStroke(*runtime,diameter,configuration,StrokeWidthMode::Fixed);
		runtime->stroke.hasInputStartPoint = true; runtime->stroke.inputStartPoint = {point.x,point.y,diameter * .5f,0};
		if (!InitializeRuntimeShape(*runtime)) throw std::runtime_error("shape interaction shape init");
		if (!state)
		{
			state = std::make_shared<ShapeConstructionRuntime>(); state->tool = runtime->shape.tool; state->style = runtime->shape.style;
			state->identity.objectId = runtime->shape.id; state->identity.session = contactId;
			if (!state->session.Begin(state->tool,state->style,runtime->shape.id)) throw std::runtime_error("shape interaction session init");
		}
		runtime->shape.construction = state; state->contactActive = true; runtime->shape.id = state->identity.objectId;
		runtime->shape.tool = state->tool; runtime->shape.style = state->style;
		PrepareRuntimeConstructionContact(*runtime,1.5f);
		runtime->shape.nextRevision = [&](const ObjectId& id,uint64_t observed) { return pages[0].shapeRevisionAllocator.Next(id,observed); };
		return runtime;
	};
	const auto move = [&](RuntimeStroke& runtime,Vec2 raw,Vec2 prediction)
	{
		auto sample = runtime.lastInputSnapshot; sample.position = {raw.x,raw.y}; sample.qpc += 20;
		if (!input.PublishMove(0x5A91,runtime.handle.record->ContactId(),sample) || !input.TryReadSnapshot(runtime.handle,sample)) throw std::runtime_error("shape interaction Move");
		runtime.lastInputSnapshot = sample; runtime.lastConsumedSequence = sample.sequence; runtime.shape.rawEndpoint = {raw.x,raw.y};
		runtime.shape.construction->session.ObserveRaw(raw);
		std::array<ink::stroke_model::Result,1> predicted{}; predicted[0].position = ink::stroke_model::Vec2(prediction.x,prediction.y);
		const auto endpoint = ResolveShapeLiveEndpoint(predicted,{raw.x,raw.y},true,{raw.x,raw.y},false);
		SetShapeVisualEndpoint(runtime.shape,endpoint);
	};
	const auto up = [&](RuntimeStroke& runtime,Vec2 raw,bool cancel = false)
	{
		auto terminal = runtime.lastInputSnapshot; terminal.position = {raw.x,raw.y}; terminal.qpc += 20;
		const auto id = runtime.handle.record->ContactId();
		if (!(cancel ? input.PublishCancelled(0x5A91,id,terminal) : input.PublishUp(0x5A91,id,terminal)) || !input.TryReadSnapshot(runtime.handle,terminal)) throw std::runtime_error("shape interaction Up");
		runtime.lastInputSnapshot = terminal; runtime.lastConsumedSequence = terminal.sequence;
		runtime.shape.rawEndpoint = {raw.x,raw.y}; runtime.ended = true; runtime.cancelled = cancel;
		SetShapeVisualEndpoint(runtime.shape,{raw.x,raw.y}); ConfirmRuntimeConstructionContact(runtime);
		const auto result = CommitRuntimeStoredStrokeCpu(runtime,document,pages,0,0,StoredStrokeCommitMode::NormalUp,allocate);
		input.Recycle(runtime.handle); return result;
	};
	// 空闲多步工具的首触可转手掌；Run 的取消钩子与本测试共用失焦路径的 CPU 清理。
	for (PresetKind preset : {PresetKind::Polyline,PresetKind::Cuboid}) for (ContactPhase phase : {ContactPhase::Down,ContactPhase::Move})
	{
		PalmEraser::Settings preferences; preferences.enabled = true;
		const PalmEraser::Scale scale{1,1,true,false}; ShapeUiRuntime ui;
		const auto category = ShapePalmToolCategory(DrawingTool::Shape,ui); // 必须在该 Down 创建构造前锁存。
		const auto count = canvas->Strokes().size(), operations = pages[0].operationCursor, historyCount = pages[0].history.Items().size();
		auto contact = begin({preset,DefaultCreationMode(preset)},{80,70},{},InputDeviceType::Touch);
		const auto state = contact->shape.construction; ui.construction = state;
		contact->qpcOrigin = contact->lastInputSnapshot.qpc;
		contact->palm.Begin(preferences,scale,category,contact->qpcOrigin,1000);
		if (phase == ContactPhase::Move) move(*contact,{150,110},{180,135});
		auto area = contact->lastInputSnapshot; area.phase = phase;
		area.contactSize = {100,100}; area.contactAreaUnits = SpeedEraser::ContactAreaUnits::CanvasPixels;
		auto candidate = contact->palm;
		check(GetToolCapabilities(preset).multiStage && state->session.Status().active && state->contactActive && candidate.eligible &&
			candidate.Observe(ContactAreaFromSnapshot(area),area.qpc) && candidate.active,
			"idle multi-stage Touch Down creates a construction without losing palm eligibility");
		int abandoned = 0;
		const std::function<void(RuntimeStroke&)> abandon = [&](RuntimeStroke& runtime)
		{
			++abandoned;
			check(runtime.shape.construction == state && runtime.shape.active,"abandon hook runs before runtime shape reset");
			CancelShapeConstruction(runtime.shape.construction,ui);
		};
		check(!PromotePalmRuntime(*contact,candidate,area,configuration,1000) && abandoned == 0 && ui.construction == state,
			"construction promotion requires an owner cancellation hook");
		auto invalidModel = configuration; invalidModel.modelParams.sampling_params.min_output_rate = 0;
		check(!PromotePalmRuntime(*contact,candidate,area,invalidModel,1000,abandon) && abandoned == 0 &&
			ui.construction == state && state->session.Status().active && state->contactActive && !contact->palm.active,
			"failed palm preparation preserves the original construction and contact");
		ui.completedConstruction.finished = true; // 取消也必须撤掉旧完成提示。
		check(PromotePalmRuntime(*contact,candidate,area,configuration,1000,abandon) && abandoned == 1 &&
			contact->tool == DrawingTool::Eraser && contact->palm.active && !contact->shape.active && !contact->shape.construction && !contact->shape.object,
			"idle multi-stage Down or Move promotes through the production cancellation hook");
		check(!ui.construction && !ui.completedConstruction.finished && ui.constructionVisualDirty && state->cancelled &&
			!state->session.Status().active && !state->contactActive && !state->preview && !state->renderData.bounds.valid && state->visualPoints.empty(),
			"promotion cancels the session, releases contactActive and removes construction previews");
		check(canvas->Strokes().size() == count && pages[0].operationCursor == operations && pages[0].history.Items().size() == historyCount,
			"abandoned construction adds neither a stored shape nor a history item");
		auto terminal = area; terminal.qpc += 20;
		check(input.PublishCancelled(0x5A91,contact->handle.record->ContactId(),terminal),"promoted test contact terminates");
		input.Recycle(contact->handle);
	}
	// 大面积触摸不能抢走构造/编辑触点；资格在 Down 锁存，释放仍走原构造确认。
	{
		PalmEraser::Settings preferences; preferences.enabled = true; preferences.parallelWriting = false;
		const PalmEraser::Scale scale{1,1,true,false};
		auto first = begin({PresetKind::Polyline,CreationMode::Points},{80,70});
		const auto state = first->shape.construction;
		check(!up(*first,{80,70}) && state->session.Status().active && !state->contactActive,"pre-existing construction waits for its next contact");
		ShapeUiRuntime ui; ui.construction = state;
		const auto category = ShapePalmToolCategory(DrawingTool::Shape,ui);
		auto contact = begin({PresetKind::Polyline,CreationMode::Points},{100,90},state,InputDeviceType::Touch);
		auto area = contact->lastInputSnapshot; area.qpc += 20; area.phase = ContactPhase::Move;
		area.contactSize = {100,100}; area.contactAreaUnits = SpeedEraser::ContactAreaUnits::CanvasPixels;
		contact->palm.Begin(preferences,scale,category,contact->lastInputSnapshot.qpc,1000);
		check(!contact->palm.eligible && !contact->palm.Observe(ContactAreaFromSnapshot(area),area.qpc) && !contact->palm.active,
			"shape touch during construction is not palm eligible");
		const std::vector<RuntimeStroke*> contacts{contact.get()};
		check(!PalmBlocksDrawing(*contact) && !PalmNeighborBlocked(contacts,area.position,area.qpc),
			"suppressed construction touch neither blocks writing nor reserves a palm neighborhood");
		PalmEraser::State candidate; candidate.Begin(preferences,scale,PalmToolCategory(contact->tool),contact->lastInputSnapshot.qpc,1000);
		check(candidate.Observe(ContactAreaFromSnapshot(area),area.qpc) && candidate.active,
			"same area would activate an ordinary shape palm");
		int abandoned = 0;
		const std::function<void(RuntimeStroke&)> abandon = [&](RuntimeStroke& runtime)
		{ ++abandoned; CancelShapeConstruction(runtime.shape.construction,ui); };
		check(!PromotePalmRuntime(*contact,candidate,area,configuration,1000,abandon) && abandoned == 0 &&
			contact->shape.construction == state && state->contactActive,
			"Down eligibility guard preserves a pre-existing construction even with a cancellation hook");
		check(!up(*contact,{150,110}) && !state->contactActive && state->session.Status().active,
			"suppressed construction touch Up releases contactActive and keeps the next stage usable");
		ui.construction.reset();
		for (bool focusOnly : {false,true})
		{
			if (focusOnly) ui.focus.pending.push_back({}); else ui.editor.object = ShapeObject{};
			PalmEraser::State editing; editing.Begin(preferences,scale,ShapePalmToolCategory(DrawingTool::Shape,ui),area.qpc,1000);
			check(!editing.eligible,"shape touch during object edit or focus mode is not palm eligible");
			ui.editor.object.reset(); ui.focus.pending.clear(); // 模拟 beginShapeContact 结束编辑，本触点仍沿用 Down 决定。
			check(!editing.Observe(ContactAreaFromSnapshot(area),area.qpc) && !editing.active,
				"ending edit mode cannot enable palm promotion on the swallowed touch");
			RuntimeStroke swallowed(configuration.expectedSpeed); swallowed.tool = DrawingTool::Shape; swallowed.palm = editing;
			check(!PromotePalmRuntime(swallowed,candidate,area,configuration,1000,abandon) && abandoned == 0 && swallowed.tool == DrawingTool::Shape,
				"promotion cannot override the edit-mode Down decision with an eligible candidate");
		}
		PalmEraser::State outside; outside.Begin(preferences,scale,ShapePalmToolCategory(DrawingTool::Shape,ui),area.qpc,1000);
		check(outside.eligible && outside.Observe(ContactAreaFromSnapshot(area),area.qpc) && outside.active,
			"shape touch outside construction and editing remains palm eligible");
		ui.construction = state;
		for (DrawingTool tool : {DrawingTool::Pen,DrawingTool::Eraser})
		{
			PalmEraser::State ordinary; ordinary.Begin(preferences,scale,ShapePalmToolCategory(tool,ui),area.qpc,1000);
			check(ordinary.eligible,"shape UI state leaves ordinary pen and eraser palm eligibility unchanged");
		}
		const auto replacement = std::make_shared<ShapeConstructionRuntime>(); ui.construction = replacement; ui.completedConstruction.finished = true;
		CancelShapeConstruction(state,ui);
		check(ui.construction == replacement && ui.completedConstruction.finished && state->cancelled && !state->session.Status().active,
			"cancelling a stale contact construction preserves the newer construction and completion state");
	}
	for (uint32_t value = uint32_t(PresetKind::Arc); value <= uint32_t(PresetKind::Pyramid); ++value)
	{
		const auto preset = static_cast<PresetKind>(value); const ToolSpec tool{preset,CreationMode::MultiStage,5};
		const auto count = canvas->Strokes().size(), operations = pages[0].operationCursor;
		auto first = begin(tool,{80,70}); auto state = first->shape.construction; const auto id = state->identity.objectId;
		move(*first,{150,110},{180,135});
		check(first->shape.object.has_value(),"first stage prediction produces preview");
		check(!CommitRuntimeStoredStrokeCpu(*first,document,pages,0,0,StoredStrokeCommitMode::Fatal,allocate),"Fatal refuses unconfirmed first stage");
		check(!up(*first,{150,110}) && first->shape.constructionStepOnly && canvas->Strokes().size() == count && pages[0].operationCursor == operations,
			"first raw Up never appends or creates undo operation");
		auto second = begin(tool,{150,110},state);
		if (preset == PresetKind::Arc || preset == PresetKind::Sector)
		{
			const Vec2 center{80,70}, radius{70,40}; const float initial = std::atan2(radius.y,radius.x), length = std::hypot(radius.x,radius.y);
			for (int i = 1; i <= 24; ++i)
			{
				const float angle = initial + 4.4f * i / 24;
				move(*second,{center.x + length * std::cos(angle),center.y + length * std::sin(angle)},
					{center.x + length * std::cos(angle + .06f),center.y + length * std::sin(angle + .06f)});
			}
			check(!CommitRuntimeStoredStrokeCpu(*second,document,pages,0,0,StoredStrokeCommitMode::Fatal,allocate),"Fatal refuses second-stage preview before Up");
			const Vec2 end{center.x + length * std::cos(initial + 4.4f),center.y + length * std::sin(initial + 4.4f)};
			check(up(*second,end).has_value(),"large arc final raw Up creates one object");
			if (canvas->Strokes().size() > count)
				check(std::abs(std::get<ArcConstruction>(canvas->Strokes().back().Shape()->construction->parameters).sweepRadians - 4.4f) < .0001f,"predicted angle is absent from persisted sweep");
		}
		else
		{
			move(*second,{180,50},{195,30});
			check(!CommitRuntimeStoredStrokeCpu(*second,document,pages,0,0,StoredStrokeCommitMode::Fatal,allocate),"Fatal refuses solid second-stage preview");
			check(up(*second,{180,50}).has_value(),"solid final raw Up creates one compound object");
		}
		check(canvas->Strokes().size() == count + 1 && pages[0].operationCursor == operations + 1 && canvas->Strokes().back().Shape()->id == id,
			"two contacts share one stable id and one creation operation");
		check(ApplyRuntimeJournalCpu(*canvas,pages[0],false,allocate) && !pages[0].history.Find(pages[0].history.Items().back().id)->visible,
			"one undo hides complete multi-stage object");
		check(ApplyRuntimeJournalCpu(*canvas,pages[0],true,allocate),"one redo restores complete multi-stage object");
	}
	for (PresetKind preset : {PresetKind::Polyline,PresetKind::Polygon})
	{
		{
			// 快速连点：Up₁ 与 Down₂ 落在同一入口批次，Up₁ 已出队但帧末确认未执行；新 Down 须先确认上一步再续接，不能整根拒收。
			auto first = begin({preset,CreationMode::Points},{30,30}); const auto queued = first->shape.construction;
			RuntimeStroke probe(configuration.expectedSpeed); const std::vector<RuntimeStroke*> batch{first.get()};
			check(!FindPendingConstructionContact(batch,probe,queued),"finger still down keeps single-contact rejection");
			auto terminal = first->lastInputSnapshot; terminal.position = {30,30}; terminal.qpc += 20;
			check(input.PublishUp(0x5A91,first->handle.record->ContactId(),terminal) && input.TryReadSnapshot(first->handle,terminal),"queued Up is consumed");
			first->lastInputSnapshot = terminal; first->lastConsumedSequence = terminal.sequence;
			first->shape.rawEndpoint = {30,30}; first->ended = true; SetShapeVisualEndpoint(first->shape,{30,30});
			check(queued->contactActive,"ended contact stays active until end-of-frame confirmation");
			RuntimeStroke* pending = FindPendingConstructionContact(batch,probe,queued);
			check(pending == first.get(),"same-batch Down finds the ended unconfirmed contact");
			if (pending) ConfirmRuntimeConstructionContact(*pending);
			const auto confirmedPoints = queued->session.Points().size();
			check(!queued->contactActive && confirmedPoints >= 1,"early confirmation records the previous point and frees the session");
			check(!FindPendingConstructionContact(batch,probe,queued),"confirmed contact is not confirmed twice");
			(void)CommitRuntimeStoredStrokeCpu(*first,document,pages,0,0,StoredStrokeCommitMode::NormalUp,allocate); input.Recycle(first->handle);
			auto second = begin({preset,CreationMode::Points},{120,30},queued);
			check(!up(*second,{120,30}) && queued->session.Points().size() == confirmedPoints + 1,"next tap in the same batch adds its point");
			queued->session.Cancel(); queued->cancelled = true;
		}
		{
			auto firstDrag = begin({preset,CreationMode::Points},{10,10});
			check(firstDrag->shape.object.has_value() && !firstDrag->shape.renderData.batches.empty(),"first point is visible on Down");
			move(*firstDrag,{90,60},{95,65});
			check(firstDrag->shape.object && firstDrag->shape.object->geometry.vertices.size() == 2,"first edge is visible while dragging before Up");
			up(*firstDrag,{90,60},true);
		}
		const auto count = canvas->Strokes().size(), operations = pages[0].operationCursor; std::shared_ptr<ShapeConstructionRuntime> state;
		for (Vec2 point : {Vec2{20,20},Vec2{140,20},Vec2{140,100},Vec2{20,100}})
		{
			auto contact = begin({preset,CreationMode::Points},point,state); state = contact->shape.construction;
			move(*contact,point,{point.x + 5,point.y + 7}); check(!up(*contact,point),"point Up never commits document");
			const auto status = state->session.Status();
			check(status.canComplete == (status.confirmedSteps >= (preset == PresetKind::Polygon ? 3u : 2u)),"Complete observes actual point threshold");
		}
		check(state->session.CanComplete() && canvas->Strokes().size() == count,"point session waits for Complete");
		const auto beforeMove = state->session.Points();
		// 覆盖层命中的控件经真实Bridge FIFO进入同一构造会话，不能依赖穿透重新产生画布Down。
		{
			Bridge::StateBridge bridge; bridge.Reset();
			ShapeUiSnapshot published; published.identity = state->identity; published.identity.generation = 100;
			const auto scene = published.identity;
			ShapeUiAction drag; drag.identity = published.identity; drag.handleId = 1000;
			drag.down = {24,23}; drag.point = {44,43}; // 热区边缘按下保留相对位移。
			drag.kind = ShapeUiActionKind::ConstructionBeginPointEdit;
			check(bridge.PublishShapeAction(drag) == Bridge::CommandResult::Accepted,"construction point Down enters production bridge");
			Bridge::Command command;
			check(bridge.TryConsume(command) && command.shapeAction && ApplyConstructionPointAction(*state,&published,*command.shapeAction,scene,{}),
				"construction point Down reaches production runtime action handler");
			++published.identity.generation;
			drag.kind = ShapeUiActionKind::ConstructionPreviewPointEdit;
			check(bridge.PublishShapeAction(drag) == Bridge::CommandResult::Accepted,"construction preview enters bridge");
			drag.point = {49,48}; bool coalesced = false;
			check(bridge.PublishShapeAction(drag,&coalesced) == Bridge::CommandResult::Accepted && coalesced,"construction preview uses bounded latest intent");
			check(bridge.TryConsume(command) && ApplyConstructionPointAction(*state,&published,*command.shapeAction,scene,{}),"preview retains Down ticket across snapshot publication");
			check(state->session.Points().front() == (Vec2{45,45}) && state->session.Points().size() == beforeMove.size() && !state->renderData.batches.empty(),
				"overlay point drag updates live render data without adding vertex");
			drag.predictedPoint = Vec2{90,90};
			check(ApplyConstructionPointAction(*state,&published,drag,scene,{}) && state->session.Points().front() == (Vec2{45,45}),
				"predicted point preview does not change authoritative construction points");
			published.handles = ConstructionPreviewHandles(*state);
			check(!published.handles.empty() && published.handles.front().position == (Vec2{86,87}) && state->session.Points().front() == (Vec2{45,45}),
				"published construction handle follows predicted geometry without altering real points");
			drag.kind = ShapeUiActionKind::ConstructionCommitPointEdit; drag.point = {54,53};
			check(bridge.PublishShapeAction(drag) == Bridge::CommandResult::Accepted && bridge.TryConsume(command) &&
				ApplyConstructionPointAction(*state,&published,*command.shapeAction,scene,{}),"overlay Up commits final point through production dispatcher");
			check(!state->pointGesture && state->session.Points().front() == (Vec2{50,50}) && canvas->Strokes().size() == count,
				"overlay Up leaves construction active without a document append");
			check(state->session.Back() && state->session.Points() == beforeMove && state->session.Forward() && state->session.Points().front() == (Vec2{50,50}),
				"overlay point operation participates in construction undo redo");
			check(state->session.Back(),"restore original points for following physical-contact regression");
			RefreshConstructionRuntimePreview(*state,{});
			check(!ApplyConstructionPointAction(*state,&published,drag,scene,{}),"late overlay Up cannot add a new point after gesture retirement");
		}
		auto edit = begin({preset,CreationMode::Points},{20,20},state);
		check(edit->shape.object == state->preview && !edit->shape.renderData.batches.empty(),"point Down retains drawable preview before first Move");
		move(*edit,{30,35},{33,38});
		check(!up(*edit,{30,35}) && state->session.Points().size() == beforeMove.size() && state->session.Points().front() == (Vec2{30,35}),
			"existing point contact moves vertex without adding or completing");
		check(state->session.Back() && state->session.Points() == beforeMove && state->session.Forward() && state->session.Points().front() == (Vec2{30,35}),
			"point adjustment participates in construction undo redo");
		auto cancelledEdit = begin({preset,CreationMode::Points},{30,35},state);
		move(*cancelledEdit,{45,45},{48,48});
		check(!up(*cancelledEdit,{45,45},true) && state->session.Status().active && state->session.Points().front() == (Vec2{30,35}),
			"cancel point edit restores vertex and retains construction");
		auto added = begin({preset,CreationMode::Points},{0,60},state);
		check(!up(*added,{0,60}) && state->session.Points().size() == beforeMove.size()+1,"add point after adjusting existing vertex");
		check(state->session.Back() && state->session.Points().size() == beforeMove.size(),"undo addition retains adjusted vertices");
		const auto confirmed = state->session.Candidate();
		check(state->session.Back() && state->session.Status().canForward && state->session.Forward() && state->session.Candidate() == confirmed,"Back then Forward restores exact production vertices");
		auto object = state->session.Complete(); check(object.has_value(),"Complete returns real confirmed vertices");
		if (object)
		{
			object->revision = pages[0].shapeRevisionAllocator.Next(object->id,state->revision);
			object->transform.dx = canvas->Viewport().x; object->transform.dy = canvas->Viewport().y;
			check(AppendRuntimeConstructedShapeCpu(*canvas,pages[0],*object,allocate).has_value() && canvas->Strokes().size() == count + 1,
				"Complete shares production append transaction");
			check(canvas->Strokes().back().Shape()->geometry.vertices.back() == (Vec2{20,100}),"prediction does not replace final point");
			GeometryResult outline;
			check(BuildGeometry(*object,outline) && outline.parts[0].paths[0].closed == (preset == PresetKind::Polygon),"Complete closes only the polygon ring");
			check(pages[0].operationCursor == operations + 1 && ApplyRuntimeJournalCpu(*canvas,pages[0],false,allocate),"Complete creates exactly one undo operation");
			check(ApplyRuntimeJournalCpu(*canvas,pages[0],true,allocate),"one redo restores the complete object");
		}
	}
	const auto beforeCancel = canvas->Strokes().size();
	auto cancelled = begin({PresetKind::Cuboid,CreationMode::MultiStage},{80,70});
	move(*cancelled,{140,110},{170,130}); cancelled->shape.construction->session.Cancel(); cancelled->shape.construction->cancelled = true;
	check(!CommitRuntimeStoredStrokeCpu(*cancelled,document,pages,0,0,StoredStrokeCommitMode::Fatal,allocate),"cancelled inactive session cannot be fatally sealed");
	check(!up(*cancelled,{140,110}) && canvas->Strokes().size() == beforeCancel,"cancel barrier suppresses late real Up");
	auto changedPage = begin({PresetKind::Cuboid,CreationMode::MultiStage},{80,70}); auto changeState = changedPage->shape.construction;
	check(!up(*changedPage,{140,110}),"prepare page cancel between stages");
	auto late = begin({PresetKind::Cuboid,CreationMode::MultiStage},{140,110},changeState);
	move(*late,{170,50},{190,20}); changeState->session.Cancel(); changeState->cancelled = true;
	check(!up(*late,{170,50}) && canvas->Strokes().size() == beforeCancel,"page/tool barrier cancels second-stage late Up");
	ShapeUiSnapshot published; published.identity.session = 80; published.identity.generation = 8;
	published.identity.documentGeneration = 4; published.identity.displayGeneration = 3; published.identity.pageId = pageGuid.Bytes();
	published.identity.objectId = canvas->Strokes().back().Shape()->id; published.identity.objectRevision = canvas->Strokes().back().Shape()->revision;
	ShapeEditorRuntime editor; editor.object = canvas->Strokes().back().Shape(); editor.identity = published.identity;
	ShapeUiAction action; action.identity = published.identity; action.kind = ShapeUiActionKind::BeginEdit;
	check(ShapeEditorActionIdentityMatches(editor,&published,action,published.identity),"fresh Begin action validates current shown identity");
	editor.gestureActive = true; editor.gestureIdentity = action.identity;
	++published.identity.generation; ++published.identity.objectRevision; action.kind = ShapeUiActionKind::PreviewEdit;
	check(ShapeEditorActionIdentityMatches(editor,&published,action,published.identity),"gesture keeps Down identity across preview publication");
	auto staleScene = published.identity; ++staleScene.documentGeneration;
	check(!ShapeEditorActionIdentityMatches(editor,&published,action,staleScene),"page install invalidates frozen gesture");
	action.kind = ShapeUiActionKind::SetStyle;
	check(!ShapeEditorActionIdentityMatches(editor,&published,action,published.identity),"old property action cannot target later object revision");
	// 终结重试的Commit先执行后，已排队Esc仍可关闭同一编辑会话。
	auto terminalEditor = editor; terminalEditor.gestureActive = false;
	terminalEditor.object->revision = published.identity.objectRevision;
	action.kind = ShapeUiActionKind::CloseEditor;
	check(ShapeEditorActionIdentityMatches(terminalEditor,&published,action,published.identity),"same-session Close survives committed generation and object revision advance");
	++terminalEditor.identity.session;
	check(!ShapeEditorActionIdentityMatches(terminalEditor,&published,action,published.identity),"old Close cannot terminate a newer editor session");
	--terminalEditor.identity.session;
	auto otherObject = terminalEditor.object->id; otherObject[15] ^= 0xFF; terminalEditor.object->id = otherObject;
	check(!ShapeEditorActionIdentityMatches(terminalEditor,&published,action,published.identity),"old Close cannot terminate another object in same scene");
	terminalEditor.object = editor.object;
	check(!ShapeEditorActionIdentityMatches(terminalEditor,&published,action,staleScene),"old Close cannot cross page installation boundary");

	ShapeConstructionRuntime construction; construction.identity = published.identity; construction.identity.session = 81;
	ShapeUiSnapshot prompt = published; prompt.identity = construction.identity;
	action.identity = prompt.identity; action.kind = ShapeUiActionKind::ConstructionBack;
	check(ShapeConstructionActionIdentityMatches(construction,&prompt,action,prompt.identity),"current step Back is accepted");
	++prompt.identity.generation;
	check(!ShapeConstructionActionIdentityMatches(construction,&prompt,action,prompt.identity),"old step Back cannot mutate newer vertex state");
	action.kind = ShapeUiActionKind::ConstructionForward;
	check(!ShapeConstructionActionIdentityMatches(construction,&prompt,action,prompt.identity),"old Forward cannot restore a later step");
	action.identity = prompt.identity;
	check(ShapeConstructionActionIdentityMatches(construction,&prompt,action,prompt.identity),"current Forward validates full identity");
	construction.contactActive = true;
	check(!ShapeConstructionActionIdentityMatches(construction,&prompt,action,prompt.identity),"Forward waits for physical contact end");
	action.kind = ShapeUiActionKind::ConstructionCancel; construction.contactActive = true;
	check(ShapeConstructionActionIdentityMatches(construction,&prompt,action,prompt.identity),"cancel barrier remains valid during physical contact");
	// 属性/几何意图经同一纯编辑器形成候选，再走运行期Replace事务与统一journal。
	const auto item = pages[0].history.LastVisibleItem();
	if (item && editor.object)
	{
		const auto handles = BuildEditHandles(*editor.object);
		const auto found = std::find_if(handles.begin(),handles.end(),[](const EditHandle& h) { return h.kind == EditHandleKind::Vertex; });
		check(found != handles.end(),"current object exposes a construction vertex rather than an eight-point frame");
		if (found == handles.end()) throw std::runtime_error("shape edit vertex missing");
		const auto handle = *found;
		const auto before = *editor.object; const auto count = canvas->Strokes().size();
		check(editor.gesture.Begin(before,handle.id,handle.position),"production edit gesture begins on current object handle");
		const auto revision = pages[0].shapeRevisionAllocator.Next(before.id,before.revision);
		check(editor.gesture.Preview({handle.position.x + 10,handle.position.y + 7},revision),"production edit gesture builds candidate");
		const auto candidate = editor.gesture.Commit();
		check(candidate && CommitRuntimeShapeEditCpu(*canvas,pages[0],*item,before,*candidate,allocate).has_value() && canvas->Strokes().size() == count,
			"edit action replaces one logical item without append");
		check(ApplyRuntimeJournalCpu(*canvas,pages[0],false,allocate) && SameShapeSemantics(*canvas->Strokes()[pages[0].history.Find(*item)->strokeIndex].Shape(),before),
			"one undo restores geometry before gesture");
		pages[0].clearRedoAvailable = true;
		check(!pages[0].history.LastRedoItem() && !CanRedoClearAfterOperations(pages[0]),"real Replace redo beats Clear continuation when no append redo exists");
		check(ApplyRuntimeJournalCpu(*canvas,pages[0],true,allocate) && CanRedoClearAfterOperations(pages[0]),"redo edit first then permit Clear redo");
		check(ApplyRuntimeJournalCpu(*canvas,pages[0],false,allocate),"return to before geometry for cancel and new branch tests");
		const auto current = *canvas->Strokes()[pages[0].history.Find(*item)->strokeIndex].Shape();
		check(editor.gesture.Begin(current,handle.id,handle.position) && editor.gesture.Preview({handle.position.x + 20,handle.position.y + 4},
			pages[0].shapeRevisionAllocator.Next(current.id,current.revision)),"prepare cancellable current object preview");
		editor.gesture.Cancel();
		check(!editor.gesture.Commit() && canvas->Strokes()[pages[0].history.Find(*item)->strokeIndex].Shape() == std::optional<ShapeObject>(current),
			"cancelled edit never mutates CPU document or history");
		const auto cursor = pages[0].operationCursor; const auto invalidPoint = ApplyTransform(current.transform,current.geometry.vertices[1]);
		check(editor.gesture.Begin(current,handle.id,handle.position) && editor.gesture.Preview(invalidPoint,
			pages[0].shapeRevisionAllocator.Next(current.id,current.revision)) && editor.gesture.Candidate(),
			"degenerate polygon drag retains a clamped production preview");
		check(!editor.gesture.Commit() && pages[0].operationCursor == cursor &&
			canvas->Strokes()[pages[0].history.Find(*item)->strokeIndex].Shape() == std::optional<ShapeObject>(current),
			"invalid release does not replace the item or advance the shared journal");
		auto changedStyle = current.style; changedStyle.width += 2;
		const auto restyled = RestyleShape(current,changedStyle,pages[0].shapeRevisionAllocator.Next(current.id,current.revision));
		check(restyled && CommitRuntimeShapeEditCpu(*canvas,pages[0],*item,current,*restyled,allocate).has_value() && window.ProductVisualStyleSnapshot().shapeStyle == style,
			"style edit commits current object without altering next tool snapshot");
	}
	// 离散旋转与翻转共用属性候选和Replace历史，不能追加新对象。
	for (auto kind : {ShapeUiActionKind::Rotate45Clockwise,ShapeUiActionKind::Rotate90Clockwise})
	{
		auto drawn = begin({PresetKind::Rectangle,CreationMode::Drag},{40,50});
		const auto created = up(*drawn,{160,130}); check(created.has_value(),"rotation fixture commits");
		if (!created) continue;
		const auto item = pages[0].history.LastVisibleItem(); if (!item) continue;
		const auto before = *canvas->Strokes()[created->strokeIndex].Shape();
		ShapeUiAction rotate; rotate.kind = kind;
		const auto after = BuildShapePropertyCandidate(before,rotate,pages[0].shapeRevisionAllocator.Next(before.id,before.revision));
		const auto cursor = pages[0].operationCursor, count = canvas->Strokes().size();
		check(after && CommitRuntimeShapeEditCpu(*canvas,pages[0],*item,before,*after,allocate) &&
			pages[0].operationCursor == cursor+1 && pages[0].operations.back().kind == CanvasOperationKind::Replace && canvas->Strokes().size() == count,
			"rotation records one Replace without append");
		check(ApplyRuntimeJournalCpu(*canvas,pages[0],false,allocate) && SameShapeSemantics(*canvas->Strokes()[created->strokeIndex].Shape(),before),"rotation undo restores before geometry");
		check(after && ApplyRuntimeJournalCpu(*canvas,pages[0],true,allocate) && SameShapeSemantics(*canvas->Strokes()[created->strokeIndex].Shape(),*after),"rotation redo restores after geometry");
	}
	// SetStyle辅助开关只替换当前对象，撤销/重做和冷历史都沿用同一journal。
	for (auto preset : {PresetKind::Rectangle,PresetKind::Circle,PresetKind::Ellipse})
	{
		style.auxiliary = DefaultAuxiliary(preset);
		auto drawn = begin({preset,DefaultCreationMode(preset)},{40,50});
		const auto created = up(*drawn,{160,130}); check(created.has_value(),"default auxiliary drawing commits");
		if (!created) continue;
		const auto item = pages[0].history.LastVisibleItem(); if (!item) continue;
		const auto before = *canvas->Strokes()[created->strokeIndex].Shape();
		GeometryResult aux; check(BuildAuxiliaryGeometry(before,aux) && aux.parts.size() == (preset == PresetKind::Rectangle ? 0u : preset == PresetKind::Circle ? 1u : 2u),"default circle center and ellipse foci survive real Down and raw Up");
		ShapeUiAction toggle; toggle.kind = ShapeUiActionKind::SetStyle; toggle.style = before.style; toggle.style.auxiliary = preset == PresetKind::Rectangle ? Auxiliary::Diagonals : Auxiliary::None;
		const auto candidate = BuildShapePropertyCandidate(before,toggle,pages[0].shapeRevisionAllocator.Next(before.id,before.revision));
		const auto cursor = pages[0].operationCursor, count = canvas->Strokes().size();
		check(candidate && CommitRuntimeShapeEditCpu(*canvas,pages[0],*item,before,*candidate,allocate) &&
			pages[0].operationCursor == cursor + 1 && pages[0].operations.back().kind == CanvasOperationKind::Replace && canvas->Strokes().size() == count,
			"SetStyle auxiliary toggle records exactly one Replace");
		const auto cold = WhiteboardHistoryJson(*document.PageAt(0),pages[0]);
		InkPage restored(pageGuid); CanvasPageRuntimeState restoredRuntime;
		check(RestoreWhiteboardHistory(cold,restored,restoredRuntime),"auxiliary Replace cold history decodes");
		auto* restoredCanvas = restored.FindCanvas(kDefaultDeviceKey);
		if (restoredCanvas)
		{
			check(ApplyRuntimeJournalCpu(*restoredCanvas,restoredRuntime,false,allocate) &&
				restoredCanvas->Strokes()[created->strokeIndex].Shape()->style.auxiliary == style.auxiliary,"cold undo restores auxiliary before snapshot");
			check(ApplyRuntimeJournalCpu(*restoredCanvas,restoredRuntime,true,allocate) &&
				restoredCanvas->Strokes()[created->strokeIndex].Shape()->style.auxiliary == toggle.style.auxiliary,"cold redo restores auxiliary after snapshot");
		}
		check(ApplyRuntimeJournalCpu(*canvas,pages[0],false,allocate) &&
			canvas->Strokes()[created->strokeIndex].Shape()->style.auxiliary == style.auxiliary,"one undo restores object auxiliary");
		check(ApplyRuntimeJournalCpu(*canvas,pages[0],true,allocate) &&
			canvas->Strokes()[created->strokeIndex].Shape()->style.auxiliary == toggle.style.auxiliary && window.ProductVisualStyleSnapshot().shapeStyle == style,
			"one redo disables auxiliary without changing next stroke defaults");
		// 反向再切换一次，同样只能新增一条Replace，撤销与重做保留工具默认值。
		const auto enabledBefore = *canvas->Strokes()[created->strokeIndex].Shape();
		toggle.style.auxiliary = style.auxiliary;
		const auto reversed = BuildShapePropertyCandidate(enabledBefore,toggle,pages[0].shapeRevisionAllocator.Next(enabledBefore.id,enabledBefore.revision));
		const auto reverseCursor = pages[0].operationCursor;
		check(reversed && CommitRuntimeShapeEditCpu(*canvas,pages[0],*item,enabledBefore,*reversed,allocate)
			&& pages[0].operationCursor == reverseCursor+1 && pages[0].operations.back().kind == CanvasOperationKind::Replace,
			"reverse auxiliary toggle records one Replace");
		check(ApplyRuntimeJournalCpu(*canvas,pages[0],false,allocate) && canvas->Strokes()[created->strokeIndex].Shape()->style.auxiliary == enabledBefore.style.auxiliary,
			"one undo restores reverse auxiliary toggle");
		check(ApplyRuntimeJournalCpu(*canvas,pages[0],true,allocate) && canvas->Strokes()[created->strokeIndex].Shape()->style.auxiliary == style.auxiliary
			&& window.ProductVisualStyleSnapshot().shapeStyle == style,"reverse auxiliary redo preserves next tool defaults");
	}
	for (auto preset : {PresetKind::Triangle,PresetKind::Trapezoid})
	{
		auto drawn = begin({preset,DefaultCreationMode(preset)},{40,50});
		const auto created = up(*drawn,{160,130}); check(created.has_value(),"constraint fixture commits");
		if (!created) continue;
		const auto item = pages[0].history.LastVisibleItem(); if (!item) continue;
		const auto before = *canvas->Strokes()[created->strokeIndex].Shape();
		ShapeUiAction action; action.kind = ShapeUiActionKind::SetConstraint;
		action.constraint = preset == PresetKind::Triangle ? PlanarConstraint::Equilateral : PlanarConstraint::Isosceles;
		const auto candidate = BuildShapePropertyCandidate(before,action,pages[0].shapeRevisionAllocator.Next(before.id,before.revision));
		const auto cursor = pages[0].operationCursor;
		check(candidate && CommitRuntimeShapeEditCpu(*canvas,pages[0],*item,before,*candidate,allocate) &&
			pages[0].operationCursor == cursor+1 && pages[0].operations.back().kind == CanvasOperationKind::Replace,"constraint change records one Replace");
		check(ApplyRuntimeJournalCpu(*canvas,pages[0],false,allocate) &&
			canvas->Strokes()[created->strokeIndex].Shape()->construction->tool.constraint == PlanarConstraint::Free,"constraint undo restores free mode");
		check(ApplyRuntimeJournalCpu(*canvas,pages[0],true,allocate) &&
			canvas->Strokes()[created->strokeIndex].Shape()->construction->tool.constraint == action.constraint,"constraint redo restores selected mode");
	}
	// 复现Host先scene再实际动作的真实WindowController队列顺序，调用生产同场景门禁。
	for (auto workspace : {Bridge::Workspace::Desktop,Bridge::Workspace::Whiteboard,Bridge::Workspace::Presentation})
	{
		WindowController queue; std::optional<Bridge::PresentationTarget> target;
		CanvasCommand prelude;
		if (workspace == Bridge::Workspace::Presentation)
		{
			target.emplace(); target->key.bytes[0] = 1; target->pageIndex = 0; target->totalPages = 2; target->targetRevision = 9;
			prelude.type = CanvasCommandType::SetPresentationTarget; prelude.presentationTarget = std::make_shared<const Bridge::PresentationTarget>(*target);
		}
		else { prelude.type = CanvasCommandType::SetWorkspace; prelude.workspace = static_cast<uint8_t>(workspace); }
		ShapeUiSnapshot shown = published; shown.identity.workspace = static_cast<uint8_t>(workspace);
		ShapeEditorRuntime currentEditor; currentEditor.object = editor.object; currentEditor.identity = shown.identity;
		ShapeUiAction intent; intent.kind = ShapeUiActionKind::BeginEdit; intent.identity = shown.identity;
		CanvasCommand actual; actual.type = CanvasCommandType::ShapeInteraction; actual.shapeAction = std::make_shared<const ShapeUiAction>(intent);
		queue.EnqueueCanvasCommand(prelude); queue.EnqueueCanvasCommand(actual);
		CanvasCommand consumed; int accepted = 0; bool kept = true;
		while (queue.TryDequeueCanvasCommand(consumed))
		{
			if (ShapeCommandChangesScene(consumed,workspace,0,target)) { kept = false; ++shown.identity.documentGeneration; }
			if (consumed.type == CanvasCommandType::ShapeInteraction && kept &&
				ShapeEditorActionIdentityMatches(currentEditor,&shown,*consumed.shapeAction,shown.identity)) ++accepted;
		}
		check(kept && accepted == 1,"Host scene prelude preserves current object action in all workspaces");
		CanvasCommand changed = prelude;
		if (workspace == Bridge::Workspace::Presentation)
		{
			auto next = *target; next.pageIndex = 1; changed.presentationTarget = std::make_shared<const Bridge::PresentationTarget>(std::move(next));
		}
		else changed.workspace = static_cast<uint8_t>(workspace == Bridge::Workspace::Desktop ? Bridge::Workspace::Whiteboard : Bridge::Workspace::Desktop);
		check(ShapeCommandChangesScene(changed,workspace,0,target),"genuine Host scene change still invalidates old editing");
		if (workspace == Bridge::Workspace::Presentation)
		{
			CanvasCommand isolate; isolate.type = CanvasCommandType::SetWorkspace; isolate.workspace = static_cast<uint8_t>(workspace);
			check(ShapeCommandChangesScene(isolate,workspace,0,target),"bound Presentation to isolated scene is a real change within same workspace");
			check(!ShapeCommandChangesScene(isolate,workspace,0,std::nullopt),"already isolated Presentation scene declaration remains no-op");
		}

		queue.EnqueueCanvasCommand(prelude); actual.type = CanvasCommandType::Undo; queue.EnqueueCanvasCommand(actual);
		bool constructorPreserved = true; bool routedBack = false;
		while (queue.TryDequeueCanvasCommand(consumed))
		{
			if (ShapeCommandChangesScene(consumed,workspace,0,target)) constructorPreserved = false;
			if (consumed.type == CanvasCommandType::Undo && constructorPreserved) routedBack = true;
		}
		check(routedBack,"Host Undo prelude preserves construction for Back routing");
	}
	CanvasPageRuntimeState redoBoundary;
	redoBoundary.clearRedoAvailable = true; redoBoundary.operationsInitialized = true;
	redoBoundary.operations.push_back({CanvasOperationKind::Replace,{1,1},{},{}}); redoBoundary.operationCursor = 0;
	check(!CanRedoClearAfterOperations(redoBoundary),"pending Replace redo takes precedence over Clear redo");
	redoBoundary.operationCursor = 1;
	check(CanRedoClearAfterOperations(redoBoundary),"Clear redo becomes available only after all document operations");
	// 绘制线程最新预览槽：同手势只保留最后一条；同手势终结直接取代；其他命令前先执行旧预览；每帧最多一次。
	{
		ShapePreviewSlot slot; ShapeUiAction base; base.identity.session = 90; base.handleId = 3; base.down = {5,6};
		base.kind = ShapeUiActionKind::PreviewEdit;
		std::shared_ptr<const ShapeUiAction> flushed;
		for (int index = 0; index < 64; ++index)
		{
			auto preview = std::make_shared<ShapeUiAction>(base); preview->point = {static_cast<float>(index),0}; preview->angleDelta = static_cast<float>(index) * .1f;
			if (auto flush = slot.AcceptPreview(preview)) flushed = flush;
		}
		check(!flushed && slot.coalesced == 63 && slot.held && slot.held->point.x == 63,"64 same-gesture previews collapse into the newest held preview");
		auto latest = slot.AtBatchEnd();
		check(latest && latest->point.x == 63 && latest->angleDelta && std::abs(*latest->angleDelta - 6.3f) < .0001f && !slot.held,
			"batch end executes the newest pointer and preserves the accumulated trajectory angle");
		slot.executedThisFrame = 1;
		slot.AcceptPreview(std::make_shared<ShapeUiAction>(base));
		check(!slot.AtBatchEnd() && slot.held,"second preview in the same frame waits for the next frame");
		slot.BeginFrame();
		check(slot.AtBatchEnd() && !slot.held,"next frame drains the deferred preview");
		slot.AcceptPreview(std::make_shared<ShapeUiAction>(base));
		check(!slot.BeforeCommand(nullptr,true) && slot.held,"same-scene declaration does not interrupt preview coalescing");
		auto commit = base; commit.kind = ShapeUiActionKind::CommitEdit; const auto coalescedBefore = slot.coalesced;
		check(!slot.BeforeCommand(&commit,false) && !slot.held && slot.coalesced == coalescedBefore + 1,
			"same-gesture commit supersedes the pending preview because it carries the final point");
		slot.AcceptPreview(std::make_shared<ShapeUiAction>(base));
		check(slot.BeforeCommand(nullptr,false) && !slot.held,"unrelated command runs only after the older pending preview");
		auto other = base; other.handleId = 4;
		slot.AcceptPreview(std::make_shared<ShapeUiAction>(base));
		const auto older = slot.AcceptPreview(std::make_shared<ShapeUiAction>(other));
		check(older && older->handleId == 3 && slot.held && slot.held->handleId == 4,"different gesture flushes the older preview first");
		auto foreignCommit = commit; foreignCommit.identity.session = 91;
		check(slot.BeforeCommand(&foreignCommit,false) && !slot.held,"another session terminal cannot drop this gesture preview");
	}
	// 画布焦点：所有失焦原因的落定/丢弃决策表，以及待落定项必须是 Z 序可见后缀。
	{
		using R = CanvasFocusEndReason;
		bool table = true;
		for (unsigned index = 0; index < static_cast<unsigned>(R::Count); ++index)
		{
			const auto reason = static_cast<R>(index);
			const bool rebuild = reason == R::PageSwitch || reason == R::Resize || reason == R::DeviceLost || reason == R::ViewportRefresh;
			table = table && ResolveCanvasFocusEnd(reason) == (rebuild ? CanvasFocusEndAction::Drop : CanvasFocusEndAction::Materialize);
		}
		check(table,"every focus-loss reason materializes except whole-L2 rebuilds, which drop L1");
		const auto last = pages[0].history.LastVisibleItem();
		if (last && last->index > 0 && pages[0].history.Items()[last->index - 1].visible)
		{
			PendingL2Item top; top.item = *last;
			PendingL2Item middle; middle.item = pages[0].history.Items()[last->index - 1].id;
			check(CanvasFocusSession::IsVisibleSuffix(pages[0].history,std::span<const PendingL2Item>(&top,1)),"topmost visible item is a valid focus-layer suffix");
			check(!CanvasFocusSession::IsVisibleSuffix(pages[0].history,std::span<const PendingL2Item>(&middle,1)),"an item below another visible item is rejected as focus-layer content");
			const std::array<PendingL2Item,2> both{middle,top};
			check(CanvasFocusSession::IsVisibleSuffix(pages[0].history,both),"a contiguous visible tail is a valid multi-item suffix");
			CanvasFocusSession session; session.pending = {top};
			check(session.Active() && session.Find(*last) && !session.Find(middle.item),"focus session finds only its pending item");
		}
		else check(false,"focus suffix fixture has two visible tail items");
	}
	std::fprintf(stderr,"[Shape.Interaction] failures=%d\n",failures); return failures;
}
