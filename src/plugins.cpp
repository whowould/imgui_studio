#include "plugins.h"
#include "platform.h"

#include <kit.h>

#include <windows.h>

#include <filesystem>

namespace
{
    using attach_fn = void ( * )( ImGuiContext*, ImGuiMemAllocFunc, ImGuiMemFreeFunc, void* );
    using widgets_fn = int ( * )( const char**, int );
    using draw_fn = bool ( * )( const char*, const ImRect*, const kit::theme_t*, const kit::palette_t*, ImFont* );

    struct plugin_t
    {
        std::string path;
        std::filesystem::file_time_type stamp{ };
        HMODULE module = nullptr;
        std::wstring copy;
        draw_fn draw = nullptr;
        std::vector<std::string> widgets;
        std::string status;
        double checked = -10;
        int generation = 0;
    } plugin;

    void unload( )
    {
        if ( plugin.module )
            FreeLibrary( plugin.module );
        if ( !plugin.copy.empty( ) )
            DeleteFileW( plugin.copy.c_str( ) );
        plugin.module = nullptr;
        plugin.copy.clear( );
        plugin.draw = nullptr;
        plugin.widgets.clear( );
    }

    void load( )
    {
        unload( );
        namespace fs = std::filesystem;
        const fs::path source = platform::widen( plugin.path );
        std::error_code error;
        plugin.stamp = fs::last_write_time( source, error );
        if ( error )
        {
            plugin.status = "Can't find " + plugin.path;
            return;
        }
        // Load a copy, so rebuilding the plugin doesn't fail on a locked file.
        wchar_t temp[MAX_PATH]{ };
        GetTempPathW( MAX_PATH, temp );
        plugin.copy = std::wstring( temp ) + L"imgui_studio_plugin_" + std::to_wstring( GetCurrentProcessId( ) ) + L"_" +
                      std::to_wstring( ++plugin.generation ) + L".dll";
        fs::copy_file( source, plugin.copy, fs::copy_options::overwrite_existing, error );
        plugin.module = error ? nullptr : LoadLibraryW( plugin.copy.c_str( ) );
        const auto attach = plugin.module ? reinterpret_cast<attach_fn>( GetProcAddress( plugin.module, "studio_attach" ) ) : nullptr;
        const auto list = plugin.module ? reinterpret_cast<widgets_fn>( GetProcAddress( plugin.module, "studio_widgets" ) ) : nullptr;
        plugin.draw = plugin.module ? reinterpret_cast<draw_fn>( GetProcAddress( plugin.module, "studio_draw" ) ) : nullptr;
        if ( !attach || !list || !plugin.draw )
        {
            plugin.status = plugin.module ? "Not a widget plugin (missing STUDIO_WIDGETS)" : "Couldn't load " + plugin.path;
            unload( );
            return;
        }
        ImGuiMemAllocFunc alloc = nullptr;
        ImGuiMemFreeFunc free = nullptr;
        void* user = nullptr;
        ImGui::GetAllocatorFunctions( &alloc, &free, &user );
        attach( ImGui::GetCurrentContext( ), alloc, free, user );
        const char* names[64]{ };
        const int count = std::min( list( names, 64 ), 64 );
        for ( int i = 0; i < count; ++i )
            plugin.widgets.emplace_back( names[i] );
        plugin.status = "Loaded " + std::to_string( count ) + ( count == 1 ? " widget" : " widgets" );
    }
}

void plugins::use( const std::string& path )
{
    if ( path != plugin.path )
    {
        plugin.path = path;
        plugin.status.clear( );
        if ( path.empty( ) )
            unload( );
        else
            load( );
        return;
    }
    if ( path.empty( ) )
        return;
    // Reload after a rebuild.
    const double now = ImGui::GetTime( );
    if ( now - plugin.checked < .5 )
        return;
    plugin.checked = now;
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time( platform::widen( path ), error );
    if ( !error && stamp != plugin.stamp )
        load( );
}

auto plugins::widgets( ) -> const std::vector<std::string>&
{
    return plugin.widgets;
}

auto plugins::draw( const std::string& name, const ImRect& rect ) -> bool
{
    if ( !plugin.draw )
        return false;
    return plugin.draw( name.c_str( ), &rect, &kit::theme, &kit::palette, kit::font( ) );
}

auto plugins::status( ) -> const std::string&
{
    return plugin.status;
}
