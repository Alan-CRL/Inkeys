#if defined(DRAW3_HAPTIC_TESTING)
	namespace
	{
		template<class Interface> struct FakeInspectable : Interface
		{
			ULONG references = 1;
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** value) override { if (value) *value = nullptr; return E_NOINTERFACE; }
			ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
			ULONG STDMETHODCALLTYPE Release() override { return --references; }
			HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING*) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel*) override { return E_NOTIMPL; }
		};
		struct FakeFeedback : FakeInspectable<ISimpleHapticsControllerFeedback>
		{
			uint16_t waveform = 0;
			HRESULT STDMETHODCALLTYPE get_Waveform(uint16_t* value) override { *value = waveform; return S_OK; }
			HRESULT STDMETHODCALLTYPE get_Duration(TimeSpanAbi*) override { return E_NOTIMPL; }
		};
		struct FakeFeedbackList : FakeInspectable<IVectorViewSimpleHapticsControllerFeedback>
		{
			std::array<FakeFeedback, 3> values;
			uint32_t count = 3;
			FakeFeedbackList() { values[0].waveform = 0x100B; values[1].waveform = 0x1003; values[2].waveform = 0x100E; }
			HRESULT STDMETHODCALLTYPE GetAt(uint32_t index, ISimpleHapticsControllerFeedback** value) override
			{ if (index >= count) return E_BOUNDS; *value = &values[index]; (*value)->AddRef(); return S_OK; }
			HRESULT STDMETHODCALLTYPE get_Size(uint32_t* value) override { *value = count; return S_OK; }
			HRESULT STDMETHODCALLTYPE IndexOf(ISimpleHapticsControllerFeedback*, uint32_t*, boolean*) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE GetMany(uint32_t, uint32_t, ISimpleHapticsControllerFeedback**, uint32_t*) override { return E_NOTIMPL; }
		};
		struct FakeController : FakeInspectable<ISimpleHapticsController>
		{
			FakeFeedbackList feedback;
			int sends = 0, stops = 0;
			bool failSend = false, failCapability = false;
			uint16_t waveform = 0;
			double intensity = -1;
			HRESULT STDMETHODCALLTYPE get_Id(HSTRING*) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE get_SupportedFeedback(IVectorViewSimpleHapticsControllerFeedback** value) override
			{ if (failCapability) return E_FAIL; *value = &feedback; (*value)->AddRef(); return S_OK; }
			HRESULT STDMETHODCALLTYPE get_IsIntensitySupported(boolean* value) override { *value = true; return S_OK; }
			HRESULT STDMETHODCALLTYPE get_IsPlayCountSupported(boolean* value) override { *value = false; return S_OK; }
			HRESULT STDMETHODCALLTYPE get_IsPlayDurationSupported(boolean* value) override { *value = false; return S_OK; }
			HRESULT STDMETHODCALLTYPE get_IsReplayPauseIntervalSupported(boolean*) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE StopFeedback() override { ++stops; return S_OK; }
			HRESULT STDMETHODCALLTYPE SendHapticFeedback(ISimpleHapticsControllerFeedback* value) override
			{ ++sends; value->get_Waveform(&waveform); intensity = -1; return failSend ? E_FAIL : S_OK; }
			HRESULT STDMETHODCALLTYPE SendHapticFeedbackWithIntensity(ISimpleHapticsControllerFeedback* value, double explicitIntensity) override
			{ auto result = SendHapticFeedback(value); intensity = explicitIntensity; return result; }
			HRESULT STDMETHODCALLTYPE SendHapticFeedbackForDuration(ISimpleHapticsControllerFeedback*, double, TimeSpanAbi) override { return E_NOTIMPL; }
			HRESULT STDMETHODCALLTYPE SendHapticFeedbackForPlayCount(ISimpleHapticsControllerFeedback*, double, int32_t, TimeSpanAbi) override { return E_NOTIMPL; }
		};
		struct FakePen2 : FakeInspectable<IPenDevice2>
		{
			ISimpleHapticsController* controller = nullptr;
			HRESULT STDMETHODCALLTYPE get_SimpleHapticsController(ISimpleHapticsController** value) override
			{ *value = controller; if (*value) (*value)->AddRef(); return S_OK; }
		};
		struct FakePen : FakeInspectable<IPenDevice>
		{
			FakePen2 property;
			bool hasProperty = true;
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override
			{
				*value = nullptr;
				if (!hasProperty || iid != __uuidof(IPenDevice2)) return E_NOINTERFACE;
				*value = &property; property.AddRef(); return S_OK;
			}
			HRESULT STDMETHODCALLTYPE get_PenId(GUID*) override { return E_NOTIMPL; }
		};
		struct FakePenFactory : FakeInspectable<IPenDeviceStatics>
		{
			FakePen pen;
			uint32_t supportedId = 1;
			int attempts = 0;
			HRESULT STDMETHODCALLTYPE GetFromPointerId(uint32_t id, IPenDevice** result) override
			{
				++attempts; *result = nullptr;
				if (id != supportedId) return E_FAIL;
				*result = &pen; pen.AddRef(); return S_OK;
			}
		};
		int testInitializations = 0, testUninitializations = 0;
		HRESULT testInitializationResult = S_OK;
		HRESULT WINAPI TestRoInitialize(int) { ++testInitializations; return testInitializationResult; }
		void WINAPI TestRoUninitialize() { ++testUninitializations; }
		const char* missingHapticExport = nullptr;
		FARPROC WINAPI ResolveTestHapticExport(HMODULE library, LPCSTR name)
		{
			return missingHapticExport && std::strcmp(missingHapticExport, name) == 0
				? nullptr : GetProcAddress(library, name);
		}
	}

	int RunPenHapticBackendTests()
	{
		int failures = 0;
		auto check = [&](bool result, const char* name)
		{ if (!result) { ++failures; std::cerr << "FAILED pen haptics backend: " << name << '\n'; } };
		{
			WinRtApi api;
			api.loadLibrary = [](const wchar_t*) noexcept -> HMODULE { return nullptr; };
			check(!api.Initialize(), "missing DLL is optional");
			api.Shutdown();
		}
		for (const char* name : { "RoInitialize", "RoUninitialize", "RoGetActivationFactory", "WindowsCreateString", "WindowsDeleteString" })
		{
			WinRtApi api;
			missingHapticExport = name;
			api.getProcAddress = &ResolveTestHapticExport;
			check(!api.Initialize(), name);
			api.Shutdown();
		}
		missingHapticExport = nullptr;
		for (HRESULT result : { S_OK, S_FALSE, RPC_E_CHANGED_MODE, E_FAIL })
		{
			WinRtApi api;
			api.available = true;
			api.roInitialize = &TestRoInitialize;
			api.roUninitialize = &TestRoUninitialize;
			testInitializations = testUninitializations = 0;
			testInitializationResult = result;
			check(api.Initialize() == (SUCCEEDED(result) || result == RPC_E_CHANGED_MODE), "COM optional initialization result");
			api.Shutdown();
			check(testInitializations == 1 && testUninitializations == (SUCCEEDED(result) ? 1 : 0), "S_OK/S_FALSE uninitialization paired; changed apartment borrowed");
		}
		PenHapticFeedback disabled(false);
		disabled.SetEnabled(false);
		check(!disabled.AttachPointerId(1) && !disabled.TickContinuous(HapticContinuousFeedback::InkContinuous) &&
			!disabled.PlayDiscrete(HapticDiscreteFeedback::Click), "disabled backend does not initialize or send");
		FakeController controller;
		FakePenFactory factory;
		factory.pen.property.controller = &controller;
		PenHapticFeedbackImpl backend;
		backend.initialized = true;
		backend.allowInputManagerFallback = false;
		check(!backend.IsAvailable() && !backend.AttachPointerId(1) &&
			!backend.PlayDiscrete(HapticDiscreteFeedback::Click, -1), "missing factory is no-op");
		backend.penDeviceStatics = &factory;
		check(backend.AttachPointerId(1), "exact pen controller attached");
		check(backend.TickContinuous(HapticContinuousFeedback::ChiselMarkerContinuous, -1) &&
			controller.waveform == 0x100E && controller.intensity == -1, "preferred highlighter/system intensity");
		const int firstSends = controller.sends;
		check(backend.TickContinuous(HapticContinuousFeedback::ChiselMarkerContinuous, -1) &&
			controller.sends == firstSends, "unchanged preset only sent once");
		check(backend.TickContinuous(HapticContinuousFeedback::EraserContinuous, -1) && controller.waveform == 0x100B,
			"missing optional waveform falls back to Ink");
		const int stops = controller.stops;
		backend.DetachPointer();
		check(controller.stops > stops && !backend.continuousFeedbackStarted && !backend.pointerController,
			"hover preset detach stops and releases");
		// 同一生产 mailbox 的 Begin/End 驱动真实后端 fake，重放实测只有 Enter/RTS/Leave 的序列。
		{
			Haptics::CanvasPointerMailbox pointers;
			Haptics::CanvasPointerRequest request, bound;
			auto consume = [&](HapticContinuousFeedback feedback)
			{
				if (pointers.TakeLeave()) backend.DetachPointer();
				if (!pointers.Take(request)) return false;
				const bool reuse = backend.pointerController && Haptics::SameCanvasBinding(bound, request);
				if (!reuse) backend.DetachPointer();
				if (request.revision != pointers.Revision() || (!reuse && !backend.AttachPointerId(request.pointerId))) return false;
				bound = request;
				return backend.TickContinuous(feedback, -1);
			};
			const char* reason = nullptr;
			check(!pointers.Observe(1, false, true, false, 0), "missing optional cursor API is safe no-op");
			check(pointers.Observe(1, false, true, false, 21), "hover records exact pointer/tablet cursor");
			check(consume(HapticContinuousFeedback::InkContinuous), "hover preset");
			const auto rangeRevision = pointers.Revision();
			const int hoverAttempts = factory.attempts;
			const int hoverStops = controller.stops;
			const int hoverSends = controller.sends;
			check(pointers.Begin(21, 7, 0, true, false) && consume(HapticContinuousFeedback::InkContinuous) &&
				factory.attempts == hoverAttempts && controller.stops == hoverStops && controller.sends == hoverSends,
				"first same-range Down preserves hover controller/preset without lookup or stop");
			check(pointers.End(7, 0, false), "first contact ends normally"); backend.StopFeedback();
			for (uint64_t generation : { 1ULL, 2ULL, 3ULL })
			{
				const int beforeDownStops = controller.stops, beforeDownSends = controller.sends;
				check(pointers.Begin(21, 7, generation, true, false, &reason), "RTS Down resumes same range without Pointer Update");
				check(pointers.Revision() == rangeRevision, "ordinary stroke keeps range epoch");
				check(consume(HapticContinuousFeedback::ChiselMarkerContinuous) && controller.waveform == 0x100E,
					"locked highlighter waveform starts on each RTS stroke");
				check(factory.attempts == hoverAttempts && controller.stops == beforeDownStops && controller.sends == beforeDownSends + 1,
					"normal Up then Down resends on retained controller without reattachment");
				check(pointers.End(7, generation, false), "normal Up retains range");
				backend.StopFeedback();
				check(!backend.continuousFeedbackStarted && !pointers.Take(request), "Up stops and clears pending stroke request");
			}
			check(pointers.Begin(21, 8, 4, true, false), "next contact starts");
			check(!pointers.End(7, 3, true), "late old terminal cannot cancel next contact");
			check(consume(HapticContinuousFeedback::InkContinuous), "next contact survives old cancellation");
			pointers.Reset(); backend.DetachPointer();
			check(!pointers.Begin(21, 8, 5, true, false, &reason) && !pointers.Take(request), "Leave removes old range");
			check(!pointers.Observe(1, false, false, true, 21), "late Update cannot resurrect range");
			check(pointers.Observe(1, true, true, true, 21), "RTS-first accepts later matching Pointer Down");
			check(consume(HapticContinuousFeedback::InkContinuous), "inverted exact pen gets Ink");
			check(pointers.End(8, 5, true), "cancel accepted"); backend.DetachPointer();
			check(!pointers.Begin(21, 9, 6, true, false), "cancel requires fresh range");
			pointers.Reset();
			check(pointers.Observe(1, false, true, false, 21) && pointers.Begin(21, 9, 7, true, false),
				"Pointer-first pending Enter survives matching RTS Down");
			check(consume(HapticContinuousFeedback::InkContinuous), "Pointer-first sends without another Pointer event");
			check(!pointers.Begin(22, 10, 8, true, false, &reason) && !pointers.Take(request), "different cursor cannot borrow hover device");
			check(!pointers.Observe(1, false, true, true, 21), "mismatching Pointer cannot borrow active RTS pen");
			pointers.Reset(); pointers.Observe(1, false, true, false, 21);
			check(!pointers.Begin(21, 11, 9, false, false) && !pointers.Take(request), "unknown/conflicting RTS source clears range");
			pointers.Reset(); pointers.Observe(1, false, true, false, 21);
			check(!pointers.Begin(21, 11, 10, true, true) && !pointers.Take(request), "multiple pens invalidate ambiguous range");
			pointers.Reset(); pointers.Observe(1, false, true, false, 21);
			pointers.Begin(21, 12, 11, true, false, nullptr, 4, 100);
			check(!pointers.End(12, 11, true, 5), "other tablet terminal cannot cancel contact");
			pointers.End(12, 11, false, 4);
			check(!pointers.Begin(21, 12, 12, true, false, nullptr, 4, 101), "source generation change rejects retained range");
			pointers.Reset(); pointers.Observe(1, false, true, false, 21);
			pointers.Begin(21, 12, 13, true, false);
			pointers.Reset(); pointers.Observe(1, false, true, false, 21);
			check(!pointers.End(12, 13, true) && pointers.Take(request), "reset invalidates old terminal ownership before new Enter");
			pointers.Reset(); pointers.TakeLeave();
			const auto emptyRevision = pointers.Revision();
			pointers.Reset(); pointers.Reset();
			check(pointers.Revision() == emptyRevision && !pointers.TakeLeave(), "idle blocked frames do not invalidate UI click epoch");
			backend.DetachPointer();
		}
		check(backend.AttachPointerId(1) && backend.PlayDiscrete(HapticDiscreteFeedback::Success, -1) && controller.waveform == 0x1003,
			"discrete optional waveform falls back to Click");
		check(!backend.AttachPointerId(2) && !backend.PlayDiscrete(HapticDiscreteFeedback::Click, -1), "failed new pen never uses old controller");
		const int failedAttempts = factory.attempts;
		check(!backend.AttachPointerId(2) && factory.attempts == failedAttempts, "failed ID cached within lifetime");
		backend.DetachPointer();
		factory.supportedId = 2;
		check(backend.AttachPointerId(2), "detach retries reused numeric pointer ID");
		controller.failSend = true;
		check(!backend.TickContinuous(HapticContinuousFeedback::InkContinuous, -1) &&
			!backend.PlayDiscrete(HapticDiscreteFeedback::Click, -1) && !backend.continuousFeedbackStarted, "send failure stays optional");
		controller.failSend = false;
		backend.DetachPointer();
		controller.feedback.count = 0;
		check(backend.AttachPointerId(2) && !backend.TickContinuous(HapticContinuousFeedback::InkContinuous, -1) &&
			!backend.PlayDiscrete(HapticDiscreteFeedback::Click, -1), "missing required waveforms no-op");
		backend.DetachPointer();
		controller.failCapability = true;
		check(backend.AttachPointerId(2) && !backend.TickContinuous(HapticContinuousFeedback::InkContinuous, -1), "capability HRESULT failure no-op");
		backend.DetachPointer();
		factory.pen.hasProperty = false;
		check(!backend.AttachPointerId(2), "missing PenDevice2 property no-op");
		backend.DetachPointer();
		factory.pen.hasProperty = true;
		factory.pen.property.controller = nullptr;
		check(!backend.AttachPointerId(2), "null pen controller no-op");
		backend.Shutdown();
		check(factory.references == 1 && factory.pen.references == 1 && factory.pen.property.references == 1 &&
			controller.references == 1, "shutdown releases all borrowed COM references");
		return failures;
	}
#endif
