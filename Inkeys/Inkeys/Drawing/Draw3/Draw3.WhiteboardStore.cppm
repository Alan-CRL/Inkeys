module;
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
export module Inkeys.Drawing.Draw3.whiteboard_store;
export import draw3.uink_draw3_export;
export namespace Inkeys::Drawing::Draw3
{
 struct WhiteboardStoreRequest
 {
  std::size_t page = 0;
  std::uint64_t revision = 0;
  draw3::uink::Draw3UInkExportSnapshot snapshot;
  std::vector<draw3::uink::UInkGuid> pages;
  std::uint32_t currentPage = 0;
 };
 struct WhiteboardStoreCompletion
 {
  std::size_t page = 0;
  std::uint64_t revision = 0;
  std::uint64_t generation = 0;
  bool load = false;
  bool success = false;
  std::optional<draw3::uink::UInkDocument> document;
 };
 class WhiteboardStore
 {
 public:
  WhiteboardStore(std::wstring root, std::function<void()> wake);
  ~WhiteboardStore();
  bool Save(WhiteboardStoreRequest request);
  bool Load(std::size_t page, draw3::uink::UInkGuid id, std::uint64_t generation = 0);
  bool Take(WhiteboardStoreCompletion& completion);
  void Drain();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
 };
}
