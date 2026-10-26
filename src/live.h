#pragma once
// Live link: an exported menu or app draws from its .imstudio project file instead of its compiled layout, and reloads
// the file whenever it changes, so moving something in ImGui Studio moves it in your running program. Your values,
// events and custom widgets stay connected by name.
//
// Build with KIT_LIVE defined and these files (kit_live/*.cpp) added, then call <namespace>::live( "path.imstudio" )
// once after setup( ). Leave KIT_LIVE out of release builds.

#include <cstddef>
#include <vector>

namespace kit_live
{
    enum class kind_t
    {
        flag,     // bool
        number,   // float
        whole,    // int (and enums)
        buffer,   // char[] (size given)
        string,   // std::string
        color,    // float[4]
        event,    // std::function<void( )>
        callback, // void ( * )( ) stored in a variable
        widget    // void ( * )( const ImRect& )
    };

    struct binding_t
    {
        const char* name;
        kind_t kind;
        void* value;
        size_t size = 0;
    };

    // Starts watching the project file.
    void watch( const char* path, std::vector<binding_t> bindings );
    // Draws from the project; false until it has loaded (draw the compiled layout then).
    bool draw( );
}
