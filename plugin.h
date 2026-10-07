#pragma once
#include "clap/clap.h"

const clap_plugin_descriptor_t *getPluginDescriptor();
const clap_plugin_t *createPlugin(const clap_host_t *host);
const clap_preset_discovery_factory_t *getPresetDiscoveryFactory();

namespace slide
{
inline constexpr char pluginId[] = "com.charlieculbert.slide-lab";
inline constexpr uint32_t stateMagic = 0x534c4944;
inline constexpr uint32_t stateVersion = 2;
}
