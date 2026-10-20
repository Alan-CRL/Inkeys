#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <variant>
#include <optional>
#include <utility>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
import draw3.uink_page_file;
namespace {
using namespace draw3::uink;
void Check(bool good,const char* label,int& failures){if(!good){++failures;std::cerr<<"UInk page file FAILED: "<<label<<'\n';}}
UInkDocument Page(const UInkGuid& file,const UInkGuid& page,int value){
 UInkDocument d;d.header.guid=file;d.header.pageNum=1;
 // 使用生产白板的显式 workspace 1，并验证私有扩展原样往返。
 d.usesImplicitWorkspace=false;d.headerExtension.emplace();
 UInkWorkspace workspace;workspace.guid=file;workspace.workspaceType=1;
 workspace.extra=UInkExtra{{UInkMessagePackValue{std::string("inkeysWorkspace")},UInkMessagePackValue{std::string("whiteboard")}}};
 d.headerExtension->workspaces.push_back(std::move(workspace));
 UInkCanvas c;c.workspaceGuid=file;c.pageGuid=page;c.pageNumber=1;c.layerNumber=1;
 c.extra=UInkExtra{{UInkMessagePackValue{std::string("test-history")},UInkMessagePackValue{std::to_string(value)}}};
 UInkInk ink;ink.color.fallbackRgb=static_cast<uint32_t>(value);ink.undoId=0;ink.contentId=0;
 for(int i=0;i<400;++i)ink.points.push_back({float(i),float(value),2.0f,{}});
 c.content.emplace_back(std::move(ink));d.canvases.push_back(std::move(c));return d;
}
}
int RunUInkPageFileTests(){
 int failures=0;auto identity=CreateUInkGuid();Check(identity.has_value(),"GUID",failures);if(!identity)return failures;
 auto narrow=FormatUInkGuid(*identity);std::wstring suffix(narrow.begin(),narrow.end());
 auto root=std::filesystem::temp_directory_path()/(L"Inkeys-PageFile-"+suffix);std::error_code error;
 if(!std::filesystem::create_directory(root,error)){Check(false,"create temp",failures);return failures;}
 std::vector<UInkGuid> ids;std::optional<UInkPageFileDirectory> directory;
 for(int i=0;i<100;++i){auto guid=CreateUInkGuid();if(!guid){Check(false,"page GUID",failures);break;}ids.push_back(*guid);
  auto path=root/(std::to_wstring(i)+L".uink");auto d=Page(*identity,*guid,i+1);
  auto saved=RewriteUInkPageFile(path.wstring(),directory?&*directory:nullptr,d,ids,static_cast<uint32_t>(i));
  Check(saved.status==UInkSaveStatus::Committed&&saved.directory.has_value(),"stream commit",failures);if(!saved.directory)break;directory=std::move(saved.directory);
 }
 if(directory&&directory->pages.size()==100){
  Check(directory->metadata.canvases.size()==100,"directory count",failures);
  for(const auto& c:directory->metadata.canvases)Check(c.content.empty()&&!c.extra,"directory never owns ink/history",failures);
  Check(directory->peakBufferBytes<directory->revision.length,"buffer smaller than document",failures);
  for(size_t i=0;i<ids.size();++i){auto read=ReadUInkPageFile(*directory,ids[i]);Check(read.status==UInkReadStatus::Complete&&read.document&&read.document->canvases.size()==1,"page read",failures);
   if(read.document){const auto& c=read.document->canvases.front();Check(c.pageGuid==ids[i]&&c.pageIndex==0&&c.content.size()==1,"identity normalization",failures);
    Check(c.extra&&c.extra->size()==1&&std::get<std::string>((*c.extra)[0].second.value)==std::to_string(i+1),"private page extra preserved",failures);
    if(!c.content.empty())Check(std::get<UInkInk>(c.content[0]).color.fallbackRgb==i+1,"unchanged page preserved",failures);}}
  auto malformed=*directory;malformed.metadata.canvases.clear();
  Check(!ReadUInkPageFile(malformed,ids.back()).document,"malformed directory rejected without indexing",failures);
  auto duplicateIds=ids;duplicateIds.back()=ids.front();
  auto duplicatePath=root/L"duplicate.uink";
  Check(RewriteUInkPageFile(duplicatePath.wstring(),&*directory,Page(*identity,ids[3],123),duplicateIds,0).status!=UInkSaveStatus::Committed&&!std::filesystem::exists(duplicatePath),"duplicate GUID rejected before creation",failures);
  auto reordered=ids;std::swap(reordered[0],reordered[1]);
  Check(RewriteUInkPageFile((root/L"reordered.uink").wstring(),&*directory,Page(*identity,ids[3],123),reordered,0).status!=UInkSaveStatus::Committed,"page reordering rejected",failures);
  auto foreign=Page(*identity,ids[0],123);foreign.canvases[0].workspaceGuid=ids[1];
  Check(RewriteUInkPageFile((root/L"foreign.uink").wstring(),&*directory,foreign,ids,0).status!=UInkSaveStatus::Committed,"foreign workspace rejected",failures);
  auto duplicate=RewriteUInkPageFile(directory->path,&*directory,Page(*identity,ids[0],123),ids,0);Check(duplicate.status!=UInkSaveStatus::Committed,"never overwrite existing version",failures);
  auto invalid=RewriteUInkPageFile((root/L"missing"/L"version.uink").wstring(),&*directory,Page(*identity,ids[0],123),ids,0);Check(invalid.status!=UInkSaveStatus::Committed,"write failure",failures);
  auto copied=*directory;copied.revision.sha256[0]^=1;Check(!ReadUInkPageFile(copied,ids[0]).document,"revision gate",failures);
  auto updated=RewriteUInkPageFile((root/L"updated.uink").wstring(),&*directory,Page(*identity,ids[3],123),ids,3);Check(updated.status==UInkSaveStatus::Committed,"replace middle page",failures);
  if(updated.directory){auto read=ReadUInkPageFile(*updated.directory,ids[3]);Check(read.document&&std::get<UInkInk>(read.document->canvases[0].content[0]).color.fallbackRgb==123,"updated page content",failures);}
  auto original=ReadUInkPageFile(*directory,ids[3]);
  Check(original.document&&std::get<UInkInk>(original.document->canvases[0].content[0]).color.fallbackRgb==4,"prior version immutable",failures);
  // 同路径内容被外部改写后，旧目录不能再读取任何页。
  auto changed=root/L"changed.uink";std::filesystem::copy_file(directory->path,changed,error);
  if(!error){auto index=ScanUInkPageFile(changed.wstring());if(index){
   auto writeTime=std::filesystem::last_write_time(changed);
   std::fstream stream(changed,std::ios::in|std::ios::out|std::ios::binary);char last=0;
   stream.seekg(-1,std::ios::end);stream.get(last);stream.seekp(-1,std::ios::end);stream.put(char(last^1));stream.close();
   std::filesystem::last_write_time(changed,writeTime);
   Check(!ReadUInkPageFile(*index,ids[0]).document,"modified source rejected",failures);
   Check(!TryRemoveOwnedUInkPageFile(*index)&&std::filesystem::exists(changed),"tampered file never removed",failures);
  }else Check(false,"scan source before mutation",failures);}
  if(updated.directory){
   Check(TryRemoveOwnedUInkPageFile(*updated.directory)&&!std::filesystem::exists(updated.directory->path),"owned version removed by verified handle",failures);
   Check(!TryRemoveOwnedUInkPageFile(*updated.directory),"missing version safely refused",failures);
  }
  auto replacedPath=root/L"replaced.uink";
  auto owned=RewriteUInkPageFile(replacedPath.wstring(),nullptr,Page(*identity,ids[0],1),{ids[0]},0);
  Check(owned.directory.has_value(),"create replacement test version",failures);
  if(owned.directory){
   // 文件内容与时间相同，但替代对象的 fileIndex 不同，必须拒绝删除。
   const auto writeTime=std::filesystem::last_write_time(replacedPath);
   auto originalPath=root/L"replaced-original.uink";
   std::filesystem::rename(replacedPath,originalPath,error);
   Check(!error,"move owned file before replacement",failures);
   if(!error){
    std::filesystem::copy_file(originalPath,replacedPath,error);
    Check(!error,"replace path with another file object",failures);
    if(!error){
     std::filesystem::last_write_time(replacedPath,writeTime);
     Check(!TryRemoveOwnedUInkPageFile(*owned.directory)&&std::filesystem::exists(replacedPath)&&std::filesystem::exists(originalPath),"replaced file never removed",failures);
    }
   }
  }
  auto truncated=root/L"truncated.uink";std::filesystem::copy_file(directory->path,truncated,error);if(!error){std::filesystem::resize_file(truncated,directory->revision.length-3,error);Check(!ScanUInkPageFile(truncated.wstring()),"reject truncated content",failures);}
 }
 else Check(false,"100 pages",failures);
 // 只清理本测试刚创建的唯一目录，绝不删除调用方文件。
 std::filesystem::remove_all(root,error);
 std::cout<<"UInk page file tests: "<<failures<<" failures\n";return failures;
}
