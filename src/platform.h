#pragma once

#include <imgui.h>

#include <string>

// The host window: borderless (the editor draws its own title bar) but with the system's resize edges, snapping,
// shadow and Windows 11 rounded corners; DX11 underneath ImGui.
namespace platform
{
    auto create( const wchar_t* title, int width, int height ) -> bool;
    void destroy( );
    // Handles messages; false once the window is closed.
    auto pump( ) -> bool;
    void begin_frame( );
    void end_frame( ImVec4 clear );

    // Title bar behaviour for an empty spot under the cursor.
    void drag_window( );
    void minimize( );
    void toggle_maximize( );
    auto maximized( ) -> bool;
    void close( );
    void set_title( const std::wstring& title );

    auto open_file( ) -> std::wstring;
    auto open_image( ) -> std::wstring;
    auto open_library( ) -> std::wstring;
    // %APPDATA%\ImGui Studio, created if needed.
    auto app_data( ) -> std::wstring;
    auto save_file( const std::wstring& suggested ) -> std::wstring;
    auto pick_folder( ) -> std::wstring;
    void copy_text( const std::string& text );

    auto widen( const std::string& text ) -> std::wstring;
    auto narrow( const std::wstring& text ) -> std::string;
}
