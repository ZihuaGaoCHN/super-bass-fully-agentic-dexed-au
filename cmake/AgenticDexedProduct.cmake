include_guard(GLOBAL)

set(AGENTIC_DEXED_PRODUCT_NAME "Agentic Dexed")
set(AGENTIC_DEXED_TARGET_NAME "AgenticDexed")
set(AGENTIC_DEXED_BUNDLE_ID "com.agenticdexed.AgenticDexed")
set(AGENTIC_DEXED_PLUGIN_CODE "AgDx")
set(AGENTIC_DEXED_MANUFACTURER_CODE "Agnt")
set(AGENTIC_DEXED_FORMATS Standalone VST3)

option(
    AGENTIC_DEXED_COPY_PLUGIN_AFTER_BUILD
    "Copy Agentic Dexed into the system plug-in directory after building"
    OFF
)

set(
    CMAKE_OSX_DEPLOYMENT_TARGET
    "11.0"
    CACHE STRING
    "Minimum supported macOS version"
    FORCE
)
