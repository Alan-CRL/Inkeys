module;
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
export module draw3.uink_page_file;
export import draw3.uink_file;
export namespace draw3::uink {
struct UInkPageByteRange { UInkGuid pageGuid; uint64_t offset=0; uint64_t length=0; };
struct UInkPageFileDirectory {
 std::wstring path;
 UInkSourceRevision revision;
 // 仅保存页身份及注册表，不保留各页 extra、笔迹和历史。
 UInkDocument metadata;
 std::vector<UInkPageByteRange> pages;
 // 序列化工作缓冲的高水位估计；不含解码模型、分配器余量或调用方快照。
 uint64_t peakBufferBytes=0;
};
struct UInkPageFileResult {
 UInkSaveStatus status=UInkSaveStatus::IoError;
 std::optional<UInkPageFileDirectory> directory;
 uint32_t systemError=0;
};
// 仅供创建者清理本进程已提交且不再被索引引用的版本；不能用于扫描到的外部文件。
// 校验强版本身份并按同一文件句柄删除；失败保留文件，调用方不得退化为按路径删除。
bool TryRemoveOwnedUInkPageFile(const UInkPageFileDirectory& directory) noexcept;
std::optional<UInkPageFileDirectory> ScanUInkPageFile(const std::wstring& path);
// 返回规范化单页文档（pageIndex/currentPageIndex=0），页 GUID 和内容不变。
UInkReadResult ReadUInkPageFile(const UInkPageFileDirectory& directory,const UInkGuid& pageGuid);
// newPath 必须不存在；旧版本只读。调用者在成功后原子发布其索引。
// orderedPages 必须保留旧页顺序且只可追加；不提供删除或重排页面。
UInkPageFileResult RewriteUInkPageFile(const std::wstring& newPath,
 const UInkPageFileDirectory* previous,const UInkDocument& onePage,
 const std::vector<UInkGuid>& orderedPages,uint32_t currentPageIndex);
}
