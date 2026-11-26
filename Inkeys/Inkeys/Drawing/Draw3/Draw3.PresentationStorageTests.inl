
	namespace
	{
		using draw3::uink::Draw3UInkCanvasSnapshot;
		using draw3::uink::Draw3UInkClearSnapshot;
		using draw3::uink::Draw3UInkStrokeSnapshot;
		using draw3::uink::UInkGuid;

		struct PptStorageTest
		{
			const char* name;
			int failures = 0;

			void Check(bool condition, const char* message)
			{
				if (condition) return;
				++failures;
				std::fprintf(stderr, "[%s] FAIL: %s\n", name, message);
			}

			void Require(bool condition, const char* message)
			{
				Check(condition, message);
				if (!condition) throw std::runtime_error(message);
			}
		};

		struct PptStorageTestRoot
		{
			explicit PptStorageTestRoot(PptStorageTest& test) : test(test)
			{
				wchar_t temporary[MAX_PATH]{}, unique[MAX_PATH]{};
				const auto length = GetTempPathW(MAX_PATH, temporary);
				test.Require(length != 0 && length < MAX_PATH &&
					GetTempFileNameW(temporary, L"IPS", 0, unique), "reserve private temp path");
				test.Require(DeleteFileW(unique) && CreateDirectoryW(unique, nullptr),
					"create private PPT storage root");
				path = unique;
			}

			~PptStorageTestRoot()
			{
				try { test.Check(Clean(path), "remove private PPT storage root"); }
				catch (...) { test.Check(false, "remove private PPT storage root"); }
			}

			// 只枚举本用例独占创建的测试根，且不进入任何重解析点。
			static bool Clean(const std::wstring& folder)
			{
				WIN32_FIND_DATAW entry{};
				if (const HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &entry);
					find != INVALID_HANDLE_VALUE)
				{
					const std::unique_ptr<void, decltype(&FindClose)> owner(find, FindClose);
					do
					{
						const std::wstring name(entry.cFileName);
						if (name == L"." || name == L"..") continue;
						const auto child = folder + L"\\" + entry.cFileName;
						if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
						{
							if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
							{
								if (!RemoveDirectoryW(child.c_str())) return false;
							}
							else if (!Clean(child)) return false;
						}
						else if (!DeleteFileW(child.c_str())) return false;
					} while (FindNextFileW(find, &entry));
					if (GetLastError() != ERROR_NO_MORE_FILES) return false;
				}
				else if (GetLastError() != ERROR_FILE_NOT_FOUND) return false;
				return RemoveDirectoryW(folder.c_str()) != FALSE;
			}

			PptStorageTest& test;
			std::wstring path;
		};

		UInkGuid PptTestGuid(uint8_t marker)
		{
			std::array<uint8_t, 16> bytes{};
			bytes[0] = marker;
			bytes[6] = 0x40;
			bytes[8] = 0x80;
			bytes[15] = 0xA5;
			return UInkGuid(bytes);
		}

		Bridge::PresentationTarget PptTestTarget(uint8_t marker, std::vector<int32_t> slideIds)
		{
			Bridge::PresentationTarget target;
			target.key.bytes = PptTestGuid(marker).Bytes();
			target.bindingMode = Bridge::SlideBindingMode::StableSlideId;
			target.presentationName = "test-" + std::to_string(marker) + ".pptx";
			target.sourceIdentity = "path:c:\\lessons\\" + target.presentationName;
			target.totalPages = static_cast<uint32_t>(slideIds.size());
			target.slideIds = std::move(slideIds);
			target.slideId = target.slideIds.front();
			target.bindingRevision = 1;
			target.targetRevision = 1;
			target.sessionRevision = 1;
			return target;
		}

		PagedInkStoreOptions PptTestStoreOptions(const PptStorageTestRoot& root,
			const Bridge::PresentationTarget& target, const UInkGuid& workspace)
		{
			PagedInkStoreOptions options;
			options.root = root.path;
			options.naming.kind = PagedInkFileKind::Presentation;
			options.naming.presentationName.assign(target.presentationName.begin(), target.presentationName.end());
			options.naming.presentationKey = UInkGuid(target.key.bytes);
			options.pageContext.workspaceGuid = workspace;
			options.pageContext.workspaceType = 2;
			options.hostId = FormatPresentationKey(target.key);
			return options;
		}

		Draw3UInkStrokeSnapshot PptTestStroke(float x, uint32_t undoId = 0)
		{
			Draw3UInkStrokeSnapshot stroke;
			stroke.undoId = undoId;
			stroke.points = { { x, 10.0f, 3.0f }, { x + 8.0f, 18.0f, 3.0f } };
			return stroke;
		}

		Draw3UInkCanvasSnapshot PptTestPage(uint8_t marker, uint32_t index,
			std::optional<int32_t> slideId, std::optional<float> x, bool retained = false,
			bool endScreen = false, int32_t binding = 0)
		{
			Draw3UInkCanvasSnapshot page;
			page.pageGuid = PptTestGuid(marker);
			page.pageIndex = index;
			page.pageNumber = index + 1;
			page.slideId = slideId;
			if (binding) page.binding = binding;
			page.retained = retained;
			page.endScreen = endScreen;
			if (x) page.strokes.push_back(PptTestStroke(*x));
			return page;
		}

		PagedInkSaveRequest PptTestSave(Draw3UInkCanvasSnapshot page, uint64_t slot,
			uint64_t revision, PagedInkRequestKind kind = PagedInkRequestKind::Full,
			std::optional<uint64_t> boundary = std::nullopt)
		{
			return { { page.pageGuid, revision, slot, 0, boundary, kind }, std::move(page), std::nullopt };
		}

		PagedInkCompletion PptTestTake(PptStorageTest& test, PagedInkStore& store)
		{
			store.Drain();
			auto terminal = store.Take();
			test.Require(terminal.has_value(), "real worker publishes its reserved terminal");
			return std::move(*terminal);
		}

		PagedInkCompletion PptTestLoad(PptStorageTest& test, PagedInkStore& store, int32_t binding,
			uint64_t slot, uint64_t generation = 1)
		{
			test.Require(store.LoadLane({ binding, slot, generation }) == PagedInkAdmission::Accepted,
				"real worker accepts lane load");
			auto result = PptTestTake(test, store);
			test.Check(result.identity.kind == PagedInkRequestKind::LoadLane &&
				result.identity.laneBinding == binding && result.identity.slotGeneration == slot &&
				result.identity.loadGeneration == generation, "lane terminal retains request identity");
			return result;
		}

		const PagedInkLanePage* PptTestFindPage(const PagedInkCompletion& result, uint8_t marker)
		{
			const auto item = std::find_if(result.lanePages.begin(), result.lanePages.end(),
				[&](const auto& page) { return page.metadata.pageGuid == PptTestGuid(marker); });
			return item == result.lanePages.end() ? nullptr : &*item;
		}

		bool PptTestHasStroke(const Draw3UInkCanvasSnapshot& page, float x)
		{
			return page.strokes.size() == 1 && page.strokes.front().points.size() == 2 &&
				page.strokes.front().points.front().x == x;
		}

		bool PptTestHasStroke(const InkPage* page, float x)
		{
			const auto* canvas = page ? page->FindCanvas(kDefaultDeviceKey) : nullptr;
			return canvas && canvas->Strokes().size() == 1 &&
				canvas->Strokes().front().Points().size() == 2 &&
				canvas->Strokes().front().Points().front().x == x;
		}

		std::optional<std::string> PptTestReadBytes(const std::wstring& path)
		{
			const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return std::nullopt;
			const std::unique_ptr<void, decltype(&CloseHandle)> owner(file, CloseHandle);
			LARGE_INTEGER size{};
			if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 16 * 1024 * 1024)
				return std::nullopt;
			std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
			DWORD read = 0;
			if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
				read != bytes.size()) return std::nullopt;
			return bytes;
		}

		bool PptTestWriteBytes(const std::wstring& path, const std::string& bytes)
		{
			const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			const std::unique_ptr<void, decltype(&CloseHandle)> owner(file, CloseHandle);
			DWORD written = 0;
			return WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
				written == bytes.size() && FlushFileBuffers(file);
		}

		struct PptWorkerFaults
		{
			std::wstring readPath;
			uint64_t throwLoadGeneration = 0;
			uint64_t throwSaveRevision = 0;
			bool saveFailureOnce = false;

			static void BeforeWork(void* context, const PagedInkRequestIdentity& identity)
			{
				auto& self = *static_cast<PptWorkerFaults*>(context);
				if ((identity.kind == PagedInkRequestKind::LoadLane && self.throwLoadGeneration != 0 &&
					identity.loadGeneration == self.throwLoadGeneration) ||
					(self.throwSaveRevision != 0 && identity.revision == self.throwSaveRevision))
				{
					if (self.saveFailureOnce && identity.revision == self.throwSaveRevision) self.throwSaveRevision = 0;
					throw std::runtime_error("injected PPT worker exception");
				}
			}

			static const std::wstring* ReadPath(void* context, const std::wstring&)
			{
				const auto& self = *static_cast<PptWorkerFaults*>(context);
				return self.readPath.empty() ? nullptr : &self.readPath;
			}

			PagedInkStoreTestHook Hook() noexcept
			{
				return { .beforeWork = BeforeWork, .context = this, .readPathOverride = ReadPath };
			}
		};
	}

	namespace
	{
		DrawingDocumentSlot PptTestInstall(PptStorageTest& test, PagedInkStore& store,
			const PagedInkCompletion& loaded, const Bridge::PresentationTarget& target,
			const UInkGuid& workspace)
		{
			uint64_t token = 1;
			const auto allocate = [&] { return token++; };
			DrawingDocumentSlot slot;
			slot.presentationTarget = target;
			slot.slotGeneration = loaded.identity.slotGeneration;
			slot.presentationStore = &store;
			test.Require(TryCreateBlankDocumentSlot(slot, Bridge::PresentationDocumentPageCount(target),
				allocate), "create unresolved presentation slot");
			auto candidate = MaterializePresentationSlot(loaded, target, InkGuid(workspace.Bytes()), allocate);
			test.Require(candidate && InstallLoadedPresentationSlot(DocumentSlotRefs(slot), std::move(*candidate)),
				"install lane candidate as a group");
			slot.persistenceInitialized = true;
			return slot;
		}

		void PptTestFinishSaves(PptStorageTest& test, DrawingDocumentSlot& slot, bool allowFollowup = true)
		{
			auto& store = *slot.presentationStore;
			do
			{
				store.Drain();
				if (auto receipt = store.Take())
				{
					test.Check(receipt->identity.slotGeneration == slot.slotGeneration,
						"save terminal preserves CPU slot generation");
					CompletePresentationPageSave(DocumentSlotRefs(slot), *receipt, allowFollowup);
				}
				else return;
			} while (true);
		}

		void PptTestAppendInk(PptStorageTest& test, InkPage& page, CanvasPageRuntimeState& runtime, float x)
		{
			auto* canvas = page.GetOrCreateCanvas(kDefaultDeviceKey);
			test.Require(canvas != nullptr, "create test ink canvas");
			const auto strokeIndex = canvas->AppendStroke(InkStroke({}, {
				{ x, 10.0f, 3.0f }, { x + 8.0f, 18.0f, 3.0f } }));
			test.Require(strokeIndex.has_value(), "append test ink");
			auto footprint = BuildStrokeTileFootprint(canvas->Strokes()[*strokeIndex]);
			test.Require(footprint && runtime.history.AppendStroke(*strokeIndex, std::move(*footprint), true),
				"append undoable history");
		}

		void PptTestWrite(PptStorageTest& test, PagedInkStore& store, Draw3UInkCanvasSnapshot page,
			uint64_t slot, uint64_t revision = 7)
		{
			test.Require(store.Save(PptTestSave(std::move(page), slot, revision)) == PagedInkAdmission::Accepted,
				"seed page through real worker");
			const auto saved = PptTestTake(test, store);
			test.Require(saved.result == PagedInkResult::Succeeded && saved.confirmedRevision == revision,
				"seed page has a durable confirmation");
		}
	}


	namespace
	{
		void CheckPresentationClearAndEviction(PptStorageTest& test, const PptStorageTestRoot& root)
		{
			const auto target = PptTestTarget(0xD1, { 101 });
			const auto workspace = PptTestGuid(0x91);
			PptWorkerFaults fault;
			PagedInkStore store(PptTestStoreOptions(root, target, workspace), fault.Hook());
			PptTestWrite(test, store, PptTestPage(0x92, 0, 101, 10.0f), 7);
			auto slot = PptTestInstall(test, store, PptTestLoad(test, store, 0, 7), target, workspace);
			auto& runtime = slot.pageRuntimeStates[0];
			auto& state = slot.presentationPages[0];
			const auto guid = PptTestGuid(0x92);
			const auto initialBoundary = state.boundaryGeneration;
			uint64_t token = 1000;
			const auto allocate = [&] { return token++; };
			const auto clear = [&]
			{
				auto prefix = CaptureCanvasTail(*slot.document->PageAt(0), runtime, 0, 101);
				test.Require(prefix && ApplyCanvasClearDocument(*slot.document, 0, runtime,
					Bridge::Workspace::Presentation, allocate, {}), "CPU installs effective Clear before admission");
				CommitPresentationClear(DocumentSlotRefs(slot), std::move(*prefix));
				test.Check(runtime.intervalOrdinal == 1, "product uses only one Clear interval ordinal");
			};
			const auto restore = [&](DrawingDocumentSlot& destination,
				const Draw3UInkCanvasSnapshot& prefix, float expected)
			{
				auto& targetRuntime = destination.pageRuntimeStates[0];
				auto candidate = PrepareClearUndoFromSnapshot(*destination.document->PageAt(0),
					targetRuntime, prefix, allocate);
				test.Require(candidate.has_value() &&
					destination.document->ReplacePage(0, std::move(candidate->first)),
					"install previous interval into the same page GUID");
				targetRuntime = std::move(candidate->second);
				auto& targetState = destination.presentationPages[0];
				++targetState.revision;
				AdvancePresentationBoundary(targetState, false);
				test.Check(PptTestHasStroke(destination.document->PageAt(0), expected) &&
					targetRuntime.history.LastVisibleItem() && !targetRuntime.intervalOrdinal &&
					!targetState.previousIntervalOnDisk && targetState.Dirty() &&
					targetRuntime.clearRedoContinuation, "undo Clear restores the expected ink and successor redo");
			};
			clear();
			test.Require(runtime.boundaryFallback.has_value() && state.pendingWrites == 1,
				"unconfirmed clear retains a CPU fallback");
			auto beforeReceipt = slot;
			restore(beforeReceipt, *runtime.boundaryFallback, 10.0f);
			PptTestAppendInk(test, *slot.document->PageAt(0), runtime, 20.0f);
			++state.revision;
			clear();
			test.Require(state.pendingWrites == 2 && state.clearRevision == 10 &&
				runtime.boundaryFallback && PptTestHasStroke(*runtime.boundaryFallback, 20.0f),
				"two same-page Clears are independently admitted before either receipt is consumed");
			const auto first = PptTestTake(test, store);
			test.Check(first.result == PagedInkResult::Succeeded &&
				first.identity.kind == PagedInkRequestKind::Clear && first.identity.revision == 8 &&
				first.confirmedBoundary == PagedInkBoundaryIdentity{initialBoundary + 1, 8},
				"the first clear commits first");
			CompletePresentationPageSave(DocumentSlotRefs(slot), first, true);
			test.Check(runtime.boundaryFallback && PptTestHasStroke(*runtime.boundaryFallback, 20.0f),
				"an older Clear receipt cannot release the newer memory fallback");
			const auto second = PptTestTake(test, store);
			test.Check(second.result == PagedInkResult::Succeeded &&
				second.identity.kind == PagedInkRequestKind::Clear && second.identity.revision == 10 &&
				second.confirmedBoundary == PagedInkBoundaryIdentity{initialBoundary + 2, 10},
				"the second clear commits separately after the first");
			CompletePresentationPageSave(DocumentSlotRefs(slot), second, true);
			test.Require(!runtime.boundaryFallback && state.previousIntervalOnDisk == second.confirmedBoundary &&
				state.previousIntervalOnDisk && !state.Dirty() && !state.pendingWrites,
				"only matching confirmation releases fallback and records the disk prefix");
			const auto latestPage = PptTestLoad(test, store, 0, 7, 5);
			const auto* latest = PptTestFindPage(latestPage, 0x92);
			test.Check(latest && latest->snapshot && latest->snapshot->intervalOrdinal == 1 &&
				latest->snapshot->strokes.empty() && latest->snapshot->operations.size() == 2 &&
				std::holds_alternative<Draw3UInkClearSnapshot>(latest->snapshot->operations.back()),
				"latest page contains exactly its last prefix and one Clear");
			test.Require(first.confirmedBoundary && store.LoadPreviousInterval(
				{{guid, 7, 3}, *first.confirmedBoundary}) == PagedInkAdmission::Accepted,
				"submit an obsolete clear boundary");
			const auto stale = PptTestTake(test, store);
			test.Check(stale.result == PagedInkResult::Failed && stale.error == PagedInkError::BoundaryMismatch &&
				!stale.snapshot, "only the latest Clear interval can be read");
			runtime.intervalLoadPending = true;
			auto page = FindPresentationPage(DocumentSlotRefs(slot), guid);
			test.Require(page.has_value(), "find the current page by GUID");
			RequestPresentationPreviousInterval(DocumentSlotRefs(slot), *page);
			const auto previous = PptTestTake(test, store);
			test.Require(previous.result == PagedInkResult::Succeeded && previous.snapshot &&
				previous.identity.pageGuid == guid && previous.identity.loadGeneration == state.previousLoadGeneration &&
				previous.confirmedBoundary == state.previousIntervalOnDisk,
				"actual disk read returns the expected page, load generation and boundary");
			auto staleRead = previous;
			++staleRead.identity.loadGeneration;
			test.Check(!SettlePresentationPreviousInterval(DocumentSlotRefs(slot), *page, staleRead, allocate) &&
				runtime.intervalLoadPending && state.previousLoadBoundary,
				"stale previous-interval load generation does not consume the active request");
			auto superseded = slot;
			++superseded.presentationPages[0].boundaryGeneration;
			auto supersededPage = FindPresentationPage(DocumentSlotRefs(superseded), guid);
			test.Check(supersededPage && !SettlePresentationPreviousInterval(DocumentSlotRefs(superseded),
				*supersededPage, previous, allocate) && !superseded.pageRuntimeStates[0].history.LastVisibleItem(),
				"changed boundary generation rejects an otherwise valid disk restore");
			test.Require(SettlePresentationPreviousInterval(DocumentSlotRefs(slot), *page, previous, allocate),
				"real product settlement restores the disk prefix");
			test.Check(PptTestHasStroke(slot.document->PageAt(0), 20.0f) &&
				runtime.history.LastVisibleItem() && !runtime.intervalOrdinal && !state.previousIntervalOnDisk &&
				state.Dirty() && runtime.clearRedoContinuation, "disk Clear undo restores ink and its successor redo branch");
			test.Check(!CanEvictPresentationCpuSlot(slot), "undo Clear redo branch still protects the slot");
			runtime.clearRedoContinuation.reset();
			runtime.clearRedoAvailable = false;

			fault.throwSaveRevision = state.revision;
			test.Require(SavePresentationSlot(DocumentSlotRefs(slot)), "attempt dirty post-undo page save");
			const auto failed = PptTestTake(test, store);
			test.Check(failed.result == PagedInkResult::Failed && failed.error == PagedInkError::Exception,
				"real worker settles the injected write failure");
			CompletePresentationPageSave(DocumentSlotRefs(slot), failed, true);
			store.Drain();
			test.Check(!store.Take() && state.Dirty() && !state.pendingWrites &&
				!IsPresentationSlotDurable(DocumentSlotRefs(slot)) && !CanEvictPresentationCpuSlot(slot) &&
				PptTestHasStroke(slot.document->PageAt(0), 20.0f),
				"failed write never auto-retries, cannot evict and keeps the restored ink");
			fault.throwSaveRevision = 0;
			test.Require(SavePresentationSlot(DocumentSlotRefs(slot)), "next natural save retries the dirty page");
			PptTestFinishSaves(test, slot);
			test.Check(IsPresentationSlotDurable(DocumentSlotRefs(slot)) && CanEvictPresentationCpuSlot(slot),
				"the slot becomes evictable only after a later natural save durably confirms it");

			PptTestAppendInk(test, *slot.document->PageAt(0), runtime, 30.0f);
			++state.revision;
			fault.throwSaveRevision = state.revision;
			fault.saveFailureOnce = true;
			test.Require(SavePresentationSlot(DocumentSlotRefs(slot)) &&
				!SavePresentationSlot(DocumentSlotRefs(slot)) && state.saveRequestedWhileWriting,
				"a separate natural save while the latest revision is in flight records one intent");
			const auto inFlightFailure = PptTestTake(test, store);
			test.Check(inFlightFailure.result == PagedInkResult::Failed, "the in-flight write fails once");
			CompletePresentationPageSave(DocumentSlotRefs(slot), inFlightFailure, true);
			const auto followup = PptTestTake(test, store);
			test.Check(followup.result == PagedInkResult::Succeeded && followup.confirmedRevision == state.revision,
				"the independent save intent is consumed by exactly one fresh attempt");
			CompletePresentationPageSave(DocumentSlotRefs(slot), followup, true);
			const auto cold = PptTestLoad(test, store, 0, 11);
			auto reinstalled = PptTestInstall(test, store, cold, target, workspace);
			test.Check(!state.pendingWrites && !state.saveRequestedWhileWriting &&
				reinstalled.presentationPages[0].boundaryGeneration >= state.boundaryGeneration,
				"cold load of the canonical post-undo page cannot decrease its boundary generation");
		}

		void CheckPresentationReadDegradation(PptStorageTest& test, const PptStorageTestRoot& root)
		{
			const auto target = PptTestTarget(0xD2, { 102 });
			const auto workspace = PptTestGuid(0x93);
			auto page = PptTestPage(0x94, 0, 102, 10.0f);
			page.strokes.push_back(PptTestStroke(20.0f, 1));
			PptWorkerFaults fault;
			PagedInkStore store(PptTestStoreOptions(root, target, workspace), fault.Hook());
			PptTestWrite(test, store, page, 9);
			const auto healthy = PptTestLoad(test, store, 0, 9);
			auto clean = PptTestInstall(test, store, healthy, target, workspace);
			test.Check(CanEvictPresentationCpuSlot(clean), "healthy confirmed lane can evict");
			auto bytes = PptTestReadBytes(store.FilePath());
			test.Require(bytes.has_value(), "read healthy file for a same-length damaged copy");
			const auto firstKey = bytes->find("inkType");
			const auto secondKey = firstKey == std::string::npos ? std::string::npos :
				bytes->find("inkType", firstKey + 1);
			test.Require(secondKey != std::string::npos, "locate a second encoded ink block");
			(*bytes)[secondKey] = 'X';
			fault.readPath = root.path + L"\\partial.uink";
			test.Require(PptTestWriteBytes(fault.readPath, *bytes), "write the bounded read-degradation fixture");
			const auto partial = PptTestLoad(test, store, 0, 9, 2);
			const auto* damaged = PptTestFindPage(partial, 0x94);
			test.Require(damaged && damaged->result == PagedInkResult::Succeeded && damaged->readDegraded &&
				damaged->tailSkippedBlocks == 1, "real read reports a displayable partial page");
			auto active = PptTestInstall(test, store, partial, target, workspace);
			test.Check(active.presentationPages[0].degraded && PptTestHasStroke(active.document->PageAt(0), 10.0f) &&
				IsPresentationSlotDurable(DocumentSlotRefs(active)) && !CanEvictPresentationCpuSlot(active) &&
				!store.FileState().sealed, "degraded confirmed page displays intact ink and blocks eviction");
		}
	}

	int RunParkedPresentationRetainedSaveTest() noexcept
	{
		PptStorageTest test{ "Draw3PptRetained" };
		try
		{
			PptStorageTestRoot root(test);
			CheckPresentationClearAndEviction(test, root);
			CheckPresentationReadDegradation(test, root);
			const auto activeTarget = PptTestTarget(0xA1, { 101 });
			const auto parkedTarget = PptTestTarget(0xB1, { 101 });
			const auto activeWorkspace = PptTestGuid(0x10), parkedWorkspace = PptTestGuid(0x20);
			PagedInkStore activeStore(PptTestStoreOptions(root, activeTarget, activeWorkspace));
			PptWorkerFaults parkedFault;
			PagedInkStore parkedStore(PptTestStoreOptions(root, parkedTarget, parkedWorkspace), parkedFault.Hook());
			PptTestWrite(test, activeStore, PptTestPage(0x11, 0, 101, 10.0f), 1);
			PptTestWrite(test, activeStore, PptTestPage(0x13, 2, 102, 100.0f, true), 1);
			PptTestWrite(test, parkedStore, PptTestPage(0x21, 0, 101, 20.0f), 2);
			PptTestWrite(test, parkedStore, PptTestPage(0x23, 2, 102, 200.0f, true), 2);
			auto active = PptTestInstall(test, activeStore, PptTestLoad(test, activeStore, 0, 1),
				activeTarget, activeWorkspace);
			auto parked = PptTestInstall(test, parkedStore, PptTestLoad(test, parkedStore, 0, 2),
				parkedTarget, parkedWorkspace);
			test.Check(active.document->Pages().size() == 2 && parked.document->Pages().size() == 2,
				"each CPU slot keeps its independent unregistered EndScreen");
			++active.presentationPages[0].revision;
			++parked.presentationPages[0].revision;
			++active.retainedSlides.at(0).storage.revision;
			++parked.retainedSlides.at(0).storage.revision;

			// 退出先结算进程中已准入的失败写，然后独立重试该页，并覆盖活动及停放记录。
			parkedFault.throwSaveRevision = 8;
			parkedFault.saveFailureOnce = true;
			test.Require(SavePresentationSlot(DocumentSlotRefs(parked)), "parked writes are already in flight");
			PresentationCurrentLoadRetry exit;
			int acknowledgments = 0;
			bool durable = false;
			ProcessPresentationExitBarrier(exit, [&]
			{
				durable = SavePresentationAtExit([&](auto&& visit)
				{
					visit(DocumentSlotRefs(active));
					visit(DocumentSlotRefs(parked));
				}, [&](bool allowFollowup)
				{
					PptTestFinishSaves(test, active, allowFollowup);
					PptTestFinishSaves(test, parked, allowFollowup);
				});
			}, [&] { ++acknowledgments; });
			test.Check(acknowledgments == 1 && durable && IsPresentationSlotDurable(DocumentSlotRefs(active)) &&
				IsPresentationSlotDurable(DocumentSlotRefs(parked)) && !parkedFault.throwSaveRevision,
				"exit settles the in-flight failure, flushes and confirms active and parked pages before ACK");
			const auto diskA = PptTestLoad(test, activeStore, 0, 1, 2);
			const auto diskB = PptTestLoad(test, parkedStore, 0, 2, 2);
			const auto* aRetained = PptTestFindPage(diskA, 0x13);
			const auto* bActive = PptTestFindPage(diskB, 0x21);
			const auto* bRetained = PptTestFindPage(diskB, 0x23);
			test.Check(diskA.lanePages.size() == 2 && aRetained && aRetained->snapshot &&
				aRetained->confirmedRevision == 8 && PptTestHasStroke(*aRetained->snapshot, 100.0f) &&
				!PptTestFindPage(diskA, 0x23), "active file keeps its own retained identity, ink and revision");
			test.Check(diskB.lanePages.size() == 2 && bActive && bRetained && bActive->snapshot &&
				bRetained->snapshot && bActive->confirmedRevision == 8 && bRetained->confirmedRevision == 8 &&
				PptTestHasStroke(*bActive->snapshot, 20.0f) && PptTestHasStroke(*bRetained->snapshot, 200.0f) &&
				bRetained->metadata.slideId == 102 && bRetained->metadata.pageState == 1 &&
				!PptTestFindPage(diskB, 0x13), "parked file keeps its own active and retained ink and GUIDs");
			const auto wireB = draw3::uink::ReadUInkStoreFile(parkedStore.FilePath());
			test.Check(wireB.document && wireB.document->headerExtension && wireB.document->headerExtension->workspaces.size() == 1 &&
				wireB.document->headerExtension->workspaces.front().guid.Bytes() == parkedWorkspace.Bytes(),
				"parked wire workspace belongs to the parked presentation");
			active.retainedSlides.clear();
			++parked.retainedSlides.at(0).storage.revision;
			SavePresentationSlot(DocumentSlotRefs(parked));
			PptTestFinishSaves(test, parked);
			const auto diskAfterEmptyActive = PptTestLoad(test, parkedStore, 0, 2, 3);
			const auto* stillRetained = PptTestFindPage(diskAfterEmptyActive, 0x23);
			test.Check(stillRetained && stillRetained->confirmedRevision == 9 &&
				stillRetained->snapshot && PptTestHasStroke(*stillRetained->snapshot, 200.0f),
				"parked retained page is saved again even when the active retained collection is empty");
		}
		catch (...) { test.Check(false, "parked presentation fixture completed"); }
		if (!test.failures) std::fputs("[Draw3PptRetained] PASS: active and parked exit writes\n", stderr);
		return test.failures != 0;
	}

	int RunPresentationLoadedRetainedInstallTest() noexcept
	{
		PptStorageTest test{ "Draw3PptLoadedRetained" };
		try
		{
			PptStorageTestRoot root(test);
			const auto target = PptTestTarget(0xA1, { 101 });
			const auto workspace = PptTestGuid(0x41);
			PagedInkStore store(PptTestStoreOptions(root, target, workspace));
			PptTestWrite(test, store, PptTestPage(0x42, 0, 101, 10.0f), 7);
			PptTestWrite(test, store, PptTestPage(0x43, 1, std::nullopt, std::nullopt, false, true), 7);
			PptTestWrite(test, store, PptTestPage(0x44, 2, 102, 100.0f, true), 7);

			const auto loaded = PptTestLoad(test, store, 0, 7);
			auto slot = PptTestInstall(test, store, loaded, target, workspace);
			test.Check(slot.presentationTarget == target &&
				slot.document->WorkspaceGuid().Bytes() == workspace.Bytes() &&
				slot.document->PageAt(0)->PageGuid().Bytes() == PptTestGuid(0x42).Bytes() &&
				slot.document->PageAt(1)->PageGuid().Bytes() == PptTestGuid(0x43).Bytes() &&
				slot.retainedSlides.size() == 1 && slot.retainedSlides.front().slideId == 102 &&
				slot.retainedSlides.front().page.PageGuid().Bytes() == PptTestGuid(0x44).Bytes() &&
				slot.presentationPages[0].revision == 7 && slot.presentationPages[0].confirmedRevision == 7 &&
				slot.retainedSlides.front().storage.confirmedRevision == 7 &&
				PptTestHasStroke(slot.document->PageAt(0), 10.0f) &&
				PptTestHasStroke(&slot.retainedSlides.front().page, 100.0f) &&
				slot.pageRuntimeStates[0].history.LastVisibleItem() &&
				slot.retainedSlides.front().runtime.history.LastVisibleItem(),
				"production install preserves workspace, active history, retained history, end screen and confirmations");

			++slot.presentationPages[0].revision;
			++slot.retainedSlides.at(0).storage.revision;
			SavePresentationSlot(DocumentSlotRefs(slot));
			PptTestFinishSaves(test, slot);
			const auto written = PptTestLoad(test, store, 0, 7, 2);
			const auto* retained = PptTestFindPage(written, 0x44);
			const auto* slide = PptTestFindPage(written, 0x42);
			const auto* endScreen = PptTestFindPage(written, 0x43);
			test.Check(written.lanePages.size() == 3 && retained && slide && endScreen &&
				retained->snapshot && slide->snapshot && retained->confirmedRevision == 8 &&
				slide->confirmedRevision == 8 && retained->metadata.pageState == 1 &&
				endScreen->metadata.pageKind == 1 && !endScreen->metadata.slideId &&
				PptTestHasStroke(*retained->snapshot, 100.0f) && PptTestHasStroke(*slide->snapshot, 10.0f),
				"post-load per-page save preserves retained and active ink in official UInk fields");

			uint64_t token = 100;
			auto late = MaterializePresentationSlot(loaded, target, InkGuid(workspace.Bytes()), [&] { return token++; });
			PptTestAppendInk(test, *slot.document->PageAt(0), slot.pageRuntimeStates[0], 80.0f);
			++slot.presentationPages[0].revision;
			const auto previousRetainedGuid = slot.retainedSlides.front().page.PageGuid().Bytes();
			// 独立验证本地改动使候选失效，不能只依赖已初始化的快速拒绝。
			slot.persistenceInitialized = false;
			test.Check(late && !InstallLoadedPresentationSlot(DocumentSlotRefs(slot), std::move(*late)) &&
				slot.retainedSlides.size() == 1 &&
				slot.retainedSlides.front().page.PageGuid().Bytes() == previousRetainedGuid &&
				slot.presentationPages[0].revision == 9 &&
				slot.document->PageAt(0)->FindCanvas(kDefaultDeviceKey)->Strokes().size() == 2,
				"late load cannot overwrite new history, revision or independent retained identity");
		}
		catch (...) { test.Check(false, "loaded-retained fixture completed"); }
		if (!test.failures) std::fputs("[Draw3PptLoadedRetained] PASS: group install and per-page writes\n", stderr);
		return test.failures != 0;
	}

	int RunPendingPresentationTopologyLoadTest() noexcept
	{
		PptStorageTest test{ "Draw3PptTopologyLoad" };
		try
		{
			PptStorageTestRoot root(test);
			const auto original = PptTestTarget(0xA1, { 101, 102 });
			const auto workspace = PptTestGuid(0x41);
			PagedInkStore store(PptTestStoreOptions(root, original, workspace));
			PptTestWrite(test, store, PptTestPage(0x42, 0, 101, 10.0f), 7);
			PptTestWrite(test, store, PptTestPage(0x43, 1, 102, 20.0f), 7);
			PptTestWrite(test, store, PptTestPage(0xEE, 2, std::nullopt, std::nullopt, false, true), 7);
			test.Require(store.LoadLane({ 0, 7, 1 }) == PagedInkAdmission::Accepted,
				"submit cold load before a topology change");
			auto reduced = original;
			reduced.totalPages = 1;
			reduced.slideIds = { 101 };
			++reduced.targetRevision;
			const auto loaded = PptTestTake(test, store);
			auto slot = PptTestInstall(test, store, loaded, reduced, workspace);
			test.Require(slot.retainedSlides.size() == 1, "late lane completion accounts for the deleted slide");
			test.Check(slot.retainedSlides[0].slideId == 102 &&
				slot.retainedSlides[0].page.PageGuid().Bytes() == PptTestGuid(0x43).Bytes() &&
				PptTestHasStroke(&slot.retainedSlides[0].page, 20.0f) &&
				slot.retainedSlides[0].runtime.history.LastVisibleItem() &&
				slot.retainedSlides[0].storage.revision > slot.retainedSlides[0].storage.confirmedRevision &&
				slot.document->PageAt(1)->PageGuid().Bytes() == PptTestGuid(0xEE).Bytes(),
				"cold projection keeps deleted ink/history and moves the separate EndScreen");
			SavePresentationSlot(DocumentSlotRefs(slot));
			PptTestFinishSaves(test, slot);
			auto afterDelete = PptTestLoad(test, store, 0, 7, 2);
			const auto* removed = PptTestFindPage(afterDelete, 0x43);
			const auto* oldEnd = PptTestFindPage(afterDelete, 0xEE);
			test.Check(removed && oldEnd && removed->metadata.pageState == 1 && removed->metadata.pageIndex == 2 &&
				removed->confirmedRevision == 8 && oldEnd->metadata.pageKind == 1 &&
				oldEnd->metadata.pageIndex == 1 && oldEnd->confirmedRevision == 8,
				"delete writes the retained marker, new indices and revision for the EndScreen");

			auto reordered = original;
			reordered.slideIds = { 102, 101, 103 };
			reordered.totalPages = 3;
			reordered.slideId = 102;
			reordered.targetRevision = 3;
			uint64_t token = 100;
			const auto allocate = [&] { return token++; };
			// 保留前一个页序版本的写入终态，使重排确实与在途记录交错。
			++slot.presentationPages[0].revision;
			test.Require(SavePresentationSlot(DocumentSlotRefs(slot)), "queue write with the old topology before rebind");
			auto rebound = RebindStablePresentationTopology(DocumentSlotRefs(slot), reordered, allocate);
			test.Require(rebound.has_value(), "production hot topology rebind succeeds");
			SwapActiveDocumentSlot(DocumentSlotRefs(slot), *rebound);
			slot.presentationTarget = reordered;
			test.Check(slot.retainedSlides.empty() &&
				slot.document->PageAt(0)->PageGuid().Bytes() == PptTestGuid(0x43).Bytes() &&
				slot.document->PageAt(1)->PageGuid().Bytes() == PptTestGuid(0x42).Bytes() &&
				slot.pageRuntimeStates[0].history.LastVisibleItem() &&
				PptTestHasStroke(slot.document->PageAt(0), 20.0f) &&
				slot.document->PageAt(3)->PageGuid().Bytes() == PptTestGuid(0xEE).Bytes(),
				"reappearing 102 is active with the original GUID, history and ink after hot reorder");
			const auto path = store.FilePath();
			SavePresentationSlot(DocumentSlotRefs(slot));
			PptTestFinishSaves(test, slot);
			test.Check(store.FilePath() == path && IsPresentationSlotDurable(DocumentSlotRefs(slot)),
				"hot rebind appends metadata to the existing durable file");
			const auto afterAdd = PptTestLoad(test, store, 0, 7, 3);
			const auto* revived = PptTestFindPage(afterAdd, 0x43);
			const auto* shifted = PptTestFindPage(afterAdd, 0x42);
			const auto* end = PptTestFindPage(afterAdd, 0xEE);
			test.Check(afterAdd.lanePages.size() == 3 && revived && shifted && end && revived->snapshot &&
				revived->metadata.pageState.value_or(0) == 0 && revived->metadata.slideId == 102 &&
				revived->metadata.pageIndex == 0 && revived->metadata.pageNumber == 1 &&
				revived->confirmedRevision == 9 && PptTestHasStroke(*revived->snapshot, 20.0f) &&
				shifted->metadata.pageIndex == 1 && shifted->metadata.pageNumber == 2 &&
				shifted->confirmedRevision == 9 &&
				end->metadata.pageIndex == 3 && end->metadata.pageKind == 1 && !end->metadata.slideId,
				"reorder writes official page indices/numbers without registering the new empty placeholder");

			const auto rejected = [&](PagedInkCompletion malformed, const char* message)
			{
				test.Check(!MaterializePresentationSlot(malformed, original, InkGuid(workspace.Bytes()),
					allocate), message);
			};
			auto duplicateRetainedId = loaded;
			auto duplicateRetained = duplicateRetainedId.lanePages.at(1);
			duplicateRetained.metadata.pageState = 1;
			duplicateRetained.metadata.pageGuid = PptTestGuid(0x45);
			duplicateRetained.snapshot->pageGuid = duplicateRetained.metadata.pageGuid;
			duplicateRetainedId.lanePages.push_back(std::move(duplicateRetained));
			rejected(std::move(duplicateRetainedId), "duplicate active/retained SlideID is rejected");
			auto duplicateActive = loaded;
			auto repeated = duplicateActive.lanePages.at(0);
			repeated.metadata.pageGuid = PptTestGuid(0x46);
			repeated.snapshot->pageGuid = repeated.metadata.pageGuid;
			duplicateActive.lanePages.push_back(std::move(repeated));
			rejected(std::move(duplicateActive), "duplicate active SlideID is rejected");
			auto duplicatedGuid = loaded;
			auto* retainedPage = PptTestFindPage(duplicatedGuid, 0x43);
			auto duplicate = *retainedPage;
			duplicate.metadata.pageState = 1;
			duplicate.metadata.pageGuid = PptTestGuid(0x42);
			duplicate.snapshot->pageGuid = duplicate.metadata.pageGuid;
			duplicatedGuid.lanePages.push_back(std::move(duplicate));
			rejected(std::move(duplicatedGuid), "duplicate active/retained GUID is rejected");
			auto badId = loaded;
			badId.lanePages.front().metadata.slideId = -1;
			badId.lanePages.front().snapshot->slideId = -1;
			rejected(std::move(badId), "nonpositive source SlideID is rejected");
			auto twoEnds = loaded;
			auto extraEnd = *PptTestFindPage(twoEnds, 0xEE);
			extraEnd.metadata.pageGuid = PptTestGuid(0x48);
			extraEnd.snapshot->pageGuid = extraEnd.metadata.pageGuid;
			twoEnds.lanePages.push_back(std::move(extraEnd));
			rejected(std::move(twoEnds), "independent EndScreen must be unique");
		}
		catch (...) { test.Check(false, "topology load fixture completed"); }
		if (!test.failures) std::fputs("[Draw3PptTopologyLoad] PASS: cold and hot SlideID projection\n", stderr);
		return test.failures != 0;
	}

	int RunPresentationWorkerLoadRetryProbe() noexcept
	{
		PptStorageTest test{ "Draw3PptLoadRetry" };
		try
		{
			PptStorageTestRoot root(test);
			const auto target = PptTestTarget(0xB2, { 101 });
			const auto workspace = PptTestGuid(0x21);
			const auto page = PptTestPage(0x31, 0, 101, 10.0f);
			constexpr uint64_t generation = 7;
			PptWorkerFaults fault;
			PagedInkStore store(PptTestStoreOptions(root, target, workspace), fault.Hook());
			PptTestWrite(test, store, page, generation, 3);
			const auto file = store.FilePath();
			const auto original = PptTestReadBytes(file);
			test.Require(original.has_value(), "fixture reads the confirmed UInk bytes");
			const auto wire = draw3::uink::ReadUInkStoreFile(file);
			test.Check(wire.summary.outcome == draw3::uink::UInkReadOutcome::Complete && wire.document &&
				wire.document->headerExtension &&
				wire.document->headerExtension->workspaces.size() == 1 &&
				wire.document->headerExtension->workspaces.front().guid == workspace,
				"committed fixture has a readable explicit Workspace identity");

			fault.readPath = root.path + L"\\copy.uink";
			test.Require(PptTestWriteBytes(fault.readPath, *original), "prepare equal-length read fixture");
			int submitted = 0;
			const auto submit = [&](PagedInkLoadLaneRequest&& request)
			{
				test.Check(request.binding == 0 && request.slotGeneration == generation &&
					request.loadGeneration == generation, "retry keeps effective binding and CPU generation");
				++submitted;
				return store.LoadLane(request) == PagedInkAdmission::Accepted;
			};
			bool pending = false;
			PagedInkCompletion blocked;
			{
				// 只独占读夹具副本，使真实工作线程遇到共享失败，保留服务的写句柄。
				const HANDLE lease = CreateFileW(fault.readPath.c_str(), GENERIC_READ,
					0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				test.Require(lease != INVALID_HANDLE_VALUE, "hold lane fixture without sharing");
				const std::unique_ptr<void, decltype(&CloseHandle)> owner(lease, CloseHandle);
				pending = SubmitCurrentPresentationLoad(target, generation, submit);
				test.Require(pending, "first real lane load is accepted");
				blocked = PptTestTake(test, store);
			}
			const auto* failed = PptTestFindPage(blocked, 0x31);
			test.Check(blocked.result == PagedInkResult::Succeeded && blocked.identity.laneBinding == 0 &&
				blocked.identity.slotGeneration == generation && failed && failed->confirmedRevision == 3 &&
				failed->result == PagedInkResult::Failed && failed->error == PagedInkError::ReadFailed &&
				!failed->snapshot && IsPresentationReadRetryable(*failed),
				"sharing failure is retryable and stays attached to the registered page");
			uint64_t token = 1;
			const auto allocate = [&] { return token++; };
			test.Check(!MaterializePresentationSlot(blocked, target, InkGuid(workspace.Bytes()), allocate),
				"registered read failure keeps the lane closed to input");
			PresentationCurrentLoadRetry retry;
			retry.OnIoError(target, generation, 1000);
			pending = false;
			test.Check(!TrySubmitCurrentLoadAtRunSafePoint(retry, target, generation, 1249,
				pending, false, false, submit) && submitted == 1,
				"real failed load cannot submit before the first deadline");
			test.Require(TrySubmitCurrentLoadAtRunSafePoint(retry, target, generation, 1250, pending,
				false, false, submit), "unlocked fixture allows the due retry");
			test.Check(pending && submitted == 2 &&
				!TrySubmitCurrentLoadAtRunSafePoint(retry, target, generation, 1250, pending,
					false, false, submit), "one real retry is admitted at its deadline");
			auto loaded = PptTestTake(test, store);
			auto slot = PptTestInstall(test, store, loaded, target, workspace);
			retry.Cancel();
			test.Check(slot.presentationTarget == target && PptTestHasStroke(slot.document->PageAt(0), 10.0f) &&
				slot.presentationPages[0].confirmedRevision == 3 && slot.presentationPages[0].revision == 3,
				"successful same-lane retry restores the confirmed revision and original ink");
			test.Check(PptTestReadBytes(file) == original && PptTestReadBytes(fault.readPath) == original,
				"sharing failure and recovery leave confirmed and fixture bytes intact");

			auto other = target;
			other.key.bytes[0] ^= 1;
			const auto otherWorkspace = PptTestGuid(0x22);
			PagedInkStore isolated(PptTestStoreOptions(root, other, otherWorkspace));
			const auto isolatedRead = PptTestLoad(test, isolated, 0, generation);
			test.Check(isolatedRead.result == PagedInkResult::Succeeded && isolatedRead.lanePages.empty() &&
				isolated.FilePath().empty() && PptTestReadBytes(file) == original,
				"distinct full key cannot see or change this committed document");

			const auto rejectPage = [&](const std::string& bytes, const char* reason)
			{
				test.Require(PptTestWriteBytes(fault.readPath, bytes), "write exact damaged fixture");
				auto rejected = PptTestLoad(test, store, 0, generation);
				const auto* entry = PptTestFindPage(rejected, 0x31);
				test.Check(rejected.lanePages.size() == 1 && entry && entry->confirmedRevision == 3 &&
					entry->result == PagedInkResult::Failed && entry->error == PagedInkError::ReadFailed &&
					!entry->snapshot && !IsPresentationReadRetryable(*entry) && !MaterializePresentationSlot(rejected, target,
						InkGuid(workspace.Bytes()), allocate), reason);
				test.Check(PptTestReadBytes(file) == original && PptTestReadBytes(fault.readPath) == bytes,
					"rejected load preserves confirmed and damaged bytes");
			};
			const auto workspaceText = draw3::uink::FormatUInkGuid(workspace);
			auto wrongWorkspace = *original;
			const auto workspaceReference = wrongWorkspace.rfind(workspaceText);
			test.Require(workspaceReference != std::string::npos, "locate encoded workspace identity");
			wrongWorkspace[workspaceReference] = '9';
			rejectPage(wrongWorkspace, "wire workspace mismatch rejects the registered lane");
			auto wrongGuid = *original;
			const auto pageText = draw3::uink::FormatUInkGuid(page.pageGuid);
			const auto pageReference = wrongGuid.find(pageText);
			test.Require(pageReference != std::string::npos, "locate encoded page identity");
			wrongGuid[pageReference] = '9';
			rejectPage(wrongGuid, "externally changed page identity is rejected");
			rejectPage(original->substr(0, 1), "out-of-range page directory entry is rejected");
			rejectPage(original->substr(0, original->size() - 1), "truncated confirmed page range is rejected");
			auto unrelatedHeaderChange = *original;
			const auto hostReference = unrelatedHeaderChange.find(FormatPresentationKey(target.key));
			test.Require(hostReference != std::string::npos, "locate the header host identity");
			unrelatedHeaderChange[hostReference] = '9';
			test.Require(PptTestWriteBytes(fault.readPath, unrelatedHeaderChange), "write equal-size header-only fixture");
			const auto unaffected = PptTestLoad(test, store, 0, generation);
			const auto* unchanged = PptTestFindPage(unaffected, 0x31);
			test.Check(unchanged && unchanged->snapshot && PptTestHasStroke(*unchanged->snapshot, 10.0f),
				"page-range reads are unaffected by unrelated header bytes outside the confirmed range");

			fault.readPath.clear();
			fault.throwLoadGeneration = 19;
			const auto workerFailure = PptTestLoad(test, store, 0, generation, 19);
			test.Check(workerFailure.result == PagedInkResult::Failed &&
				workerFailure.error == PagedInkError::Exception && workerFailure.lanePages.empty() &&
				IsPresentationReadRetryable(workerFailure),
				"worker exception publishes one explicit failure without certifying an empty lane");
		}
		catch (...) { test.Check(false, "real retry fixture completed"); }
		return test.failures;
	}

	int RunPresentationCurrentLoadRetryProbe() noexcept
	{
		int failures = 0;
		const auto check = [&failures](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3PptLoadRetry] FAIL: %s\n", name);
		};
		Bridge::PresentationTarget target;
		target.key.bytes[0] = 0xB1;
		target.sourceIdentity = "path:c:\\lessons\\retry.pptx";
		target.bindingMode = Bridge::SlideBindingMode::StableSlideId;
		target.totalPages = 1;
		target.slideIds = { 101 };
		target.slideId = 101;
		target.targetRevision = 5;
		target.sessionRevision = 9;
		constexpr std::uint64_t generation = 7;
		int submitted = 0;
		const auto submit = [&](PagedInkLoadLaneRequest&& request)
		{
			check(request.binding == 0 && request.loadGeneration == generation &&
				request.slotGeneration == generation,
				"production Current request preserves target and slot generation");
			++submitted;
			return true;
		};
		bool loadPending = SubmitCurrentPresentationLoad(target, generation, submit);
		bool initialized = false;
		check(loadPending && submitted == 1 &&
			PresentationLoadUnresolved(Bridge::Workspace::Presentation,
				initialized, 0), "first async load keeps input closed");
		PresentationCurrentLoadRetry retry;
		loadPending = false; // 同一代次的 worker 返回一次性 IoError。
		retry.OnIoError(target, generation, 1000);
		check(!retry.TrySubmit(target, generation, 1249,
			loadPending, initialized, submit) && submitted == 1,
			"transient failure does not busy retry before deadline");
		const bool retried = retry.TrySubmit(target, generation, 1250,
			loadPending, initialized, submit);
		check(retried && loadPending && submitted == 2,
			"same targetRevision retries Current load once after deadline");
		check(!retry.TrySubmit(target, generation, 1250,
			loadPending, initialized, submit) && submitted == 2,
			"in-flight retry has no duplicate submit");
		// 空目录确认后才开放新 lane。该结算由真实 worker 用例另行验证。
		if (retried)
		{
			loadPending = false;
			initialized = true;
			retry.Cancel();
		}
		check(initialized && !PresentationLoadUnresolved(
			Bridge::Workspace::Presentation, initialized, 0),
			"verified empty lane ends recovery gate after a successful retry");
		PresentationCurrentLoadRetry repeated;
		std::uint64_t nowMs = 5000;
		bool repeatedPending = false;
		for (std::size_t index = 0;
			index < PresentationCurrentLoadRetry::kDelaysMs.size(); ++index)
		{
			repeated.OnIoError(target, generation, nowMs);
			const auto wait = repeated.RemainingWaitMilliseconds(
				target, generation, nowMs);
			check(wait && *wait == static_cast<double>(
				PresentationCurrentLoadRetry::kDelaysMs[index]),
				"retry wait follows bounded deadline without polling");
			nowMs += PresentationCurrentLoadRetry::kDelaysMs[index];
			const int before = submitted;
			check(repeated.TrySubmit(target, generation, nowMs,
				repeatedPending, false, submit) && repeatedPending &&
				submitted == before + 1,
				"each transient error permits only its due retry");
			repeatedPending = false; // 下一次 worker 仍返回 IoError。
		}
		repeated.OnIoError(target, generation, nowMs);
		const int afterLimit = submitted;
		check(!repeated.RemainingWaitMilliseconds(target, generation, nowMs) &&
			!repeated.TrySubmit(target, generation, nowMs + 10000,
				repeatedPending, false, submit) && submitted == afterLimit,
			"persistent IoError stops after four automatic submissions");
		PresentationCurrentLoadRetry loadedRetry;
		loadedRetry.OnIoError(target, generation, 20000);
		bool loadedPending = false;
		const bool loadedSubmitted = loadedRetry.TrySubmit(target, generation,
			20250, loadedPending, false, submit);
		const bool loadedInitialized = loadedSubmitted && loadedPending;
		if (loadedInitialized) loadedRetry.Cancel();
		check(loadedInitialized && !PresentationLoadUnresolved(
			Bridge::Workspace::Presentation, loadedInitialized, 0),
			"initialized lane after a retry also releases the input gate");
		PresentationCurrentLoadRetry terminal;
		terminal.OnIoError(target, generation, 2000);
		terminal.OnTerminalFailure();
		bool terminalPending = false;
		check(!terminal.TrySubmit(target, generation, 10000,
			terminalPending, false, submit) && !terminalPending,
			"terminal classification remains fail-closed without automatic retry");
		Bridge::PresentationTarget explicitTarget = target;
		++explicitTarget.targetRevision;
		terminal.Cancel();
		int explicitRequests = 0;
		check(SubmitCurrentPresentationLoad(explicitTarget, generation,
			[&](PagedInkLoadLaneRequest&& request)
			{
				++explicitRequests;
				return request.binding == 0 && request.loadGeneration == generation &&
					request.slotGeneration == generation;
			}) && explicitRequests == 1,
			"new explicit target can safely request a fresh Current read");
		PresentationCurrentLoadRetry replaced;
		replaced.OnIoError(target, generation, 2000);
		bool replacedPending = false;
		check(!replaced.TrySubmit(target, generation + 1, 10000,
			replacedPending, false, submit) && !replacedPending,
			"new slot generation cancels stale retry");
		replaced.OnIoError(target, generation, 2000);
		Bridge::PresentationTarget newer = target;
		++newer.targetRevision;
		check(!replaced.TrySubmit(newer, generation, 10000,
			replacedPending, false, submit) && !replacedPending,
			"new target revision cancels old retry");
		replaced.OnIoError(target, generation, 2000);
		replaced.Cancel();
		check(!replaced.TrySubmit(target, generation, 10000,
			replacedPending, false, submit),
			"Exit cancels a scheduled retry");
		PresentationCurrentLoadRetry exitBarrier;
		exitBarrier.OnIoError(target, generation, 1000);
		int exitCaptures = 0;
		int exitAcks = 0;
		ProcessPresentationExitBarrier(exitBarrier,
			[&] { ++exitCaptures; }, [&] { ++exitAcks; });
		check(exitBarrier.ExitPrepared() && !exitBarrier.AllowsLoad(false),
			"ACK terminal state also rejects fresh/manual Current loads");
		check(!CanvasCommandAllowedAfterExitBarrier(exitBarrier,
			CanvasCommandType::SetPresentationTarget) &&
			!CanvasCommandAllowedAfterExitBarrier(exitBarrier,
				CanvasCommandType::Clear) &&
			!CanvasCommandAllowedAfterExitBarrier(exitBarrier,
				CanvasCommandType::PrepareExitAutoSave) &&
			!CanvasCommandAllowedAfterExitBarrier(exitBarrier,
				CanvasCommandType::PresentationPersistenceCompleted),
			"terminal command gate blocks late target/Clear/completion Save");
		// 同一 command 批次可在退出 ACK 后收到迟到的 Current IoError。
		exitBarrier.OnIoError(target, generation, 1001);
		bool exitLoadPending = false;
		const int beforeExit = submitted;
		const bool afterAckSubmitted = TrySubmitCurrentLoadAtRunSafePoint(
			exitBarrier, target, generation, 1251, exitLoadPending, false,
			false, submit);
		check(!afterAckSubmitted && !exitLoadPending && submitted == beforeExit &&
			exitCaptures == 1 && exitAcks == 1,
			"PrepareExit ACK and late IoError cannot enqueue another Load");
		ProcessPresentationExitBarrier(exitBarrier,
			[&] { ++exitCaptures; }, [&] { ++exitAcks; });
		check(exitCaptures == 1 && exitAcks == 1,
			"duplicate PrepareExit cannot capture Save or ACK twice");
		PresentationCurrentLoadRetry failedCapture;
		int failedCaptureAck = 0;
		ProcessPresentationExitBarrier(failedCapture,
			[] { throw 1; }, [&] { ++failedCaptureAck; });
		check(failedCapture.ExitPrepared() && failedCaptureAck == 1,
			"exit capture failure still sends one final barrier ACK");
		PresentationCurrentLoadRetry directExit;
		directExit.OnIoError(target, generation, 1000);
		bool directExitPending = false;
		const int beforeDirectExit = submitted;
		check(!TrySubmitCurrentLoadAtRunSafePoint(directExit,
			target, generation, 1250, directExitPending, false, true, submit) &&
			!directExitPending && submitted == beforeDirectExit,
			"ExitRequested suppresses a due retry at the Run safe point");
		failures += RunPresentationWorkerLoadRetryProbe();
		if (failures == 0)
			std::fputs("[Draw3PptLoadRetry] PASS: bounded same-target Current recovery\n",
				stderr);
		return failures == 0 ? 0 : 1;
	}

	int RunFallbackStableControllerLaneProbe() noexcept
	{
		PptStorageTest test{ "Draw3PptLane" };
		const auto check = [&](bool ok, const char* message) { test.Check(ok, message); };
		try
		{
			PptStorageTestRoot root(test);
			auto stable = PptTestTarget(0xF1, { 202, 101 });
			auto fallback = stable;
			fallback.bindingMode = Bridge::SlideBindingMode::PageIndexFallback;
			fallback.slideIds.clear();
			fallback.slideId.reset();
			const auto workspaceGuid = PptTestGuid(0x10);
			PagedInkStore store(PptTestStoreOptions(root, stable, workspaceGuid));
			PptTestWrite(test, store, PptTestPage(0x20, 0, std::nullopt, 10.0f, false, false, 1), 1);
			const auto path = store.FilePath();
			const auto emptyStable = PptTestLoad(test, store, 0, 2);
			test.Check(emptyStable.result == PagedInkResult::Succeeded && emptyStable.lanePages.empty() &&
				emptyStable.identity.laneBinding == 0 && !path.empty(),
				"Stable lane is confirmed empty even when the document has fallback records");
			auto activeSlot = PptTestInstall(test, store, PptTestLoad(test, store, 1, 1), fallback, workspaceGuid);
			auto& document = activeSlot.document;
			auto& runtimes = activeSlot.pageRuntimeStates;
			auto& pageIndex = activeSlot.currentPageIndex;
			auto& activeTarget = activeSlot.presentationTarget;
			auto& generation = activeSlot.slotGeneration;
			auto& initialized = activeSlot.persistenceInitialized;
			auto& loadPending = activeSlot.loadPending;
			const auto oldWorkspaceGuid = document->WorkspaceGuid().Bytes();
			const auto oldPageGuid = document->PageAt(0)->PageGuid().Bytes();
			++activeSlot.presentationPages[0].revision;
			SavePresentationSlot(DocumentSlotRefs(activeSlot));
			Bridge::Workspace workspace = Bridge::Workspace::Presentation;
			std::optional<Bridge::PresentationKey> activeKey = fallback.key;
			PresentationParkedSlots parked;
			auto& source = parked[LaneFor(fallback)];
			DrawingDocumentSlot isolated;
			uint64_t nextSlotGeneration = 2, nextRasterToken = 100;
			const auto allocate = [&]() -> InkRasterStateToken { return ++nextRasterToken; };
			const auto active = DocumentSlotRefs(activeSlot);
			const auto switched = SwitchPresentationCpuSlot(workspace, activeKey, active, source,
				isolated, parked, nextSlotGeneration, stable, allocate);
			test.Require(switched.accepted && !switched.topologyConflict && document && activeTarget,
				"same source cross-mode switch selects a persistent new lane");
			check(activeKey == stable.key && activeTarget->bindingMode == Bridge::SlideBindingMode::StableSlideId &&
				document->Pages().size() == 3 && generation == 2 &&
				document->PageAt(0)->PageGuid().Bytes() != oldPageGuid &&
				document->PageAt(2)->PageGuid().Bytes() != document->PageAt(1)->PageGuid().Bytes(),
				"new Stable CPU lane reserves independent N+1 page identities");
			auto& old = parked.at(LaneFor(fallback));
			test.Require(old.document && old.presentationTarget == fallback && old.slotGeneration == 1 &&
				old.document->WorkspaceGuid().Bytes() == oldWorkspaceGuid &&
				old.document->PageAt(0)->PageGuid().Bytes() == oldPageGuid &&
				old.presentationPages[0].pendingWrites == 1 && old.presentationPages[0].confirmedRevision == 7 &&
				PptTestHasStroke(old.document->PageAt(0), 10.0f),
				"old fallback ink and pending page save stay in their original slot");
			auto saved = PptTestTake(test, store);
			PresentationPagedCompletion oldSave{ fallback.key, std::move(saved) };
			const auto saveRoute = RoutePresentationCompletion(oldSave, workspace, activeKey, active, parked);
			check(!saveRoute.active && saveRoute.parked == &old &&
				oldSave.completion.identity.slotGeneration == old.slotGeneration &&
				oldSave.completion.identity.pageGuid.Bytes() == oldPageGuid,
				"late fallback Save maps only to its original slot and page GUID");
			auto wrongPage = oldSave.completion;
			wrongPage.identity.pageGuid = PptTestGuid(0x31);
			CompletePresentationPageSave(DocumentSlotRefs(old), wrongPage, true);
			check(old.presentationPages[0].confirmedRevision == 7 && old.presentationPages[0].pendingWrites == 1,
				"an unowned page GUID cannot commit the pending write");
			CompletePresentationPageSave(DocumentSlotRefs(old), oldSave.completion, true);
			check(old.presentationPages[0].confirmedRevision == 8 && !old.presentationPages[0].pendingWrites,
				"real old-generation receipt settles only fallback");
			auto crossKey = oldSave;
			crossKey.key.bytes[15] ^= 1;
			const auto foreign = RoutePresentationCompletion(crossKey, workspace, activeKey, active, parked);
			check(!foreign.active && !foreign.parked, "foreign document key cannot commit this slot");
			auto stale = oldSave;
			stale.completion.identity.slotGeneration = 99;
			const auto staleRoute = RoutePresentationCompletion(stale, workspace, activeKey, active, parked);
			check(!staleRoute.active && !staleRoute.parked, "stale slot generation cannot alias another lane");

			for (const auto kind : { PagedInkRequestKind::LoadLane, PagedInkRequestKind::LoadPreviousInterval })
			{
				auto oldRead = oldSave;
				oldRead.completion.identity.kind = kind;
				if (kind == PagedInkRequestKind::LoadLane)
				{
					oldRead.completion.identity.laneBinding = 1;
					oldRead.completion.identity.pageGuid = {};
				}
				const auto route = RoutePresentationCompletion(oldRead, workspace, activeKey, active, parked);
				check(!route.active && route.parked == &old,
					"late fallback lane/previous interval terminal cannot update Stable");
			}
			const auto placeholder = document->PageAt(0)->PageGuid();
			test.Require(UpdatePresentationPageMetadata(active, stable),
				"successful empty manifest initializes existing page metadata");
			activeSlot.presentationStore = &store;
			initialized = true;
			activeSlot.emptyLaneVerified = true;
			check(document->PageAt(0)->PageGuid() == placeholder && CanRetireEmptyPresentationLane(active),
				"verified empty lane keeps its placeholder and retires independently of the other binding");
			auto wrongBinding = emptyStable;
			wrongBinding.identity.laneBinding = 1;
			check(!MaterializePresentationSlot(wrongBinding, stable, InkGuid(workspaceGuid.Bytes()), allocate),
				"empty fallback receipt cannot certify an empty Stable slot");
			for (const auto status : { PagedInkResult::Failed, PagedInkResult::Canceled, PagedInkResult::FileFailed })
			{
				auto failed = emptyStable;
				failed.result = status;
				check(!MaterializePresentationSlot(failed, stable, InkGuid(workspaceGuid.Bytes()), allocate),
					"failed empty-shaped receipt cannot publish blank ink");
			}

			PptTestAppendInk(test, *document->PageAt(0), runtimes[0], 20.0f);
			PptTestAppendInk(test, *document->PageAt(1), runtimes[1], 30.0f);
			PptTestAppendInk(test, *document->PageAt(2), runtimes[2], 40.0f);
			for (auto& page : activeSlot.presentationPages) ++page.revision;
			SavePresentationSlot(active);
			PptTestFinishSaves(test, activeSlot);
			const auto stableRead = PptTestLoad(test, store, 0, generation, 2);
			const auto fallbackRead = PptTestLoad(test, store, 1, old.slotGeneration, 2);
			const auto* fallbackPage = PptTestFindPage(fallbackRead, 0x20);
			const auto stablePage = std::find_if(stableRead.lanePages.begin(), stableRead.lanePages.end(),
				[](const auto& page) { return page.metadata.slideId == 202; });
			const auto endScreen = std::find_if(stableRead.lanePages.begin(), stableRead.lanePages.end(),
				[](const auto& page) { return page.metadata.pageKind == 1; });
			check(stableRead.lanePages.size() == 3 && fallbackRead.lanePages.size() == 1 && fallbackPage &&
				fallbackPage->metadata.binding == 1 && !fallbackPage->metadata.slideId &&
				fallbackPage->snapshot && PptTestHasStroke(*fallbackPage->snapshot, 10.0f) &&
				stablePage != stableRead.lanePages.end() && stablePage->metadata.binding == 0 &&
				stablePage->snapshot && PptTestHasStroke(*stablePage->snapshot, 20.0f) &&
				stablePage->metadata.pageGuid.Bytes() != oldPageGuid &&
				endScreen != stableRead.lanePages.end() && !endScreen->metadata.slideId &&
				endScreen->snapshot && PptTestHasStroke(*endScreen->snapshot, 40.0f),
				"both bindings read their own ink/GUIDs and Stable has an independent official EndScreen");
			const auto wire = draw3::uink::ReadUInkStoreFile(path);
			check(store.FilePath() == path && wire.document && wire.document->headerExtension &&
				wire.document->headerExtension->workspaces.size() == 1 &&
				wire.document->headerExtension->workspaces.front().workspaceType == 2 &&
				wire.document->headerExtension->workspaces.front().hostId == FormatPresentationKey(stable.key) &&
				wire.document->canvases.size() == 4,
				"overlapping lane indices share one Workspace and file");

			// 后续场景修改使两个 lane 在来回切换测试中都继续常驻。
			++activeSlot.presentationPages[0].revision;
			++old.presentationPages[0].revision;
			const auto stableWorkspaceGuid = document->WorkspaceGuid().Bytes();
			const auto stableGeneration = generation;
			auto stableSource = parked.find(LaneFor(stable));
			test.Require(stableSource != parked.end(), "Stable parked lane exists for reverse switch");
			const auto back = SwitchPresentationCpuSlot(workspace, activeKey,
				active, stableSource->second, isolated, parked, nextSlotGeneration, fallback, allocate);
			check(back.accepted && !back.topologyConflict && document && generation == 1 &&
				activeTarget == fallback && document->WorkspaceGuid().Bytes() == oldWorkspaceGuid &&
				PptTestHasStroke(document->PageAt(0), 10.0f),
				"return to fallback recovers original hot document and ink");
			const auto& parkedStable = parked.at(LaneFor(stable));
			check(parkedStable.document && parkedStable.document->WorkspaceGuid().Bytes() == stableWorkspaceGuid &&
				parkedStable.slotGeneration == stableGeneration && PptTestHasStroke(parkedStable.document->PageAt(0), 20.0f),
				"Stable CPU document remains independently parked with its own ink");
			const auto forward = SwitchPresentationCpuSlot(workspace, activeKey,
				active, parked.at(LaneFor(fallback)), isolated, parked, nextSlotGeneration, stable, allocate);
			test.Require(forward.accepted && !forward.topologyConflict && document &&
				document->WorkspaceGuid().Bytes() == stableWorkspaceGuid && generation == stableGeneration &&
				PptTestHasStroke(document->PageAt(0), 20.0f), "repeat Stable target reuses its existing lane");
			SavePresentationSlot(active);
			PptTestFinishSaves(test, activeSlot);
			const auto reentered = PptTestLoad(test, store, 0, generation, 3);
			check(store.FilePath() == path && std::any_of(reentered.lanePages.begin(),
				reentered.lanePages.end(), [](const auto& page)
				{ return page.metadata.slideId == 202 && page.confirmedRevision == 2; }),
				"same-process re-entry appends newer page revision to the same file");

			Bridge::PresentationTarget failedTarget = stable;
			failedTarget.key.bytes[0] = 0xF2;
			failedTarget.sourceIdentity += ":new";
			const auto beforeFailureGuid = document->WorkspaceGuid().Bytes();
			const auto beforeFailureGeneration = generation;
			const auto failAllocation = []() -> InkRasterStateToken { throw 1; };
			const auto failed = SwitchPresentationCpuSlot(workspace, activeKey, active,
				parked.at(LaneFor(stable)), isolated, parked, nextSlotGeneration, failedTarget, failAllocation);
			check(!failed.accepted && document->WorkspaceGuid().Bytes() == beforeFailureGuid &&
				generation == beforeFailureGeneration && activeTarget == stable,
				"candidate allocation failure preserves the committed CPU target and generation");

			// 同 lane 与已停放 lane 均在候选准备失败时保留原权威文稿。
			const auto originalGuid = document->WorkspaceGuid().Bytes();
			const auto originalTarget = activeTarget;
			const auto originalGeneration = generation;
			const auto originalPage = pageIndex;
			auto* currentSource = &parked.at(LaneFor(*activeTarget));
			auto requested = *activeTarget;
			++requested.targetRevision;
			const auto rebindFailure = [](ActivePresentationSlotRefs,
				const Bridge::PresentationTarget&,
				std::optional<DrawingDocumentSlot>&) { return false; };
			const auto unchanged = [&]()
			{
				return document && document->WorkspaceGuid().Bytes() == originalGuid &&
					activeTarget == originalTarget && generation == originalGeneration &&
					pageIndex == originalPage;
			};
			const auto failedRebind = SwitchPresentationCpuSlot(workspace, activeKey,
				active, *currentSource, isolated, parked, nextSlotGeneration,
				requested, allocate, rebindFailure);
			check(!failedRebind.accepted && unchanged(),
				"same lane rebind failure changes neither CPU document nor target");
			const auto malformed = [](ActivePresentationSlotRefs,
				const Bridge::PresentationTarget&, std::optional<DrawingDocumentSlot>& candidate)
			{ candidate.emplace(); return true; };
			const auto failedCount = SwitchPresentationCpuSlot(workspace, activeKey,
				active, *currentSource, isolated, parked, nextSlotGeneration,
				requested, allocate, malformed);
			check(!failedCount.accepted && unchanged(),
				"candidate page count failure cannot commit target equality");
			auto other = requested.bindingMode == Bridge::SlideBindingMode::StableSlideId
				? fallback : stable;
			const auto otherSlot = parked.find(LaneFor(other));
			if (otherSlot != parked.end() && otherSlot->second.document)
			{
				const auto parkedGuid = otherSlot->second.document->WorkspaceGuid().Bytes();
				const auto failedParked = SwitchPresentationCpuSlot(workspace, activeKey,
					active, *currentSource, isolated, parked, nextSlotGeneration,
					other, allocate, rebindFailure);
				check(!failedParked.accepted && unchanged() && otherSlot->second.document &&
					otherSlot->second.document->WorkspaceGuid().Bytes() == parkedGuid,
					"parked lane preparation failure preserves both lanes before swap");
			}
			const auto recovered = SwitchPresentationCpuSlot(workspace, activeKey,
				active, *currentSource, isolated, parked, nextSlotGeneration,
				requested, allocate);
			check(recovered.accepted && PresentationSceneMatches(workspace, activeKey,
				active, requested) && document->WorkspaceGuid().Bytes() == originalGuid,
				"same stable revision retries successfully without discarding old ink");
			runtimes.emplace_back();
			check(!PresentationSceneMatches(workspace, activeKey, active, requested),
				"equal target with invalid runtime count cannot acknowledge or execute business");
			runtimes.pop_back();

			CanvasCommand business;
			business.type = CanvasCommandType::Clear;
			business.requireSceneMatch = true;
			business.workspace = static_cast<std::uint8_t>(Bridge::Workspace::Presentation);
			business.presentationTarget = std::make_shared<Bridge::PresentationTarget>(requested);
			check(CanvasCommandSceneMatches(business, workspace, activeKey, active),
				"fully committed matching prefix allows business once");
			check(!CanvasCommandSceneMatches(business, Bridge::Workspace::Desktop, activeKey, active),
				"failed PPT prefix cannot Clear the old desktop");
			business.presentationTarget = std::make_shared<Bridge::PresentationTarget>(*originalTarget);
			check(!CanvasCommandSceneMatches(business, workspace, activeKey, active),
				"old full target cannot mutate a newer CPU scene");
			business.type = CanvasCommandType::PrepareExitAutoSave;
			check(CanvasCommandSceneMatches(business, Bridge::Workspace::Desktop, activeKey, active),
				"final save barrier remains admissible regardless of failed prefix");

			SceneApplicationRetry sceneRetry;
			CanvasCommand scene;
			scene.type = CanvasCommandType::SetPresentationTarget;
			scene.presentationTarget = std::make_shared<Bridge::PresentationTarget>(requested);
			CanvasCommand retryCommand;
			std::uint64_t nowMs = 100;
			for (const auto delay : { 250, 500, 1000, 2000, 2000 })
			{
				sceneRetry.Schedule(scene, nowMs);
				check(sceneRetry.RemainingWaitMilliseconds(nowMs) == delay &&
					!sceneRetry.TryTake(nowMs + delay - 1, retryCommand),
					"CPU retry respects capped backoff without polling");
				nowMs += delay;
				check(sceneRetry.TryTake(nowMs, retryCommand) &&
					retryCommand.type == CanvasCommandType::SetPresentationTarget &&
					retryCommand.sceneClosedAdmission && !sceneRetry.TryTake(nowMs, retryCommand),
					"retry yields exactly one scene and requires successful-present acknowledgment");
			}
			scene.explicitRecoveryRetry = true;
			scene.recoveryRevision = 12;
			sceneRetry.Schedule(scene, nowMs);
			check(sceneRetry.TryTake(nowMs + 2000, retryCommand) &&
				!retryCommand.explicitRecoveryRetry && retryCommand.recoveryRevision == 0,
				"first preparation failure transfers explicit request to token-free owner retry");
			sceneRetry.Schedule(retryCommand, nowMs + 2000);
			check(sceneRetry.TryTake(nowMs + 4000, retryCommand) &&
				!retryCommand.explicitRecoveryRetry && retryCommand.recoveryRevision == 0,
				"second preparation failure retains owner responsibility after UI token revision changes");
			sceneRetry.Schedule(scene, nowMs);
			sceneRetry.Cancel();
			check(!sceneRetry.RemainingWaitMilliseconds(nowMs) &&
				!sceneRetry.TryTake(UINT64_MAX, retryCommand),
				"new scene or final Exit cancels old retry responsibility");

			// PreviousInterval 的终态不携带新页身份；回执始终取真实活动目标而非旧 completion。
			std::optional<Bridge::PresentationReadyIdentity> staged;
			const auto stage = [&](const Bridge::PresentationTarget* value)
			{ staged = value ? std::optional(Bridge::ReadyIdentityFor(*value)) : std::nullopt; };
			initialized = true;
			loadPending = true;
			check(!StageResolvedPresentationScene(workspace, activeKey, active, stage) && !staged,
				"pending history load cannot stage ready for a new page");
			loadPending = false;
			check(StageResolvedPresentationScene(workspace, activeKey, active, stage) &&
				staged == Bridge::ReadyIdentityFor(requested),
				"success or failure terminal stages current actual target after history load");
			staged.reset();
			check(!StageResolvedPresentationScene(Bridge::Workspace::Desktop, activeKey,
				active, stage) && !staged,
				"parked history completion cannot acknowledge the active desktop");
			initialized = false;
			check(!StageResolvedPresentationScene(workspace, activeKey, active, stage) && !staged,
				"uninitialized current document stays blocked after history load");
			// A1：拓扑冲突不能 accepted 后丢失 target/load/ready；确认读取成功的完整空 lane 才能退休。
			DrawingDocumentSlot conflict;
			check(TryCreateBlankDocumentSlot(conflict, 3, allocate), "build empty fallback conflict lane");
			conflict.presentationTarget = fallback; conflict.slotGeneration = 41;
			conflict.presentationPages.resize(3);
			conflict.persistenceInitialized = true;
			Bridge::Workspace conflictWorkspace = Bridge::Workspace::Presentation;
			std::optional<Bridge::PresentationKey> conflictKey = fallback.key;
			PresentationParkedSlots conflictParked;
			auto& conflictSource = conflictParked[LaneFor(fallback)];
			DrawingDocumentSlot unused;
			std::uint64_t conflictGeneration = 42;
			auto expanded = fallback; expanded.totalPages = 3; expanded.targetRevision = 30;
			const auto conflictGuid = conflict.document->WorkspaceGuid();
			auto denied = SwitchPresentationCpuSlot(conflictWorkspace, conflictKey, DocumentSlotRefs(conflict),
				conflictSource, unused, conflictParked, conflictGeneration, expanded, allocate);
			check(!denied.accepted && denied.outcome == PresentationCpuSwitchOutcome::ProtectedConflict &&
				conflict.document->WorkspaceGuid() == conflictGuid && conflict.presentationTarget == fallback,
				"unverified empty conflict preserves complete old lane instead of entering isolated blank");
			conflict.emptyLaneVerified = true;
			check(CanRetireEmptyPresentationLane(DocumentSlotRefs(conflict)),
				"successful empty lane confirmation establishes retirement proof");
			for (unsigned condition = 0; condition != 9; ++condition)
			{
				auto protectedSlot = conflict;
				switch (condition)
				{
				case 0: protectedSlot.loadPending = true; break;
				case 1: protectedSlot.presentationPages[0].pendingWrites = 1; break;
				case 2: protectedSlot.presentationPages[0].revision = 1; break;
				case 3: protectedSlot.pageRuntimeStates[0].clearRedoAvailable = true; break;
				case 4: protectedSlot.pageRuntimeStates[0].boundaryFallback.emplace(); break;
				case 5: protectedSlot.retainedSlides.push_back({101, *protectedSlot.document->PageAt(0), {}, {}}); break;
				case 6: protectedSlot.document->PageAt(0)->GetOrCreateCanvas(kDefaultDeviceKey)->AppendStroke(
					InkStroke({}, { { 1.f, 1.f, 2.f }, { 2.f, 2.f, 2.f } })); break;
				case 7: protectedSlot.presentationPages[0].confirmedRevision = 1; break;
				case 8:
				{
					InkStroke stroke({}, { { 1.f, 1.f, 2.f }, { 2.f, 2.f, 2.f } });
					const auto coverage = BuildStrokeTileFootprint(stroke);
					if (coverage) (void)protectedSlot.pageRuntimeStates[0].history.AppendStroke(0, *coverage, true);
					break;
				}
				}
				check(!CanRetireEmptyPresentationLane(DocumentSlotRefs(protectedSlot)),
					"load, queued save, mutation, redo, fallback, retained or hidden ink protects conflict lane");
			}
			const auto fresh = SwitchPresentationCpuSlot(conflictWorkspace, conflictKey, DocumentSlotRefs(conflict),
				conflictSource, unused, conflictParked, conflictGeneration, expanded, allocate);
			check(fresh.accepted && conflict.presentationTarget == expanded && conflict.slotGeneration == 42 &&
				conflict.document->Pages().size() == 4 && !conflict.persistenceInitialized &&
				!conflict.emptyLaneVerified && !conflict.loadPending,
				"verified empty conflict prepares a fresh generation and valid target but still requires real Load");
			bool earlyReady = false;
			check(!StageResolvedPresentationScene(conflictWorkspace, conflictKey, DocumentSlotRefs(conflict),
				[&](auto*) { earlyReady = true; }) && !earlyReady,
				"retired empty lane never fabricates initialization or ready before verified new-target Load");

			// 页序 lane 已被释放后，磁盘只含稀疏页，不能靠记录数量推断旧拓扑。
			PresentationParkedSlots coldSlots;
			auto& knownFallback = coldSlots[LaneFor(fallback)];
			knownFallback = PptTestInstall(test, store, PptTestLoad(test, store, 1, 77),
				fallback, workspaceGuid);
			test.Require(CanEvictPresentationCpuSlot(knownFallback), "durable fallback lane can be evicted");
			EvictPresentationCpuSlot(knownFallback);
			test.Check(!knownFallback.document && knownFallback.presentationTarget &&
				knownFallback.presentationTarget->totalPages == fallback.totalPages,
				"fallback eviction records the last trusted page count");
			DrawingDocumentSlot exitingDesktop, reentering;
			test.Require(TryCreateBlankDocumentSlot(reentering, 1, allocate), "initialize independent active desktop");
			const auto desktopGuid = reentering.document->WorkspaceGuid();
			Bridge::Workspace reenteringWorkspace = Bridge::Workspace::Desktop;
			std::optional<Bridge::PresentationKey> reenteringKey;
			uint64_t newGeneration = 78;
			const auto conflictAfterEvict = SwitchPresentationCpuSlot(reenteringWorkspace, reenteringKey,
				DocumentSlotRefs(reentering), exitingDesktop, isolated, coldSlots, newGeneration, expanded, allocate);
			test.Check(!conflictAfterEvict.accepted && conflictAfterEvict.topologyConflict && reentering.document &&
				reentering.document->WorkspaceGuid() == desktopGuid && !reenteringKey &&
				knownFallback.presentationTarget->totalPages == fallback.totalPages,
				"changed fallback count stays protected after eviction and before any cold projection");
			const auto reenter = SwitchPresentationCpuSlot(reenteringWorkspace, reenteringKey,
				DocumentSlotRefs(reentering), exitingDesktop, isolated, coldSlots, newGeneration, fallback, allocate);
			test.Require(reenter.accepted && reentering.slotGeneration == 78 && !reentering.persistenceInitialized,
				"unchanged fallback count creates a fresh unresolved generation");
			reentering.presentationStore = &store;
			const auto coldLoad = PptTestLoad(test, store, 1, reentering.slotGeneration);
			auto coldCandidate = MaterializePresentationSlot(coldLoad, fallback, InkGuid(workspaceGuid.Bytes()), allocate);
			test.Require(coldCandidate && InstallLoadedPresentationSlot(DocumentSlotRefs(reentering),
				std::move(*coldCandidate)), "re-enter evicted lane with a real cold load");
			test.Check(reentering.document->PageAt(0)->PageGuid().Bytes() == oldPageGuid &&
				PptTestHasStroke(reentering.document->PageAt(0), 10.0f),
				"same-count fallback projection restores the original GUID and ink after eviction");
			++reentering.presentationPages[0].revision;
			test.Require(SavePresentationSlot(DocumentSlotRefs(reentering)), "queued write after cold re-entry");
			PptTestFinishSaves(test, reentering);
			const auto savedCold = PptTestLoad(test, store, 1, reentering.slotGeneration, 2);
			const auto* coldPage = PptTestFindPage(savedCold, 0x20);
			test.Check(store.FilePath() == path && coldPage && coldPage->confirmedRevision == 9 &&
				coldPage->snapshot && PptTestHasStroke(*coldPage->snapshot, 10.0f),
				"write after cold re-entry appends to the same document file");

		}
		catch (...)
		{
			check(false, "probe exception");
		}
		if (test.failures == 0)
			std::fputs("[Draw3PptLane] PASS: two lanes share one file and preserve independent CPU ownership\n",
				stderr);
		return test.failures == 0 ? 0 : 1;
	}
