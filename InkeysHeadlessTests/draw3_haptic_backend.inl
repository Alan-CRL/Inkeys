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
		// 生产 mailbox 驱动真实后端 fake；次序不明时只停止，不能沿用上一支笔。
		{
			Haptics::CanvasPointerMailbox pointers;
			Haptics::CanvasPointerRequest empty;
			pointers.Reset();
			check(!pointers.Take(empty) && pointers.TakeLeave(), "empty poll preserves pending stop");
			auto drain = [&](unsigned penContacts, bool waveformMatches = true)
			{
				if (pointers.TakeLeave()) backend.DetachPointer();
				Haptics::CanvasPointerRequest request;
				if (!pointers.Take(request)) return;
				backend.DetachPointer();
				if (Haptics::ContinuousCandidateAllowed(penContacts, request.inContact, waveformMatches) &&
					request.revision == pointers.Revision() && backend.AttachPointerId(request.pointerId))
					backend.TickContinuous(request.eraserHint ? HapticContinuousFeedback::InkContinuous :
						HapticContinuousFeedback::ChiselMarkerContinuous, -1);
			};
			pointers.Observe(1, false, true, false);
			check(pointers.Inspect().pending.pointerId == 1 && pointers.Inspect().pending.pointerId == 1,
				"diagnostic snapshots never consume pending Pointer");
			drain(0);
			check(backend.attachedPointerId == 1 && backend.continuousFeedbackStarted, "fresh hover preset uses observed pen");
			// 窗口线程在 TakeLeave 和 Take 之间重置并发布新接触，旧停止不能留到下一帧。
			check(!pointers.TakeLeave(), "owner consumed leave before interleaved contact");
			pointers.Reset(true);
			check(pointers.Observe(1, false, false, true), "new contact published between owner polls");
			Haptics::CanvasPointerRequest interleaved;
			check(pointers.Take(interleaved) && !pointers.Inspect().leave,
				"taking fresh request settles its preceding stop atomically");
			backend.DetachPointer();
			check(Haptics::ContinuousCandidateAllowed(1, interleaved.inContact, true) &&
				interleaved.revision == pointers.Revision() && backend.AttachPointerId(interleaved.pointerId) &&
				backend.TickContinuous(HapticContinuousFeedback::ChiselMarkerContinuous, -1),
				"interleaved fresh request restores feedback on observed pen");
			const int stopsAfterRestore = controller.stops;
			drain(1);
			check(controller.stops == stopsAfterRestore && backend.continuousFeedbackStarted,
				"next frame cannot stop new binding with preceding leave");
			int sent = controller.sends;
			const int stopped = controller.stops;
			const auto oldRevision = pointers.Revision();
			pointers.Observe(1, false, true, true);
			pointers.Reset(true); // 新 RTS Down 使此前 pending Pointer 也失效。
			drain(1);
			check(controller.sends == sent && controller.stops > stopped && !backend.pointerController &&
				pointers.Revision() != oldRevision, "RTS Down stops old hover and clears earlier Pointer request/Bar epoch");
			check(!pointers.Observe(1, false, false, false), "old hover Update cannot satisfy contact refresh");
			drain(1);
			check(controller.sends == sent, "RTS-first order cannot send to previous hover pen");
			factory.supportedId = 2;
			check(pointers.Observe(2, true, true, true), "new observed contact accepted after RTS Down");
			drain(1);
			check(controller.sends == ++sent && backend.attachedPointerId == 2 && controller.waveform == 0x100B,
				"fresh inverted contact gets required Ink on its own pen");
			pointers.Observe(3, false, true, true); // 反向次序：Pointer Down 已排队，RTS Down 随后到达。
			pointers.Reset(true);
			drain(1);
			check(controller.sends == sent && !backend.pointerController, "Pointer-first request invalidated by newer RTS Down");
			factory.supportedId = 3;
			check(pointers.Observe(3, false, false, true), "next fresh in-contact Update can restore after Pointer-first order");
			drain(1);
			check(controller.sends == ++sent && backend.attachedPointerId == 3, "only fresh contact restarts continuous feedback");
			check(!pointers.Observe(3, false, false, true), "ordinary repeated Update does not rebind or resend");
			pointers.Reset(true);
			pointers.Observe(3, false, true, true);
			drain(2);
			check(controller.sends == sent && !backend.pointerController, "multiple RTS pens drop ambiguous preset");
			pointers.Reset();
			drain(1);
			check(!pointers.Observe(3, false, false, true) && controller.sends == sent,
				"multiple-to-single terminal cannot revive old binding with late Update");
			pointers.Observe(3, false, true, false);
			drain(1);
			check(controller.sends == sent, "hover cannot borrow another active RTS contact");
			pointers.Observe(3, false, true, true);
			drain(1, false);
			check(controller.sends == sent, "locked runtime tool mismatch drops optional preset");
			pointers.Observe(3, false, true, true);
			pointers.Reset(); // Up/Cancel 清 pending，直到新的 Enter/Down 才允许新生命周期。
			drain(0);
			check(!pointers.Observe(3, false, false, true) && controller.sends == sent,
				"Up/cancel drops pending and late in-contact Update");
			pointers.Observe(3, false, true, true);
			drain(1);
			check(controller.sends == ++sent && backend.attachedPointerId == 3, "fresh Down can reuse numeric ID after terminal");
			backend.DetachPointer();
			factory.supportedId = 1;
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
