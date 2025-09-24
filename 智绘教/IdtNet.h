#pragma once

#include <string>
#include <atomic>

std::string GetEditionInformation();
bool DownloadEdition(std::string domain, std::string path, std::wstring directory, std::wstring fileName, std::atomic_ullong& downloadedSize);