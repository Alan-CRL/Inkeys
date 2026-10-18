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
	const auto begin = [&](ToolSpec tool,Vec2 point,std::shared_ptr<ShapeConstructionRuntime> state = {})
	{
		window.SetActiveTool(DrawingTool::Shape); window.SetProductVisualStyle(0x12345680,3,context,tool,style);
		ContactSnapshot down; down.position = {point.x,point.y}; down.qpc = 100000 + 1000 * ++contactId;
		if (!input.PublishDown(0x5A91,contactId,InputDeviceType::Pen,down)) throw std::runtime_error("shape interaction Down");
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
		const auto count = canvas->Strokes().size(); std::shared_ptr<ShapeConstructionRuntime> state;
		for (Vec2 point : {Vec2{20,20},Vec2{140,20},Vec2{140,100},Vec2{20,100}})
		{
			auto contact = begin({preset,CreationMode::Points},point,state); state = contact->shape.construction;
			move(*contact,point,{point.x + 5,point.y + 7}); check(!up(*contact,point),"point Up never commits document");
		}
		check(state->session.CanComplete() && canvas->Strokes().size() == count,"point session waits for Complete");
		auto object = state->session.Complete(); check(object.has_value(),"Complete returns real confirmed vertices");
		if (object)
		{
			object->revision = pages[0].shapeRevisionAllocator.Next(object->id,state->revision);
			object->transform.dx = canvas->Viewport().x; object->transform.dy = canvas->Viewport().y;
			check(AppendRuntimeConstructedShapeCpu(*canvas,pages[0],*object,allocate).has_value() && canvas->Strokes().size() == count + 1,
				"Complete shares production append transaction");
			check(canvas->Strokes().back().Shape()->geometry.vertices.back() == (Vec2{20,100}),"prediction does not replace final point");
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
	action.kind = ShapeUiActionKind::ConstructionCancel; construction.contactActive = true;
	check(ShapeConstructionActionIdentityMatches(construction,&prompt,action,prompt.identity),"cancel barrier remains valid during physical contact");
	// 属性/几何意图经同一纯编辑器形成候选，再走运行期Replace事务与统一journal。
	const auto item = pages[0].history.LastVisibleItem();
	if (item && editor.object)
	{
		const auto handles = BuildEditHandles(*editor.object); const auto handle = handles.front();
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
		auto changedStyle = current.style; changedStyle.width += 2;
		const auto restyled = RestyleShape(current,changedStyle,pages[0].shapeRevisionAllocator.Next(current.id,current.revision));
		check(restyled && CommitRuntimeShapeEditCpu(*canvas,pages[0],*item,current,*restyled,allocate).has_value() && window.ProductVisualStyleSnapshot().shapeStyle == style,
			"style edit commits current object without altering next tool snapshot");
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
	std::fprintf(stderr,"[Shape.Interaction] failures=%d\n",failures); return failures;
}
