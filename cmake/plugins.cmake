# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
option(ENABLE_PLUGIN_WESTON_SCREENSHOOTER "Enable Weston Screenshooter Plugin" ON)
if (ENABLE_PLUGIN_WESTON_SCREENSHOOTER)
    add_compile_definitions(ENABLE_PLUGIN_WESTON_SCREENSHOOTER)
endif ()

option(ENABLE_PLUGIN_AGL_HEALTH "Enable AGL Health Plugin" ON)
if (ENABLE_PLUGIN_AGL_HEALTH)
    add_compile_definitions(ENABLE_PLUGIN_AGL_HEALTH)
endif ()

