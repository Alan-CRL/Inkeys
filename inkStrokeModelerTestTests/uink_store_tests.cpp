#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

import draw3.uink_store;

namespace
{
	using namespace draw3::uink;

	struct TestState
	{
		const char* name;
		int failures = 0;

		bool Check(bool condition, const char* expression, int line)
		{
			if (!condition)
			{
				++failures;
				std::cerr << "UInk store FAILED " << name << ':' << line << ": " << expression << '\n';
			}
			return condition;
		}
	};

#define STORE_CHECK(expression) state.Check(!!(expression), #expression, __LINE__)
#define STORE_REQUIRE(expression) do { if (!STORE_CHECK(expression)) return; } while (false)

	class TestHandle
	{
	public:
		explicit TestHandle(HANDLE handle) : handle_(handle) {}
		~TestHandle() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
		TestHandle(const TestHandle&) = delete;
		TestHandle& operator=(const TestHandle&) = delete;
		explicit operator bool() const { return handle_ != INVALID_HANDLE_VALUE; }
		HANDLE Get() const { return handle_; }

	private:
		HANDLE handle_;
	};

	class TempDirectory
	{
	public:
		TempDirectory()
		{
			std::array<wchar_t, 32768> temp{};
			const DWORD length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
			const auto id = CreateUInkGuid();
			if (length == 0 || length >= temp.size() || !id) return;
			const std::string suffix = FormatUInkGuid(*id);
			std::wstring candidate = std::wstring(temp.data(), length) + L"Inkeys-UInkStore-" +
				std::wstring(suffix.begin(), suffix.end());
			if (CreateDirectoryW(candidate.c_str(), nullptr)) path_ = std::move(candidate);
		}

		~TempDirectory() { (void)Clean(); }
		TempDirectory(const TempDirectory&) = delete;
		TempDirectory& operator=(const TempDirectory&) = delete;
		explicit operator bool() const { return !path_.empty(); }
		std::wstring At(const wchar_t* name) const { return path_ + L"\\" + name; }

		std::vector<std::wstring> Files() const
		{
			std::vector<std::wstring> files;
			WIN32_FIND_DATAW entry{};
			const HANDLE listing = FindFirstFileW(At(L"*").c_str(), &entry);
			if (listing != INVALID_HANDLE_VALUE)
			{
				const std::unique_ptr<void, decltype(&FindClose)> search(listing, FindClose);
				do
				{
					if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
						files.push_back(At(entry.cFileName));
				} while (FindNextFileW(search.get(), &entry));
			}
			std::sort(files.begin(), files.end());
			return files;
		}

		bool Clean() noexcept
		{
			if (path_.empty()) return true;
			try
			{
				// 只清理本次测试成功创建的唯一目录。
				for (const auto& file : Files()) DeleteFileW(file.c_str());
			}
			catch (...) { return false; }
			if (!RemoveDirectoryW(path_.c_str())) return false;
			path_.clear();
			return true;
		}

	private:
		std::wstring path_;
	};

	class ScopedCurrentDirectory
	{
	public:
		ScopedCurrentDirectory() : length_(GetCurrentDirectoryW(
			static_cast<DWORD>(original_.size()), original_.data())) {}
		~ScopedCurrentDirectory() { (void)Restore(); }

		bool Set(const std::wstring& path)
		{
			if (length_ == 0 || length_ >= original_.size() || !SetCurrentDirectoryW(path.c_str()))
				return false;
			changed_ = true;
			return true;
		}

		bool Restore()
		{
			if (!changed_) return true;
			if (!SetCurrentDirectoryW(original_.data())) return false;
			changed_ = false;
			return true;
		}

	private:
		std::array<wchar_t, 32768> original_;
		DWORD length_;
		bool changed_ = false;
	};

	struct ScopedFaults
	{
		explicit ScopedFaults(const UInkStoreTestFaultInjection& faults)
		{
			SetUInkStoreTestFaultInjection(faults);
		}
		~ScopedFaults() { ResetUInkStoreTestFaultInjection(); }
	};

	std::optional<std::vector<std::byte>> ReadBytes(const std::wstring& path)
	{
		const TestHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		LARGE_INTEGER size{};
		if (!file || !GetFileSizeEx(file.Get(), &size) || size.QuadPart < 0 ||
			static_cast<uint64_t>(size.QuadPart) > UInkReadLimits{}.maxFileBytes)
			return std::nullopt;
		std::vector<std::byte> bytes(static_cast<size_t>(size.QuadPart));
		DWORD count = 0;
		if (!bytes.empty() && (!ReadFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()),
			&count, nullptr) || count != bytes.size())) return std::nullopt;
		return bytes;
	}

	bool WriteBytes(const std::wstring& path, std::span<const std::byte> bytes)
	{
		const TestHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL, nullptr));
		DWORD count = 0;
		return file && WriteFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()),
			&count, nullptr) && count == bytes.size();
	}

	UInkGuid Guid(uint8_t ending)
	{
		return UInkGuid({ 0xb1, 0, 0, 0, 0, 0, 0x40, 0, 0x80, 0, 0, 0, 0, 0, 0, ending });
	}

	struct Fixture
	{
		UInkHeader header;
		std::optional<UInkHeaderExtension> extension;

		Fixture()
		{
			header.guid = Guid(1);
			header.time = 1234;
			header.pageNum = 1;
			extension.emplace();
			extension->name = "Store test";
			UInkWorkspace workspace;
			workspace.guid = Guid(2);
			workspace.workspaceType = 1;
			extension->workspaces.push_back(workspace);
			UInkDevice device;
			device.guid = Guid(3);
			device.geometry = UInkDisplayDevice{ 0, 0, 1920, 1080 };
			extension->devices.push_back(device);
		}

		UInkCanvas Page(uint8_t ending, uint32_t pageIndex, uint32_t revision) const
		{
			UInkCanvas canvas;
			canvas.workspaceGuid = extension->workspaces.front().guid;
			canvas.deviceGuid = extension->devices.front().guid;
			canvas.pageGuid = Guid(ending);
			canvas.pageIndex = pageIndex;
			canvas.pageNumber = 100 + pageIndex;
			canvas.layerNumber = 1;
			canvas.viewport = UInkViewport{ static_cast<float>(revision), -3.5f, 1.25f };
			canvas.background = UInkBackground{ UInkColor{ 0xeeeeee, std::nullopt }, 1 };
			canvas.pageState = 1;
			canvas.pageKind = 0;
			canvas.binding = 1;

			UInkInk ink;
			ink.color = { revision, UInkExtendedColor{ UInkColorSpace::Srgb, { 0.25f, 0.5f, 0.75f } } };
			ink.cap = 1;
			ink.opacity = 0.75f;
			ink.points = { { 1.5f, 2.5f, 5.0f, std::nullopt }, { 4.5f, 1.5f, 4.0f, std::nullopt } };
			canvas.content.emplace_back(ink);
			canvas.content.emplace_back(UInkClear{ 1, 2 });
			ink.contentId = 2;
			ink.undoId = 3;
			ink.color.fallbackRgb = 0x112233;
			canvas.content.emplace_back(std::move(ink));
			return canvas;
		}

		std::unique_ptr<UInkPagedFileWriter> Create(const std::wstring& path, const UInkCanvas& page,
			UInkStoreWriteResult& result) const
		{
			return UInkPagedFileWriter::Create(path, header, extension, page, result);
		}

		UInkStreamContext Context(uint64_t baseOffset) const
		{
			UInkStreamContext context;
			context.version = kUInkVersion;
			context.headerExtension = extension;
			context.usesImplicitDevice = false;
			context.usesImplicitWorkspace = false;
			context.baseOffset = baseOffset;
			return context;
		}
	};

	template <typename T, typename Equal>
	bool SameOptional(const std::optional<T>& left, const std::optional<T>& right, Equal equal)
	{
		return left.has_value() == right.has_value() && (!left || equal(*left, *right));
	}

	bool SameColor(const UInkColor& left, const UInkColor& right)
	{
		return left.fallbackRgb == right.fallbackRgb && SameOptional(left.extended, right.extended,
			[](const UInkExtendedColor& a, const UInkExtendedColor& b)
			{
				return a.space == b.space && a.components == b.components;
			});
	}

	bool SameInk(const UInkInk& left, const UInkInk& right)
	{
		return left.contentId == right.contentId && left.undoId == right.undoId &&
			left.declaredInkType == right.declaredInkType && left.effectiveKind == right.effectiveKind &&
			SameColor(left.color, right.color) && left.opacity == right.opacity &&
			left.declaredTexture == right.declaredTexture && left.effectiveTexture == right.effectiveTexture &&
			left.points.size() == right.points.size() &&
			std::equal(left.points.begin(), left.points.end(), right.points.begin(),
				[](const UInkInkPoint& a, const UInkInkPoint& b)
				{
					return a.x == b.x && a.y == b.y && a.width == b.width &&
						SameOptional(a.style, b.style, [](const UInkPointStyle& x, const UInkPointStyle& y)
						{
							return SameColor(x.color, y.color) && x.opacity == y.opacity;
						});
				}) &&
			left.renderOnlyWhenLatest == right.renderOnlyWhenLatest &&
			SameOptional(left.nib, right.nib, [](const UInkNib& a, const UInkNib& b)
			{
				return a.shape == b.shape && a.aspect == b.aspect && a.orientation == b.orientation;
			}) && left.cap == right.cap;
	}

	bool SameContent(const UInkContent& left, const UInkContent& right)
	{
		if (const auto* ink = std::get_if<UInkInk>(&left))
		{
			const auto* other = std::get_if<UInkInk>(&right);
			return other && SameInk(*ink, *other);
		}
		const auto* clear = std::get_if<UInkClear>(&left);
		const auto* other = std::get_if<UInkClear>(&right);
		return clear && other && clear->contentId == other->contentId &&
			clear->undoId == other->undoId;
	}

	bool SameCanvas(const UInkCanvas& left, const UInkCanvas& right)
	{
		return left.workspaceGuid == right.workspaceGuid && left.deviceGuid == right.deviceGuid &&
			left.pageGuid == right.pageGuid && left.pageIndex == right.pageIndex &&
			left.pageNumber == right.pageNumber && left.layerIndex == right.layerIndex &&
			left.layerNumber == right.layerNumber && left.slideId == right.slideId &&
			SameOptional(left.viewport, right.viewport, [](const UInkViewport& a, const UInkViewport& b)
			{
				return a.x == b.x && a.y == b.y && a.scale == b.scale;
			}) &&
			left.content.size() == right.content.size() &&
			std::equal(left.content.begin(), left.content.end(), right.content.begin(), SameContent) &&
			left.temporaryWorkspace == right.temporaryWorkspace && left.temporaryDevice == right.temporaryDevice &&
			left.temporaryPage == right.temporaryPage && left.temporaryLayer == right.temporaryLayer &&
			left.presentationUnbound == right.presentationUnbound &&
			SameOptional(left.background, right.background, [](const UInkBackground& a, const UInkBackground& b)
			{
				return a.pattern == b.pattern && SameColor(a.color, b.color);
			}) &&
			left.pageState == right.pageState && left.pageKind == right.pageKind && left.binding == right.binding;
	}

	bool SameRange(const UInkPageRange& left, const UInkPageRange& right)
	{
		return left.offset == right.offset && left.length == right.length &&
			left.pageIndex == right.pageIndex && left.writeSequence == right.writeSequence &&
			left.nextContentId == right.nextContentId && left.lastUndoId == right.lastUndoId;
	}

	bool SameDirectory(const UInkPageDirectory& left, const UInkPageDirectory& right)
	{
		return left.pages.size() == right.pages.size() &&
			std::equal(left.pages.begin(), left.pages.end(), right.pages.begin(),
				[](const auto& a, const auto& b) { return a.first == b.first && SameRange(a.second, b.second); }) &&
			left.supersededBytes == right.supersededBytes && left.lastPageGuid == right.lastPageGuid &&
			left.nextContentId == right.nextContentId && left.lastUndoId == right.lastUndoId &&
			left.context.version == right.context.version && left.context.baseOffset == right.context.baseOffset &&
			left.context.usesImplicitDevice == right.context.usesImplicitDevice &&
			left.context.usesImplicitWorkspace == right.context.usesImplicitWorkspace;
	}

	bool ReadMatches(const UInkReadResult& read, const UInkCanvas& expected)
	{
		return read.status == UInkReadStatus::Complete && read.summary.outcome == UInkReadOutcome::Complete &&
			read.document && read.document->canvases.size() == 1 &&
			SameCanvas(read.document->canvases.front(), expected);
	}

	bool HasDiagnostic(const UInkStoreWriteResult& result, UInkDiagnosticCode code)
	{
		return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
			[&](const UInkDiagnostic& diagnostic) { return diagnostic.code == code; });
	}

	std::span<const std::byte> PageBytes(const std::vector<std::byte>& bytes, const UInkPageRange& range)
	{
		if (range.offset > bytes.size() || range.length > bytes.size() - range.offset) return {};
		return std::span<const std::byte>(bytes).subspan(static_cast<size_t>(range.offset),
			static_cast<size_t>(range.length));
	}

	bool CheckAppendedRange(TestState& state, const UInkPagedFileWriter& writer, const UInkCanvas& page,
		uint64_t expectedOffset, uint64_t expectedSequence)
	{
		const auto range = writer.Directory().pages.find(page.pageGuid.Bytes());
		if (!STORE_CHECK(range != writer.Directory().pages.end())) return false;
		const auto encoded = EncodeUInkPage(page, false, false);
		if (!STORE_CHECK(encoded.status == UInkEncodeStatus::Success)) return false;
		STORE_CHECK(range->second.pageIndex == page.pageIndex);
		STORE_CHECK(range->second.offset == expectedOffset);
		STORE_CHECK(range->second.length == encoded.bytes.size());
		STORE_CHECK(range->second.writeSequence == expectedSequence);
		STORE_CHECK(writer.FileBytes() == expectedOffset + encoded.bytes.size());
		return true;
	}

	void AppendReplaceCompact(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const UInkCanvas first = fixture.Page(11, 0, 1);
		const UInkCanvas second = fixture.Page(12, 8, 2);
		const UInkCanvas third = fixture.Page(13, 8, 3);
		const UInkCanvas firstMoved = fixture.Page(11, 4, 4);
		const UInkCanvas firstFinal = fixture.Page(11, 0, 5);
		const UInkCanvas secondFinal = fixture.Page(12, 8, 6);
		const auto prefix = EncodeUInkHeaderPrefix(fixture.header, fixture.extension);
		const auto headerOnly = EncodeUInkHeaderPrefix(fixture.header, std::nullopt);
		STORE_REQUIRE(prefix.status == UInkEncodeStatus::Success);
		STORE_REQUIRE(headerOnly.status == UInkEncodeStatus::Success && headerOnly.bytes.size() < prefix.bytes.size());

		UInkStoreWriteResult created;
		const auto path = root.At(L"roundtrip.uink");
		auto writer = fixture.Create(path, first, created);
		STORE_REQUIRE(created.success && writer);
		STORE_REQUIRE(writer->Directory().pages.size() == 1);
		STORE_CHECK(writer->Path() == path && !writer->IsSealed() && writer->SupersededBytes() == 0);
		STORE_REQUIRE(CheckAppendedRange(state, *writer, first, prefix.bytes.size(), 0));

		uint64_t replaced = 0;
		uint64_t sequence = 1;
		for (const UInkCanvas* page : { &second, &third, &firstMoved, &firstFinal, &secondFinal })
		{
			const auto prior = writer->Directory().pages.find(page->pageGuid.Bytes());
			if (prior != writer->Directory().pages.end()) replaced += prior->second.length;
			const uint64_t offset = writer->FileBytes();
			STORE_REQUIRE(writer->AppendPage(*page).success);
			STORE_REQUIRE(CheckAppendedRange(state, *writer, *page, offset, sequence++));
			STORE_CHECK(writer->SupersededBytes() == replaced && writer->Directory().supersededBytes == replaced);
			STORE_CHECK(writer->Directory().lastPageGuid == page->pageGuid);
			STORE_CHECK(writer->Directory().nextContentId == 3 && writer->Directory().lastUndoId == 3);
		}
		STORE_REQUIRE(writer->Directory().pages.size() == 3);
		STORE_CHECK(replaced > 0);

		const std::array expected{ &firstFinal, &third, &secondFinal };
		UInkStoreReadFacts fullFacts;
		const auto fullBefore = ReadUInkStoreFile(path, {}, nullptr, &fullFacts);
		STORE_REQUIRE(fullBefore.status == UInkReadStatus::Complete && fullBefore.document);
		STORE_CHECK(fullFacts.ioMs > 0 && fullFacts.bytesRead == writer->FileBytes() && fullFacts.rangeStart == 0);
		STORE_REQUIRE(fullBefore.document->canvases.size() == expected.size());
		STORE_CHECK(fullBefore.document->header.guid == fixture.header.guid && fullBefore.document->header.pageNum == 1);
		STORE_CHECK(fullBefore.document->header.time == fixture.header.time);
		STORE_CHECK(fullBefore.diagnostics.empty() && !fullBefore.safeAppendOffset);
		for (size_t index = 0; index < expected.size(); ++index)
		{
			UInkStoreReadFacts pageFacts;
			const auto page = ReadUInkPage(path, writer->Directory(), expected[index]->pageGuid, {}, &pageFacts);
			STORE_REQUIRE(ReadMatches(page, *expected[index]));
			const auto& range = writer->Directory().pages.at(expected[index]->pageGuid.Bytes());
			STORE_CHECK(pageFacts.ioMs > 0 && pageFacts.bytesRead == range.length && pageFacts.rangeStart == range.offset);
			STORE_CHECK(SameCanvas(page.document->canvases.front(), fullBefore.document->canvases[index]));
		}

		const auto before = ReadBytes(path);
		STORE_REQUIRE(before && before->size() == writer->FileBytes());
		const UInkPageDirectory oldDirectory = writer->Directory();
		const auto compactStarted = std::time(nullptr);
		const auto compacted = writer->Compact();
		const auto compactFinished = std::time(nullptr);
		STORE_REQUIRE(compacted.success);
		STORE_CHECK(compacted.replacementCommitted && compacted.reopen == UInkStoreCompactReopen::Succeeded &&
			compacted.reopenSystemError == 0 && compacted.systemError == 0);
		STORE_REQUIRE(writer->Directory().pages.size() == expected.size());
		const auto after = ReadBytes(path);
		STORE_REQUIRE(after && after->size() == writer->FileBytes());
		STORE_CHECK(after->size() < before->size() && before->size() - after->size() == replaced);
		STORE_CHECK(writer->SupersededBytes() == 0 && writer->Directory().supersededBytes == 0);
		STORE_CHECK(!writer->IsSealed());

		// Header 统计和时间之外，Extension 和各页都原样保留。
		const auto extensionBefore = PageBytes(*before, { headerOnly.bytes.size(),
			prefix.bytes.size() - headerOnly.bytes.size(), 0, 0 });
		const auto extensionAfter = PageBytes(*after, { headerOnly.bytes.size(),
			prefix.bytes.size() - headerOnly.bytes.size(), 0, 0 });
		STORE_REQUIRE(!extensionBefore.empty() && !extensionAfter.empty());
		STORE_CHECK(std::ranges::equal(extensionBefore, extensionAfter));
		const auto fullAfter = ReadUInkStoreFile(path);
		STORE_REQUIRE(fullAfter.status == UInkReadStatus::Complete && fullAfter.document);
		STORE_REQUIRE(fullAfter.document->canvases.size() == expected.size());
		STORE_CHECK(fullAfter.document->header.guid == fixture.header.guid);
		STORE_CHECK(fullAfter.document->header.pageNum == expected.size());
		STORE_CHECK(compactStarted > 0 && compactFinished >= compactStarted &&
			fullAfter.document->header.time >= static_cast<uint64_t>(compactStarted) &&
			fullAfter.document->header.time <= static_cast<uint64_t>(compactFinished));
		STORE_CHECK(fullAfter.document->header.deviceNum == 1 && fullAfter.document->header.workspaceNum == 1);

		uint64_t compactedOffset = prefix.bytes.size();
		for (size_t index = 0; index < expected.size(); ++index)
		{
			const auto guid = expected[index]->pageGuid;
			const auto oldRange = oldDirectory.pages.find(guid.Bytes());
			const auto newRange = writer->Directory().pages.find(guid.Bytes());
			STORE_REQUIRE(oldRange != oldDirectory.pages.end() && newRange != writer->Directory().pages.end());
			const auto oldPage = PageBytes(*before, oldRange->second);
			const auto newPage = PageBytes(*after, newRange->second);
			STORE_REQUIRE(!oldPage.empty() && !newPage.empty());
			STORE_CHECK(std::ranges::equal(oldPage, newPage));
			STORE_CHECK(newRange->second.offset == compactedOffset);
			STORE_CHECK(newRange->second.pageIndex == expected[index]->pageIndex);
			compactedOffset += newRange->second.length;
			STORE_CHECK(ReadMatches(ReadUInkPage(path, writer->Directory(), guid), *expected[index]));
			STORE_CHECK(SameCanvas(fullAfter.document->canvases[index], *expected[index]));
		}
		STORE_CHECK(compactedOffset == writer->FileBytes());
		STORE_CHECK(writer->Directory().lastPageGuid == secondFinal.pageGuid &&
			writer->Directory().nextContentId == 3 && writer->Directory().lastUndoId == 3);

		const UInkCanvas appended = fixture.Page(14, 9, 7);
		const uint64_t compactedSize = writer->FileBytes();
		STORE_REQUIRE(writer->AppendPage(appended).success);
		STORE_REQUIRE(CheckAppendedRange(state, *writer, appended, compactedSize, expected.size()));
		STORE_CHECK(ReadMatches(ReadUInkPage(path, writer->Directory(), appended.pageGuid), appended));
		const auto fullAppended = ReadUInkStoreFile(path);
		STORE_REQUIRE(fullAppended.document && fullAppended.document->canvases.size() == 4);
		STORE_CHECK(SameCanvas(fullAppended.document->canvases.back(), appended));
		const auto beforeClose = ReadBytes(path);
		STORE_REQUIRE(beforeClose);
		writer->Close();
		writer->Close();
		STORE_CHECK(ReadBytes(path) == beforeClose);
	}

	void PublishDoesNotOverwrite(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const auto path = root.At(L"collision.uink");
		UInkStoreWriteResult created;
		auto first = fixture.Create(path, fixture.Page(21, 0, 1), created);
		STORE_REQUIRE(created.success && first && !first->Directory().pages.empty());
		const auto original = ReadBytes(path);
		STORE_REQUIRE(original && !original->empty());

		const auto page = fixture.Page(22, 0, 2);
		auto second = fixture.Create(path, page, created);
		STORE_REQUIRE(created.success && second && !second->Directory().pages.empty());
		STORE_CHECK(second->Path() == root.At(L"collision_1.uink"));
		STORE_CHECK(ReadBytes(path) == original);
		STORE_CHECK(ReadMatches(ReadUInkPage(second->Path(), second->Directory(), page.pageGuid), page));
	}

	void RelativeCreateSurvivesCurrentDirectoryChange(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const auto path = root.At(L"relative.uink");
		TempDirectory other;
		STORE_REQUIRE(other);
		const auto decoy = other.At(L"relative.uink");
		constexpr std::array decoyBytes{ std::byte{ 0xde }, std::byte{ 0xad } };
		STORE_REQUIRE(WriteBytes(decoy, decoyBytes));
		const auto beforeDecoy = ReadBytes(decoy);
		STORE_REQUIRE(beforeDecoy && std::ranges::equal(*beforeDecoy, decoyBytes));

		ScopedCurrentDirectory cwd;
		STORE_REQUIRE(cwd.Set(root.At(L".")));
		const auto first = fixture.Page(23, 0, 1);
		const auto latest = fixture.Page(23, 0, 2);
		UInkStoreWriteResult created;
		auto writer = fixture.Create(L"relative.uink", first, created);
		STORE_REQUIRE(created.success && writer && !writer->Directory().pages.empty());
		STORE_CHECK(writer->Path() == path);
		STORE_REQUIRE(writer->AppendPage(latest).success);
		const auto before = ReadBytes(path);
		STORE_REQUIRE(before && before->size() == writer->FileBytes() && writer->SupersededBytes() > 0);

		STORE_REQUIRE(cwd.Set(other.At(L".")));
		STORE_REQUIRE(writer->Compact().success);
		STORE_CHECK(writer->Path() == path && writer->SupersededBytes() == 0);
		const auto after = ReadBytes(path);
		STORE_REQUIRE(after && after->size() == writer->FileBytes());
		STORE_CHECK(after->size() < before->size());
		STORE_CHECK(ReadMatches(ReadUInkPage(writer->Path(), writer->Directory(), latest.pageGuid), latest));
		STORE_CHECK(ReadBytes(decoy) == beforeDecoy);
		STORE_REQUIRE(cwd.Restore());
		STORE_CHECK(other.Clean());
	}

	void CreateFailuresCleanTemp(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const auto page = fixture.Page(31, 0, 1);
		const auto path = root.At(L"create-failure.uink");
		const auto files = root.Files();
		for (const auto faults : { UInkStoreTestFaultInjection{ .shortWrite = true },
			UInkStoreTestFaultInjection{ .failFlush = true }, UInkStoreTestFaultInjection{ .failMove = true } })
		{
			UInkStoreWriteResult result;
			{
				const ScopedFaults injected(faults);
				STORE_CHECK(!fixture.Create(path, page, result));
			}
			STORE_CHECK(!result.success && result.systemError != 0 && !result.diagnostics.empty());
			STORE_CHECK(root.Files() == files);
		}
		UInkStoreWriteResult created;
		auto writer = fixture.Create(path, page, created);
		STORE_REQUIRE(created.success && writer && !writer->Directory().pages.empty());
		STORE_CHECK(writer->Path() == path);

		const auto published = root.At(L"create-failure_1.uink");
		const auto original = ReadBytes(path);
		STORE_REQUIRE(original);
		auto expectedFiles = root.Files();
		expectedFiles.push_back(published);
		std::sort(expectedFiles.begin(), expectedFiles.end());
		{
			const ScopedFaults injected({ .failReopen = true });
			STORE_CHECK(!fixture.Create(path, page, created));
		}
		STORE_CHECK(!created.success && created.systemError != 0 && !created.diagnostics.empty());
		STORE_CHECK(created.published && created.publishedPath == published);
		STORE_CHECK(root.Files() == expectedFiles);
		STORE_CHECK(ReadBytes(path) == original);
		STORE_CHECK(ReadMatches(ReadUInkStoreFile(published), page));
	}

	void SnapshotPublish(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const auto path = root.At(L"snapshot.uink");
		const auto first = fixture.Page(32, 0, 1);
		const auto second = fixture.Page(32, 0, 2);
		const auto third = fixture.Page(32, 0, 3);
		const auto committed = PublishUInkSnapshotFile(path, fixture.header, fixture.extension, first);
		STORE_REQUIRE(committed.success && committed.published && committed.publishedPath == path);
		const auto original = ReadBytes(path);
		STORE_REQUIRE(original && ReadMatches(ReadUInkStoreFile(path), first));

		const auto suffix1 = PublishUInkSnapshotFile(path, fixture.header, fixture.extension, second);
		STORE_REQUIRE(suffix1.success && suffix1.published && suffix1.publishedPath == root.At(L"snapshot_1.uink"));
		const auto secondBytes = ReadBytes(suffix1.publishedPath);
		STORE_REQUIRE(secondBytes && ReadMatches(ReadUInkStoreFile(suffix1.publishedPath), second));

		const auto originalFiles = root.Files();
		{
			const ScopedFaults fault({ .failReopen = true });
			const auto suffix2 = PublishUInkSnapshotFile(path, fixture.header, fixture.extension, third);
			STORE_REQUIRE(suffix2.success && suffix2.published &&
				suffix2.publishedPath == root.At(L"snapshot_2.uink"));
			STORE_CHECK(ReadMatches(ReadUInkStoreFile(suffix2.publishedPath), third));
			// 一次发布没有重开文件，所以下一次分页创建仍能命中重开故障。
			UInkStoreWriteResult writerResult;
			STORE_CHECK(!fixture.Create(root.At(L"snapshot-reopen-check.uink"), first, writerResult));
			STORE_CHECK(!writerResult.success && writerResult.published &&
				writerResult.systemError == ERROR_SHARING_VIOLATION);
		}
		auto expectedFiles = originalFiles;
		expectedFiles.push_back(root.At(L"snapshot_2.uink"));
		expectedFiles.push_back(root.At(L"snapshot-reopen-check.uink"));
		std::sort(expectedFiles.begin(), expectedFiles.end());
		STORE_CHECK(root.Files() == expectedFiles);
		STORE_CHECK(ReadBytes(path) == original && ReadBytes(suffix1.publishedPath) == secondBytes);
	}

	void SnapshotPublishLongPath(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const std::wstring fileName(240, L'x');
		const auto baseName = fileName + L".uink";
		const auto collisionName = fileName + L"_1.uink";
		// 显式扩展路径让长路径检查不依赖 Windows 长路径组策略。
		const auto path = LR"(\\?\)" + root.At(baseName.c_str());
		const auto collisionPath = LR"(\\?\)" + root.At(collisionName.c_str());
		STORE_REQUIRE(path.size() - 4 > MAX_PATH);
		struct Cleanup
		{
			const std::wstring& first;
			const std::wstring& second;
			~Cleanup() { DeleteFileW(first.c_str()); DeleteFileW(second.c_str()); }
		} cleanup{ path, collisionPath };
		const auto first = fixture.Page(32, 0, 1);
		const auto second = fixture.Page(32, 0, 2);
		const auto saved = PublishUInkSnapshotFile(path, fixture.header, fixture.extension, first);
		STORE_REQUIRE(saved.success && saved.published && saved.publishedPath == path);
		const auto original = ReadBytes(path);
		STORE_REQUIRE(original && ReadMatches(ReadUInkStoreFile(path), first));
		const auto collision = PublishUInkSnapshotFile(path, fixture.header, fixture.extension, second);
		STORE_REQUIRE(collision.success && collision.published && collision.publishedPath == collisionPath);
		STORE_CHECK(ReadBytes(path) == original && ReadMatches(ReadUInkStoreFile(collisionPath), second));
		STORE_CHECK(DeleteFileW(collisionPath.c_str()) && DeleteFileW(path.c_str()));
	}

	void SnapshotPublishFailuresCleanTemp(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const auto page = fixture.Page(33, 0, 1);
		const auto path = root.At(L"snapshot-failure.uink");
		const auto files = root.Files();
		const std::array faults{
			std::pair{ UInkStoreTestFaultInjection{ .shortWrite = true }, UInkDiagnosticCode::WriteFailed },
			std::pair{ UInkStoreTestFaultInjection{ .failFlush = true }, UInkDiagnosticCode::FlushFailed },
			std::pair{ UInkStoreTestFaultInjection{ .failMove = true }, UInkDiagnosticCode::IoError },
		};
		for (const auto& [fault, diagnostic] : faults)
		{
			const ScopedFaults injected(fault);
			const auto failed = PublishUInkSnapshotFile(path, fixture.header, fixture.extension, page);
			STORE_CHECK(!failed.success && !failed.published && failed.publishedPath.empty() &&
				failed.systemError != 0 && HasDiagnostic(failed, diagnostic));
			STORE_CHECK(root.Files() == files);
		}

		const auto tempFailed = PublishUInkSnapshotFile(root.At(L"missing\\snapshot.uink"),
			fixture.header, fixture.extension, page);
		STORE_CHECK(!tempFailed.success && !tempFailed.published && tempFailed.systemError == ERROR_PATH_NOT_FOUND);
		const auto invalidPath = PublishUInkSnapshotFile(L"", fixture.header, fixture.extension, page);
		STORE_CHECK(!invalidPath.success && !invalidPath.published && invalidPath.systemError == ERROR_INVALID_NAME);
		auto invalidCanvas = page;
		invalidCanvas.layerIndex = 1;
		const auto invalid = PublishUInkSnapshotFile(path, fixture.header, fixture.extension, invalidCanvas);
		STORE_CHECK(!invalid.success && !invalid.published && HasDiagnostic(invalid, UInkDiagnosticCode::InvalidFieldValue));
		auto limits = UInkReadLimits{};
		limits.maxFileBytes = 1;
		const auto limited = PublishUInkSnapshotFile(path, fixture.header, fixture.extension, page, limits);
		STORE_CHECK(!limited.success && !limited.published && HasDiagnostic(limited, UInkDiagnosticCode::LimitExceeded));
		STORE_CHECK(root.Files() == files);
	}

	void SnapshotPublishValidatesFullStreamLimits(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		auto page = fixture.Page(34, 0, 1);
		page.content.resize(1);
		for (const bool withExtension : { false, true })
		{
			auto extension = fixture.extension;
			auto snapshot = page;
			if (!withExtension)
			{
				extension.reset();
				snapshot.workspaceGuid.reset();
				snapshot.deviceGuid.reset();
			}
			const auto path = root.At(withExtension ? L"snapshot-extension-limit.uink" : L"snapshot-object-limit.uink");
			const auto files = root.Files();
			UInkReadLimits limits;
			// Canvas 和一道 Ink 的页段可读，整读还要计算 Header 及可选扩展。
			limits.maxTopLevelObjects = withExtension ? 3 : 2;
			const auto rejected = PublishUInkSnapshotFile(path, fixture.header, extension, snapshot, limits);
			STORE_CHECK(!rejected.success && !rejected.published && rejected.publishedPath.empty());
			STORE_CHECK(HasDiagnostic(rejected, UInkDiagnosticCode::LimitExceeded) &&
				HasDiagnostic(rejected, UInkDiagnosticCode::SelfValidationFailed));
			STORE_CHECK(root.Files() == files);

			++limits.maxTopLevelObjects;
			const auto published = PublishUInkSnapshotFile(path, fixture.header, extension, snapshot, limits);
			STORE_REQUIRE(published.success && published.published && published.publishedPath == path);
			STORE_CHECK(ReadMatches(ReadUInkStoreFile(path, limits), snapshot));
		}
	}

	void TempCleanupFailurePreservesWriteError(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const auto page = fixture.Page(35, 0, 1);
		constexpr wchar_t targetName[] = L"x.uink";
		const auto path = root.At(targetName);
		const auto files = root.Files();
		for (const bool paged : { false, true })
		{
			UInkStoreWriteResult failed;
			{
				const ScopedFaults injected({ .shortWrite = true, .failDeleteTemp = true });
				if (paged)
					STORE_CHECK(!fixture.Create(path, page, failed));
				else
					failed = PublishUInkSnapshotFile(path, fixture.header, fixture.extension, page);
			}
			STORE_CHECK(!failed.success && !failed.published && failed.publishedPath.empty());
			STORE_CHECK(failed.systemError == ERROR_WRITE_FAULT);
			STORE_REQUIRE(failed.diagnostics.size() == 2);
			STORE_CHECK(failed.diagnostics.front().code == UInkDiagnosticCode::WriteFailed &&
				failed.diagnostics.front().fieldPath == "create.header" &&
				failed.diagnostics.front().systemError == ERROR_WRITE_FAULT);
			STORE_CHECK(failed.diagnostics.back().code == UInkDiagnosticCode::IoError &&
				failed.diagnostics.back().fieldPath == "temp.cleanup" &&
				failed.diagnostics.back().systemError == ERROR_SHARING_VIOLATION);

			const auto remaining = root.Files();
			STORE_REQUIRE(remaining.size() == files.size() + 1);
			const auto temporary = std::find_if(remaining.begin(), remaining.end(), [&](const std::wstring& candidate)
			{
				return !std::binary_search(files.begin(), files.end(), candidate);
			});
			STORE_REQUIRE(temporary != remaining.end());
			const auto leaf = std::wstring_view(*temporary).substr(root.At(L"").size());
			STORE_CHECK(*temporary != path && !leaf.empty() && leaf.size() <= std::wstring_view(targetName).size() &&
				leaf.find_first_not_of(L"0123456789abcdef") == std::wstring_view::npos);
			STORE_CHECK(DeleteFileW(temporary->c_str()));
			STORE_CHECK(root.Files() == files);
		}
	}

	void AppendRollbackAndSeal(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const auto first = fixture.Page(41, 0, 1);
		const auto path = root.At(L"rollback.uink");
		UInkStoreWriteResult created;
		auto writer = fixture.Create(path, first, created);
		STORE_REQUIRE(created.success && writer && !writer->Directory().pages.empty());
		const std::array faults{ UInkStoreTestFaultInjection{ .shortWrite = true },
			UInkStoreTestFaultInjection{ .failFlush = true } };
		const std::array codes{ UInkDiagnosticCode::WriteFailed, UInkDiagnosticCode::FlushFailed };
		for (size_t index = 0; index < faults.size(); ++index)
		{
			const auto replacement = fixture.Page(41, 0, static_cast<uint32_t>(index) + 2);
			const auto before = ReadBytes(path);
			STORE_REQUIRE(before && before->size() == writer->FileBytes());
			const auto directory = writer->Directory();
			UInkStoreWriteResult failed;
			{
				const ScopedFaults injected(faults[index]);
				failed = writer->AppendPage(replacement);
				STORE_CHECK(!failed.success && failed.systemError != 0 && HasDiagnostic(failed, codes[index]));
				STORE_CHECK(ReadBytes(path) == before && writer->FileBytes() == before->size());
				STORE_CHECK(SameDirectory(writer->Directory(), directory) && !writer->IsSealed());
				// 同一故障作用域内重试，确认开关只拦截下一次目标调用。
				STORE_REQUIRE(writer->AppendPage(replacement).success);
			}
			STORE_CHECK(ReadMatches(ReadUInkPage(path, writer->Directory(), replacement.pageGuid), replacement));
		}

		const auto beforeSeal = ReadBytes(path);
		STORE_REQUIRE(beforeSeal && beforeSeal->size() == writer->FileBytes());
		const auto directory = writer->Directory();
		const auto next = fixture.Page(42, 1, 4);
		{
			const ScopedFaults injected({ .shortWrite = true, .failTruncate = true });
			const auto failed = writer->AppendPage(next);
			STORE_CHECK(!failed.success && failed.systemError != 0);
			STORE_CHECK(HasDiagnostic(failed, UInkDiagnosticCode::RollbackFailed));
		}
		STORE_CHECK(writer->IsSealed() && SameDirectory(writer->Directory(), directory));
		const auto sealed = ReadBytes(path);
		STORE_REQUIRE(sealed && sealed->size() > beforeSeal->size());
		STORE_CHECK(writer->FileBytes() == sealed->size());
		STORE_CHECK(!writer->AppendPage(next).success && !writer->Compact().success);
		STORE_CHECK(ReadBytes(path) == sealed && SameDirectory(writer->Directory(), directory));
	}

	void AppendUnconfirmedTail(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const auto first = fixture.Page(43, 0, 1);
		const auto latest = fixture.Page(43, 0, 2);
		for (const bool failRollbackFlush : { false, true })
		{
			const auto path = root.At(failRollbackFlush ? L"rollback-flush.uink" : L"rollback-truncate.uink");
			UInkStoreWriteResult created;
			auto writer = fixture.Create(path, first, created);
			STORE_REQUIRE(created.success && writer && !writer->Directory().pages.empty());
			STORE_CHECK(!writer->IsTailUnconfirmed());
			const auto directory = writer->Directory();
			const auto before = ReadBytes(path);
			STORE_REQUIRE(before && before->size() == writer->FileBytes());

			// 短写不经过首次刷盘，failFlush 此时命中截断之后的刷盘。
			const ScopedFaults injected({ .shortWrite = failRollbackFlush,
				.failFlush = true, .failTruncate = !failRollbackFlush });
			const auto failed = writer->AppendPage(latest);
			STORE_CHECK(!failed.success && HasDiagnostic(failed, failRollbackFlush ?
				UInkDiagnosticCode::WriteFailed : UInkDiagnosticCode::FlushFailed));
			STORE_REQUIRE(failed.diagnostics.size() == 2);
			const auto& rollback = failed.diagnostics.back();
			STORE_CHECK(rollback.code == UInkDiagnosticCode::RollbackFailed &&
				rollback.fieldPath == (failRollbackFlush ? "append.rollbackFlush" : "append.truncate"));
			STORE_CHECK(writer->IsSealed() && writer->IsTailUnconfirmed());
			STORE_CHECK(SameDirectory(writer->Directory(), directory));
			const auto observed = ReadBytes(path);
			STORE_REQUIRE(observed && observed->size() == writer->FileBytes());
			if (failRollbackFlush)
				STORE_CHECK(observed == before);
			else
				STORE_CHECK(observed->size() > before->size());
			STORE_CHECK(ReadMatches(ReadUInkPage(path, writer->Directory(), first.pageGuid), first));
			STORE_CHECK(ReadMatches(ReadUInkStoreFile(path), failRollbackFlush ? first : latest));
			STORE_CHECK(!writer->AppendPage(latest).success && !writer->Compact().success);
			STORE_CHECK(ReadBytes(path) == observed);
		}
	}

	void UnreadableEncodedPageRejected(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const auto first = fixture.Page(45, 0, 1);
		auto unreadable = fixture.Page(45, 0, 2);
		auto retained = unreadable;
		retained.content.pop_back(); // 超限末笔之前的 Canvas、首笔和 Clear。
		const auto prefix = EncodeUInkPage(retained, false, false);
		STORE_REQUIRE(prefix.status == UInkEncodeStatus::Success && !prefix.bytes.empty());
		// 单笔点数在编码上限内，但标准点字段的整块读取费用超过默认模型预算。
		auto& ink = std::get<UInkInk>(unreadable.content.back());
		ink.points.resize(static_cast<size_t>(UInkReadLimits{}.maxSingleGeometryPoints),
			UInkInkPoint{ 4.5f, 1.5f, 4.0f, std::nullopt });
		{
			const auto encoded = EncodeUInkPage(unreadable, false, false);
			STORE_REQUIRE(encoded.status == UInkEncodeStatus::Success && !encoded.bytes.empty());
			const auto context = fixture.Context(0);
			const auto decoded = DecodeUInkStream(encoded.bytes, {}, &context);
			STORE_CHECK(decoded.status == UInkReadStatus::LimitExceeded);
			STORE_CHECK(decoded.validPrefixLength == prefix.bytes.size());
			STORE_CHECK(decoded.summary.diagnosticCounts[static_cast<size_t>(UInkDiagnosticCode::LimitExceeded)].count != 0);
			STORE_REQUIRE(decoded.document && decoded.document->canvases.size() == 1);
			STORE_CHECK(SameCanvas(decoded.document->canvases.front(), retained));
		}

		const auto files = root.Files();
		UInkStoreWriteResult created;
		STORE_CHECK(!fixture.Create(root.At(L"unreadable-create.uink"), unreadable, created));
		STORE_CHECK(!created.success && !created.published &&
			HasDiagnostic(created, UInkDiagnosticCode::SelfValidationFailed) &&
			HasDiagnostic(created, UInkDiagnosticCode::LimitExceeded));
		STORE_CHECK(root.Files() == files);

		const auto snapshot = PublishUInkSnapshotFile(root.At(L"unreadable-snapshot.uink"),
			fixture.header, fixture.extension, unreadable);
		STORE_CHECK(!snapshot.success && !snapshot.published && snapshot.publishedPath.empty() &&
			HasDiagnostic(snapshot, UInkDiagnosticCode::SelfValidationFailed) &&
			HasDiagnostic(snapshot, UInkDiagnosticCode::LimitExceeded));
		STORE_CHECK(root.Files() == files);

		const auto path = root.At(L"unreadable-append.uink");
		auto writer = fixture.Create(path, first, created);
		STORE_REQUIRE(created.success && writer && !writer->Directory().pages.empty());
		const auto before = ReadBytes(path);
		STORE_REQUIRE(before && before->size() == writer->FileBytes());
		const auto directory = writer->Directory();
		const auto failed = writer->AppendPage(unreadable);
		STORE_CHECK(!failed.success && HasDiagnostic(failed, UInkDiagnosticCode::SelfValidationFailed) &&
			HasDiagnostic(failed, UInkDiagnosticCode::LimitExceeded));
		STORE_CHECK(ReadBytes(path) == before && writer->FileBytes() == before->size());
		STORE_CHECK(SameDirectory(writer->Directory(), directory) && !writer->IsSealed());
	}

	void OversizedPageRejected(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const auto first = fixture.Page(51, 0, 1);
		const auto path = root.At(L"oversized-append.uink");
		UInkStoreWriteResult created;
		auto writer = fixture.Create(path, first, created);
		STORE_REQUIRE(created.success && writer && !writer->Directory().pages.empty());

		auto oversized = fixture.Page(52, 1, 2);
		oversized.content.clear();
		// 每笔均低于单对象上限，四笔标准点的总页长超过 32 MiB。
		for (uint32_t index = 0; index < 4; ++index)
		{
			UInkInk ink;
			ink.contentId = index;
			ink.undoId = index;
			ink.points.resize(350000, UInkInkPoint{ 1.5f, 2.5f, 5.0f, std::nullopt });
			oversized.content.emplace_back(std::move(ink));
		}
		{
			const auto encoded = EncodeUInkPage(oversized, false, false);
			STORE_REQUIRE(encoded.status == UInkEncodeStatus::Success);
			STORE_REQUIRE(encoded.bytes.size() > UInkReadLimits{}.maxTopLevelObjectBytes);
		}
		const auto before = ReadBytes(path);
		STORE_REQUIRE(before && before->size() == writer->FileBytes());
		const auto directory = writer->Directory();
		const auto failed = writer->AppendPage(oversized);
		STORE_CHECK(!failed.success && HasDiagnostic(failed, UInkDiagnosticCode::LimitExceeded));
		STORE_CHECK(ReadBytes(path) == before && writer->FileBytes() == before->size());
		STORE_CHECK(SameDirectory(writer->Directory(), directory) && !writer->IsSealed());

		oversized.pageIndex = 0;
		const auto files = root.Files();
		STORE_CHECK(!fixture.Create(root.At(L"oversized-create.uink"), oversized, created));
		STORE_CHECK(!created.success && HasDiagnostic(created, UInkDiagnosticCode::LimitExceeded));
		STORE_CHECK(root.Files() == files);
		const auto snapshot = PublishUInkSnapshotFile(root.At(L"oversized-snapshot.uink"),
			fixture.header, fixture.extension, oversized);
		STORE_CHECK(!snapshot.success && !snapshot.published && snapshot.publishedPath.empty() &&
			HasDiagnostic(snapshot, UInkDiagnosticCode::LimitExceeded));
		STORE_CHECK(root.Files() == files);
	}

	void CompactFailureBoundaries(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const auto path = root.At(L"compact-failures.uink");
		const auto first = fixture.Page(61, 0, 1);
		const auto latest = fixture.Page(61, 0, 2);
		UInkStoreWriteResult created;
		auto writer = fixture.Create(path, first, created);
		STORE_REQUIRE(created.success && writer && !writer->Directory().pages.empty());
		STORE_REQUIRE(writer->AppendPage(latest).success);
		STORE_REQUIRE(writer->SupersededBytes() > 0);

		const auto before = ReadBytes(path);
		STORE_REQUIRE(before && before->size() == writer->FileBytes());
		const auto oldDirectory = writer->Directory();
		const auto files = root.Files();
		for (const auto faults : { UInkStoreTestFaultInjection{ .shortWrite = true },
			UInkStoreTestFaultInjection{ .failFlush = true }, UInkStoreTestFaultInjection{ .failMove = true } })
		{
			UInkStoreCompactResult failed;
			{
				const ScopedFaults injected(faults);
				failed = writer->Compact();
			}
			STORE_CHECK(!failed.success && failed.systemError != 0 && !failed.diagnostics.empty());
			STORE_CHECK(!failed.replacementCommitted && failed.reopenSystemError == 0);
			STORE_CHECK(failed.reopen == (faults.failMove ?
				UInkStoreCompactReopen::Succeeded : UInkStoreCompactReopen::NotAttempted));
			const std::string_view stage = faults.failMove ? "compact.publish" :
				faults.failFlush ? "compact.flush" : "compact.header";
			STORE_CHECK(failed.failureStage && stage == failed.failureStage);
			STORE_CHECK(!writer->IsSealed() && !writer->IsTailUnconfirmed() &&
				SameDirectory(writer->Directory(), oldDirectory));
			STORE_CHECK(ReadBytes(path) == before && writer->FileBytes() == before->size());
			STORE_CHECK(root.Files() == files);
			STORE_CHECK(ReadMatches(ReadUInkPage(path, writer->Directory(), latest.pageGuid), latest));
		}

		const auto newest = fixture.Page(61, 0, 3);
		STORE_REQUIRE(writer->AppendPage(newest).success);
		const uint64_t previouslyWritten = writer->FileBytes();
		{
			const ScopedFaults injected({ .failReopen = true });
			const auto failed = writer->Compact();
			STORE_CHECK(!failed.success && failed.systemError != 0 && !failed.diagnostics.empty());
			STORE_CHECK(failed.replacementCommitted && failed.reopen == UInkStoreCompactReopen::Failed &&
				failed.reopenSystemError == ERROR_SHARING_VIOLATION && failed.systemError == failed.reopenSystemError);
			STORE_CHECK(failed.failureStage && std::string_view(failed.failureStage) == "compact.reopen");
		}
		STORE_CHECK(writer->IsSealed() && !writer->IsTailUnconfirmed() && writer->SupersededBytes() == 0);
		STORE_REQUIRE(writer->Directory().pages.size() == 1);
		STORE_CHECK(writer->FileBytes() < previouslyWritten && root.Files() == files);
		const auto committed = ReadBytes(path);
		STORE_REQUIRE(committed && committed->size() == writer->FileBytes());
		const auto read = ReadUInkStoreFile(path);
		STORE_REQUIRE(ReadMatches(read, newest));
		STORE_CHECK(read.document->header.guid == fixture.header.guid);
		STORE_CHECK(ReadMatches(ReadUInkPage(path, writer->Directory(), newest.pageGuid), newest));
		STORE_CHECK(!writer->AppendPage(newest).success);
		const auto sealedCompact = writer->Compact();
		STORE_CHECK(!sealedCompact.success && !sealedCompact.replacementCommitted &&
			sealedCompact.reopen == UInkStoreCompactReopen::NotAttempted &&
			sealedCompact.reopenSystemError == 0 && sealedCompact.systemError == ERROR_INVALID_HANDLE);
		STORE_CHECK(ReadBytes(path) == committed);

		// 替换前后都可能重开失败，预提交失败时仍应保留旧版本目录。
		const auto originalPath = root.At(L"compact-reopen-source.uink");
		auto original = fixture.Create(originalPath, first, created);
		STORE_REQUIRE(created.success && original && !original->Directory().pages.empty());
		STORE_REQUIRE(original->AppendPage(latest).success);
		const auto originalBytes = ReadBytes(originalPath);
		STORE_REQUIRE(originalBytes && originalBytes->size() == original->FileBytes());
		const auto originalDirectory = original->Directory();
		{
			const ScopedFaults injected({ .failMove = true, .failReopen = true });
			const auto failed = original->Compact();
			STORE_CHECK(!failed.success && failed.systemError != 0 && failed.diagnostics.size() == 2);
			STORE_CHECK(!failed.replacementCommitted && failed.reopen == UInkStoreCompactReopen::Failed &&
				failed.reopenSystemError == ERROR_SHARING_VIOLATION && failed.systemError == ERROR_ACCESS_DENIED);
			STORE_CHECK(failed.failureStage && std::string_view(failed.failureStage) == "compact.publish");
		}
		STORE_CHECK(original->IsSealed() && !original->IsTailUnconfirmed() &&
			SameDirectory(original->Directory(), originalDirectory));
		STORE_CHECK(!original->AppendPage(newest).success && root.Files().size() == files.size() + 1);
		STORE_CHECK(ReadBytes(originalPath) == originalBytes);
		STORE_CHECK(ReadMatches(ReadUInkStoreFile(originalPath), latest));
		STORE_CHECK(ReadMatches(ReadUInkPage(originalPath, original->Directory(), latest.pageGuid), latest));
	}

	void CompactTempCleanupFailurePreservesFlushError(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const auto path = root.At(L"compact-temp-cleanup-failure.uink");
		const auto first = fixture.Page(61, 0, 1);
		const auto latest = fixture.Page(61, 0, 2);
		UInkStoreWriteResult created;
		auto writer = fixture.Create(path, first, created);
		STORE_REQUIRE(created.success && writer);
		STORE_REQUIRE(writer->AppendPage(latest).success);
		const auto before = ReadBytes(path);
		STORE_REQUIRE(before && before->size() == writer->FileBytes());
		const auto directory = writer->Directory();
		const auto files = root.Files();
		UInkStoreCompactResult failed;
		{
			const ScopedFaults injected({ .failFlush = true, .failDeleteTemp = true });
			failed = writer->Compact();
		}
		STORE_CHECK(!failed.success && !failed.replacementCommitted &&
			failed.reopen == UInkStoreCompactReopen::NotAttempted && failed.reopenSystemError == 0);
		STORE_CHECK(failed.systemError == ERROR_WRITE_FAULT &&
			failed.failureStage && std::string_view(failed.failureStage) == "compact.flush");
		STORE_CHECK(!writer->IsSealed() && !writer->IsTailUnconfirmed() &&
			SameDirectory(writer->Directory(), directory));
		STORE_CHECK(ReadBytes(path) == before && writer->FileBytes() == before->size());
		STORE_CHECK(ReadMatches(ReadUInkPage(path, writer->Directory(), latest.pageGuid), latest));
		STORE_REQUIRE(failed.diagnostics.size() == 2);
		STORE_CHECK(failed.diagnostics.front().code == UInkDiagnosticCode::FlushFailed &&
			failed.diagnostics.front().fieldPath == "compact.flush" &&
			failed.diagnostics.front().systemError == ERROR_WRITE_FAULT);
		STORE_CHECK(failed.diagnostics.back().code == UInkDiagnosticCode::IoError &&
			failed.diagnostics.back().fieldPath == "temp.cleanup" &&
			failed.diagnostics.back().systemError == ERROR_SHARING_VIOLATION);

		// 注入删除失败会留下临时文件，测试收尾时负责清除该夹具。
		const auto remaining = root.Files();
		STORE_REQUIRE(remaining.size() == files.size() + 1);
		const auto temporary = std::find_if(remaining.begin(), remaining.end(), [&](const std::wstring& candidate)
		{
			return candidate.starts_with(root.At(L".~")) && candidate.ends_with(L".tmp") &&
				!std::binary_search(files.begin(), files.end(), candidate);
		});
		STORE_REQUIRE(temporary != remaining.end());
		const auto parentLength = root.At(L"").size();
		STORE_CHECK(temporary->size() - parentLength <= path.size() - parentLength);
		STORE_CHECK(DeleteFileW(temporary->c_str()));
		STORE_CHECK(root.Files() == files);
	}

	void PageRangeValidation(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		const auto first = fixture.Page(71, 0, 1);
		const auto second = fixture.Page(72, 1, 2);
		const auto path = root.At(L"ranges.uink");
		UInkStoreWriteResult created;
		auto writer = fixture.Create(path, first, created);
		STORE_REQUIRE(created.success && writer && !writer->Directory().pages.empty());
		STORE_REQUIRE(writer->AppendPage(second).success);
		STORE_REQUIRE(writer->Directory().pages.size() == 2);
		STORE_CHECK(!ReadUInkPage(path, writer->Directory(), Guid(73)).document);

		auto directory = writer->Directory();
		const auto range = directory.pages.find(first.pageGuid.Bytes());
		STORE_REQUIRE(range != directory.pages.end());
		const auto original = range->second;
		range->second.length = writer->FileBytes() - original.offset;
		STORE_CHECK(!ReadUInkPage(path, directory, first.pageGuid).document);
		range->second.length += 1;
		STORE_CHECK(!ReadUInkPage(path, directory, first.pageGuid).document);
		range->second.length = UInkReadLimits{}.maxTopLevelObjectBytes + 1;
		STORE_CHECK(!ReadUInkPage(path, directory, first.pageGuid).document);
		range->second = original;
		range->second.length = 0;
		STORE_CHECK(!ReadUInkPage(path, directory, first.pageGuid).document);

		UInkReadLimits limits;
		limits.maxFileBytes = 1;
		STORE_CHECK(ReadUInkStoreFile(path, limits).status == UInkReadStatus::LimitExceeded);
		STORE_CHECK(!ReadUInkPage(path, writer->Directory(), first.pageGuid, limits).document);

		const auto goodPage = EncodeUInkPage(first, false, false);
		auto prefix = EncodeUInkHeaderPrefix(fixture.header, fixture.extension);
		auto invalidCanvas = second;
		invalidCanvas.content.clear();
		auto invalidPage = EncodeUInkPage(invalidCanvas, false, false);
		STORE_REQUIRE(goodPage.status == UInkEncodeStatus::Success && prefix.status == UInkEncodeStatus::Success);
		STORE_REQUIRE(invalidPage.status == UInkEncodeStatus::Success && !invalidPage.bytes.empty());
		// 加入重复的 type 键，构造可识别为 Canvas、但整块被跳过的第二个对象。
		const unsigned mapMarker = std::to_integer<unsigned>(invalidPage.bytes.front());
		STORE_REQUIRE(mapMarker >= 0x80 && mapMarker < 0x8f);
		invalidPage.bytes.front() = static_cast<std::byte>(mapMarker + 1);
		constexpr std::array duplicateType{ std::byte{ 0xa4 }, std::byte{ 't' },
			std::byte{ 'y' }, std::byte{ 'p' }, std::byte{ 'e' }, std::byte{ 0xcd },
			std::byte{ 0 }, std::byte{ kCanvasType } };
		invalidPage.bytes.insert(invalidPage.bytes.end(), duplicateType.begin(), duplicateType.end());
		const size_t pageStart = prefix.bytes.size();
		prefix.bytes.insert(prefix.bytes.end(), goodPage.bytes.begin(), goodPage.bytes.end());
		prefix.bytes.insert(prefix.bytes.end(), invalidPage.bytes.begin(), invalidPage.bytes.end());
		// 整文件模式用来确认重复键确实会跳过该块，页段模式还须拒绝第二个 Canvas。
		const auto raw = DecodeUInkStream(prefix.bytes);
		STORE_REQUIRE(raw.document && raw.document->canvases.size() == 1);
		STORE_REQUIRE(raw.provenance.containsInvalidCompleteBlocks);
		const auto invalidPath = root.At(L"ranges-with-skipped-canvas.uink");
		STORE_REQUIRE(WriteBytes(invalidPath, prefix.bytes));
		range->second = original;
		range->second.offset = pageStart;
		range->second.length = prefix.bytes.size() - pageStart;
		const auto invalidRange = ReadUInkPage(invalidPath, directory, first.pageGuid);
		STORE_CHECK(!invalidRange.document && invalidRange.summary.outcome == UInkReadOutcome::Failed);
	}

	void PageRangeHardLimitAndOverflow(TestState& state, const TempDirectory& root)
	{
		const auto path = root.At(L"ranges-hard-limit.uink");
		constexpr uint64_t offset = 16;
		constexpr uint64_t length = kUInkStoreMaxPageBytes + 1;
		const std::vector<std::byte> bytes(static_cast<size_t>(offset + length));
		STORE_REQUIRE(WriteBytes(path, bytes));
		UInkPageDirectory directory;
		const auto pageGuid = Guid(74);
		auto& range = directory.pages[pageGuid.Bytes()];
		range = { offset, length };
		const auto oversized = ReadUInkPage(path, directory, pageGuid);
		STORE_CHECK(oversized.status == UInkReadStatus::LimitExceeded && !oversized.document);
		STORE_REQUIRE(oversized.diagnostics.size() == 1);
		const auto& limit = oversized.diagnostics.front();
		STORE_CHECK(limit.code == UInkDiagnosticCode::LimitExceeded &&
			limit.fieldPath == "page.bytes" && limit.byteOffset == offset);

		range = { (std::numeric_limits<uint64_t>::max)() - 3, 8 };
		const auto overflowing = ReadUInkPage(path, directory, pageGuid);
		STORE_CHECK(overflowing.status == UInkReadStatus::IoError && !overflowing.document);
		STORE_REQUIRE(overflowing.diagnostics.size() == 1);
		const auto& eof = overflowing.diagnostics.front();
		STORE_CHECK(eof.code == UInkDiagnosticCode::IoError && eof.systemError == ERROR_HANDLE_EOF &&
			eof.fieldPath == "page.range" && eof.byteOffset == range.offset);
	}

	void PageRejectsRecognizedSecondCanvas(TestState& state, const TempDirectory& root)
	{
		const Fixture fixture;
		auto first = fixture.Page(75, 0, 1);
		auto second = fixture.Page(76, 1, 2);
		first.content.clear();
		second.content.clear();
		const auto firstPage = EncodeUInkPage(first, false, false);
		const auto secondPage = EncodeUInkPage(second, false, false);
		STORE_REQUIRE(firstPage.status == UInkEncodeStatus::Success && !firstPage.bytes.empty());
		STORE_REQUIRE(secondPage.status == UInkEncodeStatus::Success && secondPage.bytes.size() > 1);

		for (const bool truncated : { false, true })
		{
			auto prefix = EncodeUInkHeaderPrefix(fixture.header, fixture.extension);
			STORE_REQUIRE(prefix.status == UInkEncodeStatus::Success);
			const auto offset = prefix.bytes.size();
			prefix.bytes.insert(prefix.bytes.end(), firstPage.bytes.begin(), firstPage.bytes.end());
			prefix.bytes.insert(prefix.bytes.end(), secondPage.bytes.begin(), secondPage.bytes.end());
			if (truncated) prefix.bytes.pop_back();
			const auto path = root.At(truncated ? L"ranges-truncated-canvas.uink" : L"ranges-limited-canvas.uink");
			STORE_REQUIRE(WriteBytes(path, prefix.bytes));

			UInkPageDirectory directory;
			directory.context = fixture.Context(offset);
			directory.pages.emplace(first.pageGuid.Bytes(), UInkPageRange{ offset, prefix.bytes.size() - offset });
			UInkReadLimits limits;
			if (!truncated) limits.maxTopLevelObjects = 1;
			const auto read = ReadUInkPage(path, directory, first.pageGuid, limits);
			STORE_CHECK(read.status == UInkReadStatus::StoppedAtCorruptTail &&
				!read.document && read.summary.outcome == UInkReadOutcome::Failed);
			STORE_CHECK(std::any_of(read.diagnostics.begin(), read.diagnostics.end(), [&](const auto& diag)
			{
				return diag.code == UInkDiagnosticCode::InvalidObjectOrder &&
					diag.fieldPath == "page.canvas" && diag.byteOffset == offset + firstPage.bytes.size();
			}));
			uint64_t canvasCount = 0;
			const auto stream = DecodeUInkStream(std::span<const std::byte>(prefix.bytes).subspan(offset),
				limits, directory.context, offset, canvasCount);
			STORE_CHECK(canvasCount == 2 && !stream.document);
		}
	}

	void CompactUpdatesContinuation(TestState& state, const TempDirectory& root)
	{
		Fixture fixture;
		fixture.extension->workspaces.front().currentPageIndex = 5;
		auto high = fixture.Page(77, 9, 1);
		high.content.pop_back();
		std::get<UInkClear>(high.content.back()).undoId = 12;
		auto empty = fixture.Page(78, 5, 2);
		empty.content.clear();
		auto low = fixture.Page(79, 0, 3);
		std::get<UInkInk>(low.content.back()).undoId = 17;
		UInkStoreWriteResult created;
		auto writer = fixture.Create(root.At(L"continuation.uink"), high, created);
		STORE_REQUIRE(created.success && writer && !writer->Directory().pages.empty());
		const auto& directory = writer->Directory();
		STORE_CHECK(directory.lastPageGuid == high.pageGuid &&
			directory.nextContentId == 2 && directory.lastUndoId == 12);
		STORE_REQUIRE(writer->AppendPage(empty).success);
		STORE_CHECK(directory.lastPageGuid == empty.pageGuid &&
			directory.nextContentId == 0 && directory.lastUndoId == 0);
		STORE_REQUIRE(writer->AppendPage(low).success);
		STORE_CHECK(directory.lastPageGuid == low.pageGuid &&
			directory.nextContentId == 3 && directory.lastUndoId == 17);
		const auto physicalTail = [&](const UInkGuid& guid)
		{
			const auto found = directory.pages.find(guid.Bytes());
			return found != directory.pages.end() &&
				found->second.offset + found->second.length == writer->FileBytes();
		};
		STORE_CHECK(physicalTail(low.pageGuid));
		STORE_REQUIRE(writer->Compact().success);
		const auto compacted = ReadUInkStoreFile(writer->Path());
		STORE_REQUIRE(compacted.status == UInkReadStatus::Complete && compacted.document &&
			compacted.document->headerExtension && compacted.document->headerExtension->workspaces.size() == 1);
		STORE_CHECK(compacted.document->headerExtension->workspaces.front().currentPageIndex ==
			fixture.extension->workspaces.front().currentPageIndex);
		STORE_CHECK(physicalTail(high.pageGuid));
		STORE_CHECK(directory.lastPageGuid == high.pageGuid &&
			directory.nextContentId == 2 && directory.lastUndoId == 12);
		const auto emptyRange = directory.pages.find(empty.pageGuid.Bytes());
		STORE_REQUIRE(emptyRange != directory.pages.end());
		STORE_CHECK(emptyRange->second.nextContentId == 0 && emptyRange->second.lastUndoId == 0);
	}

	void StreamVersionsAndFullKey(TestState& state, const TempDirectory& root)
	{
		Fixture fixture;
		auto otherWorkspace = fixture.extension->workspaces.front();
		otherWorkspace.guid = Guid(81);
		fixture.extension->workspaces.push_back(otherWorkspace);
		auto otherDevice = fixture.extension->devices.front();
		otherDevice.guid = Guid(82);
		fixture.extension->devices.push_back(otherDevice);
		fixture.header.workspaceNum = 2;
		fixture.header.deviceNum = 2;

		const auto original = fixture.Page(83, 0, 1);
		auto onWorkspace = fixture.Page(83, 1, 2);
		onWorkspace.workspaceGuid = otherWorkspace.guid;
		auto onDevice = fixture.Page(83, 3, 3);
		onDevice.deviceGuid = otherDevice.guid;
		auto onLayer = fixture.Page(83, 3, 4);
		onLayer.layerIndex = 1;
		onLayer.layerNumber = 2;
		onLayer.viewport.reset();
		onLayer.background.reset();
		onLayer.pageState.reset();
		onLayer.pageKind.reset();
		onLayer.binding.reset();
		const auto latest = fixture.Page(83, 3, 5);

		auto prefix = EncodeUInkHeaderPrefix(fixture.header, fixture.extension);
		STORE_REQUIRE(prefix.status == UInkEncodeStatus::Success);
		const std::array<const UInkCanvas*, 5> versions{ &original, &onWorkspace, &onDevice, &onLayer, &latest };
		for (const UInkCanvas* page : versions)
		{
			const auto encoded = EncodeUInkPage(*page, false, false);
			STORE_REQUIRE(encoded.status == UInkEncodeStatus::Success);
			prefix.bytes.insert(prefix.bytes.end(), encoded.bytes.begin(), encoded.bytes.end());
		}
		const auto decoded = DecodeUInkStream(prefix.bytes);
		STORE_REQUIRE(decoded.status == UInkReadStatus::Complete && decoded.document);
		STORE_REQUIRE(decoded.document->canvases.size() == 5);
		STORE_CHECK(decoded.diagnostics.empty() && !decoded.safeAppendOffset && !decoded.provenance.usedTemporaryIdentity);
		STORE_CHECK(SameCanvas(decoded.document->canvases.front(), original));
		STORE_CHECK(SameCanvas(decoded.document->canvases.back(), latest));

		const auto path = root.At(L"full-key.uink");
		STORE_REQUIRE(WriteBytes(path, prefix.bytes));
		const auto stored = ReadUInkStoreFile(path);
		STORE_REQUIRE(stored.status == UInkReadStatus::Complete && stored.document);
		STORE_REQUIRE(stored.document->canvases.size() == 4);
		const std::array<const UInkCanvas*, 4> expected{ &onWorkspace, &onDevice, &onLayer, &latest };
		for (size_t index = 0; index < expected.size(); ++index)
			STORE_CHECK(SameCanvas(stored.document->canvases[index], *expected[index]));

		auto anonymous = EncodeUInkHeaderPrefix(fixture.header, fixture.extension);
		STORE_REQUIRE(anonymous.status == UInkEncodeStatus::Success);
		std::array temporaryPages{ fixture.Page(84, 3, 6), fixture.Page(85, 3, 7) };
		constexpr std::array guidKey{ std::byte{ 0xa8 }, std::byte{ 'p' }, std::byte{ 'a' },
			std::byte{ 'g' }, std::byte{ 'e' }, std::byte{ 'G' }, std::byte{ 'u' },
			std::byte{ 'i' }, std::byte{ 'd' } };
		for (auto& page : temporaryPages)
		{
			auto encoded = EncodeUInkPage(page, false, false);
			STORE_REQUIRE(encoded.status == UInkEncodeStatus::Success);
			const auto found = std::search(encoded.bytes.begin(), encoded.bytes.end(), guidKey.begin(), guidKey.end());
			STORE_REQUIRE(found != encoded.bytes.end());
			// 将 pageGuid 改为同宽度的未知键，保留其余 Canvas 字节。
			found[guidKey.size() - 1] = std::byte{ 'x' };
			anonymous.bytes.insert(anonymous.bytes.end(), encoded.bytes.begin(), encoded.bytes.end());
			page.pageGuid = {};
			page.temporaryPage = true;
		}
		const auto anonymousPath = root.At(L"temporary-page-identities.uink");
		STORE_REQUIRE(WriteBytes(anonymousPath, anonymous.bytes));
		const auto anonymousResult = ReadUInkStoreFile(anonymousPath);
		STORE_REQUIRE(anonymousResult.document && anonymousResult.document->canvases.size() == temporaryPages.size());
		STORE_CHECK(anonymousResult.summary.outcome == UInkReadOutcome::Partial &&
			anonymousResult.provenance.usedTemporaryIdentity);
		for (size_t index = 0; index < temporaryPages.size(); ++index)
			STORE_CHECK(SameCanvas(anonymousResult.document->canvases[index], temporaryPages[index]));
	}

	void StreamPageBoundary(TestState& state, const TempDirectory&)
	{
		const Fixture fixture;
		const auto page = fixture.Page(91, 7, 1);
		auto truncatedPage = page;
		truncatedPage.content.emplace_back(UInkClear{ 3, 4 });
		const auto valid = EncodeUInkPage(page, false, false);
		const auto full = EncodeUInkPage(truncatedPage, false, false);
		const auto next = EncodeUInkPage(fixture.Page(92, 8, 2), false, false);
		STORE_REQUIRE(valid.status == UInkEncodeStatus::Success && next.status == UInkEncodeStatus::Success);
		STORE_REQUIRE(full.status == UInkEncodeStatus::Success && full.bytes.size() > valid.bytes.size() + 1);
		auto isolated = full.bytes;
		isolated.pop_back();
		auto adjacent = isolated;
		// 若读取器越过 span，后续页首字节会补齐上页最后一个 Clear 的 uint32。
		adjacent.insert(adjacent.end(), next.bytes.begin(), next.bytes.end());
		const auto context = fixture.Context(uint64_t{ 1 } << 35);
		const auto first = DecodeUInkStream(isolated, {}, &context);
		const auto bounded = DecodeUInkStream(std::span<const std::byte>(adjacent).first(isolated.size()), {}, &context);
		STORE_REQUIRE(first.status == UInkReadStatus::RecoveredTruncatedTail && first.document);
		STORE_REQUIRE(bounded.status == first.status && bounded.document);
		STORE_REQUIRE(first.document->canvases.size() == 1 && bounded.document->canvases.size() == 1);
		STORE_CHECK(SameCanvas(first.document->canvases.front(), page));
		STORE_CHECK(SameCanvas(bounded.document->canvases.front(), first.document->canvases.front()));
		STORE_CHECK(first.validPrefixLength == bounded.validPrefixLength &&
			first.failedObjectOffset == bounded.failedObjectOffset &&
			first.decodedObjectCount == bounded.decodedObjectCount &&
			first.geometryPointCount == bounded.geometryPointCount && first.modelCharge == bounded.modelCharge);
		STORE_CHECK(!first.safeAppendOffset && !bounded.safeAppendOffset &&
			first.summary.outcome == UInkReadOutcome::Partial && bounded.summary.outcome == first.summary.outcome);
		STORE_REQUIRE(first.diagnostics.size() == 1 && bounded.diagnostics.size() == first.diagnostics.size());
		const auto& a = first.diagnostics.front();
		const auto& b = bounded.diagnostics.front();
		STORE_CHECK(a.code == UInkDiagnosticCode::TruncatedTail && a.byteOffset == context.baseOffset + valid.bytes.size());
		STORE_CHECK(a.code == b.code && a.severity == b.severity && a.byteOffset == b.byteOffset &&
			a.objectIndex == b.objectIndex && a.fieldPath == b.fieldPath && a.canvasOrdinal == b.canvasOrdinal);
		const auto& count = bounded.summary.diagnosticCounts[static_cast<size_t>(a.code)];
		STORE_CHECK(count.count == 1 && count.firstByteOffset == a.byteOffset);

		// 超限字段转入恢复扫描时，同样只能扫描到 span 末尾。
		isolated = full.bytes;
		STORE_REQUIRE(isolated[valid.bytes.size()] == std::byte{ 0x83 });
		isolated[valid.bytes.size()] = std::byte{ 0x84 };
		// 测试直接构造 Clear 的 extra: {"blob": bin16(1024)}，以便制造截断载荷。
		constexpr std::array extra{ std::byte{ 0xa5 }, std::byte{ 'e' }, std::byte{ 'x' },
			std::byte{ 't' }, std::byte{ 'r' }, std::byte{ 'a' }, std::byte{ 0x81 },
			std::byte{ 0xa4 }, std::byte{ 'b' }, std::byte{ 'l' }, std::byte{ 'o' }, std::byte{ 'b' },
			std::byte{ 0xc5 }, std::byte{ 0x04 }, std::byte{ 0x00 } };
		isolated.insert(isolated.end(), extra.begin(), extra.end());
		isolated.resize(isolated.size() + 1024, std::byte{ 0x41 });
		isolated.pop_back();
		adjacent = isolated;
		adjacent.insert(adjacent.end(), next.bytes.begin(), next.bytes.end());
		UInkReadLimits limits;
		limits.maxStringOrBinaryBytes = 512;
		const auto limited = DecodeUInkStream(isolated, limits, &context);
		const auto limitedAdjacent = DecodeUInkStream(
			std::span<const std::byte>(adjacent).first(isolated.size()), limits, &context);
		STORE_REQUIRE(limited.status == UInkReadStatus::LimitExceeded && limited.document);
		STORE_REQUIRE(limitedAdjacent.status == limited.status && limitedAdjacent.document);
		STORE_REQUIRE(limited.document->canvases.size() == 1 && limitedAdjacent.document->canvases.size() == 1);
		STORE_CHECK(SameCanvas(limited.document->canvases.front(), page));
		STORE_CHECK(SameCanvas(limitedAdjacent.document->canvases.front(), page));
		STORE_CHECK(limited.failedObjectOffset == context.baseOffset + valid.bytes.size());
		STORE_CHECK(limitedAdjacent.failedObjectOffset == limited.failedObjectOffset &&
			limitedAdjacent.decodedObjectCount == limited.decodedObjectCount &&
			limitedAdjacent.validPrefixLength == limited.validPrefixLength);

		auto missingRegistry = context;
		missingRegistry.headerExtension->workspaces.front().guid = Guid(93);
		missingRegistry.headerExtension->devices.front().guid = Guid(94);
		const auto unresolved = DecodeUInkStream(valid.bytes, {}, &missingRegistry);
		STORE_REQUIRE(unresolved.document && unresolved.document->canvases.size() == 1);
		const auto& temporary = unresolved.document->canvases.front();
		STORE_CHECK(temporary.temporaryWorkspace && temporary.temporaryDevice &&
			!temporary.temporaryPage && !temporary.temporaryLayer);
		STORE_CHECK(unresolved.provenance.usedTemporaryIdentity && !unresolved.safeAppendOffset);
	}
}

int RunUInkStoreTests()
{
	constexpr std::pair<const char*, void (*)(TestState&, const TempDirectory&)> cases[] = {
		{ "AppendReplaceCompact", AppendReplaceCompact },
		{ "PublishDoesNotOverwrite", PublishDoesNotOverwrite },
		{ "RelativeCreateSurvivesCurrentDirectoryChange", RelativeCreateSurvivesCurrentDirectoryChange },
		{ "CreateFailuresCleanTemp", CreateFailuresCleanTemp },
		{ "SnapshotPublish", SnapshotPublish },
		{ "SnapshotPublishLongPath", SnapshotPublishLongPath },
		{ "SnapshotPublishFailuresCleanTemp", SnapshotPublishFailuresCleanTemp },
		{ "SnapshotPublishValidatesFullStreamLimits", SnapshotPublishValidatesFullStreamLimits },
		{ "TempCleanupFailurePreservesWriteError", TempCleanupFailurePreservesWriteError },
		{ "AppendRollbackAndSeal", AppendRollbackAndSeal },
		{ "AppendUnconfirmedTail", AppendUnconfirmedTail },
		{ "UnreadableEncodedPageRejected", UnreadableEncodedPageRejected },
		{ "OversizedPageRejected", OversizedPageRejected },
		{ "CompactFailureBoundaries", CompactFailureBoundaries },
		{ "CompactTempCleanupFailurePreservesFlushError", CompactTempCleanupFailurePreservesFlushError },
		{ "PageRangeValidation", PageRangeValidation },
		{ "PageRangeHardLimitAndOverflow", PageRangeHardLimitAndOverflow },
		{ "PageRejectsRecognizedSecondCanvas", PageRejectsRecognizedSecondCanvas },
		{ "CompactUpdatesContinuation", CompactUpdatesContinuation },
		{ "StreamVersionsAndFullKey", StreamVersionsAndFullKey },
		{ "StreamPageBoundary", StreamPageBoundary }
	};
	TestState setup{ "TempDirectory" };
	TempDirectory root;
	if (!setup.Check(!!root, "created unique GetTempPathW directory", __LINE__)) return setup.failures;
	int failures = 0;
	for (const auto& [name, run] : cases)
	{
		TestState state{ name };
		run(state, root);
		failures += state.failures;
	}
	setup.Check(root.Clean(), "removed temporary directory", __LINE__);
	failures += setup.failures;
	std::cout << "UInk store tests: " << failures << " failures\n";
	return failures;
}
