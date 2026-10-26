#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <string>
#include <vector>

// Widget plugins (see kit/studio_plugin.h): loaded from a copy so the original can be rebuilt while the editor runs,
// and reloaded when it changes.
namespace plugins
{
    // Loads the plugin at `path` (UTF-8), or unloads with an empty path. Cheap to call every frame.
    void use( const std::string& path );
    auto widgets( ) -> const std::vector<std::string>&;
    auto draw( const std::string& name, const ImRect& rect ) -> bool;
    auto status( ) -> const std::string&;
}
