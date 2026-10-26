#include "kit.h"
#include "inter_font.h"

#include <windows.h>
#include <objbase.h>
#include <wincodec.h>

#pragma comment( lib, "windowscodecs.lib" )
#pragma comment( lib, "ole32.lib" )

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace
{
    ImFont* menu_font = nullptr;

    struct layout_t
    {
        float x = 0, width = 0, top = 0;
        float y[4]{ };
        int count = 1, column = 0;
        bool section_open = false;
    } layout;

    ImVec2 surface_origin, surface_size, surface_offset;
    bool surface_faded = false;

    struct toast_entry_t
    {
        std::string text;
        kit::toast_t kind;
        double at;
    };
    std::vector<toast_entry_t> toasts;

    // Popups open at the drawn position of their anchor and read the real mouse.
    auto popup_at( ImVec2 p ) -> ImVec2
    {
        return kit::to_screen ? kit::to_screen( p ) : p;
    }

    struct popup_mouse_t
    {
        popup_mouse_t( )
        {
            if ( kit::popup_mouse )
                kit::popup_mouse( true );
        }
        ~popup_mouse_t( )
        {
            if ( kit::popup_mouse )
                kit::popup_mouse( false );
        }
    };

    ImGuiID binding = 0;
    int binding_frame = 0;
    bool held_keys[256]{ };

    auto current( ) -> ImGuiWindow*
    {
        return ImGui::GetCurrentWindow( );
    }

    auto radius( ) -> float
    {
        return std::max( 0.f, kit::theme.control_rounding );
    }

    auto column_width( ) -> float
    {
        return ( layout.width - kit::metric::column_gap * static_cast<float>( layout.count - 1 ) ) /
               static_cast<float>( layout.count );
    }

    auto column_x( ) -> float
    {
        return current( )->Pos.x + layout.x + static_cast<float>( layout.column ) * ( column_width( ) + kit::metric::column_gap );
    }

    auto rect( ImVec2 min, ImVec2 size ) -> ImRect
    {
        return { min, min + size };
    }

    auto mix( ImU32 a, ImU32 b, float t ) -> ImU32
    {
        return ImGui::ColorConvertFloat4ToU32( ImLerp( ImGui::ColorConvertU32ToFloat4( a ), ImGui::ColorConvertU32ToFloat4( b ), t ) );
    }

    void frame( ImDrawList* draw, const ImRect& bb, ImU32 fill, ImU32 border, float rounding )
    {
        if ( fill )
            draw->AddRectFilled( bb.Min, bb.Max, kit::faded( fill ), rounding );
        if ( border )
            draw->AddRect( bb.Min + ImVec2( .5f, .5f ), bb.Max - ImVec2( .5f, .5f ), kit::faded( border ), rounding );
    }

    auto centred( const ImRect& bb, float size = 13.f ) -> float
    {
        return std::floor( bb.Min.y + ( bb.GetHeight( ) - size ) * .5f );
    }

    void report( const ImRect& bb )
    {
        if ( kit::on_row && kit::tag >= 0 )
            kit::on_row( kit::tag, bb );
    }

    // The visible part of a label ("Sign in##login" shows "Sign in").
    auto visible( const char* label ) -> std::string
    {
        return std::string( label, ImGui::FindRenderedTextEnd( label ) );
    }

    auto next_row( const char* label, const char* hint, float height = kit::metric::row ) -> ImRect
    {
        ImGuiWindow* w = current( );
        const float x = column_x( );
        const float y = w->Pos.y - w->Scroll.y + layout.y[layout.column];
        layout.y[layout.column] += height;
        const ImRect bb( { x, y }, { x + column_width( ), y + height } );
        w->DrawList->AddLine( { bb.Min.x, bb.Min.y + .5f }, { bb.Max.x, bb.Min.y + .5f }, kit::faded( kit::tone::row_line ) );
        if ( label )
        {
            if ( hint && *hint )
            {
                kit::text( w->DrawList, { bb.Min.x, bb.Min.y + 6 }, kit::tone::label, label );
                kit::text( w->DrawList, { bb.Min.x, bb.Min.y + 24 }, kit::tone::muted, hint, 11.5f );
            }
            else
                kit::text( w->DrawList, { bb.Min.x, centred( bb ) }, kit::tone::label, label );
        }
        report( bb );
        return bb;
    }

    auto behave( const ImRect& bb, ImGuiID id, bool& hovered, bool& held, ImGuiButtonFlags flags = 0 ) -> bool
    {
        if ( !ImGui::ItemAdd( bb, id ) )
        {
            hovered = held = false;
            return false;
        }
        return ImGui::ButtonBehavior( bb, id, &hovered, &held, flags );
    }

    void check_mark( ImDrawList* draw, ImVec2 at, ImU32 color )
    {
        const ImVec2 points[] = { at + ImVec2( 2.f, 5.2f ), at + ImVec2( 4.1f, 7.2f ), at + ImVec2( 8.f, 3.f ) };
        draw->AddPolyline( points, 3, kit::faded( color ), ImDrawFlags_None, 1.6f );
    }

    void chevron( ImDrawList* draw, ImVec2 at, ImU32 color )
    {
        const ImVec2 points[] = { at + ImVec2( 3.f, 4.5f ), at + ImVec2( 6.f, 7.5f ), at + ImVec2( 9.f, 4.5f ) };
        draw->AddPolyline( points, 3, kit::faded( color ), ImDrawFlags_None, 1.4f );
    }

    void push_popup_style( )
    {
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 4, 4 } );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0, 0 } );
        ImGui::PushStyleVar( ImGuiStyleVar_PopupRounding, std::min( 12.f, radius( ) + 2.f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_PopupBorderSize, 1 );
        ImGui::PushStyleColor( ImGuiCol_PopupBg, kit::tone::popup );
        ImGui::PushStyleColor( ImGuiCol_Border, kit::tone::window_border );
    }

    void pop_popup_style( )
    {
        ImGui::PopStyleColor( 2 );
        ImGui::PopStyleVar( 4 );
    }

    auto choice_popup( ImGuiID id, const ImRect& anchor, int& value, kit::items options ) -> bool
    {
        if ( !ImGui::IsPopupOpen( id, ImGuiPopupFlags_None ) )
            return false;
        float width = anchor.GetWidth( );
        for ( const char* option : options )
            width = std::max( width, kit::text_width( option ) + 44 );
        const popup_mouse_t real_mouse;
        ImGui::SetNextWindowPos( popup_at( { anchor.Max.x, anchor.Max.y + 4 } ), ImGuiCond_Always, { 1, 0 } );
        ImGui::SetNextWindowSize( { width, 0 } );
        push_popup_style( );
        bool changed = false;
        if ( ImGui::BeginPopupEx( id, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) )
        {
            ImGuiWindow* w = current( );
            for ( int i = 0; i < static_cast<int>( options.size( ) ); ++i )
            {
                const ImVec2 at = w->DC.CursorPos;
                const ImRect bb = rect( at, { w->WorkRect.Max.x - at.x, 30 } );
                ImGui::ItemSize( bb.GetSize( ) );
                bool hovered = false, held = false;
                if ( behave( bb, w->GetID( i ), hovered, held ) )
                {
                    value = i;
                    changed = true;
                    ImGui::CloseCurrentPopup( );
                }
                if ( hovered )
                    w->DrawList->AddRectFilled( bb.Min, bb.Max, kit::faded( kit::tone::overlay( 14 ) ), std::max( 0.f, radius( ) - 1 ) );
                kit::text( w->DrawList, { bb.Min.x + 10, centred( bb ) }, value == i ? kit::tone::title : kit::tone::label,
                           options[static_cast<size_t>( i )] );
                if ( value == i )
                    check_mark( w->DrawList, { bb.Max.x - 22, bb.Min.y + 10 }, kit::tone::title );
            }
            ImGui::EndPopup( );
        }
        pop_popup_style( );
        return changed;
    }

    void snapshot_keys( )
    {
        for ( int vk = 1; vk < 256; ++vk )
            held_keys[vk] = ( GetAsyncKeyState( vk ) & 0x8000 ) != 0;
    }

    auto is_mouse( int vk ) -> bool
    {
        return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
    }

    auto poll_vk( int& out ) -> bool
    {
        constexpr int buttons[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
        for ( int i = 0; i < 5; ++i )
            if ( ImGui::IsMouseClicked( i ) )
            {
                out = buttons[i];
                return true;
            }
        for ( int vk = VK_BACK; vk <= VK_OEM_CLEAR; ++vk )
        {
            const bool down = ( GetAsyncKeyState( vk ) & 0x8000 ) != 0;
            const bool fresh = down && !held_keys[vk];
            held_keys[vk] = down;
            if ( !fresh || is_mouse( vk ) || ( vk >= VK_LSHIFT && vk <= VK_RMENU ) )
                continue;
            out = vk == VK_ESCAPE ? 0 : vk;
            return true;
        }
        return false;
    }

    // ---- painters shared by rows and free elements -------------------------------------------------------------

    // The rect a toggle control takes: a 30 x 18 pill, or a square checkbox of the same height.
    auto toggle_rect( ImVec2 pos, ImVec2 size, bool flush_right ) -> ImRect
    {
        if ( kit::theme.toggles == kit::toggle_t::check )
        {
            const float side = size.y;
            return rect( { flush_right ? pos.x + size.x - side : pos.x, pos.y }, { side, side } );
        }
        return rect( pos, size );
    }

    void paint_toggle( ImDrawList* draw, const ImRect& bb, ImGuiID id, bool value, bool hovered )
    {
        auto* storage = ImGui::GetStateStorage( );
        const float goal = value ? 1.f : 0.f;
        const float t = ImLerp( storage->GetFloat( id, goal ), goal, kit::approach( 24.f ) );
        storage->SetFloat( id, t );
        const ImU32 on = kit::tone::accent( );
        if ( kit::theme.toggles == kit::toggle_t::check )
        {
            const float r = std::min( radius( ), 5.f );
            const ImU32 off = hovered ? kit::tone::control_hover : kit::tone::control;
            draw->AddRectFilled( bb.Min, bb.Max, kit::faded( mix( off, on, t ) ), r );
            if ( t < .99f )
                draw->AddRect( bb.Min + ImVec2( .5f, .5f ), bb.Max - ImVec2( .5f, .5f ),
                               kit::faded( mix( hovered ? kit::tone::control_border_hover : kit::tone::control_border, on, t ) ), r );
            const float s = bb.GetWidth( );
            const ImVec2 points[] = { bb.Min + ImVec2( s * .26f, s * .52f ), bb.Min + ImVec2( s * .43f, s * .69f ),
                                      bb.Min + ImVec2( s * .75f, s * .33f ) };
            draw->AddPolyline( points, 3, kit::faded( IM_COL32( 255, 255, 255, static_cast<int>( 255 * t ) ) ), ImDrawFlags_None, 1.8f );
            return;
        }
        const ImU32 off = hovered ? kit::tone::overlay( 56 ) : kit::tone::track_off;
        const float height = bb.GetHeight( );
        draw->AddRectFilled( bb.Min, bb.Max, kit::faded( mix( off, on, t ) ), std::min( height * .5f, std::max( radius( ), 3.f ) * 1.5f ) );
        const float knob = height - 6.f;
        const ImVec2 centre( ImLerp( bb.Min.x + 3 + knob * .5f, bb.Max.x - 3 - knob * .5f, t ), bb.GetCenter( ).y );
        const ImU32 knob_color = mix( kit::tone::knob_off, IM_COL32( 255, 255, 255, 255 ), t );
        if ( radius( ) >= 4 )
            draw->AddCircleFilled( centre, knob * .5f, kit::faded( knob_color ), 24 );
        else
            draw->AddRectFilled( centre - ImVec2( knob * .5f, knob * .5f ), centre + ImVec2( knob * .5f, knob * .5f ),
                                 kit::faded( knob_color ), radius( ) * .5f );
    }

    // Where a slider's value maps along its box: the whole box for the bar, the track for the track style.
    auto slider_span( const ImRect& bb, float& reserve ) -> std::pair<float, float>
    {
        if ( kit::theme.sliders == kit::slider_t::track )
        {
            reserve = std::min( 56.f, bb.GetWidth( ) * .35f );
            return { bb.Min.x + 7, bb.Max.x - reserve - 7 };
        }
        reserve = 0;
        return { bb.Min.x + 1, bb.Max.x - 1 };
    }

    auto slider_behaviour( const ImRect& bb, ImGuiID id, float& value, float min, float max, bool& hovered, bool& held ) -> bool
    {
        behave( bb, id, hovered, held, ImGuiButtonFlags_PressedOnClick );
        bool changed = false;
        float reserve = 0;
        const auto [x0, x1] = slider_span( bb, reserve );
        if ( held && max > min && x1 > x0 )
        {
            const float t = ImSaturate( ( ImGui::GetIO( ).MousePos.x - x0 ) / ( x1 - x0 ) );
            const float next = min + ( max - min ) * t;
            changed = next != value;
            value = next;
        }
        if ( !std::isfinite( value ) )
            value = min;
        return changed;
    }

    void paint_slider( ImDrawList* draw, const ImRect& bb, float value, float min, float max, const char* format, const char* unit,
                       bool hovered, bool held )
    {
        const float t = max > min ? ImSaturate( ( value - min ) / ( max - min ) ) : 0.f;
        char number[32];
        std::snprintf( number, sizeof( number ), format && *format ? format : "%.0f", value );
        const float unit_w = unit && *unit ? kit::text_width( unit ) : 0.f;
        const float r = radius( );
        if ( kit::theme.sliders == kit::slider_t::track )
        {
            float reserve = 0;
            const auto [x0, x1] = slider_span( bb, reserve );
            const float mid = bb.GetCenter( ).y;
            draw->AddRectFilled( { x0, mid - 2 }, { x1, mid + 2 }, kit::faded( kit::tone::overlay( 28 ) ), 2 );
            const float knob_x = std::floor( ImLerp( x0, x1, t ) );
            draw->AddRectFilled( { x0, mid - 2 }, { knob_x, mid + 2 }, kit::faded( kit::tone::accent( ) ), 2 );
            const float k = held ? 7.5f : hovered ? 7.f : 6.5f;
            if ( r >= 4 )
                draw->AddCircleFilled( { knob_x, mid }, k, kit::faded( IM_COL32( 255, 255, 255, 255 ) ), 24 );
            else
                draw->AddRectFilled( { knob_x - k, mid - k }, { knob_x + k, mid + k }, kit::faded( IM_COL32( 255, 255, 255, 255 ) ), r * .5f );
            kit::text( draw, { bb.Max.x - unit_w - kit::text_width( number ), centred( bb ) }, kit::tone::primary, number );
            if ( unit_w > 0 )
                kit::text( draw, { bb.Max.x - unit_w, centred( bb ) }, kit::tone::muted, unit );
            return;
        }
        frame( draw, bb, kit::tone::control, hovered || held ? kit::tone::control_border_hover : kit::tone::control_border, r );
        const float inner = bb.GetWidth( ) - 2;
        const float fill = std::floor( inner * t );
        if ( fill > 0 )
            draw->AddRectFilled( bb.Min + ImVec2( 1, 1 ), { bb.Min.x + 1 + fill, bb.Max.y - 1 },
                                 kit::faded( held ? kit::tone::overlay( 28 ) : kit::tone::overlay( 20 ) ), std::max( 0.f, r - 1 ),
                                 fill >= inner - r ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft );
        const float text_x = bb.Max.x - 10 - unit_w - kit::text_width( number );
        // The handle hides while it passes behind the value.
        const float handle = std::clamp( bb.Min.x + 1 + fill - 1, bb.Min.x + 4, bb.Max.x - 6 );
        if ( handle + 2 < text_x - 4 || handle > bb.Max.x - 8 )
            draw->AddRectFilled( { handle, bb.Min.y + 6 }, { handle + 2, bb.Max.y - 6 }, kit::faded( kit::tone::title ), 1 );
        kit::text( draw, { text_x, centred( bb ) }, kit::tone::primary, number );
        if ( unit_w > 0 )
            kit::text( draw, { bb.Max.x - 10 - unit_w, centred( bb ) }, kit::tone::muted, unit );
    }

    // A dropdown box; the choice list opens under it.
    auto combo( const ImRect& bb, ImGuiID id, int& value, kit::items options ) -> bool
    {
        ImDrawList* draw = current( )->DrawList;
        if ( options.empty( ) )
            return false;
        value = std::clamp( value, 0, static_cast<int>( options.size( ) ) - 1 );
        bool hovered = false, held = false;
        if ( behave( bb, id, hovered, held ) )
            ImGui::OpenPopupEx( id );
        const bool open = ImGui::IsPopupOpen( id, ImGuiPopupFlags_None );
        if ( kit::theme.fields == kit::field_t::line )
            draw->AddLine( { bb.Min.x, bb.Max.y - .5f }, { bb.Max.x, bb.Max.y - .5f },
                           kit::faded( open ? kit::tone::accent( ) : hovered ? kit::tone::control_border_hover : kit::tone::overlay( 40 ) ) );
        else
            frame( draw, bb, hovered || open ? kit::tone::control_hover : kit::tone::control,
                   hovered || open ? kit::tone::control_border_hover : kit::tone::control_border, radius( ) );
        const float pad = kit::theme.fields == kit::field_t::line ? 2.f : 10.f;
        draw->PushClipRect( bb.Min, { bb.Max.x - 24, bb.Max.y }, true );
        kit::text( draw, { bb.Min.x + pad, centred( bb ) }, kit::tone::primary, options[static_cast<size_t>( value )] );
        draw->PopClipRect( );
        chevron( draw, { bb.Max.x - 20, std::floor( bb.GetCenter( ).y - 6 ) }, kit::tone::muted );
        return choice_popup( id, bb, value, options );
    }

    // A text box in the field style, with ImGui's input inside it.
    auto field( const ImRect& bb, const char* id, char* buffer, size_t capacity, const char* hint, ImGuiInputTextFlags flags ) -> bool
    {
        ImGuiWindow* w = current( );
        ImGui::PushID( id );
        const ImGuiID input = ImGui::GetID( "##field" );
        const bool focused = ImGui::GetActiveID( ) == input;
        const bool hovered = ImGui::IsMouseHoveringRect( bb.Min, bb.Max );
        const bool line = kit::theme.fields == kit::field_t::line;
        if ( line )
            w->DrawList->AddLine( { bb.Min.x, bb.Max.y - .5f }, { bb.Max.x, bb.Max.y - .5f },
                                  kit::faded( focused ? kit::tone::accent( ) : hovered ? kit::tone::control_border_hover : kit::tone::overlay( 40 ) ),
                                  focused ? 1.5f : 1.f );
        else
            frame( w->DrawList, bb, focused ? kit::tone::control_hover : kit::tone::control,
                   focused ? kit::tone::accent( 150 ) : hovered ? kit::tone::control_border_hover : kit::tone::control_border, radius( ) );
        const float pad = line ? 2.f : 12.f;
        ImGui::SetCursorScreenPos( { bb.Min.x + pad, bb.Min.y } );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, { 0, std::floor( ( bb.GetHeight( ) - 13.f ) * .5f ) } );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.f );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, IM_COL32( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgActive, IM_COL32( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_Text, kit::tone::primary );
        ImGui::PushStyleColor( ImGuiCol_TextDisabled, kit::tone::muted );
        ImGui::PushStyleColor( ImGuiCol_TextSelectedBg, kit::tone::accent( 90 ) );
        ImGui::PushStyleColor( ImGuiCol_InputTextCursor, kit::tone::title );
        ImGui::SetNextItemWidth( bb.GetWidth( ) - pad * 2 );
        const bool changed = ImGui::InputTextWithHint( "##field", hint ? hint : "", buffer, capacity, flags );
        ImGui::PopStyleColor( 7 );
        ImGui::PopStyleVar( 2 );
        ImGui::PopID( );
        return changed;
    }

    enum class button_kind
    {
        themed,   // the theme's button style
        light,    // the palette's primary pill (shell actions)
        accent    // filled with the accent (free elements' primary)
    };

    // Light text on dark fills and dark text on light ones.
    auto ink_on( ImU32 fill ) -> ImU32
    {
        const ImVec4 c = ImGui::ColorConvertU32ToFloat4( fill );
        return c.x * .299f + c.y * .587f + c.z * .114f > .62f ? IM_COL32( 20, 20, 22, 255 ) : IM_COL32( 255, 255, 255, 255 );
    }

    void paint_button( ImDrawList* draw, const ImRect& bb, const char* label, button_kind kind, bool hovered, bool held,
                       kit::icon_t icon = kit::icon_t::none, ImU32 fill = 0 )
    {
        const float r = radius( ) + 1;
        ImU32 text_color = kit::tone::primary;
        if ( fill )
        {
            draw->AddRectFilled( bb.Min, bb.Max, kit::faded( hovered ? mix( fill, IM_COL32( 255, 255, 255, 255 ), .12f ) : fill ), r );
            text_color = ink_on( fill );
        }
        else if ( kind == button_kind::light )
        {
            draw->AddRectFilled( bb.Min, bb.Max, kit::faded( hovered ? mix( kit::palette.button, IM_COL32( 255, 255, 255, 255 ), .4f ) : kit::palette.button ), r );
            text_color = kit::palette.ink;
        }
        else if ( kind == button_kind::accent )
        {
            draw->AddRectFilled( bb.Min, bb.Max, kit::faded( hovered ? mix( kit::tone::accent( ), IM_COL32( 255, 255, 255, 255 ), .12f ) : kit::tone::accent( ) ), r );
            text_color = IM_COL32( 255, 255, 255, 255 );
        }
        else
            switch ( kit::theme.buttons )
            {
            case kit::button_t::solid: frame( draw, bb, kit::tone::overlay( hovered ? 30 : 20 ), 0, r ); break;
            case kit::button_t::outline:
                frame( draw, bb, hovered ? kit::tone::overlay( 8 ) : 0, kit::tone::overlay( hovered ? 60 : 40 ), r );
                break;
            default: frame( draw, bb, kit::tone::overlay( hovered ? 20 : 10 ), kit::tone::overlay( hovered ? 40 : 26 ), r ); break;
            }
        const std::string shown = visible( label );
        const float text_w = kit::text_width( shown.c_str( ) );
        const bool has_icon = icon != kit::icon_t::none;
        constexpr float icon_w = 15.f, gap = 8.f;
        const float width = text_w + ( has_icon ? icon_w + ( shown.empty( ) ? 0.f : gap ) : 0.f );
        const float x = std::floor( bb.GetCenter( ).x - width * .5f );
        const float nudge = held ? 1.f : 0.f;
        if ( has_icon )
            kit::icon( draw, icon, { x + icon_w * .5f, bb.GetCenter( ).y + nudge }, icon_w, kit::faded( text_color ) );
        kit::text( draw, { has_icon ? x + icon_w + gap : x, centred( bb ) + nudge }, text_color, shown.c_str( ) );
    }
}

// ---- palette -------------------------------------------------------------------------------------------------------

kit::palette_t kit::preset( palette_id id )
{
    palette_t p;
    switch ( id )
    {
    case palette_id::midnight:
        p.window = { 14 / 255.f, 17 / 255.f, 28 / 255.f, 1 };
        p.overlay = { 200 / 255.f, 215 / 255.f, 1.f, 1 };
        p.title = IM_COL32( 240, 243, 250, 255 );
        p.primary = IM_COL32( 228, 232, 242, 255 );
        p.label = IM_COL32( 190, 198, 215, 255 );
        p.secondary = IM_COL32( 140, 150, 172, 255 );
        p.muted = IM_COL32( 120, 131, 154, 255 );
        p.knob_off = IM_COL32( 215, 220, 232, 255 );
        p.popup = IM_COL32( 24, 28, 42, 250 );
        p.button = IM_COL32( 236, 240, 250, 255 );
        p.ink = IM_COL32( 14, 17, 28, 255 );
        break;
    case palette_id::light:
        p.window = { 247 / 255.f, 247 / 255.f, 249 / 255.f, 1 };
        p.overlay = { 0, 0, 0, 1 };
        p.overlay_gain = 1.5f;
        p.title = IM_COL32( 17, 17, 20, 255 );
        p.primary = IM_COL32( 30, 30, 35, 255 );
        p.label = IM_COL32( 58, 58, 66, 255 );
        p.secondary = IM_COL32( 98, 98, 108, 255 );
        p.muted = IM_COL32( 124, 124, 134, 255 );
        p.knob_off = IM_COL32( 255, 255, 255, 255 );
        p.popup = IM_COL32( 255, 255, 255, 252 );
        p.button = IM_COL32( 24, 24, 28, 255 );
        p.ink = IM_COL32( 250, 250, 252, 255 );
        break;
    case palette_id::oled:
        p.window = { 0, 0, 0, 1 };
        p.overlay_gain = 1.1f;
        p.popup = IM_COL32( 12, 12, 14, 252 );
        p.ink = IM_COL32( 8, 8, 10, 255 );
        break;
    default: break;
    }
    return p;
}

void kit::use_palette( const palette_t& value )
{
    palette = value;
    tone::hairline = tone::overlay( 18 );
    tone::row_line = tone::overlay( 15 );
    tone::list_fill = tone::overlay( 4 );
    tone::control = tone::overlay( 13 );
    tone::control_hover = tone::overlay( 20 );
    tone::control_border = tone::overlay( 20 );
    tone::control_border_hover = tone::overlay( 36 );
    tone::selected = tone::overlay( 18 );
    tone::hover = tone::overlay( 10 );
    tone::track_off = tone::overlay( 41 );
    tone::window_border = tone::overlay( 23 );
    tone::title = value.title;
    tone::primary = value.primary;
    tone::label = value.label;
    tone::secondary = value.secondary;
    tone::muted = value.muted;
    tone::knob_off = value.knob_off;
    tone::popup = value.popup;
    tone::ink = value.ink;
}

ImU32 kit::tone::overlay( int alpha )
{
    const ImVec4& o = palette.overlay;
    return IM_COL32( static_cast<int>( o.x * 255.f ), static_cast<int>( o.y * 255.f ), static_cast<int>( o.z * 255.f ),
                     std::clamp( static_cast<int>( static_cast<float>( alpha ) * palette.overlay_gain ), 0, 255 ) );
}

ImU32 kit::tone::accent( int alpha )
{
    return IM_COL32( static_cast<int>( theme.accent.x * 255.f ), static_cast<int>( theme.accent.y * 255.f ),
                     static_cast<int>( theme.accent.z * 255.f ), alpha );
}

ImU32 kit::tone::window( )
{
    const ImVec4& w = palette.window;
    return IM_COL32( static_cast<int>( w.x * 255.f ), static_cast<int>( w.y * 255.f ), static_cast<int>( w.z * 255.f ),
                     static_cast<int>( std::clamp( theme.window_alpha, 0.f, 1.f ) * 255.f ) );
}

// ---- fonts and drawing ---------------------------------------------------------------------------------------------

ImFont* kit::load_font( ImGuiIO& io, float size )
{
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = 2;
    config.RasterizerMultiply = 1.3f;
    menu_font = io.Fonts->AddFontFromMemoryTTF( const_cast<unsigned char*>( inter_medium ), sizeof( inter_medium ), size, &config );
    return menu_font;
}

void kit::set_font( ImFont* value )
{
    menu_font = value;
}

ImFont* kit::font( )
{
    return menu_font ? menu_font : ImGui::GetFont( );
}

float kit::text_width( const char* value, float size )
{
    return font( )->CalcTextSizeA( size, FLT_MAX, 0.f, value ).x;
}

ImU32 kit::faded( ImU32 color )
{
    return ImGui::GetColorU32( color );
}

void kit::text( ImDrawList* draw, ImVec2 pos, ImU32 color, const char* value, float size )
{
    draw->AddText( font( ), size, ImFloor( pos ), faded( color ), value );
}

void kit::caps( ImDrawList* draw, ImVec2 pos, ImU32 color, const char* value )
{
    constexpr float size = 11.f, spacing = .66f;
    char glyph[2]{ };
    float x = std::floor( pos.x );
    for ( const char* c = value; *c; ++c )
    {
        glyph[0] = static_cast<char>( std::toupper( static_cast<unsigned char>( *c ) ) );
        draw->AddText( font( ), size, { x, std::floor( pos.y ) }, faded( color ), glyph );
        x += font( )->CalcTextSizeA( size, FLT_MAX, 0.f, glyph ).x + spacing;
    }
}

float kit::approach( float speed )
{
    return 1.f - std::exp( -std::clamp( ImGui::GetIO( ).DeltaTime, 0.f, .1f ) * speed );
}

// ---- primitives ----------------------------------------------------------------------------------------------------

bool kit::switch_at( ImGuiID id, ImVec2 pos, bool& value, ImVec2 size )
{
    ImGuiWindow* w = current( );
    const ImRect bb = toggle_rect( ImFloor( pos ), size, true );
    bool hovered = false, held = false;
    const bool pressed = behave( bb, id, hovered, held );
    if ( pressed )
        value = !value;
    paint_toggle( w->DrawList, bb, id, value, hovered );
    return pressed;
}

const char* kit::key_name( int vk )
{
    switch ( vk )
    {
    case 0: return "None";
    case VK_LBUTTON: return "Mouse 1";
    case VK_RBUTTON: return "Mouse 2";
    case VK_MBUTTON: return "Mouse 3";
    case VK_XBUTTON1: return "Mouse 4";
    case VK_XBUTTON2: return "Mouse 5";
    case VK_BACK: return "Backspace";
    case VK_TAB: return "Tab";
    case VK_RETURN: return "Enter";
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "Shift";
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "Ctrl";
    case VK_MENU: case VK_LMENU: case VK_RMENU: return "Alt";
    case VK_CAPITAL: return "Caps";
    case VK_SPACE: return "Space";
    case VK_PRIOR: return "Page Up";
    case VK_NEXT: return "Page Down";
    case VK_END: return "End";
    case VK_HOME: return "Home";
    case VK_LEFT: return "Left";
    case VK_UP: return "Up";
    case VK_RIGHT: return "Right";
    case VK_DOWN: return "Down";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    default: break;
    }
    static char name[16];
    if ( ( vk >= '0' && vk <= '9' ) || ( vk >= 'A' && vk <= 'Z' ) )
        std::snprintf( name, sizeof( name ), "%c", static_cast<char>( vk ) );
    else if ( vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9 )
        std::snprintf( name, sizeof( name ), "Num %d", vk - VK_NUMPAD0 );
    else if ( vk >= VK_F1 && vk <= VK_F24 )
        std::snprintf( name, sizeof( name ), "F%d", vk - VK_F1 + 1 );
    else
        std::snprintf( name, sizeof( name ), "0x%02X", vk );
    return name;
}

const char* kit::mode_name( int mode )
{
    return mode == 1 ? "Toggle" : mode == 2 ? "Always" : "Hold";
}

std::string kit::keyed( int vk, int mode )
{
    return vk ? std::string( mode_name( mode ) ) + " \xc2\xb7 " + key_name( vk ) : std::string( "No key" );
}

float kit::keycap( int& vk, int* mode, float right_x, float mid_y, bool* changed_out )
{
    ImGuiWindow* w = current( );
    const ImGuiID id = w->GetID( &vk );
    const bool waiting = binding == id;
    const char* name = waiting ? "Press a key" : key_name( vk );
    constexpr float size = 11.f;
    const float width = std::floor( text_width( name, size ) + 14 );
    const ImRect bb( { std::floor( right_x - width ), std::floor( mid_y - 10 ) }, { std::floor( right_x ), std::floor( mid_y + 10 ) } );
    bool hovered = false, held = false, changed = false;
    if ( behave( bb, id, hovered, held ) )
    {
        binding = waiting ? 0 : id;
        binding_frame = ImGui::GetFrameCount( );
        snapshot_keys( );
    }
    if ( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
    {
        binding = 0;
        ImGui::OpenPopupEx( id );
    }
    if ( waiting && ImGui::GetFrameCount( ) > binding_frame )
    {
        ImGui::SetNextFrameWantCaptureKeyboard( true );
        if ( poll_vk( vk ) )
        {
            binding = 0;
            changed = true;
        }
    }
    const float r = std::min( radius( ), 5.f );
    const ImU32 border = waiting ? tone::accent( ) : hovered ? tone::overlay( 60 ) : tone::overlay( 36 );
    w->DrawList->AddRectFilled( bb.Min, bb.Max, faded( hovered || waiting ? tone::overlay( 12 ) : tone::overlay( 5 ) ), r );
    w->DrawList->AddRect( bb.Min + ImVec2( .5f, .5f ), bb.Max - ImVec2( .5f, .5f ), faded( border ), r );
    w->DrawList->AddLine( { bb.Min.x + 3, bb.Max.y - 1 }, { bb.Max.x - 3, bb.Max.y - 1 }, faded( border ), 1.f );
    text( w->DrawList, { bb.Min.x + 7, std::floor( mid_y - size * .5f - 1 ) }, waiting ? tone::accent( ) : tone::label, name, size );

    if ( ImGui::IsPopupOpen( id, ImGuiPopupFlags_None ) )
    {
        constexpr const char* with_modes[] = { "Hold", "Toggle", "Always", "Clear key" };
        constexpr const char* clear_only[] = { "Clear key" };
        int picked = mode ? *mode : -1;
        if ( choice_popup( id, bb, picked, mode ? items( with_modes ) : items( clear_only ) ) )
        {
            if ( mode && picked < 3 )
                *mode = picked;
            else
                vk = 0;
            changed = true;
        }
    }
    if ( changed_out )
        *changed_out = changed;
    return bb.Min.x;
}

float kit::action_width( const char* label )
{
    return std::floor( text_width( visible( label ).c_str( ) ) + 28 );
}

bool kit::action_button( const char* label, ImVec2 pos, bool primary, float height )
{
    ImGuiWindow* w = current( );
    const ImRect bb = rect( ImFloor( pos ), { action_width( label ), height } );
    bool hovered = false, held = false;
    const bool pressed = behave( bb, w->GetID( label ), hovered, held );
    paint_button( w->DrawList, bb, label, primary ? button_kind::light : button_kind::themed, hovered, held );
    return pressed;
}

// Colour editor: saturation/value square, hue and alpha strips, hex field.
bool kit::color_popup( ImGuiID id, float* rgba, const ImRect& anchor )
{
    if ( !ImGui::IsPopupOpen( id, ImGuiPopupFlags_None ) )
        return false;
    const popup_mouse_t real_mouse;
    ImGui::SetNextWindowPos( popup_at( { anchor.Max.x, anchor.Max.y + 6 } ), ImGuiCond_Always, { 1, 0 } );
    push_popup_style( );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 12, 12 } );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 8, 10 } );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 6 );
    ImGui::PushStyleVar( ImGuiStyleVar_GrabRounding, 6 );
    ImGui::PushStyleColor( ImGuiCol_FrameBg, tone::control );
    ImGui::PushStyleColor( ImGuiCol_Text, tone::primary );
    bool changed = false;
    if ( ImGui::BeginPopupEx( id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings ) )
    {
        ImGui::SetNextItemWidth( 196 );
        changed = ImGui::ColorPicker4( "##picker", rgba,
                                       ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs |
                                           ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueBar |
                                           ImGuiColorEditFlags_NoLabel );
        char hex[10];
        std::snprintf( hex, sizeof( hex ), "%02X%02X%02X", static_cast<int>( ImSaturate( rgba[0] ) * 255.f + .5f ),
                       static_cast<int>( ImSaturate( rgba[1] ) * 255.f + .5f ), static_cast<int>( ImSaturate( rgba[2] ) * 255.f + .5f ) );
        ImGui::SetNextItemWidth( 196 );
        if ( ImGui::InputText( "##hex", hex, sizeof( hex ), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue ) &&
             std::strlen( hex ) == 6 )
        {
            const auto value = static_cast<unsigned>( std::strtoul( hex, nullptr, 16 ) );
            rgba[0] = static_cast<float>( ( value >> 16 ) & 255 ) / 255.f;
            rgba[1] = static_cast<float>( ( value >> 8 ) & 255 ) / 255.f;
            rgba[2] = static_cast<float>( value & 255 ) / 255.f;
            changed = true;
        }
        ImGui::EndPopup( );
    }
    ImGui::PopStyleColor( 2 );
    ImGui::PopStyleVar( 4 );
    pop_popup_style( );
    return changed;
}

// ---- settings rows -------------------------------------------------------------------------------------------------

void kit::begin_columns( int count )
{
    ImGuiWindow* w = current( );
    layout.count = std::clamp( count, 1, 4 );
    layout.column = 0;
    layout.x = w->DC.CursorPos.x - w->Pos.x;
    layout.top = w->DC.CursorPos.y - w->Pos.y + w->Scroll.y;
    layout.width = w->WorkRect.Max.x - w->DC.CursorPos.x;
    for ( float& y : layout.y )
        y = layout.top;
    layout.section_open = false;
    ImGui::PushID( "##columns" );
}

void kit::next_column( )
{
    if ( layout.section_open )
    {
        ImGui::PopID( );
        layout.section_open = false;
    }
    layout.column = std::min( layout.column + 1, layout.count - 1 );
}

void kit::end_columns( )
{
    if ( layout.section_open )
        ImGui::PopID( );
    layout.section_open = false;
    ImGui::PopID( );
    float bottom = layout.top;
    for ( int i = 0; i < layout.count; ++i )
        bottom = std::max( bottom, layout.y[i] );
    ImGui::SetCursorPos( { layout.x, bottom } );
    ImGui::Dummy( { 1, 16 } );
}

void kit::section( const char* title )
{
    if ( layout.section_open )
        ImGui::PopID( );
    ImGui::PushID( title );
    layout.section_open = true;
    ImGuiWindow* w = current( );
    const float x = column_x( );
    const float y = w->Pos.y - w->Scroll.y + layout.y[layout.column];
    layout.y[layout.column] += metric::section;
    caps( w->DrawList, { x, y + metric::section - 8 - 12 }, tone::muted, title );
    report( { { x, y }, { x + column_width( ), y + metric::section } } );
}

bool kit::toggle( const char* label, bool& value, const char* hint )
{
    const ImRect row = next_row( label, hint );
    return switch_at( current( )->GetID( label ), { row.Max.x - 30, row.Min.y + 13 }, value );
}

bool kit::select( const char* label, int& value, items options, const char* hint )
{
    ImGuiWindow* w = current( );
    const ImRect row = next_row( label, hint );
    const ImRect bb = rect( { row.Max.x - metric::select_w, row.Min.y + 8 }, { metric::select_w, metric::control_h } );
    return combo( bb, w->GetID( label ), value, options );
}

bool kit::segment( const char* label, int& value, items options )
{
    ImGuiWindow* w = current( );
    const ImRect row = next_row( label, nullptr );
    const int count = static_cast<int>( options.size( ) );
    if ( count == 0 )
        return false;
    value = std::clamp( value, 0, count - 1 );
    const ImRect bb = rect( { row.Max.x - metric::select_w, row.Min.y + 8 }, { metric::select_w, metric::control_h } );
    const float r = radius( );
    frame( w->DrawList, bb, tone::control, tone::control_border, r );
    const float cell = ( bb.GetWidth( ) - 4 - static_cast<float>( count - 1 ) * 2 ) / static_cast<float>( count );
    ImGui::PushID( label );
    bool changed = false;
    for ( int i = 0; i < count; ++i )
    {
        const ImRect c = rect( { std::floor( bb.Min.x + 2 + static_cast<float>( i ) * ( cell + 2 ) ), bb.Min.y + 2 },
                               { std::floor( cell ), bb.GetHeight( ) - 4 } );
        bool hovered = false, held = false;
        if ( behave( c, w->GetID( i ), hovered, held ) && value != i )
        {
            value = i;
            changed = true;
        }
        if ( value == i )
            w->DrawList->AddRectFilled( c.Min, c.Max, faded( tone::overlay( 31 ) ), std::max( 0.f, r - 2 ) );
        else if ( hovered )
            w->DrawList->AddRectFilled( c.Min, c.Max, faded( tone::overlay( 10 ) ), std::max( 0.f, r - 2 ) );
        const char* name = options[static_cast<size_t>( i )];
        text( w->DrawList, { std::floor( c.GetCenter( ).x - text_width( name, 12 ) * .5f ), centred( c, 12 ) },
              value == i ? tone::title : tone::secondary, name, 12 );
    }
    ImGui::PopID( );
    return changed;
}

bool kit::slider( const char* label, float& value, float min, float max, const char* format, const char* unit, const char* hint )
{
    ImGuiWindow* w = current( );
    const ImRect row = next_row( label, hint );
    const ImRect bb = rect( { row.Max.x - metric::slider_w, row.Min.y + 8 }, { metric::slider_w, metric::control_h } );
    bool hovered = false, held = false;
    const bool changed = slider_behaviour( bb, w->GetID( label ), value, min, max, hovered, held );
    paint_slider( w->DrawList, bb, value, min, max, format, unit, hovered, held );
    return changed;
}

bool kit::slider( const char* label, int& value, int min, int max, const char* unit, const char* hint )
{
    float v = static_cast<float>( value );
    const bool changed = slider( label, v, static_cast<float>( min ), static_cast<float>( max ), "%.0f", unit, hint );
    value = static_cast<int>( std::lround( v ) );
    return changed;
}

bool kit::chips( const char* label, std::initializer_list<chip_t> list, const char* hint )
{
    return chips( label, std::span<const chip_t>( list.begin( ), list.size( ) ), hint );
}

bool kit::chips( const char* label, std::span<const chip_t> list, const char* hint )
{
    ImGuiWindow* w = current( );
    const ImRect row = next_row( label, hint );
    constexpr float size = 12.f, gap = 6.f;
    const float r = radius( ) >= 7 ? 13.f : radius( );
    float total = 0;
    for ( const auto& chip : list )
        total += std::floor( text_width( chip.name, size ) + ( *chip.value ? 33.f : 20.f ) ) + gap;
    total -= gap;
    float x = row.Max.x - total;
    ImGui::PushID( label );
    bool changed = false;
    for ( const auto& chip : list )
    {
        const bool on = *chip.value;
        const float width = std::floor( text_width( chip.name, size ) + ( on ? 33.f : 20.f ) );
        const ImRect bb = rect( { std::floor( x ), row.Min.y + 9 }, { width, 26 } );
        bool hovered = false, held = false;
        if ( behave( bb, w->GetID( chip.name ), hovered, held ) )
        {
            *chip.value = !on;
            changed = true;
        }
        if ( on )
        {
            frame( w->DrawList, bb, tone::accent( hovered ? 56 : 41 ), tone::accent( 107 ), r );
            check_mark( w->DrawList, { bb.Min.x + 8, bb.Min.y + 8 }, tone::title );
            text( w->DrawList, { bb.Min.x + 23, centred( bb, size ) }, tone::title, chip.name, size );
        }
        else
        {
            frame( w->DrawList, bb, hovered ? tone::overlay( 8 ) : 0, hovered ? tone::overlay( 46 ) : tone::overlay( 26 ), r );
            text( w->DrawList, { bb.Min.x + 10, centred( bb, size ) }, hovered ? tone::label : tone::secondary, chip.name, size );
        }
        x += width + gap;
    }
    ImGui::PopID( );
    return changed;
}

bool kit::color( const char* label, float* rgba, bool* enabled, const char* hint )
{
    ImGuiWindow* w = current( );
    const ImRect row = next_row( label, hint );
    bool changed = false;
    float right = row.Max.x;
    if ( enabled )
    {
        changed |= switch_at( w->GetID( enabled ), { right - 30, row.Min.y + 13 }, *enabled );
        right -= 42;
    }
    const ImRect bb = rect( { right - metric::color_w, row.Min.y + 8 }, { metric::color_w, metric::control_h } );
    const ImGuiID id = w->GetID( rgba );
    bool hovered = false, held = false;
    if ( behave( bb, id, hovered, held ) )
        ImGui::OpenPopupEx( id );
    const bool open = ImGui::IsPopupOpen( id, ImGuiPopupFlags_None );
    const float r = radius( );
    frame( w->DrawList, bb, hovered || open ? tone::control_hover : tone::control,
           hovered || open ? tone::control_border_hover : tone::control_border, r );
    const ImRect swatch = rect( { bb.Min.x + 5, bb.Min.y + 6 }, { 16, 16 } );
    w->DrawList->AddRectFilled( swatch.Min, swatch.Max, faded( ImGui::ColorConvertFloat4ToU32( { rgba[0], rgba[1], rgba[2], 1 } ) ),
                                std::min( r, 4.f ) );
    w->DrawList->AddRect( swatch.Min, swatch.Max, faded( IM_COL32( 0, 0, 0, 52 ) ), std::min( r, 4.f ) );
    char hex[8];
    std::snprintf( hex, sizeof( hex ), "%02X%02X%02X", static_cast<int>( ImSaturate( rgba[0] ) * 255.f + .5f ),
                   static_cast<int>( ImSaturate( rgba[1] ) * 255.f + .5f ), static_cast<int>( ImSaturate( rgba[2] ) * 255.f + .5f ) );
    text( w->DrawList, { bb.Min.x + 29, centred( bb, 12 ) }, tone::secondary, hex, 12 );
    changed |= color_popup( id, rgba, bb );
    return changed;
}

bool kit::keybind( const char* label, int& vk, int* mode, bool* enabled, const char* hint )
{
    ImGuiWindow* w = current( );
    const ImRect row = next_row( label, hint );
    bool changed = false;
    float right = row.Max.x;
    if ( enabled )
    {
        changed |= switch_at( w->GetID( enabled ), { right - 30, row.Min.y + 13 }, *enabled );
        right -= 42;
    }
    bool bound = false;
    const float left = keycap( vk, mode, right, row.GetCenter( ).y, &bound );
    if ( mode )
    {
        const char* name = mode_name( *mode );
        text( w->DrawList, { left - 8 - text_width( name, 12 ), centred( row, 12 ) }, tone::secondary, name, 12 );
    }
    return changed || bound;
}

void kit::value( const char* label, const char* text_value, ImU32 color )
{
    ImGuiWindow* w = current( );
    const ImRect row = next_row( label, nullptr );
    text( w->DrawList, { row.Max.x - text_width( text_value ), centred( row ) }, color ? color : tone::secondary, text_value );
}

bool kit::button( const char* label, const char* text_value, bool primary )
{
    const ImRect row = next_row( label, nullptr );
    ImGui::PushID( label );
    const bool pressed = action_button( text_value, { row.Max.x - action_width( text_value ), row.Min.y + 8 }, primary, 28 );
    ImGui::PopID( );
    return pressed;
}

void kit::note( const char* text_value, ImU32 color )
{
    ImGuiWindow* w = current( );
    constexpr float size = 12.f;
    const float wrap = column_width( );
    const float height = font( )->CalcTextSizeA( size, FLT_MAX, wrap, text_value ).y;
    const ImRect row = next_row( nullptr, nullptr, std::floor( height ) + 24 );
    w->DrawList->AddText( font( ), size, { row.Min.x, row.Min.y + 12 }, faded( color ? color : tone::muted ), text_value, nullptr, wrap );
}

bool kit::text_field( const char* label, char* buffer, size_t capacity, const char* hint, float width )
{
    const ImRect row = next_row( label, nullptr );
    const ImRect bb = rect( { row.Max.x - width, row.Min.y + 8 }, { width, metric::control_h } );
    return field( bb, label, buffer, capacity, hint, ImGuiInputTextFlags_None );
}

ImRect kit::block( float height )
{
    ImGuiWindow* w = current( );
    const float x = column_x( );
    const float y = w->Pos.y - w->Scroll.y + layout.y[layout.column];
    layout.y[layout.column] += height;
    const ImRect bb( { x, y }, { x + column_width( ), y + height } );
    report( bb );
    return bb;
}

// ---- shell -------------------------------------------------------------------------------------------------------

namespace
{
    void tabs( ImDrawList* draw, ImVec2 o, const std::vector<kit::category_t>& categories, int& category )
    {
        ImGuiWindow* w = current( );
        float x = o.x + std::max( 140.f, kit::theme.list_width + 12 );
        float target_x = 0, target_w = 0;
        for ( int i = 0; i < static_cast<int>( categories.size( ) ); ++i )
        {
            const char* name = categories[static_cast<size_t>( i )].name.c_str( );
            const float width = kit::text_width( name );
            const ImRect bb( { x, o.y }, { x + width, o.y + kit::metric::header } );
            const ImGuiID id = w->GetID( name );
            ImGui::ItemAdd( bb, id );
            bool hovered = false, held = false;
            if ( ImGui::ButtonBehavior( bb, id, &hovered, &held ) )
                category = i;
            if ( hovered )
                ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
            const bool active = category == i;
            kit::text( draw, { x, std::floor( o.y + ( kit::metric::header - 13 ) * .5f ) },
                       active ? kit::tone::title : hovered ? kit::tone::label : kit::tone::muted, name );
            if ( active )
            {
                target_x = x - o.x;
                target_w = width;
            }
            x += width + 24;
        }
        auto* storage = w->DC.StateStorage;
        const ImGuiID key = w->GetID( "##tab_line" );
        const float lx = ImLerp( storage->GetFloat( key, target_x ), target_x, kit::approach( 22.f ) );
        const float lw = ImLerp( storage->GetFloat( key + 1, target_w ), target_w, kit::approach( 22.f ) );
        storage->SetFloat( key, lx );
        storage->SetFloat( key + 1, lw );
        if ( target_w > 0 )
            draw->AddRectFilled( { o.x + lx, o.y + kit::metric::header - 2 }, { o.x + lx + lw, o.y + kit::metric::header },
                                 kit::faded( kit::tone::title ) );
    }

    struct pick_t
    {
        const kit::feature_t* feature = nullptr;
        int index = -1;
    };

    auto feature_list( const kit::category_t& category, int& selected ) -> pick_t
    {
        ImGuiWindow* w = current( );
        ImDrawList* draw = w->DrawList;
        const float width = w->WorkRect.Max.x - w->DC.CursorPos.x;
        const float r = std::min( 10.f, radius( ) + 1 );
        pick_t pick;
        int index = 0, count = 0;
        for ( const auto& group : category.groups )
            count += static_cast<int>( group.features.size( ) );
        selected = std::clamp( selected, 0, std::max( 0, count - 1 ) );
        for ( const auto& group : category.groups )
        {
            const ImVec2 at = w->DC.CursorPos;
            kit::caps( draw, { at.x + 12, at.y + kit::metric::list_section - 7 - 12 }, kit::tone::muted, group.title.c_str( ) );
            ImGui::ItemSize( { width, kit::metric::list_section } );
            for ( const auto& feature : group.features )
            {
                const ImVec2 top = w->DC.CursorPos;
                const ImRect bb( top, { top.x + width, top.y + kit::metric::list_row } );
                ImGui::ItemSize( bb.GetSize( ) );
                ImGui::PushID( index );
                const ImGuiID id = w->GetID( "##row" );
                ImGui::SetNextItemAllowOverlap( );
                bool hovered = false, held = false;
                if ( ImGui::ItemAdd( bb, id ) && ImGui::ButtonBehavior( bb, id, &hovered, &held, ImGuiButtonFlags_AllowOverlap ) )
                    selected = index;
                const bool active = selected == index;
                if ( active || hovered )
                    draw->AddRectFilled( bb.Min, bb.Max, kit::faded( active ? kit::tone::selected : kit::tone::hover ), r );
                kit::text( draw, { bb.Min.x + 12, bb.Min.y + 8 }, kit::tone::primary, feature.name.c_str( ) );
                if ( feature.meta )
                {
                    const auto meta = feature.meta( );
                    draw->PushClipRect( bb.Min, { bb.Max.x - 50, bb.Max.y }, true );
                    kit::text( draw, { bb.Min.x + 12, bb.Min.y + 27 }, kit::tone::muted, meta.c_str( ), 11.5f );
                    draw->PopClipRect( );
                }
                if ( feature.enabled )
                    kit::switch_at( w->GetID( "##on" ), { bb.Max.x - 12 - 30, bb.Min.y + 15 }, *feature.enabled );
                ImGui::PopID( );
                if ( active )
                {
                    pick.feature = &feature;
                    pick.index = index;
                }
                ++index;
            }
        }
        ImGui::Dummy( { 1, 4 } );
        return pick;
    }

    void inspector_head( ImDrawList* draw, ImVec2 at, float width, const kit::feature_t& feature )
    {
        draw->AddText( kit::font( ), 18.f, ImFloor( at + ImVec2( 0, 19 ) ), kit::faded( kit::tone::title ), feature.name.c_str( ) );
        float right = at.x + width;
        const float mid = at.y + kit::metric::head * .5f;
        if ( feature.enabled )
        {
            kit::switch_at( ImGui::GetID( "##master" ), { right - 34, mid - 10 }, *feature.enabled, { 34, 20 } );
            right -= 34 + 16;
        }
        if ( feature.key )
        {
            const float left = kit::keycap( *feature.key, feature.mode, right, mid );
            if ( feature.mode )
            {
                const char* mode = kit::mode_name( *feature.mode );
                kit::text( draw, { left - 8 - kit::text_width( mode, 12 ), mid - 7 }, kit::tone::secondary, mode, 12 );
            }
        }
        draw->PushClipRect( at, { right - 120, at.y + kit::metric::head }, true );
        kit::text( draw, at + ImVec2( 0, 47 ), kit::tone::secondary, feature.description.c_str( ) );
        draw->PopClipRect( );
        draw->AddLine( { at.x, at.y + kit::metric::head - .5f }, { at.x + width, at.y + kit::metric::head - .5f },
                       kit::faded( kit::tone::hairline ) );
    }
}

void kit::shell( const std::vector<category_t>& categories, shell_state_t& state )
{
    ImGuiWindow* host = current( );
    const ImVec2 o = ImFloor( host->DC.CursorPos );
    const ImVec2 size = theme.size;
    ImDrawList* draw = host->DrawList;
    ImGui::PushID( "##kit_shell" );
    draw->AddRectFilled( o, o + size, tone::window( ), theme.rounding );
    draw->AddRect( o + ImVec2( .5f, .5f ), o + size - ImVec2( .5f, .5f ), tone::window_border, theme.rounding );

    draw->AddText( font( ), 15.f, ImFloor( o + ImVec2( 20, ( metric::header - 15.f ) * .5f ) ), tone::title, theme.title.c_str( ) );
    if ( !categories.empty( ) )
        state.category = std::clamp( state.category, 0, static_cast<int>( categories.size( ) ) - 1 );
    tabs( draw, o, categories, state.category );
    draw->AddLine( { o.x + 1, o.y + metric::header - .5f }, { o.x + size.x - 1, o.y + metric::header - .5f }, tone::hairline );

    const float list = theme.list_width;
    const float body_top = o.y + metric::header, body_bottom = o.y + size.y - metric::footer;
    draw->AddRectFilled( { o.x + 1, body_top }, { o.x + list, body_bottom }, tone::list_fill );
    draw->AddLine( { o.x + list - .5f, body_top }, { o.x + list - .5f, body_bottom }, tone::hairline );

    pick_t pick;
    if ( !categories.empty( ) )
    {
        const auto& current = categories[static_cast<size_t>( state.category )];
        auto& selection = state.selected[static_cast<size_t>( state.category ) % std::size( state.selected )];
        ImGui::SetCursorScreenPos( { o.x, body_top } );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 8, 8 } );
        ImGui::BeginChild( "##kit_features", { list - 1, body_bottom - body_top }, ImGuiChildFlags_AlwaysUseWindowPadding,
                           ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar );
        ImGui::PopStyleVar( );
        pick = feature_list( current, selection );
        ImGui::EndChild( );
    }

    if ( state.shown_category != state.category || state.shown_feature != pick.index )
    {
        state.shown_category = state.category;
        state.shown_feature = pick.index;
        state.changed_at = ImGui::GetTime( );
    }
    const float t = std::clamp( static_cast<float>( ImGui::GetTime( ) - state.changed_at ) / .16f, 0.f, 1.f );
    const float alpha = t * t * ( 3 - 2 * t );
    const ImVec2 head( o.x + list + metric::inset, body_top );
    const float width = size.x - list - metric::inset * 2;
    if ( pick.feature )
    {
        ImGui::PushStyleVar( ImGuiStyleVar_Alpha, ImGui::GetStyle( ).Alpha * ( .35f + .65f * alpha ) );
        ImGui::PushID( pick.index );
        inspector_head( draw, head, width, *pick.feature );
        ImGui::SetCursorScreenPos( { head.x, head.y + metric::head } );
        ImGui::BeginChild( "##kit_inspector", { width + metric::inset - 8, body_bottom - head.y - metric::head - 1 },
                           ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground );
        ImGuiWindow* child = current( );
        child->WorkRect.Max.x = child->Pos.x + width;
        if ( state.inert )
        {
            ImGui::PushStyleVar( ImGuiStyleVar_DisabledAlpha, 1.f );
            ImGui::BeginDisabled( );
        }
        if ( pick.feature->draw )
            pick.feature->draw( );
        if ( state.inert )
        {
            ImGui::EndDisabled( );
            ImGui::PopStyleVar( );
        }
        ImGui::EndChild( );
        ImGui::PopID( );
        ImGui::PopStyleVar( );
    }
    else
    {
        const char* empty = categories.empty( ) ? "Add a category to start." : "This category has no features yet.";
        text( draw, { std::floor( head.x + ( width - text_width( empty ) ) * .5f ), std::floor( ( body_top + body_bottom ) * .5f - 8 ) },
              tone::muted, empty );
    }

    const float top = o.y + size.y - metric::footer;
    draw->AddLine( { o.x + 1, top + .5f }, { o.x + size.x - 1, top + .5f }, tone::hairline );
    const float y = std::floor( top + ( metric::footer - 12 ) * .5f );
    draw->AddCircleFilled( { o.x + 23, top + metric::footer * .5f }, 3, tone::live, 12 );
    text( draw, { o.x + 34, y }, tone::muted, theme.footer.c_str( ), 12 );
    char fps[24];
    std::snprintf( fps, sizeof( fps ), "%.0f fps", ImGui::GetIO( ).Framerate );
    text( draw, { o.x + size.x - 20 - text_width( fps, 12 ), y }, tone::muted, fps, 12 );

    ImGui::PopID( );
    ImGui::SetCursorScreenPos( o );
    ImGui::Dummy( size );
}


namespace
{
    void host_begin( const char* id )
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport( );
        ImGui::SetNextWindowPos( viewport->GetCenter( ), ImGuiCond_FirstUseEver, { .5f, .5f } );
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;
        if ( kit::theme.resizable )
        {
            ImGui::SetNextWindowSize( kit::theme.size, ImGuiCond_FirstUseEver );
            ImGui::SetNextWindowSizeConstraints( kit::theme.min_size, { FLT_MAX, FLT_MAX } );
            flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoCollapse;
        }
        else
        {
            ImGui::SetNextWindowSize( kit::theme.size );
            flags |= ImGuiWindowFlags_NoDecoration;
        }
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0, 0 } );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0 );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0, 0 } );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, kit::theme.rounding );
        ImGui::PushStyleColor( ImGuiCol_ResizeGrip, 0u );
        ImGui::PushStyleColor( ImGuiCol_ResizeGripHovered, 0u );
        ImGui::PushStyleColor( ImGuiCol_ResizeGripActive, 0u );
        ImGui::PushFont( kit::font( ), 13.f );
        ImGui::Begin( id, nullptr, flags );
        ImGui::PopStyleColor( 3 );
    }

    void host_end( )
    {
        ImGui::End( );
        ImGui::PopFont( );
        ImGui::PopStyleVar( 4 );
    }

    void draw_toasts( ImDrawList* draw, ImVec2 origin, ImVec2 size )
    {
        constexpr double life = 3.2;
        const double now = ImGui::GetTime( );
        toasts.erase( std::remove_if( toasts.begin( ), toasts.end( ), [&]( const toast_entry_t& t ) { return now - t.at > life; } ),
                      toasts.end( ) );
        float y = origin.y + size.y - 16;
        for ( auto it = toasts.rbegin( ); it != toasts.rend( ); ++it )
        {
            const float age = static_cast<float>( now - it->at );
            const float in = std::clamp( age / .18f, 0.f, 1.f ), out = std::clamp( ( static_cast<float>( life ) - age ) / .4f, 0.f, 1.f );
            const float alpha = std::min( in, out ) * ImGui::GetStyle( ).Alpha;
            const float rise = ( 1.f - in ) * 8.f;
            const float width = std::floor( kit::text_width( it->text.c_str( ) ) + 46 );
            const ImRect bb( { std::floor( origin.x + ( size.x - width ) * .5f ), std::floor( y - 34 + rise ) },
                             { std::floor( origin.x + ( size.x + width ) * .5f ), std::floor( y + rise ) } );
            const auto a = [&]( ImU32 c ) {
                const auto alpha_byte = static_cast<ImU32>( static_cast<float>( ( c >> IM_COL32_A_SHIFT ) & 0xFF ) * alpha );
                return ( c & ~IM_COL32_A_MASK ) | ( alpha_byte << IM_COL32_A_SHIFT );
            };
            draw->AddRectFilled( bb.Min, bb.Max, a( kit::tone::popup ), 17 );
            draw->AddRect( bb.Min + ImVec2( .5f, .5f ), bb.Max - ImVec2( .5f, .5f ), a( kit::tone::window_border ), 17 );
            const ImU32 dot = it->kind == kit::toast_t::success ? kit::tone::live
                              : it->kind == kit::toast_t::error ? IM_COL32( 232, 92, 92, 255 )
                                                                : kit::tone::accent( );
            draw->AddCircleFilled( { bb.Min.x + 18, bb.GetCenter( ).y }, 3.5f, a( dot ), 12 );
            draw->AddText( kit::font( ), 13.f, ImFloor( ImVec2( bb.Min.x + 30, centred( bb ) ) ), a( kit::tone::primary ), it->text.c_str( ) );
            y -= 42;
        }
    }
}

void kit::window( const char* id, const std::vector<category_t>& categories, shell_state_t& state )
{
    const bool resizable = theme.resizable;
    theme.resizable = false; // the shell is laid out for one size
    host_begin( id );
    theme.resizable = resizable;
    shell( categories, state );
    host_end( );
}

// ---- free-placed surfaces ------------------------------------------------------------------------------------------

void kit::begin_surface( ImVec2 size )
{
    ImGuiWindow* w = current( );
    surface_origin = ImFloor( w->DC.CursorPos );
    surface_size = size.x > 0 && size.y > 0 ? ImFloor( size ) : theme.size;
    surface_offset = { 0, 0 };
    surface_faded = false;
    w->DrawList->AddRectFilled( surface_origin, surface_origin + surface_size, tone::window( ), theme.rounding );
    w->DrawList->AddRect( surface_origin + ImVec2( .5f, .5f ), surface_origin + surface_size - ImVec2( .5f, .5f ), tone::window_border,
                          theme.rounding );
    ImGui::PushID( "##kit_surface" );
}

void kit::end_surface( )
{
    if ( surface_faded )
    {
        ImGui::PopStyleVar( );
        surface_faded = false;
    }
    surface_offset = { 0, 0 };
    draw_toasts( current( )->DrawList, surface_origin, surface_size );
    ImGui::PopID( );
    ImGui::SetCursorScreenPos( surface_origin );
    ImGui::Dummy( surface_size );
}

void kit::begin_host( const char* id )
{
    host_begin( id );
}

void kit::end_host( )
{
    host_end( );
}

void kit::begin_window( const char* id )
{
    host_begin( id );
    ImGui::SetCursorScreenPos( ImGui::GetWindowPos( ) );
    begin_surface( theme.resizable ? ImGui::GetWindowSize( ) : theme.size );
}

void kit::end_window( )
{
    end_surface( );
    host_end( );
}

void kit::screen( int index )
{
    static int shown = -1, direction = 1;
    static double changed = -10;
    if ( index != shown )
    {
        if ( shown != -1 )
        {
            changed = ImGui::GetTime( );
            direction = index > shown ? 1 : -1;
        }
        shown = index;
    }
    if ( theme.transition == 0 || surface_faded )
        return;
    const float t = std::clamp( static_cast<float>( ImGui::GetTime( ) - changed ) / .24f, 0.f, 1.f );
    if ( t >= 1.f )
        return;
    const float ease = 1.f - ( 1.f - t ) * ( 1.f - t ) * ( 1.f - t );
    ImGui::PushStyleVar( ImGuiStyleVar_Alpha, ImGui::GetStyle( ).Alpha * ease );
    surface_faded = true;
    if ( theme.transition == 2 )
        surface_offset = { std::floor( ( 1.f - ease ) * 28.f * static_cast<float>( direction ) ), 0 };
}

void kit::toast( const char* text, toast_t kind )
{
    toasts.push_back( { text ? text : "", kind, ImGui::GetTime( ) } );
    if ( toasts.size( ) > 4 )
        toasts.erase( toasts.begin( ) );
}

// ---- images --------------------------------------------------------------------------------------------------------

namespace
{
    struct image_entry_t
    {
        ImTextureData* texture = nullptr;
        int width = 0, height = 0;
        int used = 0;
        bool failed = false;
    };
    std::unordered_map<const unsigned char*, image_entry_t> image_cache;
    int image_sweep = -1;

    auto decode( const unsigned char* data, size_t size, int& width, int& height, std::vector<unsigned char>* pixels ) -> bool
    {
        const HRESULT com = CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );
        bool ok = false;
        IWICImagingFactory* factory = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;
        UINT w = 0, h = 0;
        if ( SUCCEEDED( CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &factory ) ) ) &&
             SUCCEEDED( factory->CreateStream( &stream ) ) &&
             SUCCEEDED( stream->InitializeFromMemory( const_cast<BYTE*>( data ), static_cast<DWORD>( size ) ) ) &&
             SUCCEEDED( factory->CreateDecoderFromStream( stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder ) ) &&
             SUCCEEDED( decoder->GetFrame( 0, &frame ) ) && SUCCEEDED( frame->GetSize( &w, &h ) ) && w > 0 && h > 0 && w <= 8192 &&
             h <= 8192 )
        {
            width = static_cast<int>( w ), height = static_cast<int>( h );
            ok = true;
            if ( pixels )
            {
                pixels->resize( static_cast<size_t>( w ) * h * 4 );
                ok = SUCCEEDED( factory->CreateFormatConverter( &converter ) ) &&
                     SUCCEEDED( converter->Initialize( frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                                       WICBitmapPaletteTypeCustom ) ) &&
                     SUCCEEDED( converter->CopyPixels( nullptr, w * 4, static_cast<UINT>( pixels->size( ) ), pixels->data( ) ) );
            }
        }
        IUnknown* objects[] = { converter, frame, decoder, stream, factory };
        for ( IUnknown* object : objects )
            if ( object )
                object->Release( );
        if ( SUCCEEDED( com ) )
            CoUninitialize( );
        return ok;
    }

    // Images unused for a while hand their texture back to the backend, then leave the cache.
    void sweep_images( )
    {
        const int frame = ImGui::GetFrameCount( );
        if ( image_sweep == frame )
            return;
        image_sweep = frame;
        for ( auto it = image_cache.begin( ); it != image_cache.end( ); )
        {
            ImTextureData* tex = it->second.texture;
            if ( tex && tex->Status == ImTextureStatus_Destroyed && tex->WantDestroyNextFrame )
            {
                ImGui::UnregisterUserTexture( tex );
                IM_DELETE( tex );
                it = image_cache.erase( it );
                continue;
            }
            if ( tex && !tex->WantDestroyNextFrame && frame - it->second.used > 600 )
                tex->WantDestroyNextFrame = true;
            ++it;
        }
    }

    auto texture_for( const unsigned char* data, size_t size ) -> image_entry_t*
    {
        sweep_images( );
        auto& entry = image_cache[data];
        entry.used = ImGui::GetFrameCount( );
        if ( entry.texture && entry.texture->WantDestroyNextFrame )
            entry.texture->WantDestroyNextFrame = false;
        if ( entry.texture || entry.failed )
            return &entry;
        std::vector<unsigned char> pixels;
        if ( !data || !size || !decode( data, size, entry.width, entry.height, &pixels ) )
        {
            entry.failed = true;
            return &entry;
        }
        auto* tex = IM_NEW( ImTextureData )( );
        tex->Create( ImTextureFormat_RGBA32, entry.width, entry.height );
        std::memcpy( tex->GetPixels( ), pixels.data( ), pixels.size( ) );
        tex->UseColors = true;
        ImGui::RegisterUserTexture( tex );
        entry.texture = tex;
        return &entry;
    }
}

bool kit::image_size( const unsigned char* data, size_t size, int& width, int& height )
{
    return data && size && decode( data, size, width, height, nullptr );
}

// ---- icons ---------------------------------------------------------------------------------------------------------

kit::items kit::icon_names( )
{
    static constexpr const char* names[] = { "None",     "User",        "Lock",       "Key",     "Mail",     "Eye",     "Eye off",
                                             "Play",     "Download",    "Upload",     "Settings", "Close",   "Check",   "Plus",
                                             "Minus",    "Arrow right", "Arrow left", "Chevron", "Home",     "Shield",  "Bell",
                                             "Search",   "Star",        "Heart",      "Info",    "Warning",  "Refresh", "Power",
                                             "Folder",   "Log out",     "Globe",      "Chip",    "Bolt",     "Trash",   "Copy",
                                             "External", "Message",     "Calendar",   "Clock",   "Crosshair" };
    return names;
}

// Drawn on Lucide's 24-unit grid with round strokes, so they sit well next to Inter.
void kit::icon( ImDrawList* draw, icon_t id, ImVec2 centre, float size, ImU32 color )
{
    const float s = size / 24.f;
    const float thick = std::max( 1.2f, size * .085f );
    const auto p = [&]( float x, float y ) { return ImVec2( centre.x + ( x - 12.f ) * s, centre.y + ( y - 12.f ) * s ); };
    const auto line = [&]( float x1, float y1, float x2, float y2 ) { draw->AddLine( p( x1, y1 ), p( x2, y2 ), color, thick ); };
    const auto poly = [&]( std::initializer_list<ImVec2> points, bool closed ) {
        ImVec2 out[16];
        int n = 0;
        for ( const auto& q : points )
            out[n++] = p( q.x, q.y );
        draw->AddPolyline( out, n, color, closed ? ImDrawFlags_Closed : ImDrawFlags_None, thick );
    };
    const auto circle = [&]( float x, float y, float r ) { draw->AddCircle( p( x, y ), r * s, color, 0, thick ); };
    const auto dot = [&]( float x, float y ) { draw->AddCircleFilled( p( x, y ), thick * .75f, color, 8 ); };
    const auto box = [&]( float x1, float y1, float x2, float y2, float r ) { draw->AddRect( p( x1, y1 ), p( x2, y2 ), color, r * s, 0, thick ); };
    const auto arc = [&]( float x, float y, float r, float a0, float a1 ) {
        draw->PathArcTo( p( x, y ), r * s, a0, a1, 0 );
        draw->PathStroke( color, ImDrawFlags_None, thick );
    };
    switch ( id )
    {
    case icon_t::user:
        circle( 12, 8, 4 );
        arc( 12, 21, 7, IM_PI, IM_PI * 2 );
        break;
    case icon_t::lock:
        box( 5, 11, 19, 21, 2 );
        line( 8, 11, 8, 7.5f );
        line( 16, 11, 16, 7.5f );
        arc( 12, 7.5f, 4, IM_PI, IM_PI * 2 );
        break;
    case icon_t::key:
        circle( 7.5f, 15.5f, 4 );
        line( 10.5f, 12.5f, 20, 3 );
        line( 15.5f, 7.5f, 18.5f, 10.5f );
        line( 18, 5, 20.5f, 7.5f );
        break;
    case icon_t::mail:
        box( 3, 5, 21, 19, 2 );
        poly( { { 3, 7 }, { 12, 13 }, { 21, 7 } }, false );
        break;
    case icon_t::eye:
    case icon_t::eye_off:
        draw->AddBezierQuadratic( p( 2, 12 ), p( 12, 2.5f ), p( 22, 12 ), color, thick );
        draw->AddBezierQuadratic( p( 2, 12 ), p( 12, 21.5f ), p( 22, 12 ), color, thick );
        circle( 12, 12, 3 );
        if ( id == icon_t::eye_off )
            line( 3, 3, 21, 21 );
        break;
    case icon_t::play: poly( { { 7, 4 }, { 19, 12 }, { 7, 20 } }, true ); break;
    case icon_t::download:
        line( 12, 3, 12, 15 );
        poly( { { 7, 10 }, { 12, 15 }, { 17, 10 } }, false );
        poly( { { 3, 16 }, { 3, 21 }, { 21, 21 }, { 21, 16 } }, false );
        break;
    case icon_t::upload:
        line( 12, 15, 12, 3 );
        poly( { { 7, 8 }, { 12, 3 }, { 17, 8 } }, false );
        poly( { { 3, 16 }, { 3, 21 }, { 21, 21 }, { 21, 16 } }, false );
        break;
    case icon_t::settings:
        circle( 12, 12, 3 );
        circle( 12, 12, 7.5f );
        for ( int i = 0; i < 8; ++i )
        {
            const float a = static_cast<float>( i ) * IM_PI * .25f;
            line( 12 + std::cos( a ) * 7.5f, 12 + std::sin( a ) * 7.5f, 12 + std::cos( a ) * 10.5f, 12 + std::sin( a ) * 10.5f );
        }
        break;
    case icon_t::close:
        line( 6, 6, 18, 18 );
        line( 18, 6, 6, 18 );
        break;
    case icon_t::check: poly( { { 4, 12.5f }, { 9.5f, 18 }, { 20, 6 } }, false ); break;
    case icon_t::plus:
        line( 12, 5, 12, 19 );
        line( 5, 12, 19, 12 );
        break;
    case icon_t::minus: line( 5, 12, 19, 12 ); break;
    case icon_t::arrow_right:
        line( 5, 12, 19, 12 );
        poly( { { 13, 6 }, { 19, 12 }, { 13, 18 } }, false );
        break;
    case icon_t::arrow_left:
        line( 19, 12, 5, 12 );
        poly( { { 11, 6 }, { 5, 12 }, { 11, 18 } }, false );
        break;
    case icon_t::chevron_right: poly( { { 9, 6 }, { 15, 12 }, { 9, 18 } }, false ); break;
    case icon_t::home:
        poly( { { 3, 10 }, { 12, 3 }, { 21, 10 } }, false );
        poly( { { 5, 9 }, { 5, 21 }, { 19, 21 }, { 19, 9 } }, false );
        poly( { { 10, 21 }, { 10, 14 }, { 14, 14 }, { 14, 21 } }, false );
        break;
    case icon_t::shield: poly( { { 12, 3 }, { 20, 6 }, { 20, 12 }, { 17, 17 }, { 12, 21 }, { 7, 17 }, { 4, 12 }, { 4, 6 } }, true ); break;
    case icon_t::bell:
        arc( 12, 10, 6, IM_PI, IM_PI * 2 );
        poly( { { 6, 10 }, { 6, 15 }, { 4, 17 }, { 20, 17 }, { 18, 15 }, { 18, 10 } }, false );
        line( 10, 20.5f, 14, 20.5f );
        break;
    case icon_t::search:
        circle( 11, 11, 7 );
        line( 16.5f, 16.5f, 21, 21 );
        break;
    case icon_t::star:
    {
        ImVec2 points[10];
        for ( int i = 0; i < 10; ++i )
        {
            const float a = -IM_PI * .5f + static_cast<float>( i ) * IM_PI / 5.f;
            const float r = i % 2 ? 4.2f : 9.5f;
            points[i] = p( 12 + std::cos( a ) * r, 12.8f + std::sin( a ) * r );
        }
        draw->AddPolyline( points, 10, color, ImDrawFlags_Closed, thick );
        break;
    }
    case icon_t::heart:
        draw->PathArcTo( p( 8.5f, 9 ), 4.5f * s, IM_PI * .75f, IM_PI * 2.f, 0 );
        draw->PathArcTo( p( 15.5f, 9 ), 4.5f * s, IM_PI, IM_PI * 2.25f, 0 );
        draw->PathLineTo( p( 12, 20.5f ) );
        draw->PathStroke( color, ImDrawFlags_Closed, thick );
        break;
    case icon_t::info:
        circle( 12, 12, 9 );
        line( 12, 11, 12, 16.5f );
        dot( 12, 7.8f );
        break;
    case icon_t::warning:
        poly( { { 12, 3 }, { 22, 20 }, { 2, 20 } }, true );
        line( 12, 9, 12, 13.5f );
        dot( 12, 16.8f );
        break;
    case icon_t::refresh:
    {
        arc( 12, 12, 8, IM_PI * 1.2f, IM_PI * 2.8f );
        const float a = IM_PI * 2.8f;
        const ImVec2 end( 12 + std::cos( a ) * 8, 12 + std::sin( a ) * 8 );
        poly( { { end.x - 4, end.y - .5f }, end, { end.x + .5f, end.y - 4.5f } }, false );
        break;
    }
    case icon_t::power:
        line( 12, 2.5f, 12, 11 );
        arc( 12, 13, 8, -IM_PI * .3f, IM_PI * 1.3f );
        break;
    case icon_t::folder: poly( { { 3, 6 }, { 9, 6 }, { 11, 8.5f }, { 21, 8.5f }, { 21, 19 }, { 3, 19 } }, true ); break;
    case icon_t::log_out:
        poly( { { 9, 21 }, { 5, 21 }, { 5, 3 }, { 9, 3 } }, false );
        poly( { { 16, 7 }, { 21, 12 }, { 16, 17 } }, false );
        line( 21, 12, 9, 12 );
        break;
    case icon_t::globe:
        circle( 12, 12, 9 );
        line( 3, 12, 21, 12 );
        draw->AddBezierQuadratic( p( 12, 3 ), p( 6, 12 ), p( 12, 21 ), color, thick );
        draw->AddBezierQuadratic( p( 12, 3 ), p( 18, 12 ), p( 12, 21 ), color, thick );
        break;
    case icon_t::cpu:
        box( 5, 5, 19, 19, 2 );
        box( 9, 9, 15, 15, 1 );
        for ( const float v : { 9.f, 15.f } )
        {
            line( v, 2, v, 5 );
            line( v, 19, v, 22 );
            line( 2, v, 5, v );
            line( 19, v, 22, v );
        }
        break;
    case icon_t::zap: poly( { { 13, 2 }, { 4, 14 }, { 12, 14 }, { 11, 22 }, { 20, 10 }, { 12, 10 } }, true ); break;
    case icon_t::trash:
        line( 3, 6, 21, 6 );
        poly( { { 19, 6 }, { 18, 21 }, { 6, 21 }, { 5, 6 } }, false );
        poly( { { 9, 6 }, { 9, 3 }, { 15, 3 }, { 15, 6 } }, false );
        break;
    case icon_t::copy:
        box( 8, 8, 21, 21, 2 );
        poly( { { 4, 16 }, { 3, 16 }, { 3, 3 }, { 16, 3 }, { 16, 4 } }, false );
        break;
    case icon_t::external:
        poly( { { 14, 3 }, { 21, 3 }, { 21, 10 } }, false );
        line( 21, 3, 11, 13 );
        poly( { { 18, 14 }, { 18, 21 }, { 3, 21 }, { 3, 6 }, { 10, 6 } }, false );
        break;
    case icon_t::message:
        box( 3, 4, 21, 17, 3 );
        poly( { { 8, 17 }, { 8, 21 }, { 12.5f, 17 } }, false );
        break;
    case icon_t::calendar:
        box( 3, 5, 21, 21, 2 );
        line( 3, 10, 21, 10 );
        line( 8, 3, 8, 7 );
        line( 16, 3, 16, 7 );
        break;
    case icon_t::clock:
        circle( 12, 12, 9 );
        poly( { { 12, 7 }, { 12, 12 }, { 15.5f, 14 } }, false );
        break;
    case icon_t::crosshair:
        circle( 12, 12, 8 );
        line( 12, 2, 12, 7 );
        line( 12, 17, 12, 22 );
        line( 2, 12, 7, 12 );
        line( 17, 12, 22, 12 );
        break;
    default: break;
    }
}

// ---- free-placed elements ------------------------------------------------------------------------------------------

ImRect kit::el::at( float x, float y, float w, float h, pin_t x_pin, pin_t y_pin )
{
    const ImVec2 extra = surface_size - theme.size;
    const auto place = []( float& pos, float& len, float more, pin_t pin ) {
        switch ( pin )
        {
        case pin_t::end: pos += more; break;
        case pin_t::stretch: len = std::max( 1.f, len + more ); break;
        case pin_t::centre: pos += more * .5f; break;
        default: break;
        }
    };
    place( x, w, extra.x, x_pin );
    place( y, h, extra.y, y_pin );
    const ImVec2 min = ImFloor( surface_origin + surface_offset + ImVec2( x, y ) );
    return { min, min + ImFloor( ImVec2( w, h ) ) };
}

void kit::el::text( const char* value, const ImRect& r, text_style_t style )
{
    ImDrawList* draw = current( )->DrawList;
    ImU32 color = tone::primary;
    switch ( style.role )
    {
    case role_t::title: color = tone::title; break;
    case role_t::muted: color = tone::muted; break;
    case role_t::accent: color = tone::accent( ); break;
    default: break;
    }
    if ( style.color )
        color = style.color;
    const float wrap = std::max( 1.f, r.GetWidth( ) );
    const ImVec2 size = font( )->CalcTextSizeA( style.size, FLT_MAX, wrap, value );
    float x = r.Min.x;
    if ( style.align == align_t::centre )
        x = r.GetCenter( ).x - size.x * .5f;
    else if ( style.align == align_t::right )
        x = r.Max.x - size.x;
    const float y = r.Min.y + std::max( 0.f, ( r.GetHeight( ) - size.y ) * .5f );
    draw->AddText( font( ), style.size, ImFloor( ImVec2( x, y ) ), faded( color ), value, nullptr, wrap );
    report( r );
}

bool kit::el::button( const char* label, const ImRect& r, bool primary, icon_t icon, ImU32 fill )
{
    ImGuiWindow* w = current( );
    bool hovered = false, held = false;
    const bool pressed = behave( r, w->GetID( label ), hovered, held );
    paint_button( w->DrawList, r, label, primary ? button_kind::accent : button_kind::themed, hovered, held, icon, fill );
    report( r );
    return pressed;
}

bool kit::el::tabs( const char* id, int& value, items options, const ImRect& r )
{
    ImGuiWindow* w = current( );
    ImDrawList* draw = w->DrawList;
    const int count = static_cast<int>( options.size( ) );
    if ( count == 0 )
        return false;
    value = std::clamp( value, 0, count - 1 );
    ImGui::PushID( id );
    const float cell = r.GetWidth( ) / static_cast<float>( count );
    draw->AddLine( { r.Min.x, r.Max.y - .5f }, { r.Max.x, r.Max.y - .5f }, faded( tone::hairline ) );
    bool changed = false;
    float target = 0;
    for ( int i = 0; i < count; ++i )
    {
        const ImRect c( { std::floor( r.Min.x + cell * static_cast<float>( i ) ), r.Min.y },
                        { std::floor( r.Min.x + cell * static_cast<float>( i + 1 ) ), r.Max.y } );
        bool hovered = false, held = false;
        if ( behave( c, w->GetID( i ), hovered, held ) && value != i )
        {
            value = i;
            changed = true;
        }
        const char* name = options[static_cast<size_t>( i )];
        kit::text( draw, { std::floor( c.GetCenter( ).x - text_width( name ) * .5f ), centred( c ) - 1 },
                   value == i ? tone::title : hovered ? tone::label : tone::muted, name );
        if ( value == i )
            target = c.Min.x - r.Min.x;
    }
    auto* storage = ImGui::GetStateStorage( );
    const ImGuiID key = ImGui::GetID( "##line" );
    const float x = ImLerp( storage->GetFloat( key, target ), target, approach( 22.f ) );
    storage->SetFloat( key, x );
    const float inset = std::min( 16.f, cell * .2f );
    draw->AddRectFilled( { std::floor( r.Min.x + x + inset ), r.Max.y - 2 }, { std::floor( r.Min.x + x + cell - inset ), r.Max.y },
                         faded( tone::accent( ) ), 1 );
    ImGui::PopID( );
    report( r );
    return changed;
}

void kit::el::image( const unsigned char* data, size_t size, const ImRect& r, float rounding )
{
    ImDrawList* draw = current( )->DrawList;
    const auto* entry = texture_for( data, size );
    if ( entry->texture )
        draw->AddImageRounded( entry->texture->GetTexRef( ), r.Min, r.Max, { 0, 0 }, { 1, 1 }, faded( IM_COL32_WHITE ), rounding );
    else
    {
        frame( draw, r, tone::overlay( 6 ), tone::overlay( 20 ), rounding );
        draw->AddLine( r.Min, r.Max, faded( tone::overlay( 20 ) ) );
        draw->AddLine( { r.Max.x, r.Min.y }, { r.Min.x, r.Max.y }, faded( tone::overlay( 20 ) ) );
    }
    report( r );
}

void kit::el::icon( icon_t id, const ImRect& r, ImU32 color )
{
    const float size = std::min( r.GetWidth( ), r.GetHeight( ) );
    kit::icon( current( )->DrawList, id, r.GetCenter( ), size, faded( color ? color : tone::primary ) );
    report( r );
}

bool kit::el::input( const char* id, char* buffer, size_t capacity, const ImRect& r, const char* placeholder, bool password )
{
    const bool changed = field( r, id, buffer, capacity, placeholder, password ? ImGuiInputTextFlags_Password : ImGuiInputTextFlags_None );
    report( r );
    return changed;
}

bool kit::el::checkbox( const char* label, bool& value, const ImRect& r )
{
    ImGuiWindow* w = current( );
    const ImGuiID id = w->GetID( label );
    bool hovered = false, held = false;
    const bool pressed = behave( r, id, hovered, held );
    if ( pressed )
        value = !value;
    const float mid = r.GetCenter( ).y;
    const ImRect control = toggle_rect( { r.Min.x, std::floor( mid - 9 ) }, { 30, 18 }, false );
    paint_toggle( w->DrawList, control, id, value, hovered );
    const std::string shown = visible( label );
    kit::text( w->DrawList, { control.Max.x + 10, centred( r ) }, hovered ? tone::primary : tone::label, shown.c_str( ) );
    report( r );
    return pressed;
}

bool kit::el::slider( const char* id, float& value, float min, float max, const ImRect& r, const char* format )
{
    ImGuiWindow* w = current( );
    bool hovered = false, held = false;
    const bool changed = slider_behaviour( r, w->GetID( id ), value, min, max, hovered, held );
    paint_slider( w->DrawList, r, value, min, max, format, "", hovered, held );
    report( r );
    return changed;
}

bool kit::el::dropdown( const char* id, int& value, items options, const ImRect& r )
{
    const bool changed = combo( r, current( )->GetID( id ), value, options );
    report( r );
    return changed;
}

void kit::el::progress( float value, const ImRect& r )
{
    ImDrawList* draw = current( )->DrawList;
    const float round = std::min( r.GetHeight( ) * .5f, std::max( radius( ), 1.f ) );
    draw->AddRectFilled( r.Min, r.Max, faded( tone::overlay( 16 ) ), round );
    float from = 0, to = ImSaturate( value );
    if ( value < 0 )
    {
        const float phase = std::fmod( static_cast<float>( ImGui::GetTime( ) ) * .7f, 1.4f ) - .4f;
        from = ImSaturate( phase );
        to = ImSaturate( phase + .4f );
    }
    if ( to > from )
        draw->AddRectFilled( { ImLerp( r.Min.x, r.Max.x, from ), r.Min.y }, { ImLerp( r.Min.x, r.Max.x, to ), r.Max.y },
                             faded( tone::accent( ) ), round );
    report( r );
}

bool kit::el::list( const char* id, int& selected, items rows, const ImRect& r, items details )
{
    ImGuiWindow* w = current( );
    ImDrawList* draw = w->DrawList;
    const float rr = radius( );
    frame( draw, r, tone::overlay( 6 ), tone::control_border, rr + 1 );
    const float row_h = details.empty( ) ? 34.f : 48.f;
    bool changed = false;
    ImGui::PushID( id );
    draw->PushClipRect( r.Min, r.Max, true );
    for ( size_t i = 0; i < rows.size( ); ++i )
    {
        const ImRect bb( { r.Min.x + 4, r.Min.y + 4 + static_cast<float>( i ) * row_h },
                         { r.Max.x - 4, r.Min.y + 4 + static_cast<float>( i + 1 ) * row_h } );
        if ( bb.Min.y >= r.Max.y - 4 )
            break;
        bool hovered = false, held = false;
        if ( behave( bb, w->GetID( static_cast<int>( i ) ), hovered, held ) && selected != static_cast<int>( i ) )
        {
            selected = static_cast<int>( i );
            changed = true;
        }
        const bool active = selected == static_cast<int>( i );
        if ( active || hovered )
            draw->AddRectFilled( bb.Min, bb.Max, faded( active ? tone::selected : tone::hover ), std::max( 0.f, rr - 1 ) );
        if ( i < details.size( ) && details[i] && *details[i] )
        {
            kit::text( draw, { bb.Min.x + 12, bb.Min.y + 8 }, tone::primary, rows[i] );
            kit::text( draw, { bb.Min.x + 12, bb.Min.y + 26 }, tone::muted, details[i], 11.5f );
        }
        else
            kit::text( draw, { bb.Min.x + 12, centred( bb ) }, tone::primary, rows[i] );
        if ( active )
            draw->AddRectFilled( { bb.Min.x, bb.Min.y + 8 }, { bb.Min.x + 2, bb.Max.y - 8 }, faded( tone::accent( ) ), 1 );
    }
    draw->PopClipRect( );
    ImGui::PopID( );
    report( r );
    return changed;
}

void kit::el::panel( const ImRect& r, ImU32 fill )
{
    frame( current( )->DrawList, r, fill ? fill : tone::overlay( 5 ), fill ? 0 : tone::overlay( 16 ), std::max( radius( ), theme.rounding - 4 ) );
    report( r );
}

void kit::el::divider( const ImRect& r )
{
    const float y = std::floor( r.GetCenter( ).y ) + .5f;
    current( )->DrawList->AddLine( { r.Min.x, y }, { r.Max.x, y }, faded( tone::hairline ) );
    report( r );
}

void kit::el::spinner( const ImRect& r )
{
    ImDrawList* draw = current( )->DrawList;
    const ImVec2 c = r.GetCenter( );
    const float size = std::max( 4.f, std::min( r.GetWidth( ), r.GetHeight( ) ) * .5f - 2.f );
    draw->AddCircle( c, size, faded( tone::overlay( 22 ) ), 32, 2.5f );
    const float a = static_cast<float>( ImGui::GetTime( ) ) * 5.f;
    draw->PathArcTo( c, size, a, a + IM_PI * .55f, 16 );
    draw->PathStroke( faded( tone::accent( ) ), ImDrawFlags_None, 2.5f );
    report( r );
}
