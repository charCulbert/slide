#pragma once
// What plugin.cpp gives entry.cpp (and tests/SlideTests.cpp).
#include "clap/clap.h"
#include "clap/factory/preset-discovery.h"
#include <cstdint>

const clap_plugin_descriptor_t *getPluginDescriptor();
const clap_plugin_t *createPlugin(const clap_host_t *host);
const clap_preset_discovery_factory_t *getPresetDiscoveryFactory();

namespace slide
{
inline constexpr char pluginId[] = "com.charlieculbert.slide-lab";
// The state starts "SLID", then the version, so a later version can grow it
// without breaking readers that only know this much.
inline constexpr uint32_t stateMagic = 0x534c4944;
inline constexpr uint32_t stateVersion = 2;
}
