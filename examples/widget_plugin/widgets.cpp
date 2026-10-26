// Example custom widgets for ImGui Studio.
//
// The main build turns this file into bin\plugins\widgets.dll. In the editor, open Project settings > Custom widgets,
// load that DLL, then place a Custom element and set its function to one of the names below. Add this file to your
// app as well: the exported code calls these functions by name.

#include "studio_plugin.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// A ring filled to a value, with the percentage in the middle.
void ring_meter( const ImRect& r )
{
    ImDrawList* draw = ImGui::GetWindowDrawList( );
    const ImVec2 centre = r.GetCenter( );
    const float radius = std::max( 8.f, std::min( r.GetWidth( ), r.GetHeight( ) ) * .5f - 6.f );
    constexpr float value = .72f;
    draw->AddCircle( centre, radius, kit::faded( kit::tone::overlay( 22 ) ), 64, 6.f );
    draw->PathArcTo( centre, radius, -IM_PI * .5f, -IM_PI * .5f + IM_PI * 2.f * value, 64 );
    draw->PathStroke( kit::faded( kit::tone::accent( ) ), ImDrawFlags_None, 6.f );
    char text[8];
    std::snprintf( text, sizeof( text ), "%d%%", static_cast<int>( value * 100.f ) );
    const float width = kit::text_width( text, 18.f );
    kit::text( draw, { centre.x - width * .5f, centre.y - 10.f }, kit::tone::title, text, 18.f );
}

// A pill with a status dot and a label.
void status_badge( const ImRect& r )
{
    ImDrawList* draw = ImGui::GetWindowDrawList( );
    const float height = std::min( r.GetHeight( ), 28.f );
    const ImRect pill( { r.Min.x, r.GetCenter( ).y - height * .5f }, { r.Max.x, r.GetCenter( ).y + height * .5f } );
    draw->AddRectFilled( pill.Min, pill.Max, kit::faded( IM_COL32( 77, 184, 132, 36 ) ), height * .5f );
    draw->AddRect( pill.Min, pill.Max, kit::faded( IM_COL32( 77, 184, 132, 110 ) ), height * .5f );
    draw->AddCircleFilled( { pill.Min.x + 14, pill.GetCenter( ).y }, 3.5f, kit::faded( kit::tone::live ), 12 );
    kit::text( draw, { pill.Min.x + 26, pill.GetCenter( ).y - 7.f }, kit::tone::title, "Undetected" );
}

STUDIO_WIDGETS( { "ring_meter", ring_meter }, { "status_badge", status_badge } )
