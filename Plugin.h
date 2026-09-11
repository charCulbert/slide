#pragma once

#include <clap/clap.h>

#include <cstdint>

namespace slide
{

inline constexpr char pluginId[] = "com.charlieculbert.slide";
// "SLID", version 1. The header is written first so a later version can grow the
// blob without breaking readers that only know this much (D1).
inline constexpr uint32_t stateMagic = 0x534c4944;
inline constexpr uint32_t stateVersion = 1;

const clap_plugin_descriptor_t& descriptor() noexcept;
bool entryInit(const char* path);
void entryDeinit();
const void* entryGetFactory(const char* factoryId);

} // namespace slide
