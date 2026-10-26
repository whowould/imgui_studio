#include "editor.h"
#include "platform.h"
#include "plugins.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace
{
    // The editor's own surfaces, around the kit's neutrals and accent.
    namespace ed
    {
        constexpr ImU32 base = IM_COL32( 13, 13, 15, 255 );
        constexpr ImU32 bar = IM_COL32( 17, 17, 20, 255 );
        constexpr ImU32 panel = IM_COL32( 18, 18, 21, 255 );
        constexpr ImU32 canvas = IM_COL32( 10, 10, 12, 255 );
        constexpr ImU32 line = kit::tone::white( 14 );
        constexpr ImU32 dot = kit::tone::white( 22 );
        constexpr ImU32 guide = IM_COL32( 255, 72, 150, 255 );
        constexpr float title = 46, status = 28, left = 284, right = 336, code = 300;
    }

    constexpr const char* widget_kinds[] = { "Toggle", "Slider", "Whole-number slider", "Dropdown", "Segmented", "Chips",
                                             "Colour", "Keybind", "Button", "Value", "Note" };
    constexpr const char* element_kinds[] = { "Text",     "Button", "Input", "Checkbox", "Slider",    "Dropdown",
                                              "Progress", "List",   "Panel", "Divider",  "Spinner",   "Tabs",
                                              "Image",    "Icon",   "Component", "Custom" };

    using icon_fn = std::function<void( ImDrawList*, ImVec2, ImU32 )>;
    using kind = doc::element_kind_t;

    auto text_size( const char* text, float size = 13.f ) -> float
    {
        return kit::text_width( text, size );
    }

    void label( ImDrawList* draw, ImVec2 at, ImU32 color, const char* text, float size = 13.f )
    {
        kit::text( draw, at, color, text, size );
    }

    // Icons, drawn as strokes so they stay crisp at any size.
    void icon_undo( ImDrawList* d, ImVec2 c, ImU32 col )
    {
        d->PathArcTo( c + ImVec2( 1, 1 ), 5.f, IM_PI * 1.05f, IM_PI * 2.35f, 12 );
        d->PathStroke( col, 0, 1.5f );
        d->AddLine( c + ImVec2( -4.5f, -3.5f ), c + ImVec2( -4.5f, 1.f ), col, 1.5f );
        d->AddLine( c + ImVec2( -4.5f, 1.f ), c + ImVec2( -0.f, 1.f ), col, 1.5f );
    }

    void icon_redo( ImDrawList* d, ImVec2 c, ImU32 col )
    {
        d->PathArcTo( c + ImVec2( -1, 1 ), 5.f, IM_PI * -.35f, IM_PI * 1.f - .15f, 12 );
        d->PathStroke( col, 0, 1.5f );
        d->AddLine( c + ImVec2( 4.5f, -3.5f ), c + ImVec2( 4.5f, 1.f ), col, 1.5f );
        d->AddLine( c + ImVec2( 4.5f, 1.f ), c + ImVec2( 0.f, 1.f ), col, 1.5f );
    }

    void icon_plus( ImDrawList* d, ImVec2 c, ImU32 col )
    {
        d->AddLine( c - ImVec2( 5, 0 ), c + ImVec2( 5, 0 ), col, 1.5f );
        d->AddLine( c - ImVec2( 0, 5 ), c + ImVec2( 0, 5 ), col, 1.5f );
    }

    void icon_chevron( ImDrawList* d, ImVec2 c, ImU32 col, bool open )
    {
        if ( open )
        {
            const ImVec2 p[] = { c + ImVec2( -3.5f, -1.5f ), c + ImVec2( 0, 2 ), c + ImVec2( 3.5f, -1.5f ) };
            d->AddPolyline( p, 3, col, 0, 1.4f );
        }
        else
        {
            const ImVec2 p[] = { c + ImVec2( -1.5f, -3.5f ), c + ImVec2( 2, 0 ), c + ImVec2( -1.5f, 3.5f ) };
            d->AddPolyline( p, 3, col, 0, 1.4f );
        }
    }

    // A small glyph per node type for the structure tree.
    void node_icon( ImDrawList* d, ImVec2 c, ImU32 col, int depth, const doc::widget_t* w )
    {
        if ( depth == 0 )
        {
            d->AddRect( c + ImVec2( -6, -5 ), c + ImVec2( 6, 5 ), col, 2, 0, 1.3f );
            d->AddLine( c + ImVec2( -6, -1.5f ), c + ImVec2( 6, -1.5f ), col, 1.3f );
        }
        else if ( depth == 1 )
        {
            d->AddRect( c + ImVec2( -6, -4 ), c + ImVec2( 6, 5 ), col, 2, 0, 1.3f );
            d->AddLine( c + ImVec2( -6, -4 ), c + ImVec2( -2, -6 ), col, 1.3f );
            d->AddLine( c + ImVec2( -2, -6 ), c + ImVec2( 1, -4 ), col, 1.3f );
        }
        else if ( depth == 2 )
        {
            d->AddRect( c + ImVec2( -7, -4 ), c + ImVec2( 7, 4 ), col, 4, 0, 1.3f );
            d->AddCircleFilled( c + ImVec2( 3, 0 ), 2.f, col, 10 );
        }
        else if ( depth == 3 )
        {
            for ( int i = -1; i <= 1; ++i )
                d->AddLine( c + ImVec2( -6, i * 3.5f ), c + ImVec2( i == 1 ? 2.f : 6.f, i * 3.5f ), col, 1.3f );
        }
        else if ( w )
        {
            switch ( w->kind )
            {
            case doc::kind_t::toggle:
                d->AddRect( c + ImVec2( -6, -3.5f ), c + ImVec2( 6, 3.5f ), col, 3.5f, 0, 1.3f );
                d->AddCircleFilled( c + ImVec2( 2.5f, 0 ), 1.8f, col, 8 );
                break;
            case doc::kind_t::slider:
            case doc::kind_t::slider_int:
                d->AddLine( c + ImVec2( -6, 0 ), c + ImVec2( 6, 0 ), col, 1.3f );
                d->AddCircleFilled( c + ImVec2( -1, 0 ), 2.5f, col, 10 );
                break;
            case doc::kind_t::color:
                d->AddRectFilled( c + ImVec2( -5, -5 ), c + ImVec2( 5, 5 ),
                                  ImGui::ColorConvertFloat4ToU32( { w->rgba[0], w->rgba[1], w->rgba[2], 1 } ), 3 );
                break;
            case doc::kind_t::keybind:
                d->AddRect( c + ImVec2( -6, -5 ), c + ImVec2( 6, 5 ), col, 2.5f, 0, 1.3f );
                d->AddLine( c + ImVec2( -3, 2 ), c + ImVec2( 3, 2 ), col, 1.3f );
                break;
            case doc::kind_t::chips:
                d->AddRect( c + ImVec2( -7, -3 ), c + ImVec2( -1, 3 ), col, 3, 0, 1.2f );
                d->AddRect( c + ImVec2( 1, -3 ), c + ImVec2( 7, 3 ), col, 3, 0, 1.2f );
                break;
            case doc::kind_t::select:
            case doc::kind_t::segment:
            {
                d->AddRect( c + ImVec2( -7, -4 ), c + ImVec2( 7, 4 ), col, 2.5f, 0, 1.2f );
                const ImVec2 p[] = { c + ImVec2( 1.5f, -1 ), c + ImVec2( 3.5f, 1 ), c + ImVec2( 5.5f, -1 ) };
                d->AddPolyline( p, 3, col, 0, 1.1f );
                break;
            }
            case doc::kind_t::button: d->AddRectFilled( c + ImVec2( -6, -4 ), c + ImVec2( 6, 4 ), col, 3 ); break;
            default:
                d->AddLine( c + ImVec2( -6, -2.5f ), c + ImVec2( 6, -2.5f ), col, 1.3f );
                d->AddLine( c + ImVec2( -6, 2.5f ), c + ImVec2( 2, 2.5f ), col, 1.3f );
                break;
            }
        }
    }

    void screen_icon( ImDrawList* d, ImVec2 c, ImU32 col )
    {
        d->AddRect( c + ImVec2( -7, -5 ), c + ImVec2( 7, 5 ), col, 2, 0, 1.3f );
        d->AddLine( c + ImVec2( -7, -2 ), c + ImVec2( 7, -2 ), col, 1.3f );
        d->AddCircleFilled( c + ImVec2( -4.5f, -3.5f ), .9f, col, 6 );
    }

    void component_icon( ImDrawList* d, ImVec2 c, ImU32 col )
    {
        // Four diamonds, the usual mark for a reusable component.
        for ( const ImVec2 o : { ImVec2( 0, -4 ), ImVec2( 4, 0 ), ImVec2( 0, 4 ), ImVec2( -4, 0 ) } )
        {
            const ImVec2 p = c + o;
            const ImVec2 q[] = { p + ImVec2( 0, -2.6f ), p + ImVec2( 2.6f, 0 ), p + ImVec2( 0, 2.6f ), p + ImVec2( -2.6f, 0 ) };
            d->AddPolyline( q, 4, col, ImDrawFlags_Closed, 1.2f );
        }
    }

    void element_icon( ImDrawList* d, ImVec2 c, ImU32 col, kind k )
    {
        switch ( k )
        {
        case kind::text:
            d->AddLine( c + ImVec2( -5, -5 ), c + ImVec2( 5, -5 ), col, 1.4f );
            d->AddLine( c + ImVec2( 0, -5 ), c + ImVec2( 0, 5 ), col, 1.4f );
            break;
        case kind::button: d->AddRectFilled( c + ImVec2( -7, -4 ), c + ImVec2( 7, 4 ), col, 3 ); break;
        case kind::input:
            d->AddRect( c + ImVec2( -7, -4 ), c + ImVec2( 7, 4 ), col, 2, 0, 1.2f );
            d->AddLine( c + ImVec2( -4, -2 ), c + ImVec2( -4, 2 ), col, 1.2f );
            break;
        case kind::checkbox:
        {
            d->AddRect( c + ImVec2( -5, -5 ), c + ImVec2( 5, 5 ), col, 2, 0, 1.2f );
            const ImVec2 p[] = { c + ImVec2( -2.5f, 0 ), c + ImVec2( -.5f, 2 ), c + ImVec2( 3, -2 ) };
            d->AddPolyline( p, 3, col, 0, 1.3f );
            break;
        }
        case kind::slider:
            d->AddLine( c + ImVec2( -6, 0 ), c + ImVec2( 6, 0 ), col, 1.3f );
            d->AddCircleFilled( c + ImVec2( -1, 0 ), 2.5f, col, 10 );
            break;
        case kind::dropdown:
        {
            d->AddRect( c + ImVec2( -7, -4 ), c + ImVec2( 7, 4 ), col, 2.5f, 0, 1.2f );
            const ImVec2 p[] = { c + ImVec2( 1.5f, -1 ), c + ImVec2( 3.5f, 1 ), c + ImVec2( 5.5f, -1 ) };
            d->AddPolyline( p, 3, col, 0, 1.1f );
            break;
        }
        case kind::progress:
            d->AddRect( c + ImVec2( -7, -2 ), c + ImVec2( 7, 2 ), col, 2, 0, 1.1f );
            d->AddRectFilled( c + ImVec2( -7, -2 ), c + ImVec2( 0, 2 ), col, 2 );
            break;
        case kind::list:
            for ( int i = -1; i <= 1; ++i )
            {
                d->AddCircleFilled( c + ImVec2( -5.5f, i * 4.f ), 1.f, col, 6 );
                d->AddLine( c + ImVec2( -3, i * 4.f ), c + ImVec2( 6, i * 4.f ), col, 1.2f );
            }
            break;
        case kind::panel: d->AddRect( c + ImVec2( -7, -6 ), c + ImVec2( 7, 6 ), col, 3, 0, 1.3f ); break;
        case kind::divider: d->AddLine( c + ImVec2( -7, 0 ), c + ImVec2( 7, 0 ), col, 1.4f ); break;
        case kind::spinner:
            d->PathArcTo( c, 5.f, 0, IM_PI * 1.5f, 12 );
            d->PathStroke( col, 0, 1.4f );
            break;
        case kind::tabs:
            d->AddLine( c + ImVec2( -7, 4 ), c + ImVec2( 7, 4 ), col, 1.2f );
            d->AddLine( c + ImVec2( -6, 4 ), c + ImVec2( -1, 4 ), col, 2.2f );
            d->AddLine( c + ImVec2( -6, -2 ), c + ImVec2( -1, -2 ), col, 1.2f );
            d->AddLine( c + ImVec2( 2, -2 ), c + ImVec2( 6, -2 ), col, 1.2f );
            break;
        case kind::image:
        {
            d->AddRect( c + ImVec2( -7, -5 ), c + ImVec2( 7, 5 ), col, 2, 0, 1.2f );
            const ImVec2 p[] = { c + ImVec2( -6, 4 ), c + ImVec2( -2, 0 ), c + ImVec2( 1, 3 ), c + ImVec2( 3, 1 ), c + ImVec2( 6, 4 ) };
            d->AddPolyline( p, 5, col, 0, 1.1f );
            d->AddCircleFilled( c + ImVec2( 3, -2 ), 1.2f, col, 8 );
            break;
        }
        case kind::icon: kit::icon( d, kit::icon_t::star, c, 14, col ); break;
        case kind::instance: component_icon( d, c, col ); break;
        case kind::custom:
            d->AddRect( c + ImVec2( -7, -5 ), c + ImVec2( 7, 5 ), col, 2, 0, 1.2f );
            d->AddLine( c + ImVec2( -3, -2 ), c + ImVec2( -5, 0 ), col, 1.2f );
            d->AddLine( c + ImVec2( -5, 0 ), c + ImVec2( -3, 2 ), col, 1.2f );
            d->AddLine( c + ImVec2( 3, -2 ), c + ImVec2( 5, 0 ), col, 1.2f );
            d->AddLine( c + ImVec2( 5, 0 ), c + ImVec2( 3, 2 ), col, 1.2f );
            break;
        default: break;
        }
    }

    // A flat, hover-lit icon button.
    auto icon_button( const char* id, ImVec2 pos, ImVec2 size, bool enabled = true ) -> std::pair<bool, bool>
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow( );
        const ImRect bb( pos, pos + size );
        const ImGuiID item = w->GetID( id );
        ImGui::ItemAdd( bb, item );
        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior( bb, item, &hovered, &held ) && enabled;
        if ( hovered && enabled )
            w->DrawList->AddRectFilled( bb.Min, bb.Max, kit::tone::white( held ? 22 : 14 ), 7 );
        return { pressed, hovered };
    }

    // A text field for std::string rows in the properties panel.
    auto edit_text( const char* name, std::string& value, const char* hint = nullptr ) -> bool
    {
        char buffer[512];
        std::snprintf( buffer, sizeof( buffer ), "%s", value.c_str( ) );
        if ( kit::text_field( name, buffer, sizeof( buffer ), hint, 188 ) )
        {
            value = buffer;
            return true;
        }
        return false;
    }

    // A settings row with buttons flush right; returns the index of the one pressed, or -1.
    auto button_row( const char* name, std::initializer_list<const char*> labels ) -> int
    {
        const ImRect row = kit::block( kit::metric::row );
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        draw->AddLine( { row.Min.x, row.Min.y + .5f }, { row.Max.x, row.Min.y + .5f }, kit::faded( kit::tone::row_line ) );
        if ( name && *name )
            kit::text( draw, { row.Min.x, std::floor( row.Min.y + 14 ) }, kit::tone::label, name );
        ImGui::PushID( name );
        ImGui::PushID( static_cast<int>( row.Min.y ) );
        int pressed = -1;
        float x = row.Max.x;
        const std::vector<const char*> list( labels );
        for ( int i = static_cast<int>( list.size( ) ) - 1; i >= 0; --i )
        {
            x -= kit::action_width( list[static_cast<size_t>( i )] );
            if ( kit::action_button( list[static_cast<size_t>( i )], { x, row.Min.y + 8 }, false, 28 ) )
                pressed = i;
            x -= 6;
        }
        ImGui::PopID( );
        ImGui::PopID( );
        return pressed;
    }

    // A draggable number box in a settings row (drag sideways, or Ctrl+click to type).
    auto edit_number( const char* name, float& value, float speed, const char* format ) -> bool
    {
        const ImRect row = kit::block( kit::metric::row );
        ImDrawList* draw = ImGui::GetWindowDrawList( );
        draw->AddLine( { row.Min.x, row.Min.y + .5f }, { row.Max.x, row.Min.y + .5f }, kit::faded( kit::tone::row_line ) );
        kit::text( draw, { row.Min.x, std::floor( row.Min.y + 14 ) }, kit::tone::label, name );
        ImGui::SetCursorScreenPos( { row.Max.x - 120, row.Min.y + 8 } );
        ImGui::PushID( name );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, { 10, 6 } );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 7 );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 1 );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, kit::tone::control );
        ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, kit::tone::control_hover );
        ImGui::PushStyleColor( ImGuiCol_FrameBgActive, kit::tone::control_hover );
        ImGui::PushStyleColor( ImGuiCol_Border, kit::tone::control_border );
        ImGui::PushStyleColor( ImGuiCol_Text, kit::tone::primary );
        ImGui::SetNextItemWidth( 120 );
        const bool changed = ImGui::DragFloat( "##number", &value, speed, 0, 0, format );
        ImGui::PopStyleColor( 5 );
        ImGui::PopStyleVar( 3 );
        ImGui::PopID( );
        return changed;
    }

    // Rows to edit a list of options; `details` adds a second line per option, `flags` a switch per option.
    void edit_options( std::vector<std::string>& options, std::vector<std::string>* details, std::vector<bool>* flags, const char* noun )
    {
        if ( details )
            details->resize( options.size( ) );
        if ( flags )
            flags->resize( options.size( ), false );
        int erase = -1;
        for ( size_t i = 0; i < options.size( ); ++i )
        {
            ImGui::PushID( static_cast<int>( i ) );
            char name[32];
            std::snprintf( name, sizeof( name ), "%s %zu", noun, i + 1 );
            edit_text( name, options[i] );
            if ( details )
                edit_text( "Detail", ( *details )[i], "Optional second line" );
            if ( flags )
            {
                bool v = ( *flags )[i];
                kit::toggle( "Starts on", v );
                ( *flags )[i] = v;
            }
            if ( options.size( ) > 1 && kit::button( "", "Remove" ) )
                erase = static_cast<int>( i );
            ImGui::PopID( );
        }
        if ( erase >= 0 )
        {
            options.erase( options.begin( ) + erase );
            if ( details )
                details->erase( details->begin( ) + erase );
            if ( flags )
                flags->erase( flags->begin( ) + erase );
        }
        std::string add = std::string( "Add " ) + noun;
        add[4] = static_cast<char>( std::tolower( static_cast<unsigned char>( add[4] ) ) );
        if ( kit::button( "", add.c_str( ) ) )
        {
            options.push_back( std::string( noun ) + " " + std::to_string( options.size( ) + 1 ) );
            if ( details )
                details->emplace_back( );
            if ( flags )
                flags->push_back( false );
        }
    }

    auto names( const std::vector<std::string>& list ) -> std::vector<const char*>
    {
        std::vector<const char*> out;
        for ( const auto& s : list )
            out.push_back( s.c_str( ) );
        return out;
    }

    // Syntax colours for the code panel.
    auto token_color( const std::string& word ) -> ImU32
    {
        static const char* keywords[] = { "namespace", "struct", "void", "bool", "int", "float", "auto", "static", "const",
                                          "inline", "return", "if", "for", "nullptr", "true", "false", "#include", "#pragma",
                                          "once", "char", "enum", "class", "using", "switch", "case", "break", "sizeof",
                                          "#ifdef", "#endif" };
        for ( const char* k : keywords )
            if ( word == k )
                return IM_COL32( 190, 160, 245, 255 );
        if ( word.rfind( "kit::", 0 ) == 0 || word.rfind( "std::", 0 ) == 0 || word.rfind( "ImGui", 0 ) == 0 || word.rfind( "kit_live::", 0 ) == 0 )
            return IM_COL32( 120, 208, 196, 255 );
        if ( !word.empty( ) && ( std::isdigit( static_cast<unsigned char>( word[0] ) ) ) )
            return IM_COL32( 156, 195, 255, 255 );
        return kit::tone::primary;
    }

    void code_line( ImDrawList* draw, ImVec2 at, const std::string& text )
    {
        constexpr float size = 13.f;
        ImFont* font = code_font ? code_font : kit::font( );
        float x = at.x;
        size_t i = 0;
        const auto put = [&]( const std::string& piece, ImU32 color ) {
            draw->AddText( font, size, { x, at.y }, color, piece.c_str( ) );
            x += font->CalcTextSizeA( size, FLT_MAX, 0.f, piece.c_str( ) ).x;
        };
        while ( i < text.size( ) )
        {
            if ( text.compare( i, 2, "//" ) == 0 )
            {
                put( text.substr( i ), IM_COL32( 112, 112, 124, 255 ) );
                return;
            }
            if ( text[i] == '"' )
            {
                size_t j = i + 1;
                while ( j < text.size( ) && text[j] != '"' )
                    j += text[j] == '\\' ? 2 : 1;
                put( text.substr( i, std::min( j + 1, text.size( ) ) - i ), IM_COL32( 230, 176, 122, 255 ) );
                i = std::min( j + 1, text.size( ) );
                continue;
            }
            if ( std::isalnum( static_cast<unsigned char>( text[i] ) ) || text[i] == '_' || text[i] == '#' )
            {
                size_t j = i;
                while ( j < text.size( ) &&
                        ( std::isalnum( static_cast<unsigned char>( text[j] ) ) || text[j] == '_' || text[j] == '#' || text[j] == '.' ||
                          ( text[j] == ':' && j + 1 < text.size( ) && text[j + 1] == ':' ) || ( text[j] == ':' && j > 0 && text[j - 1] == ':' ) ) )
                    ++j;
                const std::string word = text.substr( i, j - i );
                put( word, token_color( word ) );
                i = j;
                continue;
            }
            size_t j = i;
            while ( j < text.size( ) && !std::isalnum( static_cast<unsigned char>( text[j] ) ) && text[j] != '_' && text[j] != '"' &&
                    text[j] != '#' && text.compare( j, 2, "//" ) != 0 )
                ++j;
            put( text.substr( i, j - i ), kit::tone::secondary );
            i = j;
        }
    }

    auto read_file( const std::wstring& path, std::string& out ) -> bool
    {
        std::ifstream file( path, std::ios::binary );
        if ( !file )
            return false;
        std::stringstream buffer;
        buffer << file.rdbuf( );
        out = buffer.str( );
        return true;
    }

    auto write_file( const std::wstring& path, const std::string& text ) -> bool
    {
        std::ofstream file( path, std::ios::binary | std::ios::trunc );
        file << text;
        return static_cast<bool>( file );
    }

    void erase_file( const std::wstring& path )
    {
        std::error_code error;
        std::filesystem::remove( path, error );
    }

    void assign( doc::document_t& d, doc::widget_t& w )
    {
        w.uid = d.next_uid++;
    }

    // The eight resize handles, clockwise from the top-left corner.
    auto handle_point( const ImRect& r, int i ) -> ImVec2
    {
        const ImVec2 c = r.GetCenter( );
        switch ( i )
        {
        case 0: return r.Min;
        case 1: return { c.x, r.Min.y };
        case 2: return { r.Max.x, r.Min.y };
        case 3: return { r.Max.x, c.y };
        case 4: return r.Max;
        case 5: return { c.x, r.Max.y };
        case 6: return { r.Min.x, r.Max.y };
        default: return { r.Min.x, c.y };
        }
    }

    auto handle_cursor( int i ) -> ImGuiMouseCursor
    {
        switch ( i )
        {
        case 0: case 4: return ImGuiMouseCursor_ResizeNWSE;
        case 2: case 6: return ImGuiMouseCursor_ResizeNESW;
        case 1: case 5: return ImGuiMouseCursor_ResizeNS;
        default: return ImGuiMouseCursor_ResizeEW;
        }
    }

    // Finds the target nearest to any of `edges` (positions on one axis) within reach: the shift that lands on it,
    // and where it is.
    auto snap( std::initializer_list<float> edges, const std::vector<float>& targets, float reach, float& shift, float& target ) -> bool
    {
        float best = reach + 1;
        for ( const float e : edges )
            for ( const float t : targets )
                if ( std::fabs( t - e ) < best )
                {
                    best = std::fabs( t - e );
                    shift = t - e;
                    target = t;
                }
        return best <= reach;
    }

    auto bounds( const std::vector<doc::element_t*>& list ) -> ImVec4
    {
        float l = FLT_MAX, t = FLT_MAX, r = -FLT_MAX, b = -FLT_MAX;
        for ( const auto* e : list )
        {
            l = std::min( l, e->x ), t = std::min( t, e->y );
            r = std::max( r, e->x + e->w ), b = std::max( b, e->y + e->h );
        }
        return { l, t, r - l, b - t };
    }

    // A pill under the canvas window or element with its size.
    void size_pill( ImDrawList* draw, ImVec2 centre_top, float w, float h, ImU32 fill )
    {
        char size[48];
        std::snprintf( size, sizeof( size ), "%.0f \xc3\x97 %.0f", w, h );
        const float tw = text_size( size, 11.f ) + 14;
        const ImRect pill( { std::floor( centre_top.x - tw * .5f ), centre_top.y }, { std::floor( centre_top.x + tw * .5f ), centre_top.y + 18 } );
        draw->AddRectFilled( pill.Min, pill.Max, fill, 5 );
        draw->AddText( kit::font( ), 11.f, { pill.Min.x + 7, pill.Min.y + 3 }, IM_COL32_WHITE, size );
    }

    auto is_child_of( ImGuiWindow* w, ImGuiWindow* parent ) -> bool
    {
        for ( ; w; w = w->ParentWindow )
            if ( w == parent )
                return true;
        return false;
    }

    void push_menu_style( float width )
    {
        ImGui::SetNextWindowSize( { width, 0 } );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 6, 6 } );
        ImGui::PushStyleVar( ImGuiStyleVar_PopupRounding, 10 );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0, 0 } );
        ImGui::PushStyleColor( ImGuiCol_PopupBg, kit::tone::popup );
        ImGui::PushStyleColor( ImGuiCol_Border, kit::tone::window_border );
    }

    void pop_menu_style( )
    {
        ImGui::PopStyleColor( 2 );
        ImGui::PopStyleVar( 3 );
    }

    // An entry in a popup menu, with an optional second line and icon.
    auto menu_entry( const char* name, const char* detail, const icon_fn& icon ) -> bool
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow( );
        const ImVec2 at = w->DC.CursorPos;
        const ImRect bb( at, { w->WorkRect.Max.x, at.y + ( detail ? 48.f : 32.f ) } );
        ImGui::ItemSize( bb.GetSize( ) );
        const ImGuiID id = w->GetID( name );
        ImGui::ItemAdd( bb, id );
        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );
        if ( hovered )
            w->DrawList->AddRectFilled( bb.Min, bb.Max, kit::tone::white( 14 ), 8 );
        if ( icon )
            icon( w->DrawList, { bb.Min.x + 20, bb.GetCenter( ).y }, hovered ? kit::tone::title : kit::tone::secondary );
        const float x = bb.Min.x + ( icon ? 40.f : 12.f );
        w->DrawList->PushClipRect( bb.Min, { bb.Max.x - 8, bb.Max.y }, true );
        label( w->DrawList, { x, bb.Min.y + ( detail ? 8.f : 9.f ) }, kit::tone::title, name );
        if ( detail )
            label( w->DrawList, { x, bb.Min.y + 26 }, kit::tone::muted, detail, 11.5f );
        w->DrawList->PopClipRect( );
        if ( pressed )
            ImGui::CloseCurrentPopup( );
        return pressed;
    }

    auto element_rect( const doc::element_t& e ) -> ImVec4
    {
        return { e.x, e.y, e.w, e.h };
    }
}

editor_c::editor_c( )
{
    m_doc = doc::starter( );
    m_last = m_saved = doc::to_json( m_doc );
    preview::custom = []( const std::string& function, const ImRect& rect ) { return plugins::draw( function, rect ); };

    // Recent files, and an unsaved project left by a session that didn't close cleanly.
    const auto folder = platform::app_data( );
    std::string text;
    if ( read_file( folder + L"\\recent.txt", text ) )
    {
        std::stringstream lines( text );
        for ( std::string l; std::getline( lines, l ); )
        {
            std::error_code error;
            if ( !l.empty( ) && std::filesystem::exists( platform::widen( l ), error ) )
                m_recent.push_back( platform::widen( l ) );
        }
    }
    doc::document_t check;
    if ( read_file( folder + L"\\autosave.imstudio", text ) && doc::from_json( text, check ) )
        m_recovered = text;
}

void editor_c::shutdown( )
{
    erase_file( ( platform::app_data( ) + L"\\autosave.imstudio" ) );
}

void editor_c::notify( std::string message )
{
    m_status = std::move( message );
    m_status_at = ImGui::GetTime( );
}

void editor_c::remember( const std::wstring& path )
{
    m_recent.erase( std::remove( m_recent.begin( ), m_recent.end( ), path ), m_recent.end( ) );
    m_recent.insert( m_recent.begin( ), path );
    if ( m_recent.size( ) > 8 )
        m_recent.resize( 8 );
    std::string text;
    for ( const auto& p : m_recent )
        text += platform::narrow( p ) + "\n";
    write_file( platform::app_data( ) + L"\\recent.txt", text );
}

// Every few seconds, unsaved work is copied to %APPDATA% so a crash loses nothing.
void editor_c::autosave( )
{
    const double now = ImGui::GetTime( );
    if ( now - m_autosaved_at < 8 )
        return;
    m_autosaved_at = now;
    const auto path = platform::app_data( ) + L"\\autosave.imstudio";
    if ( m_last == m_saved )
    {
        if ( !m_autosaved.empty( ) )
            erase_file( path );
        m_autosaved.clear( );
        return;
    }
    if ( m_last != m_autosaved && write_file( path, m_last ) )
        m_autosaved = m_last;
}

auto editor_c::locate( int uid ) -> where_t
{
    where_t w;
    if ( uid <= 0 )
        return w;
    for ( size_t sci = 0; sci < m_doc.screens.size( ); ++sci )
    {
        auto& s = m_doc.screens[sci];
        if ( s.uid == uid )
        {
            w.screen = &s, w.sci = sci;
            return w;
        }
        for ( size_t ei = 0; ei < s.elements.size( ); ++ei )
            if ( s.elements[ei].uid == uid )
            {
                w.screen = &s, w.sci = sci, w.element = &s.elements[ei], w.ei = ei;
                return w;
            }
    }
    for ( size_t coi = 0; coi < m_doc.components.size( ); ++coi )
    {
        auto& c = m_doc.components[coi];
        if ( c.uid == uid )
        {
            w.component = &c, w.coi = coi;
            return w;
        }
        for ( size_t ei = 0; ei < c.elements.size( ); ++ei )
            if ( c.elements[ei].uid == uid )
            {
                w.component = &c, w.coi = coi, w.element = &c.elements[ei], w.ei = ei;
                return w;
            }
    }
    for ( size_t ci = 0; ci < m_doc.categories.size( ); ++ci )
    {
        auto& c = m_doc.categories[ci];
        if ( c.uid == uid )
            return { &c, nullptr, nullptr, nullptr, nullptr, ci };
        for ( size_t gi = 0; gi < c.groups.size( ); ++gi )
        {
            auto& g = c.groups[gi];
            if ( g.uid == uid )
                return { &c, &g, nullptr, nullptr, nullptr, ci, gi };
            for ( size_t fi = 0; fi < g.features.size( ); ++fi )
            {
                auto& f = g.features[fi];
                if ( f.uid == uid )
                    return { &c, &g, &f, nullptr, nullptr, ci, gi, fi };
                for ( size_t si = 0; si < f.sections.size( ); ++si )
                {
                    auto& s = f.sections[si];
                    if ( s.uid == uid )
                        return { &c, &g, &f, &s, nullptr, ci, gi, fi, si };
                    for ( size_t wi = 0; wi < s.widgets.size( ); ++wi )
                        if ( s.widgets[wi].uid == uid )
                            return { &c, &g, &f, &s, &s.widgets[wi], ci, gi, fi, si, wi };
                }
            }
        }
    }
    return w;
}

auto editor_c::shown_component( ) -> doc::component_t*
{
    return doc::find_component( m_doc, m_screen );
}

auto editor_c::shown_screen( ) -> doc::screen_t*
{
    if ( shown_component( ) || m_doc.screens.empty( ) )
        return nullptr;
    for ( auto& s : m_doc.screens )
        if ( s.uid == m_screen )
            return &s;
    m_screen = m_doc.screens.front( ).uid;
    return &m_doc.screens.front( );
}

auto editor_c::multi( int uid ) const -> bool
{
    return std::find( m_multi.begin( ), m_multi.end( ), uid ) != m_multi.end( );
}

auto editor_c::selected_elements( ) -> std::vector<doc::element_t*>
{
    std::vector<doc::element_t*> out;
    for ( const int uid : m_multi )
        if ( auto* e = locate( uid ).element )
            out.push_back( e );
    return out;
}

void editor_c::select( int uid, bool add )
{
    const auto w = locate( uid );
    if ( add && w.element )
    {
        // Shift: toggle the element in the selection; it must share the container of what's already selected.
        const auto first = m_multi.empty( ) ? where_t{ } : locate( m_multi.front( ) );
        if ( first.element && first.elements( ) != w.elements( ) )
            m_multi.clear( );
        if ( multi( uid ) )
        {
            m_multi.erase( std::remove( m_multi.begin( ), m_multi.end( ), uid ), m_multi.end( ) );
            m_selected = m_multi.empty( ) ? ( w.screen ? w.screen->uid : w.component->uid ) : m_multi.back( );
            return;
        }
        m_multi.push_back( uid );
        m_selected = uid;
        return;
    }
    m_selected = uid;
    m_multi.clear( );
    if ( !uid || !w )
        return;
    if ( w.element )
        m_multi.push_back( uid );
    if ( w.screen || w.component )
    {
        m_screen = w.screen ? w.screen->uid : w.component->uid;
        m_collapsed.erase( m_screen );
        return;
    }
    preview::focus( m_doc, m_shell, uid );
    m_last_shown = preview::shown_feature( m_doc, m_shell );
    // Make sure the node is visible in the tree.
    if ( w.category ) m_collapsed.erase( w.category->uid );
    if ( w.group ) m_collapsed.erase( w.group->uid );
    if ( w.feature ) m_collapsed.erase( w.feature->uid );
    if ( w.section ) m_collapsed.erase( w.section->uid );
}

void editor_c::renumber( doc::section_t& s )
{
    s.uid = m_doc.next_uid++;
    for ( auto& w : s.widgets )
        assign( m_doc, w );
}

void editor_c::renumber( doc::feature_t& f )
{
    f.uid = m_doc.next_uid++;
    for ( auto& s : f.sections )
        renumber( s );
}

void editor_c::renumber( doc::group_t& g )
{
    g.uid = m_doc.next_uid++;
    for ( auto& f : g.features )
        renumber( f );
}

void editor_c::renumber( doc::category_t& c )
{
    c.uid = m_doc.next_uid++;
    for ( auto& g : c.groups )
        renumber( g );
}

// New uids for copied elements; tab links between them follow, links to anything outside are dropped.
void editor_c::renumber( std::vector<doc::element_t>& elements )
{
    std::map<int, int> renamed;
    for ( auto& e : elements )
    {
        const int uid = m_doc.next_uid++;
        renamed[e.uid] = uid;
        e.uid = uid;
    }
    for ( auto& e : elements )
        e.tab_owner = renamed.count( e.tab_owner ) ? renamed[e.tab_owner] : 0;
}

void editor_c::add( int what )
{
    auto at = locate( m_selected );
    if ( app( ) )
    {
        if ( what == 100 )
        {
            auto s = doc::make_screen( m_doc, "Screen " + std::to_string( m_doc.screens.size( ) + 1 ) );
            const int uid = s.uid;
            m_doc.screens.insert( at.screen && !at.element ? m_doc.screens.begin( ) + static_cast<long long>( at.sci ) + 1 : m_doc.screens.end( ), s );
            select( uid );
            return;
        }
        if ( what == 101 )
        {
            doc::component_t c;
            c.uid = m_doc.next_uid++;
            c.name = "Component " + std::to_string( m_doc.components.size( ) + 1 );
            c.id = doc::identifier( c.name );
            const int uid = c.uid;
            m_doc.components.push_back( c );
            select( uid );
            notify( "New component: add elements to it, then place it on a screen as a Component instance." );
            return;
        }
        // Into the selected element's container, or the one on the canvas.
        std::vector<doc::element_t>* list = at.elements( );
        if ( !list )
        {
            if ( auto* c = shown_component( ) )
                list = &c->elements;
            else if ( auto* s = shown_screen( ) )
                list = &s->elements;
            else
            {
                m_doc.screens.push_back( doc::make_screen( m_doc, "Main" ) );
                list = &m_doc.screens.back( ).elements;
            }
        }
        float W = m_doc.theme.width, H = m_doc.theme.height;
        bool in_component = false;
        for ( auto& c : m_doc.components )
            if ( &c.elements == list )
            {
                W = c.w, H = c.h;
                in_component = true;
            }
        const auto k = static_cast<kind>( what - 200 );
        if ( k == kind::instance && ( in_component || m_doc.components.empty( ) ) )
        {
            notify( in_component ? "Components can't hold other components." : "Make a component first (+ > Component)." );
            return;
        }
        auto e = doc::make_element( m_doc, k );
        // Under the selected element, or in the middle of the window.
        const bool after = at.element && at.elements( ) == list;
        if ( after )
        {
            e.x = at.element->x;
            e.y = at.element->y + at.element->h + 12;
        }
        else
        {
            e.x = std::floor( ( W - e.w ) * .5f );
            e.y = std::floor( ( H - e.h ) * .5f );
        }
        e.w = std::min( e.w, W );
        e.h = std::min( e.h, H );
        e.x = std::clamp( e.x, 0.f, std::max( 0.f, W - e.w ) );
        e.y = std::clamp( e.y, 0.f, std::max( 0.f, H - e.h ) );
        const int uid = e.uid;
        list->insert( after ? list->begin( ) + static_cast<long long>( at.ei ) + 1 : list->end( ), e );
        select( uid );
        return;
    }

    // Containers are created on the way down when the selection is higher up (or nothing is selected).
    const auto ensure_category = [&]( ) -> doc::category_t& {
        if ( at.category )
            return *at.category;
        if ( m_doc.categories.empty( ) )
        {
            doc::category_t c;
            c.uid = m_doc.next_uid++;
            c.name = "Category";
            m_doc.categories.push_back( c );
        }
        return m_doc.categories[std::clamp<size_t>( static_cast<size_t>( m_shell.category ), 0, m_doc.categories.size( ) - 1 )];
    };
    const auto ensure_group = [&]( ) -> doc::group_t& {
        if ( at.group )
            return *at.group;
        auto& c = ensure_category( );
        if ( c.groups.empty( ) )
        {
            doc::group_t g;
            g.uid = m_doc.next_uid++;
            c.groups.push_back( g );
        }
        return c.groups.back( );
    };
    const auto ensure_feature = [&]( ) -> doc::feature_t& {
        if ( at.feature )
            return *at.feature;
        auto& g = ensure_group( );
        if ( g.features.empty( ) )
        {
            doc::feature_t f;
            f.uid = m_doc.next_uid++;
            f.id = "feature";
            g.features.push_back( f );
        }
        return g.features.back( );
    };
    const auto ensure_section = [&]( ) -> doc::section_t& {
        if ( at.section )
            return *at.section;
        auto& f = ensure_feature( );
        if ( f.sections.empty( ) )
        {
            doc::section_t s;
            s.uid = m_doc.next_uid++;
            f.sections.push_back( s );
        }
        return f.sections.back( );
    };

    int created = 0;
    if ( what == 0 )
    {
        doc::category_t c;
        c.uid = m_doc.next_uid++;
        c.name = "New category";
        m_doc.categories.insert( at.category ? m_doc.categories.begin( ) + static_cast<long long>( at.ci ) + 1 : m_doc.categories.end( ), c );
        created = c.uid;
    }
    else if ( what == 1 )
    {
        auto& c = ensure_category( );
        doc::group_t g;
        g.uid = m_doc.next_uid++;
        g.title = "New group";
        c.groups.push_back( g );
        created = g.uid;
    }
    else if ( what == 2 )
    {
        auto& g = ensure_group( );
        doc::feature_t f;
        f.uid = m_doc.next_uid++;
        f.name = "New feature";
        f.id = "new_feature";
        f.description = "What this feature does.";
        doc::section_t s;
        s.uid = m_doc.next_uid++;
        s.title = "Settings";
        f.sections.push_back( s );
        g.features.push_back( f );
        created = f.uid;
    }
    else if ( what == 3 )
    {
        auto& f = ensure_feature( );
        doc::section_t s;
        s.uid = m_doc.next_uid++;
        s.title = "New section";
        s.column = f.sections.empty( ) ? 0 : std::min( 1, static_cast<int>( f.sections.size( ) % 2 ) );
        f.sections.push_back( s );
        created = s.uid;
    }
    else
    {
        auto& s = ensure_section( );
        auto w = doc::make_widget( m_doc, static_cast<doc::kind_t>( what - 4 ) );
        const auto position = at.widget ? s.widgets.begin( ) + static_cast<long long>( at.wi ) + 1 : s.widgets.end( );
        created = w.uid;
        s.widgets.insert( position, w );
    }
    select( created );
}

void editor_c::remove( int uid )
{
    const auto at = locate( uid );
    if ( !at )
        return;
    int next = 0;
    if ( at.element )
    {
        auto* list = at.elements( );
        list->erase( list->begin( ) + static_cast<long long>( at.ei ) );
        for ( auto& e : *list )
            if ( e.tab_owner == uid )
                e.tab_owner = 0;
        next = at.screen ? at.screen->uid : at.component->uid;
    }
    else if ( at.screen )
    {
        const int gone = at.screen->uid;
        m_doc.screens.erase( m_doc.screens.begin( ) + static_cast<long long>( at.sci ) );
        for ( auto& s : m_doc.screens )
            for ( auto& e : s.elements )
                if ( e.target == gone )
                    e.target = 0;
        if ( !m_doc.screens.empty( ) )
            next = m_doc.screens[std::min( at.sci, m_doc.screens.size( ) - 1 )].uid;
    }
    else if ( at.component )
    {
        // Its instances go with it.
        const int gone = at.component->uid;
        m_doc.components.erase( m_doc.components.begin( ) + static_cast<long long>( at.coi ) );
        for ( auto& s : m_doc.screens )
            s.elements.erase( std::remove_if( s.elements.begin( ), s.elements.end( ),
                                              [&]( const doc::element_t& e ) { return e.kind == kind::instance && e.component == gone; } ),
                              s.elements.end( ) );
        next = m_doc.screens.empty( ) ? 0 : m_doc.screens.front( ).uid;
    }
    else if ( at.widget )
    {
        at.section->widgets.erase( at.section->widgets.begin( ) + static_cast<long long>( at.wi ) );
        next = at.section->uid;
    }
    else if ( at.section )
    {
        at.feature->sections.erase( at.feature->sections.begin( ) + static_cast<long long>( at.si ) );
        next = at.feature->uid;
    }
    else if ( at.feature )
    {
        at.group->features.erase( at.group->features.begin( ) + static_cast<long long>( at.fi ) );
        next = at.group->uid;
    }
    else if ( at.group )
    {
        at.category->groups.erase( at.category->groups.begin( ) + static_cast<long long>( at.gi ) );
        next = at.category->uid;
    }
    else
        m_doc.categories.erase( m_doc.categories.begin( ) + static_cast<long long>( at.ci ) );
    select( next );
}

void editor_c::remove_selection( )
{
    if ( m_multi.size( ) <= 1 )
    {
        remove( m_selected );
        return;
    }
    const auto list = m_multi;
    for ( const int uid : list )
        remove( uid );
}

void editor_c::duplicate( int uid )
{
    const auto at = locate( uid );
    if ( !at )
        return;
    int created = 0;
    if ( at.element )
    {
        std::vector<doc::element_t> copy = { *at.element };
        renumber( copy );
        copy[0].tab_owner = at.element->tab_owner;
        copy[0].id += "_copy";
        const float H = at.component ? at.component->h : m_doc.theme.height;
        copy[0].y = std::min( copy[0].y + copy[0].h + 8, std::max( 0.f, H - copy[0].h ) );
        created = copy[0].uid;
        at.elements( )->insert( at.elements( )->begin( ) + static_cast<long long>( at.ei ) + 1, copy[0] );
    }
    else if ( at.screen )
    {
        auto copy = *at.screen;
        copy.uid = m_doc.next_uid++;
        renumber( copy.elements );
        copy.name += " copy";
        copy.id += "_copy";
        created = copy.uid;
        m_doc.screens.insert( m_doc.screens.begin( ) + static_cast<long long>( at.sci ) + 1, copy );
    }
    else if ( at.component )
    {
        auto copy = *at.component;
        copy.uid = m_doc.next_uid++;
        renumber( copy.elements );
        copy.name += " copy";
        copy.id += "_copy";
        created = copy.uid;
        m_doc.components.insert( m_doc.components.begin( ) + static_cast<long long>( at.coi ) + 1, copy );
    }
    else if ( at.widget )
    {
        auto copy = *at.widget;
        assign( m_doc, copy );
        copy.id += "_copy";
        created = copy.uid;
        at.section->widgets.insert( at.section->widgets.begin( ) + static_cast<long long>( at.wi ) + 1, copy );
    }
    else if ( at.section )
    {
        auto copy = *at.section;
        renumber( copy );
        created = copy.uid;
        at.feature->sections.insert( at.feature->sections.begin( ) + static_cast<long long>( at.si ) + 1, copy );
    }
    else if ( at.feature )
    {
        auto copy = *at.feature;
        renumber( copy );
        copy.name += " copy";
        copy.id += "_copy";
        created = copy.uid;
        at.group->features.insert( at.group->features.begin( ) + static_cast<long long>( at.fi ) + 1, copy );
    }
    else if ( at.group )
    {
        auto copy = *at.group;
        renumber( copy );
        created = copy.uid;
        at.category->groups.insert( at.category->groups.begin( ) + static_cast<long long>( at.gi ) + 1, copy );
    }
    else
    {
        auto copy = *at.category;
        renumber( copy );
        copy.name += " copy";
        created = copy.uid;
        m_doc.categories.insert( m_doc.categories.begin( ) + static_cast<long long>( at.ci ) + 1, copy );
    }
    select( created );
}

// Copies of every selected element, 12 px down and right, selected afterwards.
void editor_c::duplicate_selection( )
{
    if ( m_multi.size( ) <= 1 )
    {
        duplicate( m_selected );
        return;
    }
    auto* list = locate( m_multi.front( ) ).elements( );
    if ( !list )
        return;
    std::vector<doc::element_t> copies;
    for ( const auto* e : selected_elements( ) )
        copies.push_back( *e );
    renumber( copies );
    m_multi.clear( );
    for ( auto& e : copies )
    {
        e.x += 12, e.y += 12;
        list->push_back( e );
        m_multi.push_back( e.uid );
    }
    m_selected = m_multi.back( );
}

void editor_c::move( int uid, int delta )
{
    const auto at = locate( uid );
    const auto shift = [&]( auto& list, size_t index ) {
        const long long target = static_cast<long long>( index ) + delta;
        if ( target < 0 || target >= static_cast<long long>( list.size( ) ) )
            return;
        std::swap( list[index], list[static_cast<size_t>( target )] );
    };
    if ( at.element )
        shift( *at.elements( ), at.ei );
    else if ( at.screen )
        shift( m_doc.screens, at.sci );
    else if ( at.component )
        shift( m_doc.components, at.coi );
    else if ( at.widget )
        shift( at.section->widgets, at.wi );
    else if ( at.section )
        shift( at.feature->sections, at.si );
    else if ( at.feature )
        shift( at.group->features, at.fi );
    else if ( at.group )
        shift( at.category->groups, at.gi );
    else if ( at.category )
        shift( m_doc.categories, at.ci );
}

// Dragging a node onto another: same kind goes before it; onto its parent kind, it goes inside at the end.
void editor_c::drop( int source, int target )
{
    if ( source == target )
        return;
    auto from = locate( source );
    auto to = locate( target );
    if ( !from || !to )
        return;
    if ( from.element && ( to.screen || to.component ) )
    {
        if ( from.element->kind == kind::instance && to.component )
            return;
        const auto moved = *from.element;
        from.elements( )->erase( from.elements( )->begin( ) + static_cast<long long>( from.ei ) );
        to = locate( target );
        auto& list = *to.elements( );
        list.insert( to.element ? list.begin( ) + static_cast<long long>( to.ei ) : list.end( ), moved );
    }
    else if ( from.screen && !from.element && to.screen && !to.element )
    {
        const auto moved = *from.screen;
        m_doc.screens.erase( m_doc.screens.begin( ) + static_cast<long long>( from.sci ) );
        to = locate( target );
        m_doc.screens.insert( m_doc.screens.begin( ) + static_cast<long long>( to.sci ), moved );
    }
    else if ( from.component && !from.element && to.component && !to.element )
    {
        const auto moved = *from.component;
        m_doc.components.erase( m_doc.components.begin( ) + static_cast<long long>( from.coi ) );
        to = locate( target );
        m_doc.components.insert( m_doc.components.begin( ) + static_cast<long long>( to.coi ), moved );
    }
    else if ( from.widget && ( to.widget || to.section ) )
    {
        const auto moved = *from.widget;
        from.section->widgets.erase( from.section->widgets.begin( ) + static_cast<long long>( from.wi ) );
        to = locate( target );
        auto& list = to.section->widgets;
        list.insert( to.widget ? list.begin( ) + static_cast<long long>( to.wi ) : list.end( ), moved );
    }
    else if ( from.section && !from.widget && ( ( to.section && !to.widget ) || ( to.feature && !to.section ) ) )
    {
        const auto moved = *from.section;
        from.feature->sections.erase( from.feature->sections.begin( ) + static_cast<long long>( from.si ) );
        to = locate( target );
        auto& list = to.feature->sections;
        list.insert( to.section ? list.begin( ) + static_cast<long long>( to.si ) : list.end( ), moved );
    }
    else if ( from.feature && !from.section && ( ( to.feature && !to.section ) || ( to.group && !to.feature ) ) )
    {
        const auto moved = *from.feature;
        from.group->features.erase( from.group->features.begin( ) + static_cast<long long>( from.fi ) );
        to = locate( target );
        auto& list = to.group->features;
        list.insert( to.feature ? list.begin( ) + static_cast<long long>( to.fi ) : list.end( ), moved );
    }
    else if ( from.group && !from.feature && to.category && ( ( to.group && !to.feature ) || !to.group ) )
    {
        const auto moved = *from.group;
        from.category->groups.erase( from.category->groups.begin( ) + static_cast<long long>( from.gi ) );
        to = locate( target );
        auto& list = to.category->groups;
        list.insert( to.group ? list.begin( ) + static_cast<long long>( to.gi ) : list.end( ), moved );
    }
    else if ( from.category && !from.group && to.category && !to.group )
    {
        const auto moved = *from.category;
        m_doc.categories.erase( m_doc.categories.begin( ) + static_cast<long long>( from.ci ) );
        to = locate( target );
        m_doc.categories.insert( m_doc.categories.begin( ) + static_cast<long long>( to.ci ), moved );
    }
    else
        return;
    select( source );
}

void editor_c::copy( bool cut )
{
    if ( app( ) )
    {
        std::vector<doc::element_t> list;
        for ( const auto* e : selected_elements( ) )
            list.push_back( *e );
        if ( list.empty( ) )
            return;
        ImGui::SetClipboardText( doc::clip_elements( list ).c_str( ) );
        notify( cut ? "Cut." : "Copied." );
        if ( cut )
            remove_selection( );
        return;
    }
    if ( auto* w = locate( m_selected ).widget )
    {
        ImGui::SetClipboardText( doc::clip_widgets( { *w } ).c_str( ) );
        notify( cut ? "Cut." : "Copied." );
        if ( cut )
            remove( m_selected );
    }
}

void editor_c::paste( )
{
    const char* text = ImGui::GetClipboardText( );
    if ( !text )
        return;
    const auto at = locate( m_selected );
    if ( app( ) )
    {
        std::vector<doc::element_t> pasted;
        if ( !doc::paste_elements( text, m_doc, pasted ) )
            return;
        std::vector<doc::element_t>* list = at.elements( );
        if ( !list )
            list = shown_component( ) ? &shown_component( )->elements : shown_screen( ) ? &shown_screen( )->elements : nullptr;
        if ( !list )
            return;
        bool into_component = false;
        for ( auto& c : m_doc.components )
            into_component |= &c.elements == list;
        // Pasted over the originals, they shift so both stay visible.
        bool overlap = false;
        for ( const auto& p : pasted )
            for ( const auto& e : *list )
                overlap |= e.x == p.x && e.y == p.y;
        m_multi.clear( );
        for ( auto& p : pasted )
        {
            if ( into_component && p.kind == kind::instance )
                continue;
            if ( overlap )
                p.x += 12, p.y += 12;
            list->push_back( p );
            m_multi.push_back( p.uid );
        }
        if ( !m_multi.empty( ) )
            m_selected = m_multi.back( );
        return;
    }
    std::vector<doc::widget_t> pasted;
    doc::section_t* section = at.section;
    if ( !section || !doc::paste_widgets( text, m_doc, pasted ) )
        return;
    auto position = at.widget ? section->widgets.begin( ) + static_cast<long long>( at.wi ) + 1 : section->widgets.end( );
    section->widgets.insert( position, pasted.begin( ), pasted.end( ) );
    select( pasted.back( ).uid );
}

// One element aligns to its window; several align to their shared bounds.
void editor_c::align( int how )
{
    auto list = selected_elements( );
    if ( list.empty( ) )
        return;
    const auto at = locate( list.front( )->uid );
    ImVec4 box = bounds( list );
    if ( list.size( ) == 1 )
        box = at.component ? ImVec4( 0, 0, at.component->w, at.component->h ) : ImVec4( 0, 0, m_doc.theme.width, m_doc.theme.height );
    for ( auto* e : list )
        switch ( how )
        {
        case 0: e->x = box.x; break;
        case 1: e->x = std::floor( box.x + ( box.z - e->w ) * .5f ); break;
        case 2: e->x = box.x + box.z - e->w; break;
        case 3: e->y = box.y; break;
        case 4: e->y = std::floor( box.y + ( box.w - e->h ) * .5f ); break;
        default: e->y = box.y + box.w - e->h; break;
        }
}

// Equal gaps between three or more elements, keeping the outer two in place.
void editor_c::distribute( bool horizontal )
{
    auto list = selected_elements( );
    if ( list.size( ) < 3 )
        return;
    std::sort( list.begin( ), list.end( ), [&]( auto* a, auto* b ) { return horizontal ? a->x < b->x : a->y < b->y; } );
    const ImVec4 box = bounds( list );
    float total = 0;
    for ( const auto* e : list )
        total += horizontal ? e->w : e->h;
    const float gap = ( ( horizontal ? box.z : box.w ) - total ) / static_cast<float>( list.size( ) - 1 );
    float pos = horizontal ? box.x : box.y;
    for ( auto* e : list )
    {
        ( horizontal ? e->x : e->y ) = std::round( pos );
        pos += ( horizontal ? e->w : e->h ) + gap;
    }
}

void editor_c::match( bool width )
{
    const auto* first = locate( m_selected ).element;
    if ( !first )
        return;
    const float size = width ? first->w : first->h;
    for ( auto* e : selected_elements( ) )
        ( width ? e->w : e->h ) = size;
}

// The selected elements become a component; an instance takes their place.
void editor_c::make_component( )
{
    auto list = selected_elements( );
    const auto at = list.empty( ) ? where_t{ } : locate( list.front( )->uid );
    if ( list.empty( ) || !at.screen )
    {
        notify( "Select elements on a screen to make a component." );
        return;
    }
    const ImVec4 box = bounds( list );
    doc::component_t c;
    c.uid = m_doc.next_uid++;
    c.name = "Component " + std::to_string( m_doc.components.size( ) + 1 );
    c.id = doc::identifier( c.name );
    c.w = std::round( box.z ), c.h = std::round( box.w );
    std::set<int> taken;
    size_t place = at.screen->elements.size( );
    for ( size_t i = 0; i < at.screen->elements.size( ); ++i )
    {
        const auto& e = at.screen->elements[i];
        if ( multi( e.uid ) && e.kind != kind::instance )
        {
            auto copy = e;
            copy.x -= box.x, copy.y -= box.y;
            copy.pin_x = copy.pin_y = 0;
            c.elements.push_back( copy );
            taken.insert( e.uid );
            place = std::min( place, i );
        }
    }
    renumber( c.elements );
    auto& elements = at.screen->elements;
    elements.erase( std::remove_if( elements.begin( ), elements.end( ), [&]( const doc::element_t& e ) { return taken.count( e.uid ) > 0; } ),
                    elements.end( ) );
    m_doc.components.push_back( c );
    auto instance = doc::make_element( m_doc, kind::instance );
    instance.component = c.uid;
    instance.text = c.name;
    instance.id = c.id;
    instance.x = box.x, instance.y = box.y, instance.w = c.w, instance.h = c.h;
    const int uid = instance.uid;
    elements.insert( elements.begin( ) + static_cast<long long>( std::min( place, elements.size( ) ) ), instance );
    select( uid );
    notify( "Made " + c.name + ". Edit it under Components; every instance follows." );
}

// An instance becomes plain elements again.
void editor_c::detach( int instance )
{
    const auto at = locate( instance );
    if ( !at.element || at.element->kind != kind::instance || !at.screen )
        return;
    const auto* c = doc::find_component( m_doc, at.element->component );
    if ( !c )
        return;
    auto copies = c->elements;
    renumber( copies );
    const float x = at.element->x, y = at.element->y;
    auto& elements = at.screen->elements;
    const auto index = static_cast<long long>( at.ei );
    elements.erase( elements.begin( ) + index );
    m_multi.clear( );
    for ( auto& e : copies )
    {
        e.x += x, e.y += y;
        m_multi.push_back( e.uid );
    }
    elements.insert( elements.begin( ) + index, copies.begin( ), copies.end( ) );
    m_selected = m_multi.empty( ) ? at.screen->uid : m_multi.back( );
}

// Resizing an app's window moves its elements by their pins, from where they were when the resize began.
void editor_c::resize_window( float width, float height, const std::vector<std::pair<int, ImVec4>>& from, ImVec2 from_size )
{
    m_doc.theme.width = width, m_doc.theme.height = height;
    if ( !app( ) )
        return;
    const float dx = width - from_size.x, dy = height - from_size.y;
    const auto place = []( float& pos, float& len, float start, float size, float more, int pin ) {
        pos = start, len = size;
        switch ( pin )
        {
        case 1: pos += more; break;
        case 2: len = std::max( 1.f, len + more ); break;
        case 3: pos += std::round( more * .5f ); break;
        default: break;
        }
    };
    for ( const auto& [uid, r] : from )
        if ( auto* e = locate( uid ).element )
        {
            place( e->x, e->w, r.x, r.z, dx, e->pin_x );
            place( e->y, e->h, r.y, r.w, dy, e->pin_y );
        }
}

void editor_c::import_image( doc::element_t* into )
{
    const auto path = platform::open_image( );
    if ( path.empty( ) )
        return;
    std::string bytes;
    int w = 0, h = 0;
    if ( !read_file( path, bytes ) || !kit::image_size( reinterpret_cast<const unsigned char*>( bytes.data( ) ), bytes.size( ), w, h ) )
    {
        notify( "That file isn't an image Windows can read." );
        return;
    }
    doc::image_t image;
    image.uid = m_doc.next_uid++;
    image.name = platform::narrow( std::filesystem::path( path ).stem( ).wstring( ) );
    image.id = doc::identifier( image.name );
    image.data = std::move( bytes );
    image.width = w, image.height = h;
    m_doc.images.push_back( image );
    if ( into )
    {
        // Its own size, scaled down to fit the window if needed.
        const float scale = std::min( { 1.f, ( m_doc.theme.width - 64 ) / static_cast<float>( w ), ( m_doc.theme.height - 64 ) / static_cast<float>( h ) } );
        into->image = image.uid;
        into->w = std::round( static_cast<float>( w ) * scale );
        into->h = std::round( static_cast<float>( h ) * scale );
    }
    notify( "Imported " + image.name + " (" + std::to_string( w ) + " \xc3\x97 " + std::to_string( h ) + ")." );
}

void editor_c::start( doc::document_t d, const char* message )
{
    m_doc = std::move( d );
    m_path.clear( );
    m_undo.clear( );
    m_redo.clear( );
    m_last = m_saved = doc::to_json( m_doc );
    m_shell = { };
    m_screen = 0;
    m_drag = { };
    m_fit = true;
    select( 0 );
    if ( message && *message )
        notify( message );
}

auto editor_c::use_template( const std::wstring& name ) -> bool
{
    if ( name == L"example" )
        start( doc::starter( ), "" );
    else if ( name == L"loader" )
        start( doc::loader( ), "" );
    else if ( name == L"blank-menu" )
        start( doc::blank_menu( ), "" );
    else if ( name == L"blank-app" )
        start( doc::blank_app( ), "" );
    else
        return false;
    return true;
}

auto editor_c::export_to( const std::wstring& folder, std::string& message ) -> bool
{
    bool ok = false;
    message = codegen::export_to( m_doc, folder, ok );
    return ok;
}

void editor_c::open( )
{
    const auto path = platform::open_file( );
    if ( !path.empty( ) )
        open_path( path );
}

void editor_c::open_path( const std::wstring& path )
{
    std::string text, error;
    doc::document_t loaded;
    if ( !read_file( path, text ) || !doc::from_json( text, loaded, &error ) )
    {
        notify( "Couldn't open that file" + ( error.empty( ) ? std::string( "." ) : ": " + error ) );
        return;
    }
    start( std::move( loaded ), "" );
    m_path = path;
    remember( path );
    notify( "Opened " + platform::narrow( path.substr( path.find_last_of( L"\\/" ) + 1 ) ) + "." );
}

void editor_c::save( bool as )
{
    if ( as || m_path.empty( ) )
    {
        const auto path = platform::save_file( platform::widen( doc::identifier( m_doc.name ) ) + L".imstudio" );
        if ( path.empty( ) )
            return;
        m_path = path;
    }
    const auto text = doc::to_json( m_doc );
    if ( !write_file( m_path, text ) )
    {
        notify( "Couldn't save the project." );
        return;
    }
    m_saved = text;
    remember( m_path );
    notify( "Saved." );
}

auto editor_c::save_to( const std::wstring& path ) -> bool
{
    return write_file( path, doc::to_json( m_doc ) );
}

void editor_c::export_code( )
{
    const auto folder = platform::pick_folder( );
    if ( folder.empty( ) )
        return;
    bool ok = false;
    notify( codegen::export_to( m_doc, folder, ok ) );
}

void editor_c::undo( )
{
    if ( m_undo.empty( ) )
        return;
    m_redo.push_back( m_last );
    m_last = m_undo.back( );
    m_undo.pop_back( );
    doc::from_json( m_last, m_doc );
    if ( !locate( m_selected ) )
        select( 0 );
    m_multi.erase( std::remove_if( m_multi.begin( ), m_multi.end( ), [&]( int uid ) { return !locate( uid ).element; } ), m_multi.end( ) );
}

void editor_c::redo( )
{
    if ( m_redo.empty( ) )
        return;
    m_undo.push_back( m_last );
    m_last = m_redo.back( );
    m_redo.pop_back( );
    doc::from_json( m_last, m_doc );
    if ( !locate( m_selected ) )
        select( 0 );
    m_multi.erase( std::remove_if( m_multi.begin( ), m_multi.end( ), [&]( int uid ) { return !locate( uid ).element; } ), m_multi.end( ) );
}

// Every finished change (nothing being dragged or typed) becomes one undo step.
void editor_c::track_history( )
{
    if ( ImGui::IsAnyItemActive( ) || ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
        return;
    const auto now = doc::to_json( m_doc );
    if ( now == m_last )
        return;
    m_undo.push_back( m_last );
    if ( m_undo.size( ) > 200 )
        m_undo.erase( m_undo.begin( ) );
    m_redo.clear( );
    m_last = now;
}

void editor_c::shortcuts( )
{
    const auto& io = ImGui::GetIO( );
    const bool typing = io.WantTextInput;
    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_Z, false ) && !typing )
        io.KeyShift ? redo( ) : undo( );
    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_Y, false ) && !typing )
        redo( );
    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_S, false ) )
        save( io.KeyShift );
    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_O, false ) )
        open( );
    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_E, false ) )
        export_code( );
    if ( typing )
        return;
    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_D, false ) && m_selected )
        duplicate_selection( );
    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_C, false ) )
        copy( false );
    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_X, false ) )
        copy( true );
    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_V, false ) )
        paste( );
    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_A, false ) && app( ) )
    {
        auto* list = shown_component( ) ? &shown_component( )->elements : shown_screen( ) ? &shown_screen( )->elements : nullptr;
        if ( list && !list->empty( ) )
        {
            m_multi.clear( );
            for ( const auto& e : *list )
                m_multi.push_back( e.uid );
            m_selected = m_multi.back( );
        }
    }
    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_0, false ) )
        m_fit = true;
    if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_1, false ) )
        m_zoom = 1.f;
    if ( io.KeyCtrl && ( ImGui::IsKeyPressed( ImGuiKey_Equal ) || ImGui::IsKeyPressed( ImGuiKey_KeypadAdd ) ) )
        m_zoom = std::min( 4.f, m_zoom * 1.25f );
    if ( io.KeyCtrl && ( ImGui::IsKeyPressed( ImGuiKey_Minus ) || ImGui::IsKeyPressed( ImGuiKey_KeypadSubtract ) ) )
        m_zoom = std::max( .2f, m_zoom / 1.25f );
    if ( ImGui::IsKeyPressed( ImGuiKey_Delete, false ) && m_selected )
        remove_selection( );
    if ( ImGui::IsKeyPressed( ImGuiKey_Escape, false ) && m_selected )
    {
        const auto at = locate( m_selected );
        select( at.element ? ( at.screen ? at.screen->uid : at.component->uid ) : 0 );
    }
    if ( ImGui::IsKeyPressed( ImGuiKey_Tab, false ) )
        m_design = !m_design;

    // Arrow keys nudge the selected elements by a pixel, or 8 with Shift.
    if ( app( ) && m_design && !io.KeyCtrl )
    {
        const float step = io.KeyShift ? 8.f : 1.f;
        const ImVec2 by( ImGui::IsKeyPressed( ImGuiKey_LeftArrow ) ? -step : ImGui::IsKeyPressed( ImGuiKey_RightArrow ) ? step : 0.f,
                         ImGui::IsKeyPressed( ImGuiKey_UpArrow ) ? -step : ImGui::IsKeyPressed( ImGuiKey_DownArrow ) ? step : 0.f );
        if ( by.x != 0 || by.y != 0 )
            for ( auto* e : selected_elements( ) )
                e->x += by.x, e->y += by.y;
    }
}

void editor_c::frame( )
{
    // A relative plugin path is relative to the project file.
    std::filesystem::path plugin = platform::widen( m_doc.theme.plugin );
    if ( !plugin.empty( ) && plugin.is_relative( ) && !m_path.empty( ) )
        plugin = std::filesystem::path( m_path ).parent_path( ) / plugin;
    plugins::use( platform::narrow( plugin.wstring( ) ) );
    const ImGuiViewport* viewport = ImGui::GetMainViewport( );
    m_size = viewport->Size;
    ImGui::SetNextWindowPos( viewport->Pos );
    ImGui::SetNextWindowSize( viewport->Size );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0, 0 } );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0 );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0, 0 } );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0 );
    ImGui::PushFont( kit::font( ), 13.f );
    ImGui::Begin( "##studio", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse );
    ImDrawList* draw = ImGui::GetWindowDrawList( );
    draw->AddRectFilled( viewport->Pos, viewport->Pos + viewport->Size, ed::base );

    shortcuts( );
    title_bar( );
    structure( );
    properties( );
    canvas( );
    if ( m_code_open )
        code_panel( );
    status_bar( );

    ImGui::End( );
    ImGui::PopFont( );
    ImGui::PopStyleVar( 4 );
    track_history( );
    autosave( );

    const bool dirty = m_last != m_saved;
    platform::set_title( platform::widen( ( dirty ? "\xe2\x80\xa2 " : "" ) + m_doc.name + " - ImGui Studio" ) );
}

void editor_c::new_menu( )
{
    push_menu_style( 300 );
    if ( ImGui::BeginPopup( "##new_menu" ) )
    {
        const icon_fn menu = []( ImDrawList* d, ImVec2 c, ImU32 col ) { node_icon( d, c, col, 0, nullptr ); };
        const icon_fn window = screen_icon;
        if ( menu_entry( "Example menu", "Tabs, a feature list and settings rows", menu ) )
            start( doc::starter( ), "New menu from the example." );
        if ( menu_entry( "Loader", "Sign in, products and a loading screen", window ) )
            start( doc::loader( ), "New loader. Drag elements to move them." );
        if ( menu_entry( "Blank menu", "One empty category", menu ) )
            start( doc::blank_menu( ), "New blank menu." );
        if ( menu_entry( "Blank app", "One empty screen, any size", window ) )
            start( doc::blank_app( ), "New blank app. Add elements with +." );
        ImGui::EndPopup( );
    }
    pop_menu_style( );
}

void editor_c::open_menu( )
{
    push_menu_style( 340 );
    if ( ImGui::BeginPopup( "##open_menu" ) )
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow( );
        kit::caps( w->DrawList, { w->DC.CursorPos.x + 10, w->DC.CursorPos.y + 12 }, kit::tone::muted, "Recent" );
        ImGui::ItemSize( { 1, 30 } );
        std::wstring chosen;
        for ( const auto& path : m_recent )
        {
            const std::filesystem::path p( path );
            const auto name = platform::narrow( p.filename( ).wstring( ) );
            const auto folder = platform::narrow( p.parent_path( ).wstring( ) );
            ImGui::PushID( platform::narrow( path ).c_str( ) );
            if ( menu_entry( name.c_str( ), folder.c_str( ), nullptr ) )
                chosen = path;
            ImGui::PopID( );
        }
        const ImVec2 at = w->DC.CursorPos;
        w->DrawList->AddLine( { at.x + 8, at.y + 4.5f }, { w->WorkRect.Max.x - 8, at.y + 4.5f }, kit::tone::white( 16 ) );
        ImGui::ItemSize( { 1, 9 } );
        const bool browse = menu_entry( "Browse\xe2\x80\xa6", nullptr, nullptr );
        ImGui::EndPopup( );
        if ( !chosen.empty( ) )
            open_path( chosen );
        if ( browse )
            open( );
    }
    pop_menu_style( );
}

void editor_c::title_bar( )
{
    ImDrawList* draw = ImGui::GetWindowDrawList( );
    const ImVec2 o = ImGui::GetWindowPos( );
    const float w = m_size.x;
    draw->AddRectFilled( o, o + ImVec2( w, ed::title ), ed::bar );
    draw->AddLine( o + ImVec2( 0, ed::title - .5f ), o + ImVec2( w, ed::title - .5f ), ed::line );

    // Wordmark and project name.
    draw->AddText( kit::font( ), 15.f, ImFloor( o + ImVec2( 18, 14 ) ), kit::tone::title, "ImGui Studio" );
    float x = 18 + text_size( "ImGui Studio", 15.f ) + 14;
    draw->AddLine( o + ImVec2( x, 15 ), o + ImVec2( x, 31 ), kit::tone::white( 26 ) );
    x += 14;
    {
        const bool dirty = m_last != m_saved;
        const std::string name = m_doc.name + ( dirty ? "  \xe2\x80\xa2" : "" );
        const ImRect bb( o + ImVec2( x - 6, 9 ), o + ImVec2( x + text_size( name.c_str( ) ) + 8, 37 ) );
        const ImGuiID id = ImGui::GetID( "##project_name" );
        ImGui::ItemAdd( bb, id );
        bool hovered = false, held = false;
        if ( ImGui::ButtonBehavior( bb, id, &hovered, &held ) )
            ImGui::OpenPopup( "##rename" );
        if ( hovered )
            draw->AddRectFilled( bb.Min, bb.Max, kit::tone::white( 10 ), 7 );
        label( draw, o + ImVec2( x, 16 ), hovered ? kit::tone::primary : kit::tone::secondary, name.c_str( ) );
        ImGui::SetNextWindowPos( { bb.Min.x, bb.Max.y + 6 } );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 14, 4 } );
        ImGui::PushStyleVar( ImGuiStyleVar_PopupRounding, 10 );
        ImGui::PushStyleColor( ImGuiCol_PopupBg, kit::tone::popup );
        ImGui::PushStyleColor( ImGuiCol_Border, kit::tone::window_border );
        if ( ImGui::BeginPopup( "##rename" ) )
        {
            ImGui::Dummy( { 300, 1 } );
            kit::begin_columns( 1 );
            edit_text( "Project name", m_doc.name );
            kit::end_columns( );
            ImGui::EndPopup( );
        }
        ImGui::PopStyleColor( 2 );
        ImGui::PopStyleVar( 2 );
    }

    // Mode switch in the middle: Design (rows select) or Interact (rows work).
    {
        const char* modes[] = { "Design", "Interact" };
        const float width = 196, height = 30;
        const ImRect bb( o + ImVec2( std::floor( ( w - width ) * .5f ), 8 ), o + ImVec2( std::floor( ( w + width ) * .5f ), 8 + height ) );
        draw->AddRectFilled( bb.Min, bb.Max, kit::tone::white( 8 ), 9 );
        draw->AddRect( bb.Min + ImVec2( .5f, .5f ), bb.Max - ImVec2( .5f, .5f ), kit::tone::white( 14 ), 9 );
        const float cell = ( width - 4 ) * .5f;
        auto* storage = ImGui::GetStateStorage( );
        const ImGuiID key = ImGui::GetID( "##mode_pill" );
        const float target = m_design ? 0.f : cell;
        const float pill = ImLerp( storage->GetFloat( key, target ), target, kit::approach( 20.f ) );
        storage->SetFloat( key, pill );
        draw->AddRectFilled( { bb.Min.x + 2 + pill, bb.Min.y + 2 }, { bb.Min.x + 2 + pill + cell, bb.Max.y - 2 }, kit::tone::white( 26 ), 7 );
        for ( int i = 0; i < 2; ++i )
        {
            const ImRect c( { bb.Min.x + 2 + cell * static_cast<float>( i ), bb.Min.y }, { bb.Min.x + 2 + cell * static_cast<float>( i + 1 ), bb.Max.y } );
            const ImGuiID id = ImGui::GetID( modes[i] );
            ImGui::ItemAdd( c, id );
            bool hovered = false, held = false;
            if ( ImGui::ButtonBehavior( c, id, &hovered, &held ) )
                m_design = i == 0;
            const bool active = m_design == ( i == 0 );
            label( draw, { std::floor( c.GetCenter( ).x - text_size( modes[i], 12.5f ) * .5f ), c.Min.y + 8 },
                   active ? kit::tone::title : hovered ? kit::tone::label : kit::tone::muted, modes[i], 12.5f );
        }
        if ( ImGui::IsMouseHoveringRect( bb.Min, bb.Max ) )
            ImGui::SetTooltip( "Tab switches modes" );
    }

    // Right side: history, file actions, window controls.
    float r = w - 46 * 3;
    const struct
    {
        const char* id;
        int kind;
    } controls[] = { { "##min", 0 }, { "##max", 1 }, { "##close", 2 } };
    for ( int i = 0; i < 3; ++i )
    {
        const ImRect bb( o + ImVec2( r + 46.f * static_cast<float>( i ), 0 ), o + ImVec2( r + 46.f * static_cast<float>( i + 1 ), ed::title - 1 ) );
        const ImGuiID id = ImGui::GetID( controls[i].id );
        ImGui::ItemAdd( bb, id );
        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );
        if ( hovered )
            draw->AddRectFilled( bb.Min, bb.Max, controls[i].kind == 2 ? IM_COL32( 196, 43, 28, 255 ) : kit::tone::white( 12 ) );
        const ImVec2 c = bb.GetCenter( );
        const ImU32 ink = hovered ? kit::tone::title : kit::tone::label;
        if ( controls[i].kind == 0 )
            draw->AddLine( c + ImVec2( -5, 0 ), c + ImVec2( 5, 0 ), ink, 1.f );
        else if ( controls[i].kind == 1 )
        {
            if ( platform::maximized( ) )
            {
                draw->AddRect( c + ImVec2( -5, -3 ), c + ImVec2( 3, 5 ), ink, 1.5f, 0, 1.f );
                draw->AddLine( c + ImVec2( -3, -5 ), c + ImVec2( 5, -5 ), ink, 1.f );
                draw->AddLine( c + ImVec2( 5, -5 ), c + ImVec2( 5, 3 ), ink, 1.f );
            }
            else
                draw->AddRect( c + ImVec2( -5, -5 ), c + ImVec2( 5, 5 ), ink, 1.5f, 0, 1.f );
        }
        else
        {
            draw->AddLine( c + ImVec2( -5, -5 ), c + ImVec2( 5, 5 ), ink, 1.f );
            draw->AddLine( c + ImVec2( 5, -5 ), c + ImVec2( -5, 5 ), ink, 1.f );
        }
        if ( pressed )
            controls[i].kind == 0 ? platform::minimize( ) : controls[i].kind == 1 ? platform::toggle_maximize( ) : platform::close( );
    }
    r -= 14;
    const float export_w = kit::action_width( "Export" );
    r -= export_w;
    ImGui::PushID( "##titlebar" );
    if ( kit::action_button( "Export", o + ImVec2( r, 8 ), true, 30 ) )
        export_code( );
    r -= 8 + kit::action_width( "Save" );
    if ( kit::action_button( "Save", o + ImVec2( r, 8 ), false, 30 ) )
        save( false );
    r -= 8 + kit::action_width( "Open" );
    if ( kit::action_button( "Open", o + ImVec2( r, 8 ), false, 30 ) )
    {
        if ( m_recent.empty( ) )
            open( );
        else
            ImGui::OpenPopup( "##open_menu" );
    }
    ImGui::SetNextWindowPos( o + ImVec2( r, 44 ) );
    open_menu( );
    r -= 8 + kit::action_width( "New" );
    if ( kit::action_button( "New", o + ImVec2( r, 8 ), false, 30 ) )
        ImGui::OpenPopup( "##new_menu" );
    ImGui::SetNextWindowPos( o + ImVec2( r, 44 ) );
    new_menu( );
    ImGui::PopID( );
    r -= 14;
    draw->AddLine( o + ImVec2( r, 15 ), o + ImVec2( r, 31 ), kit::tone::white( 26 ) );
    r -= 8 + 30;
    {
        const auto [pressed, hovered] = icon_button( "##redo", o + ImVec2( r, 8 ), { 30, 30 }, !m_redo.empty( ) );
        icon_redo( draw, o + ImVec2( r + 15, 22 ), m_redo.empty( ) ? kit::tone::white( 50 ) : hovered ? kit::tone::title : kit::tone::label );
        if ( pressed )
            redo( );
        if ( hovered )
            ImGui::SetTooltip( "Redo  (Ctrl Y)" );
    }
    r -= 4 + 30;
    {
        const auto [pressed, hovered] = icon_button( "##undo", o + ImVec2( r, 8 ), { 30, 30 }, !m_undo.empty( ) );
        icon_undo( draw, o + ImVec2( r + 15, 22 ), m_undo.empty( ) ? kit::tone::white( 50 ) : hovered ? kit::tone::title : kit::tone::label );
        if ( pressed )
            undo( );
        if ( hovered )
            ImGui::SetTooltip( "Undo  (Ctrl Z)" );
    }

    // An empty spot in the bar drags the window; a double-click maximises it.
    const bool over_bar = ImGui::IsMouseHoveringRect( o, o + ImVec2( w, ed::title ) ) && !ImGui::IsAnyItemHovered( ) &&
                          !ImGui::IsPopupOpen( "", ImGuiPopupFlags_AnyPopupId );
    if ( over_bar && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
        platform::toggle_maximize( );
    else if ( over_bar && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
        platform::drag_window( );
}

void editor_c::structure( )
{
    ImDrawList* draw = ImGui::GetWindowDrawList( );
    const ImVec2 o = ImGui::GetWindowPos( ) + ImVec2( 0, ed::title );
    const float height = m_size.y - ed::title - ed::status;
    draw->AddRectFilled( o, o + ImVec2( ed::left, height ), ed::panel );
    draw->AddLine( o + ImVec2( ed::left - .5f, 0 ), o + ImVec2( ed::left - .5f, height ), ed::line );

    label( draw, o + ImVec2( 18, 16 ), kit::tone::title, app( ) ? "Screens" : "Structure", 13.5f );
    {
        const auto [pressed, hovered] = icon_button( "##add", o + ImVec2( ed::left - 44, 9 ), { 30, 30 } );
        icon_plus( draw, o + ImVec2( ed::left - 29, 24 ), hovered ? kit::tone::title : kit::tone::label );
        if ( hovered )
            ImGui::SetTooltip( "Add" );
        if ( pressed )
            ImGui::OpenPopup( "##add_menu" );
        add_menu( );
    }
    draw->AddLine( o + ImVec2( 0, 47.5f ), o + ImVec2( ed::left - 1, 47.5f ), ed::line );

    ImGui::SetCursorScreenPos( o + ImVec2( 0, 48 ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 8, 8 } );
    ImGui::BeginChild( "##tree", { ed::left - 1, height - 48 }, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground );
    ImGui::PopStyleVar( );
    tree( );
    ImGui::EndChild( );
}

void editor_c::add_menu( )
{
    push_menu_style( 236 );
    if ( ImGui::BeginPopup( "##add_menu" ) )
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow( );
        const auto entry = [&]( const char* name, int what, const icon_fn& icon ) {
            const ImVec2 at = w->DC.CursorPos;
            const ImRect bb( at, { w->WorkRect.Max.x, at.y + 30 } );
            ImGui::ItemSize( bb.GetSize( ) );
            const ImGuiID id = w->GetID( name );
            ImGui::ItemAdd( bb, id );
            bool hovered = false, held = false;
            if ( ImGui::ButtonBehavior( bb, id, &hovered, &held ) )
            {
                add( what );
                ImGui::CloseCurrentPopup( );
            }
            if ( hovered )
                w->DrawList->AddRectFilled( bb.Min, bb.Max, kit::tone::white( 14 ), 7 );
            icon( w->DrawList, { bb.Min.x + 16, bb.GetCenter( ).y }, hovered ? kit::tone::title : kit::tone::secondary );
            label( w->DrawList, { bb.Min.x + 34, bb.Min.y + 8 }, hovered ? kit::tone::title : kit::tone::label, name );
        };
        const auto heading = [&]( const char* text ) {
            const ImVec2 at = w->DC.CursorPos;
            kit::caps( w->DrawList, { at.x + 10, at.y + 12 }, kit::tone::muted, text );
            ImGui::ItemSize( { 1, 30 } );
        };
        if ( app( ) )
        {
            heading( "Layout" );
            entry( "Screen", 100, screen_icon );
            entry( "Component", 101, component_icon );
            heading( "Elements" );
            for ( int i = 0; i < static_cast<int>( kind::count ); ++i )
            {
                if ( static_cast<kind>( i ) == kind::instance && m_doc.components.empty( ) )
                    continue;
                entry( static_cast<kind>( i ) == kind::instance ? "Component instance" : element_kinds[i], 200 + i,
                       [i]( ImDrawList* d, ImVec2 c, ImU32 col ) { element_icon( d, c, col, static_cast<kind>( i ) ); } );
            }
        }
        else
        {
            heading( "Layout" );
            const char* layout[] = { "Category", "Group", "Feature", "Section" };
            for ( int i = 0; i < 4; ++i )
                entry( layout[i], i, [i]( ImDrawList* d, ImVec2 c, ImU32 col ) { node_icon( d, c, col, i, nullptr ); } );
            heading( "Rows" );
            for ( int i = 0; i < static_cast<int>( doc::kind_t::count ); ++i )
                entry( widget_kinds[i], 4 + i, [i]( ImDrawList* d, ImVec2 c, ImU32 col ) {
                    doc::widget_t sample;
                    sample.kind = static_cast<doc::kind_t>( i );
                    sample.rgba[0] = .3f, sample.rgba[1] = .49f, sample.rgba[2] = .94f;
                    node_icon( d, c, col, 4, &sample );
                } );
        }
        ImGui::EndPopup( );
    }
    pop_menu_style( );
}

void editor_c::tree( )
{
    ImGuiWindow* window = ImGui::GetCurrentWindow( );
    ImDrawList* draw = window->DrawList;
    const float width = window->WorkRect.Max.x - window->DC.CursorPos.x;
    int context_uid = 0;

    const auto row = [&]( int uid, int depth, const std::string& text, const char* detail, bool expandable, const icon_fn& icon ) -> bool {
        const ImVec2 at = window->DC.CursorPos;
        const ImRect bb( at, { at.x + width, at.y + 30 } );
        ImGui::ItemSize( bb.GetSize( ) );
        ImGui::PushID( uid );
        const ImGuiID id = window->GetID( "##node" );
        ImGui::ItemAdd( bb, id );
        bool hovered = false, held = false;
        if ( ImGui::ButtonBehavior( bb, id, &hovered, &held, ImGuiButtonFlags_PressedOnClick ) )
            select( uid, ImGui::GetIO( ).KeyShift );
        if ( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) && uid )
        {
            if ( !multi( uid ) )
                select( uid );
            context_uid = uid;
        }
        // Drag to reorder.
        if ( uid && ImGui::BeginDragDropSource( ImGuiDragDropFlags_SourceNoPreviewTooltip ) )
        {
            ImGui::SetDragDropPayload( "studio_node", &uid, sizeof( uid ) );
            ImGui::EndDragDropSource( );
        }
        bool dropping = false;
        if ( uid && ImGui::BeginDragDropTarget( ) )
        {
            dropping = true;
            if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "studio_node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect ) )
                drop( *static_cast<const int*>( payload->Data ), uid );
            ImGui::EndDragDropTarget( );
        }
        const bool selected = m_selected == uid || ( uid && multi( uid ) );
        if ( selected )
            draw->AddRectFilled( bb.Min, bb.Max, kit::tone::accent( m_selected == uid ? 46 : 28 ), 7 );
        else if ( hovered )
            draw->AddRectFilled( bb.Min, bb.Max, kit::tone::white( 9 ), 7 );
        if ( dropping )
            draw->AddLine( { bb.Min.x + 8, bb.Min.y + .5f }, { bb.Max.x - 8, bb.Min.y + .5f }, kit::tone::accent( ), 2.f );
        if ( uid && m_hovered == uid && !selected && !hovered )
            draw->AddRect( bb.Min + ImVec2( .5f, .5f ), bb.Max - ImVec2( .5f, .5f ), kit::tone::accent( 90 ), 7 );

        const float x = bb.Min.x + 8 + static_cast<float>( std::max( 0, depth ) ) * 14;
        bool open = !m_collapsed.count( uid );
        if ( expandable )
        {
            const ImRect toggle( { x - 2, bb.Min.y + 7 }, { x + 14, bb.Min.y + 23 } );
            const ImGuiID tid = window->GetID( "##fold" );
            ImGui::SetNextItemAllowOverlap( );
            ImGui::ItemAdd( toggle, tid );
            bool th = false, tl = false;
            if ( ImGui::ButtonBehavior( toggle, tid, &th, &tl ) )
            {
                if ( open )
                    m_collapsed.insert( uid );
                else
                    m_collapsed.erase( uid );
                open = !open;
            }
            icon_chevron( draw, { x + 6, bb.GetCenter( ).y }, th ? kit::tone::title : kit::tone::muted, open );
        }
        const ImU32 ink = selected ? kit::tone::title : hovered ? kit::tone::primary : kit::tone::label;
        if ( icon )
            icon( draw, { x + 24, bb.GetCenter( ).y }, selected ? kit::tone::title : kit::tone::secondary );
        const float text_x = icon ? x + 38 : x + 4;
        const float detail_w = detail ? text_size( detail, 11.5f ) + 10 : 0;
        draw->PushClipRect( bb.Min, { bb.Max.x - detail_w - 4, bb.Max.y }, true );
        label( draw, { text_x, bb.Min.y + 8 }, ink, text.empty( ) ? "Untitled" : text.c_str( ) );
        draw->PopClipRect( );
        if ( detail )
            label( draw, { bb.Max.x - detail_w, bb.Min.y + 9 }, kit::tone::muted, detail, 11.5f );
        ImGui::PopID( );
        return open;
    };
    const auto depth_icon = []( int depth, const doc::widget_t* w ) -> icon_fn {
        return [depth, w]( ImDrawList* d, ImVec2 c, ImU32 col ) { node_icon( d, c, col, depth, w ); };
    };
    const auto element_rows = [&]( std::vector<doc::element_t>& elements ) {
        // Front-most first, like layers.
        for ( auto e = elements.rbegin( ); e != elements.rend( ); ++e )
        {
            const auto k = e->kind;
            row( e->uid, 1, e->text, doc::element_name( k ), false, [k]( ImDrawList* d, ImVec2 c, ImU32 col ) { element_icon( d, c, col, k ); } );
        }
    };
    const auto heading = [&]( const char* text ) {
        const ImVec2 at = window->DC.CursorPos;
        kit::caps( draw, { at.x + 8, at.y + 14 }, kit::tone::muted, text );
        ImGui::ItemSize( { 1, 34 } );
    };

    row( 0, -1, "Project settings", nullptr, false, nullptr );
    ImGui::Dummy( { 1, 6 } );
    if ( app( ) )
    {
        for ( size_t i = 0; i < m_doc.screens.size( ); ++i )
        {
            auto& s = m_doc.screens[i];
            if ( row( s.uid, 0, s.name, i == 0 ? "start" : nullptr, !s.elements.empty( ), screen_icon ) )
                element_rows( s.elements );
        }
        heading( "Components" );
        if ( m_doc.components.empty( ) )
        {
            const ImVec2 at = window->DC.CursorPos;
            label( draw, { at.x + 8, at.y + 2 }, kit::tone::muted, "Select elements, then Make component.", 12 );
            ImGui::ItemSize( { 1, 22 } );
        }
        for ( auto& c : m_doc.components )
        {
            char size[32];
            std::snprintf( size, sizeof( size ), "%.0f \xc3\x97 %.0f", c.w, c.h );
            if ( row( c.uid, 0, c.name, size, !c.elements.empty( ), component_icon ) )
                element_rows( c.elements );
        }
    }
    else
        for ( auto& c : m_doc.categories )
        {
            if ( !row( c.uid, 0, c.name, nullptr, !c.groups.empty( ), depth_icon( 0, nullptr ) ) )
                continue;
            for ( auto& g : c.groups )
            {
                if ( !row( g.uid, 1, g.title, nullptr, !g.features.empty( ), depth_icon( 1, nullptr ) ) )
                    continue;
                for ( auto& f : g.features )
                {
                    if ( !row( f.uid, 2, f.name, nullptr, !f.sections.empty( ), depth_icon( 2, nullptr ) ) )
                        continue;
                    for ( auto& s : f.sections )
                    {
                        if ( !row( s.uid, 3, s.title, s.column ? "right" : nullptr, !s.widgets.empty( ), depth_icon( 3, nullptr ) ) )
                            continue;
                        for ( auto& w : s.widgets )
                        {
                            const std::string name = w.kind == doc::kind_t::note ? "Note" : w.label;
                            row( w.uid, 4, name, doc::kind_name( w.kind ), false, depth_icon( 4, &w ) );
                        }
                    }
                }
            }
        }
    ImGui::Dummy( { 1, 12 } );

    if ( context_uid )
        ImGui::OpenPopup( "##node_menu" );
    ImGui::SetNextWindowSize( { 220, 0 } );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 6, 6 } );
    ImGui::PushStyleVar( ImGuiStyleVar_PopupRounding, 10 );
    ImGui::PushStyleColor( ImGuiCol_PopupBg, kit::tone::popup );
    ImGui::PushStyleColor( ImGuiCol_Border, kit::tone::window_border );
    if ( ImGui::BeginPopup( "##node_menu" ) )
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow( );
        const auto entry = [&]( const char* name, const char* shortcut ) {
            const ImVec2 at = w->DC.CursorPos;
            const ImRect bb( at, { w->WorkRect.Max.x, at.y + 30 } );
            ImGui::ItemSize( bb.GetSize( ) );
            const ImGuiID id = w->GetID( name );
            ImGui::ItemAdd( bb, id );
            bool hovered = false, held = false;
            const bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );
            if ( hovered )
                w->DrawList->AddRectFilled( bb.Min, bb.Max, kit::tone::white( 14 ), 7 );
            label( w->DrawList, { bb.Min.x + 12, bb.Min.y + 8 }, hovered ? kit::tone::title : kit::tone::label, name );
            if ( shortcut )
                label( w->DrawList, { bb.Max.x - 12 - text_size( shortcut, 11.5f ), bb.Min.y + 9 }, kit::tone::muted, shortcut, 11.5f );
            return pressed;
        };
        // Elements are listed front-most first, so "up" in the tree is forward in the drawing order.
        const auto at = locate( m_selected );
        const bool element = at.element != nullptr;
        if ( entry( element ? "Bring forward" : "Move up", nullptr ) )
            move( m_selected, element ? 1 : -1 );
        if ( entry( element ? "Send backward" : "Move down", nullptr ) )
            move( m_selected, element ? -1 : 1 );
        if ( entry( "Copy", "Ctrl C" ) )
            copy( false );
        if ( entry( "Paste", "Ctrl V" ) )
            paste( );
        if ( entry( "Duplicate", "Ctrl D" ) )
            duplicate_selection( );
        if ( element && at.screen && entry( "Make component", nullptr ) )
            make_component( );
        if ( element && at.element->kind == kind::instance && entry( "Detach from component", nullptr ) )
            detach( m_selected );
        if ( entry( "Delete", "Del" ) )
            remove_selection( );
        ImGui::EndPopup( );
    }
    ImGui::PopStyleColor( 2 );
    ImGui::PopStyleVar( 2 );
}

// Draws `body` at unscaled positions around view.origin, then scales everything it drew (and every child window it
// opened) about that origin. While it runs, the mouse is mapped back so hovering and clicking land where things show;
// popups get the real mouse and open at the scaled position.
void editor_c::scaled( const view_t& view, const ImRect& clip, const std::function<void( )>& body )
{
    ImGuiContext& g = *GImGui;
    ImGuiIO& io = g.IO;
    ImGuiWindow* canvas = g.CurrentWindow;
    ImDrawList* list = canvas->DrawList;

    struct mouse_t
    {
        ImVec2 pos, prev, delta, clicked[5];
        ImGuiWindow* hovered;
    } real{ io.MousePos, io.MousePosPrev, io.MouseDelta, { }, g.HoveredWindow };
    for ( int i = 0; i < 5; ++i )
        real.clicked[i] = io.MouseClickedPos[i];
    const bool canvas_hovered = real.hovered && is_child_of( real.hovered, canvas );
    const auto map = [&]( ImVec2 p ) { return ImGui::IsMousePosValid( &p ) ? view.to_doc( p ) : p; };
    const auto enter = [&]( ) {
        io.MousePos = map( real.pos );
        io.MousePosPrev = map( real.prev );
        io.MouseDelta = real.delta / view.scale;
        for ( int i = 0; i < 5; ++i )
            io.MouseClickedPos[i] = map( real.clicked[i] );
        if ( canvas_hovered )
        {
            // The window under the mapped mouse: the canvas or one of the preview's child windows.
            ImGuiWindow* found = nullptr;
            ImGuiWindow* under = nullptr;
            ImGui::FindHoveredWindowEx( io.MousePos, false, &found, &under );
            g.HoveredWindow = found && is_child_of( found, canvas ) ? found : canvas;
        }
    };
    const auto leave = [&]( ) {
        io.MousePos = real.pos;
        io.MousePosPrev = real.prev;
        io.MouseDelta = real.delta;
        for ( int i = 0; i < 5; ++i )
            io.MouseClickedPos[i] = real.clicked[i];
        g.HoveredWindow = real.hovered;
    };

    kit::to_screen = [view]( ImVec2 p ) { return view.to_screen( p ); };
    kit::popup_mouse = [&]( bool screen ) { screen ? leave( ) : enter( ); };
    ImGui::PushClipRect( { -1e6f, -1e6f }, { 1e6f, 1e6f }, false ); // nothing is culled at unscaled positions, child windows included
    const int first_vertex = list->VtxBuffer.Size;
    const int first_command = list->CmdBuffer.Size - 1;
    enter( );
    body( );
    leave( );
    kit::to_screen = nullptr;
    kit::popup_mouse = nullptr;

    const auto transform = [&]( ImDrawList* d, int vertex, int command ) {
        for ( int i = vertex; i < d->VtxBuffer.Size; ++i )
            d->VtxBuffer[i].pos = view.to_screen( d->VtxBuffer[i].pos );
        for ( int i = std::max( 0, command ); i < d->CmdBuffer.Size; ++i )
        {
            ImVec4& c = d->CmdBuffer[i].ClipRect;
            const ImVec2 a = view.to_screen( { c.x, c.y } ), b = view.to_screen( { c.z, c.w } );
            c = { std::max( a.x, clip.Min.x ), std::max( a.y, clip.Min.y ), std::min( b.x, clip.Max.x ), std::min( b.y, clip.Max.y ) };
        }
    };
    transform( list, first_vertex, first_command );
    ImGui::PopClipRect( );
    for ( ImGuiWindow* w : g.Windows )
        if ( w != canvas && w->Active && w->LastFrameActive == g.FrameCount && ( w->Flags & ImGuiWindowFlags_ChildWindow ) && is_child_of( w, canvas ) )
            transform( w->DrawList, 0, 0 );
}

void editor_c::canvas( )
{
    const ImVec2 o = ImGui::GetWindowPos( ) + ImVec2( ed::left, ed::title );
    const float width = m_size.x - ed::left - ed::right;
    const float height = m_size.y - ed::title - ed::status - ( m_code_open ? ed::code : 0 );
    ImGui::GetWindowDrawList( )->AddRectFilled( o, o + ImVec2( width, height ), ed::canvas );
    if ( app( ) )
        canvas_app( o, width, height );
    else
        canvas_menu( o, width, height );
}

// Zoom buttons in the canvas's bottom-right corner; wheel scrolling, Ctrl+wheel zoom and middle-drag panning.
void editor_c::zoom_controls( ImVec2 o, float width, float height, ImVec2 surface )
{
    ImGuiWindow* w = ImGui::GetCurrentWindow( );
    ImDrawList* draw = w->DrawList;
    const auto& io = ImGui::GetIO( );
    const ImVec2 corner = w->Pos + ImVec2( width - 16, height - 16 );
    char percent[16];
    std::snprintf( percent, sizeof( percent ), "%.0f%%", m_zoom * 100.f );
    const float pw = 52;
    const ImRect bar( corner - ImVec2( 28 + pw + 28 + 4 + 44 + 4, 32 ), corner );
    draw->AddRectFilled( bar.Min, bar.Max, IM_COL32( 22, 22, 26, 240 ), 9 );
    draw->AddRect( bar.Min + ImVec2( .5f, .5f ), bar.Max - ImVec2( .5f, .5f ), kit::tone::white( 18 ), 9 );
    float x = bar.Min.x + 2;
    const auto [out, out_hover] = icon_button( "##zoom_out", { x, bar.Min.y + 2 }, { 28, 28 } );
    draw->AddLine( { x + 9, bar.Min.y + 16 }, { x + 19, bar.Min.y + 16 }, out_hover ? kit::tone::title : kit::tone::label, 1.5f );
    x += 28;
    const auto [reset, reset_hover] = icon_button( "##zoom_reset", { x, bar.Min.y + 2 }, { pw, 28 } );
    label( draw, { std::floor( x + ( pw - text_size( percent, 12 ) ) * .5f ), bar.Min.y + 9 }, reset_hover ? kit::tone::title : kit::tone::label, percent, 12 );
    x += pw;
    const auto [in, in_hover] = icon_button( "##zoom_in", { x, bar.Min.y + 2 }, { 28, 28 } );
    icon_plus( draw, { x + 14, bar.Min.y + 16 }, in_hover ? kit::tone::title : kit::tone::label );
    x += 28 + 4;
    const auto [fit, fit_hover] = icon_button( "##zoom_fit", { x, bar.Min.y + 2 }, { 44, 28 } );
    label( draw, { x + 13, bar.Min.y + 9 }, fit_hover ? kit::tone::title : kit::tone::label, "Fit", 12 );
    if ( out )
        m_zoom = std::max( .2f, m_zoom / 1.25f );
    if ( in )
        m_zoom = std::min( 4.f, m_zoom * 1.25f );
    if ( reset )
        m_zoom = 1.f;
    if ( fit )
        m_fit = true;
    if ( reset_hover )
        ImGui::SetTooltip( "100%%  (Ctrl 1)" );
    if ( fit_hover )
        ImGui::SetTooltip( "Fit the window  (Ctrl 0)" );

    if ( m_fit )
    {
        m_fit = false;
        const float fx = ( width - 96 ) / std::max( 1.f, surface.x ), fy = ( height - 128 ) / std::max( 1.f, surface.y );
        m_zoom = std::clamp( std::min( fx, fy ), .2f, 1.f );
        ImGui::SetScrollX( 0 );
        ImGui::SetScrollY( 0 );
    }

    // Wheel: scroll, Shift sideways, Ctrl zooms about the mouse.
    const bool hovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) && !ImGui::IsPopupOpen( "", ImGuiPopupFlags_AnyPopupId );
    if ( hovered && io.MouseWheel != 0 )
    {
        if ( io.KeyCtrl )
        {
            const float before = m_zoom;
            m_zoom = std::clamp( m_zoom * std::pow( 1.15f, io.MouseWheel ), .2f, 4.f );
            const ImVec2 mouse = io.MousePos - w->Pos + w->Scroll;
            ImGui::SetScrollX( mouse.x * m_zoom / before - ( io.MousePos.x - w->Pos.x ) );
            ImGui::SetScrollY( mouse.y * m_zoom / before - ( io.MousePos.y - w->Pos.y ) );
        }
        else if ( io.KeyShift )
            ImGui::SetScrollX( w->Scroll.x - io.MouseWheel * 60 );
        else
            ImGui::SetScrollY( w->Scroll.y - io.MouseWheel * 60 );
    }
    if ( hovered && io.MouseWheelH != 0 )
        ImGui::SetScrollX( w->Scroll.x - io.MouseWheelH * 60 );
    // Middle-drag, or Space + drag, pans.
    if ( ImGui::IsMouseDragging( ImGuiMouseButton_Middle ) ||
         ( ImGui::IsKeyDown( ImGuiKey_Space ) && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) && !io.WantTextInput ) )
    {
        ImGui::SetScrollX( w->Scroll.x - io.MouseDelta.x );
        ImGui::SetScrollY( w->Scroll.y - io.MouseDelta.y );
        ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeAll );
    }
    (void)o;
}

void editor_c::recover_banner( ImVec2 o, float width )
{
    if ( m_recovered.empty( ) )
        return;
    ImGuiWindow* w = ImGui::GetCurrentWindow( );
    ImDrawList* draw = w->DrawList;
    const char* text = "Unsaved work from your last session";
    const float bw = text_size( text ) + kit::action_width( "Restore" ) + kit::action_width( "Discard" ) + 60;
    const ImRect bb( { std::floor( w->Pos.x + ( width - bw ) * .5f ), w->Pos.y + 56 }, { std::floor( w->Pos.x + ( width + bw ) * .5f ), w->Pos.y + 100 } );
    draw->AddRectFilled( bb.Min, bb.Max, IM_COL32( 28, 28, 33, 250 ), 12 );
    draw->AddRect( bb.Min + ImVec2( .5f, .5f ), bb.Max - ImVec2( .5f, .5f ), kit::tone::accent( 120 ), 12 );
    draw->AddCircleFilled( { bb.Min.x + 20, bb.GetCenter( ).y }, 3.5f, kit::tone::warning, 12 );
    label( draw, { bb.Min.x + 32, bb.Min.y + 15 }, kit::tone::title, text );
    ImGui::PushID( "##recover" );
    float x = bb.Max.x - 8 - kit::action_width( "Discard" );
    const bool discard = kit::action_button( "Discard", { x, bb.Min.y + 8 }, false, 28 );
    x -= 8 + kit::action_width( "Restore" );
    const bool restore = kit::action_button( "Restore", { x, bb.Min.y + 8 }, true, 28 );
    ImGui::PopID( );
    if ( restore )
    {
        doc::document_t d;
        if ( doc::from_json( m_recovered, d ) )
        {
            start( std::move( d ), "Restored. Save it to keep it." );
            m_saved.clear( ); // unsaved until you save it
        }
        m_recovered.clear( );
    }
    if ( discard )
    {
        m_recovered.clear( );
        erase_file( ( platform::app_data( ) + L"\\autosave.imstudio" ) );
    }
    (void)o;
}

void editor_c::canvas_menu( ImVec2 o, float width, float height )
{
    const ImVec2 menu = { m_doc.theme.width, m_doc.theme.height };
    const ImVec2 shown = menu * m_zoom;
    const ImVec2 content( std::max( width, shown.x + 96 ), std::max( height, shown.y + 96 ) );
    ImGui::SetCursorScreenPos( o );
    ImGui::SetNextWindowContentSize( content );
    ImGui::BeginChild( "##canvas", { width, height }, ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    ImGuiWindow* w = ImGui::GetCurrentWindow( );
    ImDrawList* draw = w->DrawList;
    const ImRect visible( w->Pos, w->Pos + ImVec2( width, height ) );

    // Dot grid, fixed to the canvas so it scrolls with the menu.
    const float step = 24;
    for ( float y = std::fmod( -w->Scroll.y, step ); y < height; y += step )
        for ( float x = std::fmod( -w->Scroll.x, step ); x < width; x += step )
            draw->AddRectFilled( w->Pos + ImVec2( x, y ), w->Pos + ImVec2( x + 1.5f, y + 1.5f ), ed::dot );

    const ImVec2 origin = w->Pos - w->Scroll;
    const ImVec2 at = ImFloor( origin + ImVec2( std::max( 48.f, ( content.x - shown.x ) * .5f ), std::max( 48.f, ( content.y - shown.y ) * .5f ) ) );
    for ( int i = 1; i <= 4; ++i )
        draw->AddRectFilled( at + ImVec2( -2.f * i, 4.f * i ), at + shown + ImVec2( 2.f * i, 6.f * i ), IM_COL32( 0, 0, 0, 14 ),
                             m_doc.theme.rounding * m_zoom + 2.f * i );

    const view_t view{ at, m_zoom };
    ImGui::SetCursorScreenPos( at );
    {
        preview::scoped_theme_t keep;
        m_shell.inert = m_design;
        scaled( view, visible, [&] { preview::draw( m_doc, m_shell, m_hits ); } );
    }
    for ( auto& hit : m_hits )
        hit.rect = ImRect( view.to_screen( hit.rect.Min ), view.to_screen( hit.rect.Max ) );

    // Follow the shell: clicking a feature in the preview selects it here.
    const int shown_feature = preview::shown_feature( m_doc, m_shell );
    if ( shown_feature && shown_feature != m_last_shown )
    {
        m_last_shown = shown_feature;
        const auto sel = locate( m_selected );
        if ( !sel.feature || sel.feature->uid != shown_feature )
            m_selected = shown_feature;
    }

    recover_banner( o, width );
    zoom_controls( o, width, height, menu );

    // The window's size: drag its right or bottom edge in Design mode.
    const auto& io = ImGui::GetIO( );
    const ImRect surface( at, at + shown );
    const bool inside = ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem ) &&
                        !ImGui::IsPopupOpen( "", ImGuiPopupFlags_AnyPopupId ) && !ImGui::IsAnyItemHovered( );
    int edge = -1;
    if ( m_design && inside && m_drag.kind == drag_t::kind_t::none )
    {
        const ImVec2 m = io.MousePos;
        const bool near_right = std::fabs( m.x - surface.Max.x ) < 6 && m.y > surface.Min.y && m.y < surface.Max.y + 6;
        const bool near_bottom = std::fabs( m.y - surface.Max.y ) < 6 && m.x > surface.Min.x && m.x < surface.Max.x + 6;
        edge = near_right && near_bottom ? 2 : near_right ? 0 : near_bottom ? 1 : -1;
        if ( edge >= 0 )
        {
            ImGui::SetMouseCursor( edge == 2 ? ImGuiMouseCursor_ResizeNWSE : edge == 0 ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS );
            if ( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
                m_drag = { drag_t::kind_t::window, 0, edge, m, { menu.x, menu.y, 0, 0 }, { } };
        }
    }
    if ( m_drag.kind == drag_t::kind_t::window )
    {
        if ( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
            m_drag = { };
        else
        {
            const ImVec2 delta = ( io.MousePos - m_drag.mouse ) / m_zoom;
            const float nw = m_drag.handle != 1 ? std::clamp( std::round( m_drag.start.x + delta.x ), 760.f, 1600.f ) : m_drag.start.x;
            const float nh = m_drag.handle != 0 ? std::clamp( std::round( m_drag.start.y + delta.y ), 480.f, 1000.f ) : m_drag.start.y;
            resize_window( nw, nh, { }, { } );
            ImGui::SetMouseCursor( m_drag.handle == 2 ? ImGuiMouseCursor_ResizeNWSE : m_drag.handle == 0 ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS );
        }
    }

    // Rows: hover outline, selection outline with its name, click to select in Design mode.
    m_hovered = 0;
    const ImVec2 mouse = io.MousePos;
    const preview::hit_t* over = nullptr;
    if ( inside && edge < 0 && m_drag.kind == drag_t::kind_t::none )
        for ( const auto& hit : m_hits )
            if ( hit.rect.Contains( mouse ) )
                over = &hit;
    ImDrawList* overlay = ImGui::GetForegroundDrawList( );
    overlay->PushClipRect( o, o + ImVec2( width, height ), true );
    if ( m_drag.kind == drag_t::kind_t::window )
        size_pill( overlay, { surface.GetCenter( ).x, surface.Max.y + 10 }, m_doc.theme.width, m_doc.theme.height, kit::tone::accent( ) );
    if ( over )
    {
        m_hovered = over->uid;
        if ( m_design && over->uid != m_selected )
            overlay->AddRect( over->rect.Min + ImVec2( -8, 1 ), over->rect.Max + ImVec2( 8, 0 ), kit::tone::accent( 120 ), 7, 0, 1.f );
        if ( m_design && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
            m_selected = over->uid;
    }
    for ( const auto& hit : m_hits )
        if ( hit.uid == m_selected )
        {
            const ImRect r( hit.rect.Min + ImVec2( -8, 1 ), hit.rect.Max + ImVec2( 8, 0 ) );
            overlay->AddRectFilled( r.Min, r.Max, kit::tone::accent( m_design ? 22 : 10 ), 7 );
            overlay->AddRect( r.Min, r.Max, kit::tone::accent( m_design ? 230 : 110 ), 7, 0, 1.5f );
            if ( m_design )
            {
                const auto found = locate( hit.uid );
                std::string tag = found.widget ? std::string( doc::kind_name( found.widget->kind ) ) : "Section";
                const float tw = text_size( tag.c_str( ), 11.f ) + 14;
                const ImRect pill( { r.Max.x - tw, r.Min.y - 20 }, { r.Max.x, r.Min.y - 2 } );
                overlay->AddRectFilled( pill.Min, pill.Max, kit::tone::accent( ), 5 );
                overlay->AddText( kit::font( ), 11.f, { pill.Min.x + 7, pill.Min.y + 3 }, IM_COL32_WHITE, tag.c_str( ) );
            }
        }
    overlay->PopClipRect( );
    ImGui::EndChild( );
}

void editor_c::canvas_app( ImVec2 o, float width, float height )
{
    auto* component = shown_component( );
    auto* screen = component ? nullptr : shown_screen( );
    std::vector<doc::element_t>* elements = component ? &component->elements : screen ? &screen->elements : nullptr;
    const ImVec2 surface = component ? ImVec2( component->w, component->h ) : ImVec2( m_doc.theme.width, m_doc.theme.height );
    const ImVec2 shown = surface * m_zoom;
    constexpr float tabs_h = 60;
    const ImVec2 content( std::max( width, shown.x + 96 ), std::max( height, shown.y + tabs_h + 64 ) );
    ImGui::SetCursorScreenPos( o );
    ImGui::SetNextWindowContentSize( content );
    ImGui::BeginChild( "##canvas", { width, height }, ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    ImGuiWindow* w = ImGui::GetCurrentWindow( );
    ImDrawList* draw = w->DrawList;
    const ImRect visible( w->Pos, w->Pos + ImVec2( width, height ) );

    const ImVec2 origin = w->Pos - w->Scroll;
    const float step = 24;
    for ( float y = std::fmod( -w->Scroll.y, step ); y < height; y += step )
        for ( float x = std::fmod( -w->Scroll.x, step ); x < width; x += step )
            draw->AddRectFilled( w->Pos + ImVec2( x, y ), w->Pos + ImVec2( x + 1.5f, y + 1.5f ), ed::dot );

    const ImVec2 at = ImFloor( origin + ImVec2( std::max( 48.f, ( content.x - shown.x ) * .5f ), std::max( tabs_h, ( content.y - shown.y ) * .5f + 16 ) ) );
    if ( !component )
        for ( int i = 1; i <= 4; ++i )
            draw->AddRectFilled( at + ImVec2( -2.f * i, 4.f * i ), at + shown + ImVec2( 2.f * i, 6.f * i ), IM_COL32( 0, 0, 0, 14 ),
                                 m_doc.theme.rounding * m_zoom + 2.f * i );
    else
    {
        // A component's bounds, dashed.
        const ImRect r( at - ImVec2( 1, 1 ), at + shown + ImVec2( 1, 1 ) );
        for ( float x = r.Min.x; x < r.Max.x; x += 8 )
        {
            draw->AddLine( { x, r.Min.y }, { std::min( x + 4, r.Max.x ), r.Min.y }, kit::tone::white( 60 ) );
            draw->AddLine( { x, r.Max.y }, { std::min( x + 4, r.Max.x ), r.Max.y }, kit::tone::white( 60 ) );
        }
        for ( float y = r.Min.y; y < r.Max.y; y += 8 )
        {
            draw->AddLine( { r.Min.x, y }, { r.Min.x, std::min( y + 4, r.Max.y ) }, kit::tone::white( 60 ) );
            draw->AddLine( { r.Max.x, y }, { r.Max.x, std::min( y + 4, r.Max.y ) }, kit::tone::white( 60 ) );
        }
    }

    // The window (or component) itself.
    const view_t view{ at, m_zoom };
    ImGui::SetCursorScreenPos( at );
    if ( elements )
    {
        preview::scoped_theme_t keep;
        preview::app_options_t options;
        options.inert = m_design;
        options.ghosts = m_design;
        if ( component )
        {
            const int uid = component->uid;
            scaled( view, visible, [&] { preview::draw_component( m_doc, uid, options, m_hits ); } );
        }
        else
        {
            int now = screen->uid;
            scaled( view, visible, [&] { preview::draw_app( m_doc, now, options, m_hits ); } );
            if ( now != m_screen )
            {
                // A button led to another screen while interacting.
                select( now );
                screen = shown_screen( );
                elements = screen ? &screen->elements : nullptr;
            }
        }
    }
    else
    {
        m_hits.clear( );
        label( draw, at, kit::tone::muted, "Add a screen with + to start." );
    }
    for ( auto& hit : m_hits )
        hit.rect = ImRect( view.to_screen( hit.rect.Min ), view.to_screen( hit.rect.Max ) );

    // Screen and component tabs along the top of the canvas.
    {
        float x = w->Pos.x + 16;
        const float y = w->Pos.y + 14;
        const auto tab = [&]( int uid, const std::string& name, bool is_component ) {
            const float tw = text_size( name.c_str( ), 12.5f ) + ( is_component ? 40.f : 24.f );
            const ImRect bb( { x, y }, { x + tw, y + 28 } );
            ImGui::PushID( uid );
            const ImGuiID id = w->GetID( "##screen_tab" );
            ImGui::ItemAdd( bb, id );
            bool hovered = false, held = false;
            if ( ImGui::ButtonBehavior( bb, id, &hovered, &held ) )
                select( uid );
            ImGui::PopID( );
            const bool active = uid == m_screen;
            draw->AddRectFilled( bb.Min, bb.Max, kit::tone::white( active ? 24 : hovered ? 14 : 8 ), 8 );
            if ( is_component )
                component_icon( draw, { bb.Min.x + 16, bb.GetCenter( ).y }, active ? kit::tone::title : kit::tone::muted );
            label( draw, { bb.Min.x + ( is_component ? 28.f : 12.f ), bb.Min.y + 7 }, active ? kit::tone::title : hovered ? kit::tone::label : kit::tone::muted,
                   name.c_str( ), 12.5f );
            x += tw + 6;
        };
        for ( auto& s : m_doc.screens )
            tab( s.uid, s.name, false );
        const auto [pressed, hovered] = icon_button( "##add_screen", { x, y }, { 28, 28 } );
        icon_plus( draw, { x + 14, y + 14 }, hovered ? kit::tone::title : kit::tone::muted );
        if ( hovered )
            ImGui::SetTooltip( "Add a screen" );
        if ( pressed )
            add( 100 );
        x += 28 + 10;
        if ( !m_doc.components.empty( ) )
        {
            draw->AddLine( { x, y + 6 }, { x, y + 22 }, kit::tone::white( 22 ) );
            x += 10;
            for ( auto& c : m_doc.components )
                tab( c.uid, c.name, true );
        }
    }
    recover_banner( o, width );
    zoom_controls( o, width, height, surface );

    // Design mode: select, move and resize elements and the window, snapping to the window and to each other.
    m_hovered = 0;
    const auto& io = ImGui::GetIO( );
    const ImVec2 mouse = io.MousePos;
    const ImVec2 mouse_doc = ( mouse - at ) / m_zoom;
    const bool hovered_canvas = ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem ) &&
                                !ImGui::IsPopupOpen( "", ImGuiPopupFlags_AnyPopupId ) && !ImGui::IsAnyItemHovered( ) &&
                                !ImGui::IsKeyDown( ImGuiKey_Space );
    const auto rect_of = [&]( const doc::element_t& e ) {
        return ImRect( at + ImVec2( e.x, e.y ) * m_zoom, at + ImVec2( e.x + e.w, e.y + e.h ) * m_zoom );
    };
    const ImRect surface_rect( at, at + shown );
    std::vector<doc::element_t*> selection;
    if ( elements )
        for ( auto& e : *elements )
            if ( multi( e.uid ) )
                selection.push_back( &e );
    doc::element_t* single = selection.size( ) == 1 ? selection.front( ) : nullptr;

    if ( elements && m_design )
    {
        if ( m_drag.kind == drag_t::kind_t::none && !m_marquee && hovered_canvas )
        {
            // What's under the mouse: a handle of the selected element, the window's edges, or an element.
            int handle = -1;
            if ( single && single->kind != kind::instance )
                for ( int i = 0; i < 8; ++i )
                {
                    const ImVec2 p = handle_point( rect_of( *single ), i );
                    if ( std::fabs( mouse.x - p.x ) <= 6 && std::fabs( mouse.y - p.y ) <= 6 )
                        handle = i;
                }
            int edge = -1;
            if ( handle < 0 && !component )
            {
                const bool near_right = std::fabs( mouse.x - surface_rect.Max.x ) < 5 && mouse.y > surface_rect.Min.y && mouse.y < surface_rect.Max.y + 5;
                const bool near_bottom = std::fabs( mouse.y - surface_rect.Max.y ) < 5 && mouse.x > surface_rect.Min.x && mouse.x < surface_rect.Max.x + 5;
                edge = near_right && near_bottom ? 2 : near_right ? 0 : near_bottom ? 1 : -1;
            }
            int over = 0;
            if ( handle < 0 && edge < 0 )
                for ( auto hit = m_hits.rbegin( ); hit != m_hits.rend( ); ++hit )
                    if ( hit->rect.Contains( mouse ) )
                    {
                        over = hit->uid;
                        break;
                    }
            m_hovered = over;
            if ( handle >= 0 )
                ImGui::SetMouseCursor( handle_cursor( handle ) );
            else if ( edge >= 0 )
                ImGui::SetMouseCursor( edge == 2 ? ImGuiMouseCursor_ResizeNWSE : edge == 0 ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS );
            else if ( over && multi( over ) )
                ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeAll );

            if ( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
            {
                if ( handle >= 0 )
                    m_drag = { drag_t::kind_t::resize, single->uid, handle, mouse, element_rect( *single ), { } };
                else if ( edge >= 0 )
                {
                    // Every element on every screen follows its pins from where it starts.
                    drag_t d{ drag_t::kind_t::window, 0, edge, mouse, { surface.x, surface.y, 0, 0 }, { } };
                    for ( auto& s : m_doc.screens )
                        for ( auto& e : s.elements )
                            d.group.push_back( { e.uid, element_rect( e ) } );
                    m_drag = d;
                }
                else if ( over )
                {
                    if ( io.KeyShift )
                        select( over, true );
                    else
                    {
                        if ( !multi( over ) )
                            select( over );
                        else
                            m_selected = over;
                        // Move everything selected; snapping works on the group's bounds.
                        drag_t d{ drag_t::kind_t::move, over, -1, mouse, { }, { } };
                        std::vector<doc::element_t*> moving;
                        for ( auto& e : *elements )
                            if ( multi( e.uid ) )
                            {
                                d.group.push_back( { e.uid, element_rect( e ) } );
                                moving.push_back( &e );
                            }
                        d.start = bounds( moving );
                        m_drag = d;
                    }
                }
                else
                {
                    // Empty space: a marquee, or (if it stays a click) the screen itself.
                    m_marquee = true;
                    m_marquee_from = mouse_doc;
                }
            }
        }

        if ( m_marquee && !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
        {
            m_marquee = false;
            const ImRect box( ImMin( m_marquee_from, mouse_doc ), ImMax( m_marquee_from, mouse_doc ) );
            if ( box.GetWidth( ) * m_zoom < 3 && box.GetHeight( ) * m_zoom < 3 )
                select( component ? component->uid : screen->uid );
            else
            {
                if ( !io.KeyShift )
                    m_multi.clear( );
                for ( const auto& e : *elements )
                    if ( box.Overlaps( ImRect( e.x, e.y, e.x + e.w, e.y + e.h ) ) && !multi( e.uid ) )
                        m_multi.push_back( e.uid );
                m_selected = m_multi.empty( ) ? ( component ? component->uid : screen->uid ) : m_multi.back( );
            }
        }

        if ( m_drag.kind == drag_t::kind_t::move || m_drag.kind == drag_t::kind_t::resize )
        {
            if ( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
            {
                m_drag = { };
                m_guides.clear( );
            }
            else if ( ImGui::IsMouseDragging( ImGuiMouseButton_Left, 2.f ) )
            {
                // Snap targets: the window's edges, centre and 32 px margins, and every other element's edges and centre.
                std::vector<float> xs = { 0, surface.x * .5f, surface.x, 32, surface.x - 32 };
                std::vector<float> ys = { 0, surface.y * .5f, surface.y, 32, surface.y - 32 };
                for ( const auto& other : *elements )
                    if ( !multi( other.uid ) )
                    {
                        xs.insert( xs.end( ), { other.x, other.x + other.w * .5f, other.x + other.w } );
                        ys.insert( ys.end( ), { other.y, other.y + other.h * .5f, other.y + other.h } );
                    }
                const bool snapping = !io.KeyAlt;
                const float reach = 5.f / m_zoom;
                const ImVec2 delta = ( mouse - m_drag.mouse ) / m_zoom;
                m_guides.clear( );
                if ( m_drag.kind == drag_t::kind_t::move )
                {
                    const ImVec4 b = m_drag.start;
                    float x = std::round( b.x + delta.x ), y = std::round( b.y + delta.y );
                    float shift = 0, target = 0;
                    if ( snapping && snap( { x, x + b.z * .5f, x + b.z }, xs, reach, shift, target ) )
                    {
                        x += shift;
                        m_guides.push_back( { true, target } );
                    }
                    if ( snapping && snap( { y, y + b.w * .5f, y + b.w }, ys, reach, shift, target ) )
                    {
                        y += shift;
                        m_guides.push_back( { false, target } );
                    }
                    for ( const auto& [uid, r] : m_drag.group )
                        for ( auto& e : *elements )
                            if ( e.uid == uid )
                                e.x = r.x + ( x - b.x ), e.y = r.y + ( y - b.y );
                }
                else if ( auto* e = locate( m_drag.uid ).element )
                {
                    // Resize: the handle decides which edges follow the mouse.
                    const int h = m_drag.handle;
                    const ImVec4 s = m_drag.start;
                    float l = s.x, t = s.y, r = s.x + s.z, b = s.y + s.w;
                    const bool moves_l = h == 0 || h == 6 || h == 7, moves_r = h == 2 || h == 3 || h == 4;
                    const bool moves_t = h == 0 || h == 1 || h == 2, moves_b = h == 4 || h == 5 || h == 6;
                    const auto pull = [&]( float& edge, float d, const std::vector<float>& targets, bool vertical ) {
                        edge = std::round( edge + d );
                        float shift = 0, target = 0;
                        if ( snapping && snap( { edge }, targets, reach, shift, target ) )
                        {
                            edge += shift;
                            m_guides.push_back( { vertical, target } );
                        }
                    };
                    if ( moves_l ) pull( l, delta.x, xs, true );
                    if ( moves_r ) pull( r, delta.x, xs, true );
                    if ( moves_t ) pull( t, delta.y, ys, false );
                    if ( moves_b ) pull( b, delta.y, ys, false );
                    // Shift keeps the shape when dragging a corner.
                    if ( io.KeyShift && ( h == 0 || h == 2 || h == 4 || h == 6 ) && s.w > 0 )
                    {
                        const float tall = ( r - l ) * s.w / s.z;
                        if ( moves_t )
                            t = b - tall;
                        else
                            b = t + tall;
                    }
                    constexpr float least = 4;
                    if ( r - l < least )
                    {
                        if ( moves_l )
                            l = r - least;
                        else
                            r = l + least;
                    }
                    if ( b - t < least )
                    {
                        if ( moves_t )
                            t = b - least;
                        else
                            b = t + least;
                    }
                    e->x = l, e->y = t, e->w = r - l, e->h = b - t;
                }
            }
        }
    }
    if ( m_drag.kind == drag_t::kind_t::window && !component )
    {
        if ( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
            m_drag = { };
        else
        {
            const ImVec2 delta = ( mouse - m_drag.mouse ) / m_zoom;
            const float nw = m_drag.handle != 1 ? std::max( 160.f, std::round( m_drag.start.x + delta.x ) ) : m_drag.start.x;
            const float nh = m_drag.handle != 0 ? std::max( 120.f, std::round( m_drag.start.y + delta.y ) ) : m_drag.start.y;
            resize_window( nw, nh, m_drag.group, { m_drag.start.x, m_drag.start.y } );
            ImGui::SetMouseCursor( m_drag.handle == 2 ? ImGuiMouseCursor_ResizeNWSE : m_drag.handle == 0 ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS );
        }
    }
    if ( !m_design )
    {
        m_drag = { };
        m_marquee = false;
    }

    // Overlays, clipped to the canvas.
    ImDrawList* overlay = ImGui::GetForegroundDrawList( );
    overlay->PushClipRect( o, o + ImVec2( width, height ), true );
    if ( m_drag.kind == drag_t::kind_t::window )
        size_pill( overlay, { surface_rect.GetCenter( ).x, surface_rect.Max.y + 10 }, m_doc.theme.width, m_doc.theme.height, kit::tone::accent( ) );
    if ( m_hovered && !multi( m_hovered ) && m_drag.kind == drag_t::kind_t::none )
        for ( const auto& hit : m_hits )
            if ( hit.uid == m_hovered )
                overlay->AddRect( hit.rect.Min - ImVec2( 1, 1 ), hit.rect.Max + ImVec2( 1, 1 ), kit::tone::accent( 140 ), 2, 0, 1.f );
    for ( const auto* e : selection )
    {
        const ImRect r = rect_of( *e );
        overlay->AddRect( r.Min - ImVec2( 1, 1 ), r.Max + ImVec2( 1, 1 ), kit::tone::accent( m_design ? 255 : 110 ), 2, 0, 1.5f );
    }
    if ( m_design && selection.size( ) > 1 )
    {
        const ImVec4 b = bounds( selection );
        const ImRect r( at + ImVec2( b.x, b.y ) * m_zoom, at + ImVec2( b.x + b.z, b.y + b.w ) * m_zoom );
        overlay->AddRect( r.Min - ImVec2( 4, 4 ), r.Max + ImVec2( 4, 4 ), kit::tone::accent( 120 ), 4, 0, 1.f );
        size_pill( overlay, { r.GetCenter( ).x, r.Max.y + 10 }, b.z, b.w, kit::tone::accent( ) );
    }
    if ( m_design && single )
    {
        const ImRect r = rect_of( *single );
        if ( single->kind != kind::instance )
            for ( int i = 0; i < 8; ++i )
            {
                const ImVec2 p = ImFloor( handle_point( r, i ) );
                overlay->AddRectFilled( p - ImVec2( 3.5f, 3.5f ), p + ImVec2( 3.5f, 3.5f ), IM_COL32_WHITE, 1.5f );
                overlay->AddRect( p - ImVec2( 3.5f, 3.5f ), p + ImVec2( 3.5f, 3.5f ), kit::tone::accent( ), 1.5f, 0, 1.2f );
            }
        size_pill( overlay, { r.GetCenter( ).x, r.Max.y + 8 }, single->w, single->h, kit::tone::accent( ) );
    }
    if ( m_marquee )
    {
        const ImVec2 a = at + m_marquee_from * m_zoom;
        overlay->AddRectFilled( ImMin( a, mouse ), ImMax( a, mouse ), kit::tone::accent( 24 ) );
        overlay->AddRect( ImMin( a, mouse ), ImMax( a, mouse ), kit::tone::accent( 180 ) );
    }
    for ( const auto& g : m_guides )
    {
        if ( g.vertical )
        {
            const float x = std::floor( at.x + g.at * m_zoom ) + .5f;
            overlay->AddLine( { x, at.y - 12 }, { x, at.y + shown.y + 12 }, ed::guide, 1.f );
        }
        else
        {
            const float y = std::floor( at.y + g.at * m_zoom ) + .5f;
            overlay->AddLine( { at.x - 12, y }, { at.x + shown.x + 12, y }, ed::guide, 1.f );
        }
    }
    overlay->PopClipRect( );
    ImGui::EndChild( );
}

void editor_c::properties_project( )
{
    auto& t = m_doc.theme;
    kit::section( "Project" );
    edit_text( "Name", m_doc.name );
    edit_text( "C++ namespace", t.namespace_name );
    if ( !app( ) )
    {
        kit::section( "Window" );
        edit_text( "Title", t.title );
        edit_text( "Footer", t.footer );
    }
    kit::section( "Size" );
    if ( app( ) )
    {
        float w = t.width, h = t.height;
        edit_number( "Width", w, 1, "%.0f px" );
        edit_number( "Height", h, 1, "%.0f px" );
        w = std::clamp( std::round( w ), 160.f, 2000.f ), h = std::clamp( std::round( h ), 120.f, 1400.f );
        if ( w != t.width || h != t.height )
        {
            std::vector<std::pair<int, ImVec4>> from;
            for ( auto& s : m_doc.screens )
                for ( auto& e : s.elements )
                    from.push_back( { e.uid, element_rect( e ) } );
            resize_window( w, h, from, { t.width, t.height } );
        }
        kit::toggle( "Resizable", t.resizable, "Users can drag its edges; elements follow their pins" );
        constexpr const char* transitions[] = { "None", "Fade", "Slide" };
        kit::segment( "Screen change", t.transition, transitions );
    }
    else
    {
        kit::slider( "Width", t.width, 760, 1600, "%.0f", " px" );
        kit::slider( "Height", t.height, 480, 1000, "%.0f", " px" );
        kit::slider( "List width", t.list_width, 180, 360, "%.0f", " px" );
        int columns = t.columns - 1;
        constexpr const char* counts[] = { "1", "2", "3" };
        kit::segment( "Columns", columns, counts );
        t.columns = columns + 1;
    }
    kit::section( "Look" );
    constexpr const char* palettes[] = { "Graphite", "Midnight", "Light", "OLED", "Custom" };
    kit::select( "Palette", t.palette, palettes );
    if ( t.palette == 4 )
    {
        kit::color( "Window", t.custom_window );
        kit::color( "Text", t.custom_text );
        kit::color( "Muted text", t.custom_muted );
        kit::toggle( "Light window", t.custom_light, "Dark lines and fills" );
        const auto file = platform::app_data( ) + L"\\palette.json";
        switch ( button_row( "Saved palette", { "Load", "Save" } ) )
        {
        case 0:
        {
            std::string text;
            doc::document_t saved;
            if ( read_file( file, text ) && doc::from_json( text, saved ) )
            {
                std::copy( std::begin( saved.theme.custom_window ), std::end( saved.theme.custom_window ), t.custom_window );
                std::copy( std::begin( saved.theme.custom_text ), std::end( saved.theme.custom_text ), t.custom_text );
                std::copy( std::begin( saved.theme.custom_muted ), std::end( saved.theme.custom_muted ), t.custom_muted );
                std::copy( std::begin( saved.theme.accent ), std::end( saved.theme.accent ), t.accent );
                t.custom_light = saved.theme.custom_light;
                notify( "Loaded your saved palette." );
            }
            else
                notify( "No saved palette yet: Save stores this one for every project." );
            break;
        }
        case 1:
        {
            doc::document_t keep;
            keep.theme = t;
            write_file( file, doc::to_json( keep ) );
            notify( "Saved this palette; Load brings it into any project." );
            break;
        }
        default: break;
        }
    }
    kit::color( "Accent", t.accent );
    kit::slider( "Opacity", t.window_alpha, .3f, 1.f, "%.2f" );
    kit::slider( "Window corners", t.rounding, 0, 24, "%.0f", " px" );
    kit::slider( "Control corners", t.control_rounding, 0, 14, "%.0f", " px" );
    kit::section( "Widgets" );
    constexpr const char* toggles[] = { "Switch", "Checkbox" };
    constexpr const char* sliders[] = { "Bar", "Track" };
    constexpr const char* buttons[] = { "Soft", "Solid", "Outline" };
    constexpr const char* fields[] = { "Box", "Underline" };
    kit::segment( "Toggles", t.toggles, toggles );
    kit::segment( "Sliders", t.sliders, sliders );
    kit::select( "Buttons", t.buttons, buttons );
    kit::segment( "Fields", t.fields, fields );

    if ( app( ) )
    {
        kit::section( "Images" );
        int erase = -1;
        for ( size_t i = 0; i < m_doc.images.size( ); ++i )
        {
            const auto& image = m_doc.images[i];
            ImGui::PushID( image.uid );
            char size[96];
            std::snprintf( size, sizeof( size ), "%s  %d \xc3\x97 %d", image.name.c_str( ), image.width, image.height );
            if ( button_row( size, { "Remove" } ) == 0 )
                erase = static_cast<int>( i );
            ImGui::PopID( );
        }
        if ( erase >= 0 )
        {
            const int gone = m_doc.images[static_cast<size_t>( erase )].uid;
            m_doc.images.erase( m_doc.images.begin( ) + erase );
            for ( auto& s : m_doc.screens )
                for ( auto& e : s.elements )
                    if ( e.image == gone )
                        e.image = 0;
        }
        if ( kit::button( "", "Import image\xe2\x80\xa6" ) )
            import_image( nullptr );

        kit::section( "Custom widgets" );
        const std::string plugin = t.plugin.empty( ) ? std::string( "None" ) : platform::narrow( std::filesystem::path( platform::widen( t.plugin ) ).filename( ).wstring( ) );
        kit::value( "Plugin", plugin.c_str( ) );
        switch ( button_row( "", { "Clear", "Choose\xe2\x80\xa6" } ) )
        {
        case 0: t.plugin.clear( ); break;
        case 1:
        {
            const auto path = platform::open_library( );
            if ( !path.empty( ) )
                t.plugin = platform::narrow( path );
            break;
        }
        default: break;
        }
        if ( !plugins::status( ).empty( ) )
            kit::note( plugins::status( ).c_str( ) );
        if ( !plugins::widgets( ).empty( ) )
        {
            std::string list;
            for ( const auto& name : plugins::widgets( ) )
                list += ( list.empty( ) ? "" : ", " ) + name;
            kit::note( ( "Widgets: " + list ).c_str( ) );
        }
    }
    kit::note( app( ) ? "Tab switches Design and Interact. In Design, drag elements (Alt skips snapping), drag the window's edge "
                        "to resize it, Shift-click or drag a box to select several, Ctrl+scroll to zoom."
                      : "Tab switches between Design (click rows to select them) and Interact (try the menu). Values you set "
                        "while interacting become the exported defaults." );
}

void editor_c::properties_multi( )
{
    auto list = selected_elements( );
    kit::section( "Align" );
    switch ( button_row( "Across", { "Left", "Centre", "Right" } ) )
    {
    case 0: align( 0 ); break;
    case 1: align( 1 ); break;
    case 2: align( 2 ); break;
    default: break;
    }
    switch ( button_row( "Down", { "Top", "Middle", "Bottom" } ) )
    {
    case 0: align( 3 ); break;
    case 1: align( 4 ); break;
    case 2: align( 5 ); break;
    default: break;
    }
    if ( list.size( ) >= 3 )
        switch ( button_row( "Even gaps", { "Across", "Down" } ) )
        {
        case 0: distribute( true ); break;
        case 1: distribute( false ); break;
        default: break;
        }
    switch ( button_row( "Match", { "Width", "Height" } ) )
    {
    case 0: match( true ); break;
    case 1: match( false ); break;
    default: break;
    }
    kit::note( "Match uses the last element you picked." );
    kit::section( "Selection" );
    const ImVec4 b = bounds( list );
    char text[64];
    std::snprintf( text, sizeof( text ), "%.0f, %.0f  \xc2\xb7  %.0f \xc3\x97 %.0f", b.x, b.y, b.z, b.w );
    kit::value( "Bounds", text );
    if ( locate( m_selected ).screen && kit::button( "Component", "Make component" ) )
    {
        make_component( );
        return;
    }
    switch ( button_row( "Elements", { "Duplicate", "Delete" } ) )
    {
    case 0: duplicate_selection( ); break;
    case 1: remove_selection( ); break;
    default: break;
    }
}

void editor_c::properties_element( doc::element_t& e, const where_t& at )
{
    kit::section( "Element" );
    int k = static_cast<int>( e.kind );
    if ( kit::select( "Type", k, element_kinds ) && static_cast<kind>( k ) != e.kind )
    {
        if ( static_cast<kind>( k ) == kind::instance && ( at.component || m_doc.components.empty( ) ) )
            notify( at.component ? "Components can't hold other components." : "Make a component first." );
        else
        {
            auto fresh = doc::make_element( m_doc, static_cast<kind>( k ) );
            fresh.uid = e.uid;
            fresh.text = e.text, fresh.id = e.id;
            fresh.x = e.x, fresh.y = e.y;
            if ( static_cast<kind>( k ) != kind::instance )
                fresh.w = e.w, fresh.h = e.h;
            fresh.tab_owner = e.tab_owner, fresh.tab_index = e.tab_index;
            e = fresh;
        }
    }
    const char* text_name = e.kind == kind::button ? "Caption" : e.kind == kind::input ? "Placeholder" : e.kind == kind::checkbox ? "Label"
                            : e.kind == kind::text                                     ? "Text"
                                                                                       : "Name";
    edit_text( text_name, e.text );
    if ( e.kind != kind::panel && e.kind != kind::divider && e.kind != kind::spinner && e.kind != kind::image && e.kind != kind::icon )
        edit_text( "C++ name", e.id );

    const float W = at.component ? at.component->w : m_doc.theme.width;
    const float H = at.component ? at.component->h : m_doc.theme.height;
    kit::section( "Layout" );
    edit_number( "X", e.x, 1, "%.0f" );
    edit_number( "Y", e.y, 1, "%.0f" );
    if ( e.kind != kind::instance )
    {
        edit_number( "Width", e.w, 1, "%.0f" );
        edit_number( "Height", e.h, 1, "%.0f" );
    }
    e.w = std::max( 1.f, e.w ), e.h = std::max( 1.f, e.h );
    switch ( button_row( "Place", { "Left", "Centre", "Right" } ) )
    {
    case 0: e.x = 32; break;
    case 1: e.x = std::floor( ( W - e.w ) * .5f ); break;
    case 2: e.x = W - 32 - e.w; break;
    default: break;
    }
    switch ( button_row( "", { "Middle", "Full width" } ) )
    {
    case 0: e.y = std::floor( ( H - e.h ) * .5f ); break;
    case 1:
        if ( e.kind != kind::instance )
            e.x = 32, e.w = W - 64;
        break;
    default: break;
    }
    if ( at.screen )
    {
        constexpr const char* across[] = { "Left", "Right", "Stretch", "Centre" };
        constexpr const char* down[] = { "Top", "Bottom", "Stretch", "Middle" };
        kit::select( "Pin across", e.pin_x, across, "When the window is resized" );
        kit::select( "Pin down", e.pin_y, down );
    }

    switch ( e.kind )
    {
    case kind::text:
    {
        kit::section( "Text" );
        kit::slider( "Size", e.size, 9, 40, "%.0f", " px" );
        constexpr const char* aligns[] = { "Left", "Centre", "Right" };
        constexpr const char* roles[] = { "Title", "Body", "Muted", "Accent" };
        kit::segment( "Align", e.align, aligns );
        kit::select( "Colour", e.role, roles );
        break;
    }
    case kind::button:
    {
        kit::section( "Button" );
        kit::toggle( "Primary", e.primary, "Filled with the accent" );
        kit::select( "Icon", e.icon, kit::icon_names( ) );
        std::vector<std::string> choices = { "Nothing", "Close the window" };
        for ( const auto& s : m_doc.screens )
            choices.push_back( "Go to " + s.name );
        int pick = e.target == -1 ? 1 : 0;
        for ( size_t i = 0; i < m_doc.screens.size( ); ++i )
            if ( m_doc.screens[i].uid == e.target )
                pick = static_cast<int>( i ) + 2;
        if ( kit::select( "Opens", pick, names( choices ) ) )
            e.target = pick == 0 ? 0 : pick == 1 ? -1 : m_doc.screens[static_cast<size_t>( pick - 2 )].uid;
        edit_text( "Notification", e.toast, "Shown when pressed" );
        const std::string event = "Pressing it also calls events." + doc::identifier( e.id.empty( ) ? e.text : e.id ) + " in the export.";
        kit::note( event.c_str( ) );
        break;
    }
    case kind::input:
        kit::section( "Input" );
        kit::toggle( "Password", e.password, "Hides what is typed" );
        edit_text( "Starts with", e.value );
        break;
    case kind::checkbox:
        kit::section( "Checkbox" );
        kit::toggle( "Checked", e.on );
        break;
    case kind::slider:
        kit::section( "Range" );
        edit_number( "Minimum", e.min, .5f, "%.2f" );
        edit_number( "Maximum", e.max, .5f, "%.2f" );
        edit_number( "Default", e.number, .1f, "%.2f" );
        edit_text( "Format", e.format, "%.0f" );
        break;
    case kind::dropdown:
        kit::section( "Options" );
        edit_options( e.options, nullptr, nullptr, "Option" );
        kit::select( "Default", e.choice, names( e.options ) );
        break;
    case kind::tabs:
        kit::section( "Tabs" );
        edit_options( e.options, nullptr, nullptr, "Tab" );
        kit::select( "Showing", e.choice, names( e.options ) );
        kit::note( "Put an element on a tab with its \"Show on\" setting." );
        break;
    case kind::list:
        kit::section( "Rows" );
        edit_options( e.options, &e.details, nullptr, "Row" );
        kit::select( "Selected", e.choice, names( e.options ) );
        break;
    case kind::progress:
        kit::section( "Progress" );
        kit::slider( "Value", e.number, 0, 1, "%.2f" );
        kit::toggle( "Endless", e.endless, "A sweep that never finishes" );
        break;
    case kind::image:
    {
        kit::section( "Image" );
        std::vector<std::string> choices = { "None" };
        int pick = 0;
        for ( size_t i = 0; i < m_doc.images.size( ); ++i )
        {
            choices.push_back( m_doc.images[i].name );
            if ( m_doc.images[i].uid == e.image )
                pick = static_cast<int>( i ) + 1;
        }
        if ( kit::select( "Image", pick, names( choices ) ) )
            e.image = pick == 0 ? 0 : m_doc.images[static_cast<size_t>( pick - 1 )].uid;
        switch ( button_row( "", { "Natural size", "Import\xe2\x80\xa6" } ) )
        {
        case 0:
            if ( const auto* image = doc::find_image( m_doc, e.image ) )
                e.w = static_cast<float>( image->width ), e.h = static_cast<float>( image->height );
            break;
        case 1: import_image( &e ); break;
        default: break;
        }
        kit::slider( "Corners", e.rounding, 0, 64, "%.0f", " px" );
        break;
    }
    case kind::icon:
        kit::section( "Icon" );
        kit::select( "Icon", e.icon, kit::icon_names( ) );
        break;
    case kind::instance:
    {
        kit::section( "Component" );
        std::vector<std::string> choices;
        int pick = 0;
        for ( size_t i = 0; i < m_doc.components.size( ); ++i )
        {
            choices.push_back( m_doc.components[i].name );
            if ( m_doc.components[i].uid == e.component )
                pick = static_cast<int>( i );
        }
        if ( kit::select( "Component", pick, names( choices ) ) && !m_doc.components.empty( ) )
            e.component = m_doc.components[static_cast<size_t>( pick )].uid;
        switch ( button_row( "", { "Detach", "Edit component" } ) )
        {
        case 0: detach( e.uid ); return;
        case 1: select( e.component ); return;
        default: break;
        }
        break;
    }
    case kind::custom:
    {
        kit::section( "Custom widget" );
        edit_text( "Function", e.function, "draw_custom" );
        if ( !plugins::widgets( ).empty( ) )
        {
            const auto& choices = plugins::widgets( );
            int pick = 0;
            for ( size_t i = 0; i < choices.size( ); ++i )
                if ( choices[i] == e.function )
                    pick = static_cast<int>( i );
            if ( kit::select( "From plugin", pick, names( choices ) ) )
                e.function = choices[static_cast<size_t>( pick )];
        }
        kit::note( "Exports call this function with the element's rect; a widget plugin draws it here too (Project settings)." );
        break;
    }
    default: break;
    }

    if ( e.kind == kind::text || e.kind == kind::button || e.kind == kind::panel || e.kind == kind::icon )
    {
        kit::section( "Colour" );
        kit::color( e.kind == kind::text || e.kind == kind::icon ? "Colour" : "Fill", e.rgba, &e.tint, "Overrides the theme" );
    }

    // What your code controls, and which tab it lives on.
    kit::section( "From code" );
    if ( e.kind == kind::text )
        kit::toggle( "Changeable text", e.dynamic, "A std::string in state" );
    kit::toggle( "Can hide", e.hideable, "A bool in state" );
    if ( e.hideable )
        kit::toggle( "Visible", e.shown, "At start" );
    if ( e.kind != kind::text && e.kind != kind::panel && e.kind != kind::divider && e.kind != kind::image && e.kind != kind::icon )
    {
        kit::toggle( "Can disable", e.lockable, "A bool in state" );
        if ( e.lockable )
            kit::toggle( "Enabled", e.enabled, "At start" );
    }
    if ( auto* list = at.elements( ); list && e.kind != kind::tabs )
    {
        std::vector<std::string> choices = { "Always" };
        std::vector<const doc::element_t*> owners = { nullptr };
        int pick = 0;
        for ( const auto& other : *list )
            if ( other.kind == kind::tabs )
            {
                if ( other.uid == e.tab_owner )
                    pick = static_cast<int>( owners.size( ) );
                choices.push_back( other.text );
                owners.push_back( &other );
            }
        if ( owners.size( ) > 1 )
        {
            if ( kit::select( "Show on", pick, names( choices ), "Tabs on this screen" ) )
                e.tab_owner = owners[static_cast<size_t>( pick )] ? owners[static_cast<size_t>( pick )]->uid : 0;
            if ( pick > 0 && owners[static_cast<size_t>( pick )] )
                kit::select( "Tab", e.tab_index, names( owners[static_cast<size_t>( pick )]->options ) );
        }
    }
}

void editor_c::properties( )
{
    ImDrawList* draw = ImGui::GetWindowDrawList( );
    const ImVec2 o = ImGui::GetWindowPos( ) + ImVec2( m_size.x - ed::right, ed::title );
    const float height = m_size.y - ed::title - ed::status;
    draw->AddRectFilled( o, o + ImVec2( ed::right, height ), ed::panel );
    draw->AddLine( o + ImVec2( .5f, 0 ), o + ImVec2( .5f, height ), ed::line );

    auto at = locate( m_selected );
    if ( m_selected && !at )
    {
        select( 0 );
        at = { };
    }
    const bool several = app( ) && m_multi.size( ) > 1;
    char count[32];
    std::snprintf( count, sizeof( count ), "%zu elements", m_multi.size( ) );
    const char* kind_name = several         ? count
                            : !m_selected   ? "Project"
                            : at.element    ? doc::element_name( at.element->kind )
                            : at.screen     ? "Screen"
                            : at.component  ? "Component"
                            : at.widget     ? doc::kind_name( at.widget->kind )
                            : at.section    ? "Section"
                            : at.feature    ? "Feature"
                            : at.group      ? "Group"
                                            : "Category";
    label( draw, o + ImVec2( 20, 16 ), kit::tone::title, kind_name, 13.5f );
    draw->AddLine( o + ImVec2( 1, 47.5f ), o + ImVec2( ed::right, 47.5f ), ed::line );

    ImGui::SetCursorScreenPos( o + ImVec2( 20, 48 ) );
    ImGui::BeginChild( "##properties", { ed::right - 28, height - 48 }, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground );
    ImGui::GetCurrentWindow( )->WorkRect.Max.x = ImGui::GetWindowPos( ).x + ed::right - 40;
    ImGui::PushID( several ? -1 : m_selected );
    kit::begin_columns( 1 );
    bool arrange = m_selected != 0 && !several;
    if ( several )
        properties_multi( );
    else if ( !m_selected )
        properties_project( );
    else if ( at.element )
        properties_element( *at.element, at );
    else if ( at.screen )
    {
        kit::section( "Screen" );
        edit_text( "Name", at.screen->name );
        edit_text( "C++ name", at.screen->id );
        if ( at.sci == 0 )
            kit::value( "Start", "Shown first" );
        else if ( kit::button( "Start", "Show first" ) )
        {
            auto moved = *at.screen;
            m_doc.screens.erase( m_doc.screens.begin( ) + static_cast<long long>( at.sci ) );
            m_doc.screens.insert( m_doc.screens.begin( ), moved );
            at = locate( m_selected );
        }
        if ( at.screen )
            kit::value( "Elements", std::to_string( at.screen->elements.size( ) ).c_str( ) );
    }
    else if ( at.component )
    {
        auto& c = *at.component;
        kit::section( "Component" );
        edit_text( "Name", c.name );
        edit_text( "C++ name", c.id );
        edit_number( "Width", c.w, 1, "%.0f px" );
        edit_number( "Height", c.h, 1, "%.0f px" );
        c.w = std::max( 8.f, std::round( c.w ) ), c.h = std::max( 8.f, std::round( c.h ) );
        int instances = 0;
        for ( const auto& s : m_doc.screens )
            for ( const auto& e : s.elements )
                instances += e.kind == kind::instance && e.component == c.uid;
        kit::value( "Placed", ( std::to_string( instances ) + ( instances == 1 ? " time" : " times" ) ).c_str( ) );
        kit::note( "Changes here show in every instance. Place it with + > Component instance." );
    }
    else if ( at.widget )
    {
        auto& w = *at.widget;
        kit::section( "Row" );
        int kind_index = static_cast<int>( w.kind );
        if ( kit::select( "Type", kind_index, widget_kinds ) )
        {
            const auto fresh = doc::make_widget( m_doc, static_cast<doc::kind_t>( kind_index ) );
            const auto label_text = w.label, id = w.id;
            const int uid = w.uid;
            w = fresh;
            w.uid = uid;
            if ( !label_text.empty( ) )
                w.label = label_text, w.id = id;
        }
        if ( w.kind != doc::kind_t::note )
            edit_text( "Label", w.label );
        if ( w.kind != doc::kind_t::note && w.kind != doc::kind_t::value && w.kind != doc::kind_t::button && w.kind != doc::kind_t::segment )
            edit_text( "Hint", w.hint, "Optional second line" );
        if ( w.kind != doc::kind_t::note && w.kind != doc::kind_t::value )
            edit_text( "C++ name", w.id );

        switch ( w.kind )
        {
        case doc::kind_t::toggle:
            kit::section( "Default" );
            kit::toggle( "On", w.on );
            break;
        case doc::kind_t::slider:
        case doc::kind_t::slider_int:
            kit::section( "Range" );
            edit_number( "Minimum", w.min, .5f, "%.2f" );
            edit_number( "Maximum", w.max, .5f, "%.2f" );
            edit_number( "Default", w.number, .1f, "%.2f" );
            if ( w.kind == doc::kind_t::slider )
                edit_text( "Format", w.format, "%.0f" );
            edit_text( "Unit", w.unit, " px" );
            break;
        case doc::kind_t::select:
        case doc::kind_t::segment:
        case doc::kind_t::chips:
            kit::section( "Options" );
            edit_options( w.options, nullptr, w.kind == doc::kind_t::chips ? &w.chip_values : nullptr, "Option" );
            if ( w.kind != doc::kind_t::chips )
                kit::select( "Default", w.choice, names( w.options ) );
            break;
        case doc::kind_t::color:
            kit::section( "Default" );
            kit::color( "Colour", w.rgba );
            kit::toggle( "With a switch", w.has_switch );
            if ( w.has_switch )
                kit::toggle( "Switch on", w.on );
            break;
        case doc::kind_t::keybind:
            kit::section( "Default" );
            kit::keybind( "Key", w.key, w.has_mode ? &w.mode : nullptr );
            kit::toggle( "Hold / Toggle / Always", w.has_mode );
            kit::toggle( "With a switch", w.has_switch );
            break;
        case doc::kind_t::button: kit::section( "Button" ); edit_text( "Caption", w.text ); break;
        case doc::kind_t::value: kit::section( "Value" ); edit_text( "Text", w.text ); break;
        case doc::kind_t::note: kit::section( "Note" ); edit_text( "Text", w.text ); break;
        default: break;
        }
    }
    else if ( at.section )
    {
        kit::section( "Section" );
        edit_text( "Title", at.section->title );
        int column = at.section->column;
        constexpr const char* sides[] = { "Left", "Right", "Third" };
        kit::segment( "Column", column, std::span<const char* const>( sides, static_cast<size_t>( std::clamp( m_doc.theme.columns, 1, 3 ) ) ) );
        at.section->column = column;
    }
    else if ( at.feature )
    {
        auto& f = *at.feature;
        kit::section( "Feature" );
        edit_text( "Name", f.name );
        edit_text( "Description", f.description );
        edit_text( "C++ name", f.id );
        edit_text( "Subtitle", f.meta, "Defaults to its key" );
        kit::section( "Controls" );
        kit::toggle( "Master switch", f.has_switch, "In the list and the header" );
        if ( f.has_switch )
            kit::toggle( "Starts on", f.enabled );
        kit::toggle( "Keybind", f.has_key );
        if ( f.has_key )
            kit::keybind( "Default key", f.key, &f.mode );
    }
    else if ( at.group )
    {
        kit::section( "Group" );
        edit_text( "Title", at.group->title );
    }
    else if ( at.category )
    {
        kit::section( "Category" );
        edit_text( "Name", at.category->name );
    }
    // The selection may have changed above (detach, delete, editing a component).
    arrange = arrange && m_selected != 0 && m_multi.size( ) <= 1 && locate( m_selected );
    if ( arrange )
    {
        kit::section( "Arrange" );
        // Elements draw in list order, so "forward" is later in the list.
        const bool element = locate( m_selected ).element != nullptr;
        switch ( button_row( "Order", { element ? "Backward" : "Move up", element ? "Forward" : "Move down" } ) )
        {
        case 0: move( m_selected, -1 ); break;
        case 1: move( m_selected, 1 ); break;
        default: break;
        }
        switch ( button_row( "Copy", { "Duplicate", "Delete" } ) )
        {
        case 0: duplicate( m_selected ); break;
        case 1: remove( m_selected ); break;
        default: break;
        }
    }
    kit::end_columns( );
    ImGui::PopID( );
    ImGui::EndChild( );
}

void editor_c::code_panel( )
{
    ImDrawList* draw = ImGui::GetWindowDrawList( );
    const float width = m_size.x - ed::left - ed::right;
    const ImVec2 o = ImGui::GetWindowPos( ) + ImVec2( ed::left, m_size.y - ed::status - ed::code );
    draw->AddRectFilled( o, o + ImVec2( width, ed::code ), ed::panel );
    draw->AddLine( o + ImVec2( 0, .5f ), o + ImVec2( width, .5f ), ed::line );

    if ( m_last != m_code_for )
    {
        m_code = codegen::generate( m_doc );
        m_code_for = m_last;
    }

    // File tabs and actions.
    float x = o.x + 18;
    for ( int i = 0; i < 2; ++i )
    {
        const std::string& name = i == 0 ? m_code.header_name : m_code.source_name;
        const float tw = text_size( name.c_str( ), 12.5f );
        const ImRect bb( { x - 10, o.y + 8 }, { x + tw + 10, o.y + 36 } );
        const ImGuiID id = ImGui::GetID( name.c_str( ) );
        ImGui::ItemAdd( bb, id );
        bool hovered = false, held = false;
        if ( ImGui::ButtonBehavior( bb, id, &hovered, &held ) )
            m_code_tab = i;
        if ( m_code_tab == i )
            draw->AddRectFilled( bb.Min, bb.Max, kit::tone::white( 16 ), 7 );
        label( draw, { x, o.y + 15 }, m_code_tab == i ? kit::tone::title : hovered ? kit::tone::label : kit::tone::muted, name.c_str( ), 12.5f );
        x += tw + 26;
    }
    const std::string& text = m_code_tab == 0 ? m_code.header : m_code.source;
    ImGui::PushID( "##code_actions" );
    if ( kit::action_button( "Copy", { o.x + width - 18 - kit::action_width( "Copy" ), o.y + 7 }, false, 30 ) )
    {
        platform::copy_text( text );
        notify( "Copied " + ( m_code_tab == 0 ? m_code.header_name : m_code.source_name ) + "." );
    }
    ImGui::PopID( );
    draw->AddLine( o + ImVec2( 0, 43.5f ), o + ImVec2( width, 43.5f ), ed::line );

    // The code, with line numbers; only visible lines are drawn.
    ImGui::SetCursorScreenPos( o + ImVec2( 0, 44 ) );
    ImGui::BeginChild( "##code", { width, ed::code - 44 }, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_HorizontalScrollbar );
    std::vector<std::string> lines;
    {
        size_t start = 0;
        while ( start <= text.size( ) )
        {
            const size_t end = text.find( '\n', start );
            lines.push_back( text.substr( start, end == std::string::npos ? std::string::npos : end - start ) );
            if ( end == std::string::npos )
                break;
            start = end + 1;
        }
    }
    constexpr float line_h = 19;
    float longest = 0;
    for ( const auto& l : lines )
        longest = std::max( longest, static_cast<float>( l.size( ) ) );
    ImGui::Dummy( { 64 + longest * ( code_font ? code_font->CalcTextSizeA( 13.f, FLT_MAX, 0.f, "M" ).x : 7.2f ),
                    10 + static_cast<float>( lines.size( ) ) * line_h + 10 } );
    ImGuiWindow* w = ImGui::GetCurrentWindow( );
    ImDrawList* d = w->DrawList;
    const ImVec2 base = w->Pos - w->Scroll + ImVec2( 0, 10 );
    const int first = std::max( 0, static_cast<int>( w->Scroll.y / line_h ) - 1 );
    const int last = std::min( static_cast<int>( lines.size( ) ), first + static_cast<int>( w->Size.y / line_h ) + 3 );
    for ( int i = first; i < last; ++i )
    {
        const float y = base.y + static_cast<float>( i ) * line_h;
        char number[8];
        std::snprintf( number, sizeof( number ), "%d", i + 1 );
        d->AddText( kit::font( ), 12.f, { w->Pos.x + 40 - text_size( number, 12.f ), y + 1 }, IM_COL32( 90, 90, 100, 255 ), number );
        code_line( d, { base.x + 60, y }, lines[static_cast<size_t>( i )] );
    }
    ImGui::EndChild( );
}

void editor_c::status_bar( )
{
    ImDrawList* draw = ImGui::GetWindowDrawList( );
    const ImVec2 o = ImGui::GetWindowPos( ) + ImVec2( 0, m_size.y - ed::status );
    draw->AddRectFilled( o, o + ImVec2( m_size.x, ed::status ), ed::bar );
    draw->AddLine( o + ImVec2( 0, .5f ), o + ImVec2( m_size.x, .5f ), ed::line );

    // Left: a fading message, or what the current mode does.
    const float age = static_cast<float>( ImGui::GetTime( ) - m_status_at );
    if ( age < 4.f && !m_status.empty( ) )
    {
        const float alpha = age < 3.f ? 1.f : 1.f - ( age - 3.f );
        draw->AddCircleFilled( o + ImVec2( 18, 14 ), 3, IM_COL32( 77, 184, 132, static_cast<int>( 255 * alpha ) ), 12 );
        draw->AddText( kit::font( ), 12.f, o + ImVec2( 28, 7 ), IM_COL32( 236, 236, 239, static_cast<int>( 255 * alpha ) ), m_status.c_str( ) );
    }
    else
    {
        const char* hint = app( ) ? ( m_design ? "Design \xc2\xb7 drag to move, Shift for several, handles resize, Ctrl+scroll zooms"
                                               : "Interact \xc2\xb7 buttons change screens as they will in your app" )
                                  : ( m_design ? "Design \xc2\xb7 click a row on the canvas to edit it" : "Interact \xc2\xb7 the menu works as it will in your app" );
        label( draw, o + ImVec2( 18, 7 ), kit::tone::muted, hint, 12 );
    }

    // Middle: counts.
    char counts[128];
    if ( app( ) )
    {
        size_t elements = 0;
        for ( const auto& s : m_doc.screens )
            elements += s.elements.size( );
        std::snprintf( counts, sizeof( counts ), "%zu screens \xc2\xb7 %zu elements \xc2\xb7 %zu components \xc2\xb7 %.0f \xc3\x97 %.0f",
                       m_doc.screens.size( ), elements, m_doc.components.size( ), m_doc.theme.width, m_doc.theme.height );
    }
    else
    {
        int features = 0, rows = 0;
        for ( const auto& c : m_doc.categories )
            for ( const auto& g : c.groups )
                for ( const auto& f : g.features )
                {
                    ++features;
                    for ( const auto& s : f.sections )
                        rows += static_cast<int>( s.widgets.size( ) );
                }
        std::snprintf( counts, sizeof( counts ), "%zu categories \xc2\xb7 %d features \xc2\xb7 %d rows", m_doc.categories.size( ), features, rows );
    }
    label( draw, { std::floor( o.x + ( m_size.x - text_size( counts, 12 ) ) * .5f ), o.y + 7 }, kit::tone::muted, counts, 12 );

    // Right: the code panel toggle.
    const char* toggle = m_code_open ? "Hide code" : "Show code";
    const float tw = text_size( toggle, 12 );
    const ImRect bb( o + ImVec2( m_size.x - 18 - tw - 12, 3 ), o + ImVec2( m_size.x - 12, ed::status - 3 ) );
    const ImGuiID id = ImGui::GetID( "##code_toggle" );
    ImGui::ItemAdd( bb, id );
    bool hovered = false, held = false;
    if ( ImGui::ButtonBehavior( bb, id, &hovered, &held ) )
        m_code_open = !m_code_open;
    if ( hovered )
        draw->AddRectFilled( bb.Min, bb.Max, kit::tone::white( 12 ), 6 );
    label( draw, { bb.Min.x + 6, o.y + 7 }, hovered ? kit::tone::title : kit::tone::label, toggle, 12 );
}
