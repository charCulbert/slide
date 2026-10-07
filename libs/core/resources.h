#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace core {

// Call from clap_entry.init. Native builds find the binary that contains this
// code (wrappers pass different paths to init); WCLAP uses the bundle path.
void initResources(const char *entryPath);
void setResourceRoot(std::filesystem::path folder);

struct Resource
{
    std::string bytes;
    std::string mime;
};

// urlPath is an absolute URL path such as "/page/index.html" or
// "/fonts/font%20a.woff2?v=2". Returns nothing for anything outside the
// folder. File names must not contain '%', '?' or '#'.
std::optional<Resource> readResource(std::string_view urlPath);

}
