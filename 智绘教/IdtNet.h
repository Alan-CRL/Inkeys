#pragma once

#include <string>
#include <atomic>

std::string GetEditionInformation(std::string referer = "");
bool DownloadEdition(std::string domain, std::string path, std::wstring directory, std::wstring fileName, std::atomic_ullong& downloadedSize, std::string referer = "");