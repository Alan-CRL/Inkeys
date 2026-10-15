module;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
module Inkeys.Drawing.Draw3.whiteboard_store;
import draw3.uink_page_file;
namespace Inkeys::Drawing::Draw3
{
 struct WhiteboardStore::Impl
 {
  std::mutex mutex;
  std::condition_variable condition;
  std::deque<WhiteboardStoreRequest> saves;
  struct LoadRequest { std::size_t first; draw3::uink::UInkGuid second; std::uint64_t generation; };
  std::deque<LoadRequest> loads;
  std::deque<WhiteboardStoreCompletion> completed;
  std::optional<draw3::uink::UInkPageFileDirectory> directory;
  std::optional<draw3::uink::UInkPageFileDirectory> backup;
  std::filesystem::path root;
  std::function<void()> wake;
  bool closing = false;
  bool busy = false;
  std::uint64_t sequence = 0;
  std::thread thread;

  bool PublishIndex(const std::filesystem::path& file)
  {
   const auto temp = root / (L"current-" + std::to_wstring(sequence) + L".tmp");
   const auto index = root / L"current";
   HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
   if (handle == INVALID_HANDLE_VALUE) return false;
   const auto name = file.filename().u8string();
   DWORD written = 0;
   const bool success = WriteFile(handle, name.data(), static_cast<DWORD>(name.size()), &written, nullptr)
    && written == name.size() && FlushFileBuffers(handle);
   CloseHandle(handle);
   if (!success) return false;
   return MoveFileExW(temp.c_str(), index.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
  }
  void Run()
  {
   for (;;)
   {
    WhiteboardStoreRequest request;
    std::optional<LoadRequest> load;
    {
     std::unique_lock lock(mutex);
     condition.wait(lock, [&] { return closing || !loads.empty() || !saves.empty(); });
     if (loads.empty() && saves.empty() && closing) return;
     busy = true;
     if (!loads.empty()) { load = loads.front(); loads.pop_front(); }
     else { request = std::move(saves.front()); saves.pop_front(); }
    }
    WhiteboardStoreCompletion result;
    result.page = load ? load->first : request.page;
    result.revision = request.revision;
    result.load = load.has_value();
    result.generation = load ? load->generation : 0;
    try
    {
     if (load && directory)
     {
      auto read = draw3::uink::ReadUInkPageFile(*directory, load->second);
      if (read.status == draw3::uink::UInkReadStatus::Complete && read.document) { result.document = std::move(read.document); result.success = true; }
     }
     else if (!load && !root.empty())
     {
      std::filesystem::create_directories(root);
      auto exported = draw3::uink::ExportDraw3SnapshotToUInk(request.snapshot);
      if (exported.document)
      {
       const auto file = root / (std::to_wstring(++sequence) + L".uink");
       auto saved = draw3::uink::RewriteUInkPageFile(file.wstring(), directory ? &*directory : nullptr,
        *exported.document, request.pages, request.currentPage);
       if (saved.directory && PublishIndex(file))
       {
        // 只有索引原子提交后发布新偏移；保留一份备份，旧版本仅凭完整修订及句柄证明清理。
        if (backup) (void)draw3::uink::TryRemoveOwnedUInkPageFile(*backup);
        backup = std::move(directory);
        directory = std::move(saved.directory);
        result.success = true;
       }
      }
     }
    }
    catch (...) { result.success = false; }
    if (!result.success) std::fputs("[Whiteboard.Store] operation failed; retaining warm page\n", stderr);
    {
     std::scoped_lock lock(mutex);
     completed.push_back(std::move(result));
     busy = false;
    }
    condition.notify_all();
    if (wake) wake();
   }
  }
 };
 WhiteboardStore::WhiteboardStore(std::wstring root, std::function<void()> wake) : impl_(std::make_unique<Impl>())
 {
  auto session = draw3::uink::CreateUInkGuid();
  if (session && !root.empty()) impl_->root = std::filesystem::path(root) / L"whiteboard" /
   std::filesystem::path(draw3::uink::FormatUInkGuid(*session));
  impl_->wake = std::move(wake);
  impl_->thread = std::thread([this] { impl_->Run(); });
 }
 WhiteboardStore::~WhiteboardStore()
 {
  { std::scoped_lock lock(impl_->mutex); impl_->closing = true; }
  impl_->condition.notify_all();
  impl_->thread.join();
 }
 bool WhiteboardStore::Save(WhiteboardStoreRequest request)
 {
  std::scoped_lock lock(impl_->mutex);
  if (impl_->closing) return false;
  for (auto& pending : impl_->saves) if (pending.page == request.page)
  { pending = std::move(request); impl_->condition.notify_all(); return true; }
  // 有限在途快照；队列满时绘制线程保留 warm 页并在后续安全点重试。
  if (impl_->saves.size() >= 2) return false;
  impl_->saves.push_back(std::move(request));
  impl_->condition.notify_all();
  return true;
 }
 bool WhiteboardStore::Load(std::size_t page, draw3::uink::UInkGuid id, std::uint64_t generation)
 {
  std::scoped_lock lock(impl_->mutex);
  if (impl_->closing) return false;
  impl_->loads.push_back({page, id, generation});
  impl_->condition.notify_all();
  return true;
 }
 bool WhiteboardStore::Take(WhiteboardStoreCompletion& completion)
 {
  std::scoped_lock lock(impl_->mutex);
  if (impl_->completed.empty()) return false;
  completion = std::move(impl_->completed.front()); impl_->completed.pop_front(); return true;
 }
 void WhiteboardStore::Drain()
 {
  std::unique_lock lock(impl_->mutex);
  impl_->condition.wait(lock, [&] { return !impl_->busy && impl_->saves.empty() && impl_->loads.empty(); });
 }
}
