#pragma once

#include <string>

std::string GetEditionInformation();
bool DownloadEdition(std::string domain, std::string path, std::wstring directory, std::wstring fileName, long long& fileSize, long long& downloadedSize);