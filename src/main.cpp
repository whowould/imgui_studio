#include "editor.h"
#include "platform.h"

#include <Windows.h>

namespace
{
    void style( )
    {
        ImGuiStyle& s = ImGui::GetStyle( );
        s.WindowRounding = 0;
        s.ChildRounding = 0;
        s.PopupRounding = 10;
        s.FrameRounding = 7;
        s.GrabRounding = 4;
        s.ScrollbarRounding = 8;
        s.ScrollbarSize = 10;
        s.WindowBorderSize = 0;
        s.PopupBorderSize = 1;
        s.ItemSpacing = { 8, 6 };
        s.WindowPadding = { 12, 12 };

        ImVec4* c = s.Colors;
        c[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4( kit::tone::primary );
        c[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4( kit::tone::muted );
        c[ImGuiCol_WindowBg] = { 14 / 255.f, 14 / 255.f, 16 / 255.f, 1 };
        c[ImGuiCol_ChildBg] = { 0, 0, 0, 0 };
        c[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4( kit::tone::popup );
        c[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4( kit::tone::window_border );
        c[ImGuiCol_ScrollbarBg] = { 0, 0, 0, 0 };
        c[ImGuiCol_ScrollbarGrab] = { 1, 1, 1, .10f };
        c[ImGuiCol_ScrollbarGrabHovered] = { 1, 1, 1, .18f };
        c[ImGuiCol_ScrollbarGrabActive] = { 1, 1, 1, .24f };
        c[ImGuiCol_DragDropTarget] = { 0, 0, 0, 0 };
        c[ImGuiCol_NavCursor] = { 0, 0, 0, 0 };
    }
}

int WINAPI wWinMain( HINSTANCE, HINSTANCE, PWSTR, int )
{
    // imgui_studio.exe [project.imstudio | --template <name>] [--export <folder>] [--save <project.imstudio>]
    std::wstring project, template_name, export_folder, save_path;
    for ( int i = 1; i < __argc; ++i )
    {
        const std::wstring arg = __wargv[i];
        if ( arg == L"--export" && i + 1 < __argc )
            export_folder = __wargv[++i];
        else if ( arg == L"--template" && i + 1 < __argc )
            template_name = __wargv[++i];
        else if ( arg == L"--save" && i + 1 < __argc )
            save_path = __wargv[++i];
        else
            project = arg;
    }

    // Headless export or save: no window, the result on the parent console and in the exit code.
    if ( !export_folder.empty( ) || !save_path.empty( ) )
    {
        ImGui::CreateContext( );
        editor_c editor;
        if ( !template_name.empty( ) )
            editor.use_template( template_name );
        if ( !project.empty( ) )
            editor.open_path( project );
        std::string message;
        bool ok = true;
        if ( !export_folder.empty( ) )
            ok = editor.export_to( export_folder, message );
        if ( !save_path.empty( ) )
        {
            const bool saved = editor.save_to( save_path );
            ok = ok && saved;
            message += ( message.empty( ) ? "" : " " ) + std::string( saved ? "Saved the project." : "Couldn't save the project." );
        }
        ImGui::DestroyContext( );
        if ( AttachConsole( ATTACH_PARENT_PROCESS ) )
        {
            message += "\n";
            DWORD written = 0;
            WriteFile( GetStdHandle( STD_OUTPUT_HANDLE ), message.data( ), static_cast<DWORD>( message.size( ) ), &written, nullptr );
        }
        return ok ? 0 : 1;
    }

    if ( !platform::create( L"ImGui Studio", 1760, 1040 ) )
        return 1;
    ImGuiIO& io = ImGui::GetIO( );
    kit::load_font( io, 13.f );
    // Cascadia Mono, or Consolas, for the code drawer when Windows has it.
    for ( const char* path : { "C:\\Windows\\Fonts\\CascadiaMono.ttf", "C:\\Windows\\Fonts\\consola.ttf" } )
        if ( GetFileAttributesA( path ) != INVALID_FILE_ATTRIBUTES )
        {
            ImFontConfig config;
            config.OversampleH = 2;
            code_font = io.Fonts->AddFontFromFileTTF( path, 13.f, &config );
            break;
        }
    style( );

    editor_c editor;
    if ( !template_name.empty( ) )
        editor.use_template( template_name );
    // A project passed on the command line (double-click or drop onto the exe).
    if ( !project.empty( ) )
        editor.open_path( project );
    while ( platform::pump( ) )
    {
        platform::begin_frame( );
        editor.frame( );
        platform::end_frame( { 13 / 255.f, 13 / 255.f, 15 / 255.f, 1 } );
    }
    editor.shutdown( );
    platform::destroy( );
    return 0;
}
