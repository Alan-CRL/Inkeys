module;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define MSGPACK_NO_BOOST
#include <windows.h>
#include <bcrypt.h>
#include <msgpack.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <set>
#include <variant>
module draw3.uink_page_file;
namespace draw3::uink {
namespace {
constexpr DWORD ChunkSize=64*1024;
struct Handle {
 HANDLE value=INVALID_HANDLE_VALUE;
 explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h){}
 ~Handle(){if(value!=INVALID_HANDLE_VALUE) CloseHandle(value);}
 Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};
void Require(bool good){if(!good) throw std::runtime_error("Invalid whiteboard page file");}
bool Seek(HANDLE h,uint64_t offset){LARGE_INTEGER v;v.QuadPart=static_cast<LONGLONG>(offset);return SetFilePointerEx(h,v,nullptr,FILE_BEGIN)!=FALSE;}
bool Write(HANDLE h,std::span<const std::byte> bytes){
 while(!bytes.empty()){DWORD count=0;DWORD size=static_cast<DWORD>((std::min)(bytes.size(),size_t(ChunkSize)));
 if(!WriteFile(h,bytes.data(),size,&count,nullptr)||count!=size)return false;bytes=bytes.subspan(count);}return true;
}
bool Read(HANDLE h,std::span<std::byte> bytes){
 while(!bytes.empty()){DWORD count=0;DWORD size=static_cast<DWORD>((std::min)(bytes.size(),size_t(ChunkSize)));
 if(!ReadFile(h,bytes.data(),size,&count,nullptr)||count!=size)return false;bytes=bytes.subspan(count);}return true;
}
std::optional<UInkSourceRevision> Revision(HANDLE h){
 BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(h,&info))return {};
 UInkSourceRevision r;r.volumeSerial=info.dwVolumeSerialNumber;r.fileIndex=(uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
 r.length=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;r.lastWriteTime=(uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime;
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
 if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
 bool good=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
 std::array<std::byte,ChunkSize> buffer{};uint64_t left=r.length;good=good&&Seek(h,0);
 while(good&&left){auto n=static_cast<DWORD>((std::min)(left,uint64_t(buffer.size())));
 good=Read(h,std::span(buffer).first(n))&&BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),n,0)>=0;left-=n;}
 if(good)good=BCryptFinishHash(hash,r.sha256.data(),static_cast<ULONG>(r.sha256.size()),0)>=0;
 if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
 if(!good)return {};return r;
}
const msgpack::object* Field(const msgpack::object& object,std::string_view name){
 Require(object.type==msgpack::type::MAP);const msgpack::object* result=nullptr;
 for(uint32_t i=0;i<object.via.map.size;++i){const auto& item=object.via.map.ptr[i];Require(item.key.type==msgpack::type::STR);
 if(std::string_view(item.key.via.str.ptr,item.key.via.str.size)==name){Require(!result);result=&item.val;}}
 return result;
}
uint32_t Number(const msgpack::object& o){Require(o.type==msgpack::type::POSITIVE_INTEGER&&o.via.u64<=UINT32_MAX);return static_cast<uint32_t>(o.via.u64);}
uint32_t Number(const msgpack::object& o,std::string_view key){auto p=Field(o,key);Require(p!=nullptr);return Number(*p);}
UInkGuid Guid(const msgpack::object& o){Require(o.type==msgpack::type::STR);auto g=ParseUInkGuid(std::string(o.via.str.ptr,o.via.str.size));Require(g.has_value()&&!g->IsZero());return *g;}
uint32_t Type(const msgpack::object& o){if(o.type==msgpack::type::ARRAY){Require(o.via.array.size==7);return Number(o.via.array.ptr[0]);}return Number(o,"type");}
UInkCanvas CanvasMetadata(const msgpack::object& o){
 UInkCanvas c;auto page=Field(o,"pageGuid");Require(page!=nullptr);c.pageGuid=Guid(*page);
 c.pageIndex=Number(o,"pageIndex");c.pageNumber=Number(o,"pageNumber");c.layerIndex=Number(o,"layerIndex");c.layerNumber=Number(o,"layerNumber");
 Require(c.layerIndex==0);if(auto v=Field(o,"workspaceGuid"))c.workspaceGuid=Guid(*v);if(auto v=Field(o,"deviceGuid"))c.deviceGuid=Guid(*v);
 return c;
}
msgpack::unpack_limit Limits(){UInkReadLimits l;return msgpack::unpack_limit(l.maxContainerEntries,l.maxContainerEntries,l.maxStringOrBinaryBytes,l.maxStringOrBinaryBytes,l.maxStringOrBinaryBytes,l.maxDepth);}
struct ObjectReader {
 HANDLE handle;std::vector<std::byte> pending;uint64_t position=0;uint64_t peak=0;bool eof=false;
 bool Next(msgpack::object_handle& object,std::vector<std::byte>& raw){
  const auto limits=Limits();
  for(;;){
   if(!pending.empty())try{size_t end=0;object=msgpack::unpack(reinterpret_cast<const char*>(pending.data()),pending.size(),end,nullptr,nullptr,limits);
    Require(end<=UInkReadLimits{}.maxTopLevelObjectBytes);raw.assign(pending.begin(),pending.begin()+end);pending.erase(pending.begin(),pending.begin()+end);position+=end;return true;
   }catch(const msgpack::insufficient_bytes&){}
   if(eof){Require(pending.empty());return false;}
   Require(pending.size()<=UInkReadLimits{}.maxTopLevelObjectBytes);
   std::array<std::byte,ChunkSize> chunk{};DWORD count=0;Require(ReadFile(handle,chunk.data(),ChunkSize,&count,nullptr)!=FALSE);
   if(!count)eof=true;else pending.insert(pending.end(),chunk.begin(),chunk.begin()+count);
   peak=(std::max)(peak,uint64_t(pending.size()));
  }
 }
};
struct EncodedLayout {std::vector<std::byte> bytes;std::vector<size_t> pages;size_t prefix=0;};
EncodedLayout EncodeLayout(const UInkDocument& metadata){
 auto encoded=EncodeUInkDocument(metadata);Require(encoded.status==UInkEncodeStatus::Success);
 EncodedLayout l;l.bytes=std::move(encoded.bytes);size_t offset=0;
 while(offset<l.bytes.size()){size_t start=offset;auto object=msgpack::unpack(reinterpret_cast<const char*>(l.bytes.data()),l.bytes.size(),offset,nullptr,nullptr,Limits());
 if(Type(object.get())==kCanvasType)l.pages.push_back(start);}
 Require(l.pages.size()==metadata.canvases.size()&&!l.pages.empty());l.prefix=l.pages.front();return l;
}
bool Strict(const UInkReadResult& result){const auto& p=result.provenance;return result.status==UInkReadStatus::Complete&&result.document.has_value()&&!p.requiresSaveAs&&!p.usedFieldFallback&&!p.usedTemporaryIdentity&&!p.containsInvalidCompleteBlocks&&!p.containsUnknownTopLevel&&!p.contentSequenceRecovered;}
UInkReadResult ReadPage(HANDLE handle,const UInkPageFileDirectory& directory,size_t index,uint64_t* peak=nullptr){
 Require(index<directory.pages.size()&&directory.metadata.canvases.size()==directory.pages.size());const auto& range=directory.pages[index];
 Require(directory.metadata.canvases[index].pageGuid==range.pageGuid);
 Require(range.offset<=directory.revision.length&&range.length<=directory.revision.length-range.offset&&range.length<=UInkReadLimits{}.maxFileBytes);
 auto layout=EncodeLayout(directory.metadata);Require(index<layout.pages.size());size_t start=layout.pages[index];size_t end=index+1<layout.pages.size()?layout.pages[index+1]:layout.bytes.size();
 std::vector<std::byte> bytes;bytes.reserve(layout.bytes.size()-(end-start)+static_cast<size_t>(range.length));
 bytes.insert(bytes.end(),layout.bytes.begin(),layout.bytes.begin()+start);size_t body=bytes.size();bytes.resize(body+static_cast<size_t>(range.length));
 Require(Seek(handle,range.offset)&&Read(handle,std::span(bytes).subspan(body)));
 bytes.insert(bytes.end(),layout.bytes.begin()+end,layout.bytes.end());
 if(peak)*peak=(std::max)(*peak,uint64_t(bytes.size()+layout.bytes.size()));
 auto result=DecodeUInk(bytes);Require(Strict(result));Require(result.document->canvases.size()==directory.pages.size());
 auto canvas=std::move(result.document->canvases[index]);Require(canvas.pageGuid==range.pageGuid);canvas.pageIndex=0;
 result.document->canvases.clear();result.document->canvases.push_back(std::move(canvas));result.document->header.pageNum=1;
 if(result.document->headerExtension)for(auto& w:result.document->headerExtension->workspaces)w.currentPageIndex=0;
 result.sourcePath=directory.path;result.sourceRevision=directory.revision;return result;
}
std::optional<UInkPageFileDirectory> Scan(const std::wstring& path){
 Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(file.value==INVALID_HANDLE_VALUE)return {};
 auto revision=Revision(file.value);if(!revision)return {};Require(Seek(file.value,0));
 UInkPageFileDirectory d;d.path=path;d.revision=*revision;
 {
 ObjectReader reader{file.value};msgpack::object_handle object;std::vector<std::byte> raw,sparse;bool header=false,extension=false;uint64_t count=0;
 while(reader.Next(object,raw)){
  Require(++count<=UInkReadLimits{}.maxTopLevelObjects);const auto type=Type(object.get());const uint64_t start=reader.position-raw.size();
  if(!header){Require(type==kHeaderType);header=true;sparse.insert(sparse.end(),raw.begin(),raw.end());continue;}
  if(type==kHeaderExtensionType){Require(!extension&&d.pages.empty());extension=true;sparse.insert(sparse.end(),raw.begin(),raw.end());continue;}
  if(type==kCanvasType){
   if(!d.pages.empty())d.pages.back().length=start-d.pages.back().offset;
   auto canvas=CanvasMetadata(object.get());Require(canvas.pageIndex==d.pages.size());
   d.pages.push_back({canvas.pageGuid,start,0});
   const UInkAppendObject value=std::move(canvas);auto encoded=EncodeUInkAppendObjects(std::span(&value,1));Require(encoded.status==UInkEncodeStatus::Success);
   sparse.insert(sparse.end(),encoded.bytes.begin(),encoded.bytes.end());
  }else Require(!d.pages.empty()&&(type==kInkType||type==kShapeType||type==kClearType));
 }
 Require(!d.pages.empty()&&reader.position==revision->length);d.pages.back().length=revision->length-d.pages.back().offset;
 auto metadata=DecodeUInk(sparse);Require(Strict(metadata));d.metadata=std::move(*metadata.document);Require(d.metadata.header.pageNum==d.pages.size());d.peakBufferBytes=reader.peak+sparse.size();
 } // 逐页校验前释放扫描器、最后一个对象及其历史 extra 的临时副本。
 // 自校验逐页物化，任何一页失败都拒绝整个目录；不能全量读取来绕过页级内存约束。
 for(size_t i=0;i<d.pages.size();++i)(void)ReadPage(file.value,d,i,&d.peakBufferBytes);
 return d;
}
}
bool TryRemoveOwnedUInkPageFile(const UInkPageFileDirectory& directory) noexcept
{
 try
 {
  // 持有句柄期间禁止外部写入和替换，避免身份核验与删除之间的路径竞态。
  Handle file(CreateFileW(directory.path.c_str(), GENERIC_READ | DELETE,
   FILE_SHARE_READ, nullptr, OPEN_EXISTING,
   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (file.value == INVALID_HANDLE_VALUE) return false;
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(file.value, &info) ||
   (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
   return false;
  const auto revision = Revision(file.value);
  if (!revision || *revision != directory.revision) return false;
  FILE_DISPOSITION_INFO disposition{};
  disposition.DeleteFile = TRUE;
  // 只标记当前已核验的文件对象；不再次解析路径，也不删除替代文件。
  return SetFileInformationByHandle(file.value, FileDispositionInfo,
   &disposition, sizeof(disposition)) != FALSE;
 }
 catch (...)
 {
  return false;
 }
}
std::optional<UInkPageFileDirectory> ScanUInkPageFile(const std::wstring& path){try{return Scan(path);}catch(...){return {};}}
UInkReadResult ReadUInkPageFile(const UInkPageFileDirectory& directory,const UInkGuid& pageGuid){
 try{
  Handle file(CreateFileW(directory.path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));Require(file.value!=INVALID_HANDLE_VALUE);
  auto revision=Revision(file.value);Require(revision&&*revision==directory.revision);
  auto found=std::find_if(directory.pages.begin(),directory.pages.end(),[&](const auto& page){return page.pageGuid==pageGuid;});Require(found!=directory.pages.end());
  return ReadPage(file.value,directory,static_cast<size_t>(found-directory.pages.begin()));
 }catch(...){UInkReadResult r;r.status=UInkReadStatus::IoError;return r;}
}
UInkPageFileResult RewriteUInkPageFile(const std::wstring& newPath,const UInkPageFileDirectory* previous,const UInkDocument& onePage,const std::vector<UInkGuid>& orderedPages,uint32_t currentPageIndex){
 UInkPageFileResult result;
 try{
  Require(onePage.canvases.size()==1&&!orderedPages.empty()&&orderedPages.size()<=UINT32_MAX&&currentPageIndex<orderedPages.size());
  std::set<std::array<uint8_t,16>> uniquePages;
  for(const auto& page:orderedPages)Require(!page.IsZero()&&uniquePages.insert(page.Bytes()).second);
  const auto& sourceCanvas=onePage.canvases.front();Require(std::count(orderedPages.begin(),orderedPages.end(),sourceCanvas.pageGuid)==1);
  // 文件注册表与逻辑身份固定；旧页只能来自同一逻辑文件，索引不允许拼接外部文档。
  if(previous){
   Require(previous->metadata.header.guid==onePage.header.guid);
   Require(!previous->pages.empty()&&previous->pages.size()<=orderedPages.size());
   Require(previous->metadata.canvases.size()==previous->pages.size());
   // 初版只追加页面；不允许复制旧字节时悄悄改变其页序或所属工作区。
   for(size_t i=0;i<previous->pages.size();++i){
    Require(previous->pages[i].pageGuid==orderedPages[i]);
    const auto& canvas=previous->metadata.canvases[i];
    Require(canvas.pageGuid==orderedPages[i]&&canvas.pageIndex==i);
    Require(canvas.workspaceGuid==sourceCanvas.workspaceGuid&&canvas.deviceGuid==sourceCanvas.deviceGuid);
   }
  }
  UInkDocument metadata;metadata.header=onePage.header;metadata.header.pageNum=static_cast<uint32_t>(orderedPages.size());metadata.headerExtension=onePage.headerExtension;
  metadata.usesImplicitDevice=onePage.usesImplicitDevice;metadata.usesImplicitWorkspace=onePage.usesImplicitWorkspace;
  if(metadata.headerExtension)for(auto& w:metadata.headerExtension->workspaces)w.currentPageIndex=currentPageIndex;
  for(size_t i=0;i<orderedPages.size();++i){UInkCanvas c;c.workspaceGuid=sourceCanvas.workspaceGuid;c.deviceGuid=sourceCanvas.deviceGuid;c.pageGuid=orderedPages[i];c.pageIndex=static_cast<uint32_t>(i);c.pageNumber=static_cast<uint32_t>(i+1);c.layerNumber=1;metadata.canvases.push_back(std::move(c));}
  auto layout=EncodeLayout(metadata);
  Handle old(previous?CreateFileW(previous->path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr):INVALID_HANDLE_VALUE);
  if(previous){Require(old.value!=INVALID_HANDLE_VALUE);auto revision=Revision(old.value);Require(revision&&*revision==previous->revision);}
  {
   Handle target(CreateFileW(newPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
   if(target.value==INVALID_HANDLE_VALUE){result.systemError=GetLastError();return result;}
   Require(Write(target.value,std::span(layout.bytes).first(layout.prefix)));
   std::array<std::byte,ChunkSize> chunk{};
   for(size_t i=0;i<orderedPages.size();++i){
    if(orderedPages[i]==sourceCanvas.pageGuid){auto replacement=sourceCanvas;replacement.pageIndex=static_cast<uint32_t>(i);replacement.pageNumber=static_cast<uint32_t>(i+1);
     const UInkAppendObject object=std::move(replacement);auto encoded=EncodeUInkAppendObjects(std::span(&object,1));Require(encoded.status==UInkEncodeStatus::Success&&Write(target.value,encoded.bytes));
    }else{
     const UInkPageByteRange* range=nullptr;
     if(previous)for(const auto& p:previous->pages)if(p.pageGuid==orderedPages[i]){range=&p;break;}
     if(range){Require(range->offset<=previous->revision.length&&range->length<=previous->revision.length-range->offset);Require(Seek(old.value,range->offset));uint64_t left=range->length;
      while(left){auto n=static_cast<size_t>((std::min)(left,uint64_t(chunk.size())));Require(Read(old.value,std::span(chunk).first(n))&&Write(target.value,std::span(chunk).first(n)));left-=n;}
     }else{
      // 新页可先保存身份，随后该页自己的请求再提交主题和历史。
      size_t start=layout.pages[i],end=i+1<layout.pages.size()?layout.pages[i+1]:layout.bytes.size();Require(Write(target.value,std::span(layout.bytes).subspan(start,end-start)));
     }
    }
   }
   Require(FlushFileBuffers(target.value)!=FALSE);
  }
  result.directory=ScanUInkPageFile(newPath);if(!result.directory){result.status=UInkSaveStatus::SelfValidationFailed;return result;}
  Require(result.directory->metadata.header.guid==onePage.header.guid&&result.directory->pages.size()==orderedPages.size());
  result.status=UInkSaveStatus::Committed;return result;
 }catch(...){result.systemError=GetLastError();return result;}
}
}
