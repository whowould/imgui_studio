#include "codegen.h"
#include "names.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace
{
    auto literal( const std::string& s ) -> std::string
    {
        std::string out = "\"";
        for ( const char c : s )
        {
            if ( c == '"' || c == '\\' )
                out += '\\';
            if ( c == '\n' )
            {
                out += "\\n";
                continue;
            }
            out += c;
        }
        return out + "\"";
    }

    auto number( float v ) -> std::string
    {
        char text[32];
        std::snprintf( text, sizeof( text ), "%.6g", static_cast<double>( v ) );
        std::string s = text;
        if ( s.find_first_of( ".e" ) == std::string::npos )
            s += ".";
        return s + "f";
    }

    auto whole( float v ) -> std::string
    {
        char text[32];
        std::snprintf( text, sizeof( text ), "%g", static_cast<double>( std::round( v ) ) );
        return text;
    }

    auto hex_key( int key ) -> std::string
    {
        char text[16];
        std::snprintf( text, sizeof( text ), "0x%02X", key );
        return text;
    }

    auto color32( const float* c ) -> std::string
    {
        char text[64];
        std::snprintf( text, sizeof( text ), "IM_COL32( %d, %d, %d, %d )", static_cast<int>( c[0] * 255.f + .5f ),
                       static_cast<int>( c[1] * 255.f + .5f ), static_cast<int>( c[2] * 255.f + .5f ), static_cast<int>( c[3] * 255.f + .5f ) );
        return text;
    }

    auto vec4( const float* c ) -> std::string
    {
        return "{ " + number( c[0] ) + ", " + number( c[1] ) + ", " + number( c[2] ) + ", " + number( c[3] ) + " }";
    }

    void line( std::string& out, int depth, const std::string& text )
    {
        out += std::string( static_cast<size_t>( depth * 4 ), ' ' ) + text + "\n";
    }

    auto quoted_list( const std::vector<std::string>& list ) -> std::string
    {
        std::string out;
        for ( size_t i = 0; i < list.size( ); ++i )
            out += ( i ? ", " : "" ) + literal( list[i] );
        return out.empty( ) ? "\"\"" : out;
    }

    auto hint_arg( const doc::widget_t& w ) -> std::string
    {
        return w.hint.empty( ) ? "nullptr" : literal( w.hint );
    }

    constexpr const char* icon_ids[] = { "none",     "user",        "lock",       "key",           "mail",   "eye",     "eye_off",
                                         "play",     "download",    "upload",     "settings",      "close",  "check",   "plus",
                                         "minus",    "arrow_right", "arrow_left", "chevron_right", "home",   "shield",  "bell",
                                         "search",   "star",        "heart",      "info",          "warning", "refresh", "power",
                                         "folder",   "log_out",     "globe",      "cpu",           "zap",    "trash",   "copy",
                                         "external", "message",     "calendar",   "clock",         "crosshair" };

    auto icon_id( int icon ) -> std::string
    {
        return std::string( "kit::icon_t::" ) + icon_ids[std::clamp( icon, 0, static_cast<int>( std::size( icon_ids ) ) - 1 )];
    }

    // The theme block of setup( ): palette, accent, window and widget styles.
    void theme_lines( std::string& s, const doc::document_t& d, bool menu )
    {
        const auto& t = d.theme;
        constexpr const char* palettes[] = { "graphite", "midnight", "light", "oled" };
        constexpr const char* toggles[] = { "pill", "check" };
        constexpr const char* sliders[] = { "bar", "track" };
        constexpr const char* buttons[] = { "soft", "solid", "outline" };
        constexpr const char* fields[] = { "box", "line" };
        line( s, 2, "kit::load_font( io );" );
        if ( t.palette == 4 )
        {
            // The custom palette, mixed from its three colours the same way the editor does.
            const auto light = t.custom_light;
            const auto mix = [&]( const float* a, const float* b, float k ) {
                float out[4] = { a[0] + ( b[0] - a[0] ) * k, a[1] + ( b[1] - a[1] ) * k, a[2] + ( b[2] - a[2] ) * k, 1.f };
                return color32( out );
            };
            const float black[4] = { 0, 0, 0, 1 }, white[4] = { 1, 1, 1, 1 };
            line( s, 2, "kit::palette_t palette;" );
            line( s, 2, "palette.window = " + vec4( t.custom_window ) + ";" );
            line( s, 2, std::string( "palette.overlay = " ) + ( light ? "{ 0.f, 0.f, 0.f, 1.f }" : "{ 1.f, 1.f, 1.f, 1.f }" ) + ";" );
            line( s, 2, std::string( "palette.overlay_gain = " ) + ( light ? "1.5f;" : "1.f;" ) );
            line( s, 2, "palette.title = " + mix( t.custom_text, light ? black : white, .3f ) + ";" );
            line( s, 2, "palette.primary = " + color32( t.custom_text ) + ";" );
            line( s, 2, "palette.label = " + mix( t.custom_text, t.custom_muted, .35f ) + ";" );
            line( s, 2, "palette.secondary = " + mix( t.custom_text, t.custom_muted, .75f ) + ";" );
            line( s, 2, "palette.muted = " + color32( t.custom_muted ) + ";" );
            line( s, 2, "palette.knob_off = " + ( light ? std::string( "IM_COL32( 255, 255, 255, 255 )" ) : mix( t.custom_text, t.custom_muted, .2f ) ) + ";" );
            float popup[4];
            for ( int i = 0; i < 3; ++i )
                popup[i] = t.custom_window[i] + ( 1.f - t.custom_window[i] ) * ( light ? .6f : .05f );
            popup[3] = .98f;
            line( s, 2, "palette.popup = " + color32( popup ) + ";" );
            line( s, 2, "palette.button = " + color32( t.custom_text ) + ";" );
            line( s, 2, "palette.ink = " + color32( t.custom_window ) + ";" );
            line( s, 2, "kit::use_palette( palette );" );
        }
        else
            line( s, 2, std::string( "kit::use_palette( kit::preset( kit::palette_id::" ) + palettes[std::clamp( t.palette, 0, 3 )] + " ) );" );
        line( s, 2, "kit::theme.accent = { " + number( t.accent[0] ) + ", " + number( t.accent[1] ) + ", " + number( t.accent[2] ) + ", 1.f };" );
        line( s, 2, "kit::theme.window_alpha = " + number( t.window_alpha ) + ";" );
        line( s, 2, "kit::theme.rounding = " + number( t.rounding ) + ";" );
        line( s, 2, "kit::theme.control_rounding = " + number( t.control_rounding ) + ";" );
        line( s, 2, "kit::theme.size = { " + number( t.width ) + ", " + number( t.height ) + " };" );
        line( s, 2, std::string( "kit::theme.toggles = kit::toggle_t::" ) + toggles[std::clamp( t.toggles, 0, 1 )] + ";" );
        line( s, 2, std::string( "kit::theme.sliders = kit::slider_t::" ) + sliders[std::clamp( t.sliders, 0, 1 )] + ";" );
        line( s, 2, std::string( "kit::theme.buttons = kit::button_t::" ) + buttons[std::clamp( t.buttons, 0, 2 )] + ";" );
        line( s, 2, std::string( "kit::theme.fields = kit::field_t::" ) + fields[std::clamp( t.fields, 0, 1 )] + ";" );
        if ( menu )
        {
            line( s, 2, "kit::theme.list_width = " + number( t.list_width ) + ";" );
            line( s, 2, "kit::theme.columns = " + std::to_string( std::max( 1, t.columns ) ) + ";" );
            line( s, 2, "kit::theme.title = " + literal( t.title ) + ";" );
            line( s, 2, "kit::theme.footer = " + literal( t.footer ) + ";" );
        }
        else
        {
            if ( t.resizable )
                line( s, 2, "kit::theme.resizable = true;" );
            if ( t.transition )
                line( s, 2, "kit::theme.transition = " + std::to_string( t.transition ) + "; // " + ( t.transition == 1 ? "fade" : "slide" ) );
        }
    }

    void live_header( std::string& h )
    {
        line( h, 0, "" );
        line( h, 0, "#ifdef KIT_LIVE" );
        line( h, 1, "// Live link: draws from the project file instead, and reloads it whenever it is saved." );
        line( h, 1, "void live( const char* project_path );" );
        line( h, 0, "#endif" );
    }

    // ---- apps ----

    auto generate_app( const doc::document_t& source, const std::string& ns ) -> codegen::output_t
    {
        auto& d = const_cast<doc::document_t&>( source ); // flatten( ) only syncs instance sizes
        codegen::output_t out;
        out.header_name = ns + ".h";
        out.source_name = ns + ".cpp";
        const auto n = names::app( d );
        const std::string start = d.screens.empty( ) ? std::string( ) : n.screen.at( d.screens.front( ).uid );
        const bool any_text = !n.text.empty( );

        // Images: one header of byte arrays.
        if ( !d.images.empty( ) )
        {
            out.images_name = ns + "_images.h";
            auto& m = out.images;
            line( m, 0, "#pragma once" );
            line( m, 0, "// Images embedded by ImGui Studio (PNG / JPEG bytes)." );
            line( m, 0, "" );
            line( m, 0, "namespace " + ns );
            line( m, 0, "{" );
            for ( const auto& image : d.images )
            {
                line( m, 1, "// " + image.name + ", " + std::to_string( image.width ) + " x " + std::to_string( image.height ) );
                line( m, 1, "inline constexpr unsigned char " + n.image.at( image.uid ) + "[] = {" );
                std::string row;
                for ( size_t i = 0; i < image.data.size( ); ++i )
                {
                    row += std::to_string( static_cast<unsigned char>( image.data[i] ) ) + ",";
                    if ( row.size( ) > 110 || i + 1 == image.data.size( ) )
                    {
                        line( m, 2, row );
                        row.clear( );
                    }
                }
                line( m, 1, "};" );
            }
            line( m, 0, "}" );
        }

        // Header: screens, state, events, custom widgets.
        std::string& h = out.header;
        line( h, 0, "#pragma once" );
        line( h, 0, "// Generated by ImGui Studio from " + literal( d.name ) + ". Edit the project and export again rather than" );
        line( h, 0, "// editing this file: the next export replaces it." );
        line( h, 0, "" );
        line( h, 0, "#include \"kit.h\"" );
        line( h, 0, "" );
        line( h, 0, "#include <functional>" );
        if ( any_text )
            line( h, 0, "#include <string>" );
        line( h, 0, "" );
        if ( !n.functions.empty( ) )
        {
            line( h, 0, "// Custom widgets: write these yourself, each drawing inside the rect it is given with ImGui or kit calls." );
            line( h, 0, "// The same source can be built into a widget plugin, so the editor shows them too." );
            for ( const auto& fn : n.functions )
                line( h, 0, "void " + fn + "( const ImRect& r );" );
            line( h, 0, "" );
        }
        line( h, 0, "namespace " + ns );
        line( h, 0, "{" );
        line( h, 1, "enum class screen_t" );
        line( h, 1, "{" );
        for ( const auto& screen : d.screens )
            line( h, 2, n.screen.at( screen.uid ) + "," );
        line( h, 1, "};" );
        line( h, 0, "" );
        line( h, 1, "// Everything the user can change, and everything your code can, at the values the project was saved with." );
        line( h, 1, "struct state_t" );
        line( h, 1, "{" );
        if ( !start.empty( ) )
            line( h, 2, "screen_t screen = screen_t::" + start + ";" );
        line( h, 2, "bool open = true; // cleared by a button that closes the window" );
        for ( auto& screen : d.screens )
        {
            bool heading = false;
            for ( const auto& e : screen.elements )
            {
                const auto key = static_cast<unsigned long long>( e.uid );
                if ( e.kind != doc::element_kind_t::instance || ( !n.visible.count( key ) && !n.enabled.count( key ) ) )
                    continue;
                if ( !heading )
                {
                    line( h, 2, "// " + screen.name );
                    heading = true;
                }
                if ( n.visible.count( key ) )
                    line( h, 2, "bool " + n.visible.at( key ) + " = " + ( e.shown ? "true;" : "false;" ) );
                if ( n.enabled.count( key ) )
                    line( h, 2, "bool " + n.enabled.at( key ) + " = " + ( e.enabled ? "true;" : "false;" ) );
            }
            for ( const auto& f : doc::flatten( d, screen.elements ) )
            {
                const auto& e = *f.element;
                std::vector<std::string> rows;
                if ( n.value.count( f.key ) )
                {
                    const auto& name = n.value.at( f.key );
                    switch ( e.kind )
                    {
                    case doc::element_kind_t::input: rows.push_back( "char " + name + "[256] = " + literal( e.value ) + ";" ); break;
                    case doc::element_kind_t::checkbox: rows.push_back( "bool " + name + " = " + ( e.on ? "true;" : "false;" ) ); break;
                    case doc::element_kind_t::slider:
                    case doc::element_kind_t::progress: rows.push_back( "float " + name + " = " + number( e.number ) + ";" ); break;
                    default: rows.push_back( "int " + name + " = " + std::to_string( e.choice ) + "; // " + quoted_list( e.options ) ); break;
                    }
                }
                if ( n.text.count( f.key ) )
                    rows.push_back( "std::string " + n.text.at( f.key ) + " = " + literal( e.text ) + ";" );
                if ( n.visible.count( f.key ) )
                    rows.push_back( "bool " + n.visible.at( f.key ) + " = " + ( e.shown ? "true;" : "false;" ) );
                if ( n.enabled.count( f.key ) )
                    rows.push_back( "bool " + n.enabled.at( f.key ) + " = " + ( e.enabled ? "true;" : "false;" ) );
                if ( !rows.empty( ) && !heading )
                {
                    line( h, 2, "// " + screen.name );
                    heading = true;
                }
                for ( const auto& r : rows )
                    line( h, 2, r );
            }
        }
        line( h, 1, "};" );
        line( h, 1, "inline state_t state;" );
        if ( !n.event.empty( ) )
        {
            line( h, 0, "" );
            line( h, 1, "// Set these to run your code when a button is pressed (after any screen change it makes)." );
            line( h, 1, "struct events_t" );
            line( h, 1, "{" );
            for ( auto& screen : d.screens )
                for ( const auto& f : doc::flatten( d, screen.elements ) )
                    if ( n.event.count( f.key ) )
                        line( h, 2, "std::function<void( )> " + n.event.at( f.key ) + "; // " + screen.name + ": \"" + f.element->text + "\"" );
            line( h, 1, "};" );
            line( h, 1, "inline events_t events;" );
        }
        line( h, 0, "" );
        line( h, 1, "// Call once before the font atlas is built: loads the font and applies the theme." );
        line( h, 1, "void setup( ImGuiIO& io );" );
        line( h, 1, "// Draws the window while state.open. Call between ImGui::NewFrame( ) and ImGui::Render( )." );
        line( h, 1, "void draw( );" );
        live_header( h );
        line( h, 0, "}" );

        // Source: a draw function per screen.
        std::string& s = out.source;
        constexpr const char* aligns[] = { "left", "centre", "right" };
        constexpr const char* roles[] = { "title", "body", "muted", "accent" };
        constexpr const char* pins[] = { "start", "end", "stretch", "centre" };
        line( s, 0, "// Generated by ImGui Studio from " + literal( d.name ) + "." );
        line( s, 0, "#include \"" + out.header_name + "\"" );
        if ( !out.images_name.empty( ) )
            line( s, 0, "#include \"" + out.images_name + "\"" );
        line( s, 0, "" );
        line( s, 0, "#ifdef KIT_LIVE" );
        line( s, 0, "#include \"kit_live/live.h\"" );
        line( s, 0, "#endif" );
        line( s, 0, "" );
        line( s, 0, "namespace " + ns );
        line( s, 0, "{" );
        line( s, 1, "namespace" );
        line( s, 1, "{" );
        bool first = true;
        for ( auto& screen : d.screens )
        {
            if ( !first )
                line( s, 0, "" );
            first = false;
            std::string body;
            bool uses_state = false;
            const auto S = [&]( const std::string& name ) {
                uses_state = true;
                return "s." + name;
            };
            const auto flats = doc::flatten( d, screen.elements );
            for ( const auto& f : flats )
            {
                const auto& e = *f.element;
                int depth = 3;
                // Conditions: the tab it lives on, and whether your code hides it.
                std::vector<std::string> conditions;
                if ( e.tab_owner )
                {
                    const unsigned long long owner = f.instance ? ( static_cast<unsigned long long>( f.instance ) << 32 ) |
                                                                      static_cast<unsigned long long>( e.tab_owner )
                                                                : static_cast<unsigned long long>( e.tab_owner );
                    if ( n.value.count( owner ) )
                        conditions.push_back( S( n.value.at( owner ) ) + " == " + std::to_string( e.tab_index ) );
                }
                if ( n.visible.count( f.key ) )
                    conditions.push_back( S( n.visible.at( f.key ) ) );
                if ( f.owner )
                {
                    const auto owner_key = static_cast<unsigned long long>( f.owner->uid );
                    if ( f.owner->tab_owner && n.value.count( static_cast<unsigned long long>( f.owner->tab_owner ) ) )
                        conditions.push_back( S( n.value.at( static_cast<unsigned long long>( f.owner->tab_owner ) ) ) + " == " +
                                              std::to_string( f.owner->tab_index ) );
                    if ( n.visible.count( owner_key ) )
                        conditions.push_back( S( n.visible.at( owner_key ) ) );
                }
                if ( !conditions.empty( ) )
                {
                    std::string joined;
                    for ( size_t i = 0; i < conditions.size( ); ++i )
                        joined += ( i ? " && " : "" ) + conditions[i];
                    line( body, depth, "if ( " + joined + " )" );
                    line( body, depth, "{" );
                    ++depth;
                }
                if ( n.enabled.count( f.key ) )
                    line( body, depth, "ImGui::BeginDisabled( !" + S( n.enabled.at( f.key ) ) + " );" );
                const bool owner_lock = f.owner && n.enabled.count( static_cast<unsigned long long>( f.owner->uid ) );
                if ( owner_lock )
                    line( body, depth, "ImGui::BeginDisabled( !" + S( n.enabled.at( static_cast<unsigned long long>( f.owner->uid ) ) ) + " );" );

                std::string at = "at( " + whole( e.x + f.dx ) + ", " + whole( e.y + f.dy ) + ", " + whole( e.w ) + ", " + whole( e.h );
                if ( !f.instance && ( e.pin_x || e.pin_y ) )
                    at += std::string( ", pin_t::" ) + pins[std::clamp( e.pin_x, 0, 3 )] + ", pin_t::" + pins[std::clamp( e.pin_y, 0, 3 )];
                at += " )";
                const std::string field = n.value.count( f.key ) ? S( n.value.at( f.key ) ) : "";
                const std::string label = n.label.count( f.key ) ? literal( n.label.at( f.key ) ) : literal( e.text );
                const std::string tint = e.tint ? color32( e.rgba ) : "";
                switch ( e.kind )
                {
                case doc::element_kind_t::text:
                {
                    std::string style;
                    const auto add = [&]( const std::string& part ) { style += ( style.empty( ) ? "" : ", " ) + part; };
                    if ( e.size != 13.f )
                        add( ".size = " + number( e.size ) );
                    if ( e.align != 0 )
                        add( std::string( ".align = align_t::" ) + aligns[std::clamp( e.align, 0, 2 )] );
                    if ( e.role != 1 )
                        add( std::string( ".role = role_t::" ) + roles[std::clamp( e.role, 0, 3 )] );
                    if ( e.tint )
                        add( ".color = " + tint );
                    const std::string content = n.text.count( f.key ) ? S( n.text.at( f.key ) ) + ".c_str( )" : literal( e.text );
                    line( body, depth, "text( " + content + ", " + at + ( style.empty( ) ? "" : ", { " + style + " }" ) + " );" );
                    break;
                }
                case doc::element_kind_t::button:
                {
                    std::string args = label + ", " + at;
                    if ( e.primary || e.icon || e.tint )
                        args += e.primary ? ", true" : ", false";
                    if ( e.icon || e.tint )
                        args += ", " + icon_id( e.icon );
                    if ( e.tint )
                        args += ", " + tint;
                    std::vector<std::string> then;
                    if ( e.target == -1 )
                        then.push_back( S( "open" ) + " = false;" );
                    else if ( e.target > 0 && n.screen.count( e.target ) )
                        then.push_back( S( "screen" ) + " = screen_t::" + n.screen.at( e.target ) + ";" );
                    if ( !e.toast.empty( ) )
                        then.push_back( "kit::toast( " + literal( e.toast ) + " );" );
                    const auto& event = n.event.at( f.key );
                    line( body, depth, "if ( button( " + args + " ) )" );
                    line( body, depth, "{" );
                    for ( const auto& t : then )
                        line( body, depth + 1, t );
                    line( body, depth + 1, "if ( events." + event + " )" );
                    line( body, depth + 2, "events." + event + "( );" );
                    line( body, depth, "}" );
                    break;
                }
                case doc::element_kind_t::input:
                    line( body, depth, "input( \"##" + n.value.at( f.key ) + "\", " + field + ", sizeof( " + field + " ), " + at + ", " +
                                           literal( e.text ) + ( e.password ? ", true" : "" ) + " );" );
                    break;
                case doc::element_kind_t::checkbox: line( body, depth, "checkbox( " + label + ", " + field + ", " + at + " );" ); break;
                case doc::element_kind_t::slider:
                    line( body, depth, "slider( \"##" + n.value.at( f.key ) + "\", " + field + ", " + number( e.min ) + ", " + number( e.max ) +
                                           ", " + at + ( e.format != "%.0f" ? ", " + literal( e.format ) : "" ) + " );" );
                    break;
                case doc::element_kind_t::dropdown:
                    line( body, depth, "{" );
                    line( body, depth + 1, "static const char* const options[] = { " + quoted_list( e.options ) + " };" );
                    line( body, depth + 1, "dropdown( \"##" + n.value.at( f.key ) + "\", " + field + ", options, " + at + " );" );
                    line( body, depth, "}" );
                    break;
                case doc::element_kind_t::tabs:
                    line( body, depth, "{" );
                    line( body, depth + 1, "static const char* const pages[] = { " + quoted_list( e.options ) + " };" );
                    line( body, depth + 1, "tabs( \"##" + n.value.at( f.key ) + "\", " + field + ", pages, " + at + " );" );
                    line( body, depth, "}" );
                    break;
                case doc::element_kind_t::list:
                {
                    line( body, depth, "{" );
                    line( body, depth + 1, "static const char* const rows[] = { " + quoted_list( e.options ) + " };" );
                    bool any_detail = false;
                    for ( const auto& detail : e.details )
                        any_detail |= !detail.empty( );
                    if ( any_detail )
                    {
                        auto details = e.details;
                        details.resize( e.options.size( ) );
                        line( body, depth + 1, "static const char* const details[] = { " + quoted_list( details ) + " };" );
                    }
                    line( body, depth + 1, "list( \"##" + n.value.at( f.key ) + "\", " + field + ", rows, " + at + ( any_detail ? ", details" : "" ) + " );" );
                    line( body, depth, "}" );
                    break;
                }
                case doc::element_kind_t::progress:
                    line( body, depth, "progress( " + ( e.endless ? std::string( "-1.f" ) : field ) + ", " + at + " );" );
                    break;
                case doc::element_kind_t::panel: line( body, depth, "panel( " + at + ( e.tint ? ", " + tint : "" ) + " );" ); break;
                case doc::element_kind_t::divider: line( body, depth, "divider( " + at + " );" ); break;
                case doc::element_kind_t::spinner: line( body, depth, "spinner( " + at + " );" ); break;
                case doc::element_kind_t::image:
                    if ( n.image.count( e.image ) )
                        line( body, depth, "image( " + n.image.at( e.image ) + ", sizeof( " + n.image.at( e.image ) + " ), " + at +
                                               ( e.rounding > 0 ? ", " + number( e.rounding ) : "" ) + " );" );
                    break;
                case doc::element_kind_t::icon:
                    line( body, depth, "icon( " + icon_id( e.icon ) + ", " + at + ( e.tint ? ", " + tint : "" ) + " );" );
                    break;
                case doc::element_kind_t::custom:
                    line( body, depth, "::" + doc::identifier( e.function.empty( ) ? "draw_custom" : e.function ) + "( " + at + " );" );
                    break;
                default: break;
                }

                if ( owner_lock )
                    line( body, depth, "ImGui::EndDisabled( );" );
                if ( n.enabled.count( f.key ) )
                    line( body, depth, "ImGui::EndDisabled( );" );
                if ( !conditions.empty( ) )
                    line( body, depth - 1, "}" );
            }
            line( s, 2, "void " + n.draw.at( screen.uid ) + "( )" );
            line( s, 2, "{" );
            line( s, 3, "using namespace kit::el;" );
            if ( uses_state )
                line( s, 3, "auto& s = state;" );
            s += body;
            line( s, 2, "}" );
        }
        line( s, 1, "}" );
        line( s, 0, "" );
        line( s, 1, "void setup( ImGuiIO& io )" );
        line( s, 1, "{" );
        theme_lines( s, d, false );
        line( s, 1, "}" );
        line( s, 0, "" );
        line( s, 1, "void draw( )" );
        line( s, 1, "{" );
        line( s, 2, "if ( !state.open )" );
        line( s, 3, "return;" );
        line( s, 0, "#ifdef KIT_LIVE" );
        line( s, 2, "if ( kit_live::draw( ) )" );
        line( s, 3, "return;" );
        line( s, 0, "#endif" );
        line( s, 2, "kit::begin_window( \"##" + ns + "\" );" );
        line( s, 2, "kit::screen( static_cast<int>( state.screen ) );" );
        line( s, 2, "switch ( state.screen )" );
        line( s, 2, "{" );
        for ( const auto& screen : d.screens )
            line( s, 2, "case screen_t::" + n.screen.at( screen.uid ) + ": " + n.draw.at( screen.uid ) + "( ); break;" );
        line( s, 2, "}" );
        line( s, 2, "kit::end_window( );" );
        line( s, 1, "}" );

        // Live link: the same values, by name.
        line( s, 0, "" );
        line( s, 0, "#ifdef KIT_LIVE" );
        line( s, 1, "void live( const char* project_path )" );
        line( s, 1, "{" );
        line( s, 2, "using kit_live::kind_t;" );
        line( s, 2, "kit_live::watch( project_path, {" );
        line( s, 3, "{ \"screen\", kind_t::whole, &state.screen }," );
        line( s, 3, "{ \"open\", kind_t::flag, &state.open }," );
        for ( auto& screen : d.screens )
            for ( const auto& e : screen.elements )
                for ( const auto* names_of : { &n.visible, &n.enabled } )
                    if ( e.kind == doc::element_kind_t::instance && names_of->count( static_cast<unsigned long long>( e.uid ) ) )
                    {
                        const auto& name = names_of->at( static_cast<unsigned long long>( e.uid ) );
                        line( s, 3, "{ \"" + name + "\", kind_t::flag, &state." + name + " }," );
                    }
        for ( auto& screen : d.screens )
            for ( const auto& f : doc::flatten( d, screen.elements ) )
            {
                const auto& e = *f.element;
                if ( n.value.count( f.key ) )
                {
                    const auto& name = n.value.at( f.key );
                    switch ( e.kind )
                    {
                    case doc::element_kind_t::input:
                        line( s, 3, "{ \"" + name + "\", kind_t::buffer, state." + name + ", sizeof( state." + name + " ) }," );
                        break;
                    case doc::element_kind_t::checkbox: line( s, 3, "{ \"" + name + "\", kind_t::flag, &state." + name + " }," ); break;
                    case doc::element_kind_t::slider:
                    case doc::element_kind_t::progress: line( s, 3, "{ \"" + name + "\", kind_t::number, &state." + name + " }," ); break;
                    default: line( s, 3, "{ \"" + name + "\", kind_t::whole, &state." + name + " }," ); break;
                    }
                }
                if ( n.text.count( f.key ) )
                    line( s, 3, "{ \"" + n.text.at( f.key ) + "\", kind_t::string, &state." + n.text.at( f.key ) + " }," );
                if ( n.visible.count( f.key ) )
                    line( s, 3, "{ \"" + n.visible.at( f.key ) + "\", kind_t::flag, &state." + n.visible.at( f.key ) + " }," );
                if ( n.enabled.count( f.key ) )
                    line( s, 3, "{ \"" + n.enabled.at( f.key ) + "\", kind_t::flag, &state." + n.enabled.at( f.key ) + " }," );
                if ( n.event.count( f.key ) )
                    line( s, 3, "{ \"" + n.event.at( f.key ) + "\", kind_t::event, &events." + n.event.at( f.key ) + " }," );
            }
        for ( const auto& fn : n.functions )
            line( s, 3, "{ \"" + fn + "\", kind_t::widget, reinterpret_cast<void*>( &::" + fn + " ) }," );
        line( s, 2, "} );" );
        line( s, 1, "}" );
        line( s, 0, "#endif" );
        line( s, 0, "}" );
        return out;
    }

    // ---- menus ----

    auto generate_menu( const doc::document_t& d, const std::string& ns ) -> codegen::output_t
    {
        codegen::output_t out;
        out.header_name = ns + ".h";
        out.source_name = ns + ".cpp";
        const auto p = names::menu( d );

        std::string& h = out.header;
        line( h, 0, "#pragma once" );
        line( h, 0, "// Generated by ImGui Studio from " + literal( d.name ) + ". Edit the project and export again rather than" );
        line( h, 0, "// editing this file: the next export replaces it." );
        line( h, 0, "" );
        line( h, 0, "#include \"kit.h\"" );
        line( h, 0, "" );
        line( h, 0, "namespace " + ns );
        line( h, 0, "{" );
        line( h, 1, "struct settings_t" );
        line( h, 1, "{" );
        for ( const auto& c : d.categories )
            for ( const auto& g : c.groups )
                for ( const auto& f : g.features )
                {
                    line( h, 2, "// " + c.name + " / " + f.name );
                    if ( f.has_switch )
                        line( h, 2, "bool " + p.feature_on.at( f.uid ) + " = " + ( f.enabled ? "true" : "false" ) + ";" );
                    if ( f.has_key )
                    {
                        line( h, 2, "int " + p.feature_key.at( f.uid ) + " = " + hex_key( f.key ) + ";" );
                        line( h, 2, "int " + p.feature_mode.at( f.uid ) + " = " + std::to_string( f.mode ) + "; // 0 hold, 1 toggle, 2 always" );
                    }
                    for ( const auto& s : f.sections )
                        for ( const auto& w : s.widgets )
                        {
                            const auto& n = p.widgets.at( w.uid );
                            switch ( w.kind )
                            {
                            case doc::kind_t::toggle: line( h, 2, "bool " + n.main + " = " + ( w.on ? "true;" : "false;" ) ); break;
                            case doc::kind_t::slider: line( h, 2, "float " + n.main + " = " + number( w.number ) + ";" ); break;
                            case doc::kind_t::slider_int:
                                line( h, 2, "int " + n.main + " = " + std::to_string( static_cast<int>( w.number ) ) + ";" );
                                break;
                            case doc::kind_t::select:
                            case doc::kind_t::segment:
                                line( h, 2, "int " + n.main + " = " + std::to_string( w.choice ) + "; // " + quoted_list( w.options ) );
                                break;
                            case doc::kind_t::chips:
                                for ( size_t i = 0; i < n.chips.size( ); ++i )
                                    line( h, 2, "bool " + n.chips[i] + " = " + ( i < w.chip_values.size( ) && w.chip_values[i] ? "true;" : "false;" ) );
                                break;
                            case doc::kind_t::color:
                                line( h, 2, "float " + n.main + "[4] = " + vec4( w.rgba ) + ";" );
                                if ( w.has_switch )
                                    line( h, 2, "bool " + n.on + " = " + ( w.on ? "true;" : "false;" ) );
                                break;
                            case doc::kind_t::keybind:
                                line( h, 2, "int " + n.key + " = " + hex_key( w.key ) + ";" );
                                if ( w.has_mode )
                                    line( h, 2, "int " + n.mode + " = " + std::to_string( w.mode ) + ";" );
                                if ( w.has_switch )
                                    line( h, 2, "bool " + n.on + " = " + ( w.on ? "true;" : "false;" ) );
                                break;
                            case doc::kind_t::button:
                                line( h, 2, "void ( *" + n.main + " )( ) = nullptr; // called when \"" + w.text + "\" is pressed" );
                                break;
                            default: break;
                            }
                        }
                }
        line( h, 1, "};" );
        line( h, 1, "inline settings_t settings;" );
        line( h, 0, "" );
        line( h, 1, "// Call once before the font atlas is built: loads the font and applies the theme." );
        line( h, 1, "void setup( ImGuiIO& io );" );
        line( h, 1, "// Draws the menu window. Call between ImGui::NewFrame( ) and ImGui::Render( )." );
        line( h, 1, "void draw( );" );
        live_header( h );
        line( h, 0, "}" );

        std::string& s = out.source;
        line( s, 0, "// Generated by ImGui Studio from " + literal( d.name ) + "." );
        line( s, 0, "#include \"" + out.header_name + "\"" );
        line( s, 0, "" );
        line( s, 0, "#ifdef KIT_LIVE" );
        line( s, 0, "#include \"kit_live/live.h\"" );
        line( s, 0, "#endif" );
        line( s, 0, "" );
        line( s, 0, "namespace " + ns );
        line( s, 0, "{" );
        line( s, 1, "namespace" );
        line( s, 1, "{" );
        line( s, 2, "kit::shell_state_t state;" );
        const int columns = std::max( 1, d.theme.columns );
        for ( const auto& c : d.categories )
            for ( const auto& g : c.groups )
                for ( const auto& f : g.features )
                {
                    line( s, 0, "" );
                    line( s, 2, "void " + p.feature_draw.at( f.uid ) + "( )" );
                    line( s, 2, "{" );
                    bool uses = false;
                    for ( const auto& section : f.sections )
                        for ( const auto& w : section.widgets )
                            uses |= w.kind != doc::kind_t::value && w.kind != doc::kind_t::note;
                    if ( uses )
                        line( s, 3, "auto& s = settings;" );
                    line( s, 3, "kit::begin_columns( " + std::to_string( columns ) + " );" );
                    for ( int column = 0; column < columns; ++column )
                    {
                        if ( column > 0 )
                            line( s, 3, "kit::next_column( );" );
                        for ( const auto& section : f.sections )
                        {
                            if ( std::clamp( section.column, 0, columns - 1 ) != column )
                                continue;
                            line( s, 3, "kit::section( " + literal( section.title ) + " );" );
                            for ( const auto& w : section.widgets )
                            {
                                const auto& n = p.widgets.at( w.uid );
                                const std::string label = literal( w.label );
                                switch ( w.kind )
                                {
                                case doc::kind_t::toggle: line( s, 3, "kit::toggle( " + label + ", s." + n.main + ", " + hint_arg( w ) + " );" ); break;
                                case doc::kind_t::slider:
                                    line( s, 3, "kit::slider( " + label + ", s." + n.main + ", " + number( w.min ) + ", " + number( w.max ) + ", " +
                                                    literal( w.format ) + ", " + literal( w.unit ) + ", " + hint_arg( w ) + " );" );
                                    break;
                                case doc::kind_t::slider_int:
                                    line( s, 3, "kit::slider( " + label + ", s." + n.main + ", " + std::to_string( static_cast<int>( w.min ) ) + ", " +
                                                    std::to_string( static_cast<int>( w.max ) ) + ", " + literal( w.unit ) + ", " + hint_arg( w ) + " );" );
                                    break;
                                case doc::kind_t::select:
                                case doc::kind_t::segment:
                                    line( s, 3, "{" );
                                    line( s, 4, "static const char* const options[] = { " + quoted_list( w.options ) + " };" );
                                    if ( w.kind == doc::kind_t::select )
                                        line( s, 4, "kit::select( " + label + ", s." + n.main + ", options, " + hint_arg( w ) + " );" );
                                    else
                                        line( s, 4, "kit::segment( " + label + ", s." + n.main + ", options );" );
                                    line( s, 3, "}" );
                                    break;
                                case doc::kind_t::chips:
                                {
                                    std::string list;
                                    for ( size_t i = 0; i < n.chips.size( ); ++i )
                                        list += ( i ? ", " : "" ) + std::string( "{ " ) + literal( w.options[i] ) + ", &s." + n.chips[i] + " }";
                                    line( s, 3, "kit::chips( " + label + ", { " + list + " }, " + hint_arg( w ) + " );" );
                                    break;
                                }
                                case doc::kind_t::color:
                                    line( s, 3, "kit::color( " + label + ", s." + n.main + ", " + ( w.has_switch ? "&s." + n.on : "nullptr" ) + ", " +
                                                    hint_arg( w ) + " );" );
                                    break;
                                case doc::kind_t::keybind:
                                    line( s, 3, "kit::keybind( " + label + ", s." + n.key + ", " + ( w.has_mode ? "&s." + n.mode : "nullptr" ) + ", " +
                                                    ( w.has_switch ? "&s." + n.on : "nullptr" ) + ", " + hint_arg( w ) + " );" );
                                    break;
                                case doc::kind_t::button:
                                    line( s, 3, "if ( kit::button( " + label + ", " + literal( w.text ) + " ) && s." + n.main + " )" );
                                    line( s, 4, "s." + n.main + "( );" );
                                    break;
                                case doc::kind_t::value: line( s, 3, "kit::value( " + label + ", " + literal( w.text ) + " );" ); break;
                                case doc::kind_t::note: line( s, 3, "kit::note( " + literal( w.text ) + " );" ); break;
                                default: break;
                                }
                            }
                        }
                    }
                    line( s, 3, "kit::end_columns( );" );
                    line( s, 2, "}" );
                }

        line( s, 0, "" );
        line( s, 2, "std::vector<kit::category_t> build( )" );
        line( s, 2, "{" );
        bool any_feature = false;
        for ( const auto& c : d.categories )
            for ( const auto& g : c.groups )
                any_feature |= !g.features.empty( );
        if ( any_feature )
            line( s, 3, "auto& s = settings;" );
        line( s, 3, "std::vector<kit::category_t> categories;" );
        for ( const auto& c : d.categories )
        {
            line( s, 3, "categories.push_back( { " + literal( c.name ) + ", { } } );" );
            for ( const auto& g : c.groups )
            {
                line( s, 3, "categories.back( ).groups.push_back( { " + literal( g.title ) + ", { } } );" );
                for ( const auto& f : g.features )
                {
                    std::string meta;
                    if ( !f.meta.empty( ) )
                        meta = "[] { return std::string( " + literal( f.meta ) + " ); }";
                    else if ( f.has_key )
                        meta = "[] { return kit::keyed( settings." + p.feature_key.at( f.uid ) + ", settings." + p.feature_mode.at( f.uid ) + " ); }";
                    else
                        meta = "nullptr";
                    line( s, 3, "categories.back( ).groups.back( ).features.push_back( { " + literal( f.name ) + ", " + literal( f.description ) +
                                    ", " + ( f.has_switch ? "&s." + p.feature_on.at( f.uid ) : "nullptr" ) + ", " +
                                    ( f.has_key ? "&s." + p.feature_key.at( f.uid ) : "nullptr" ) + ", " +
                                    ( f.has_key ? "&s." + p.feature_mode.at( f.uid ) : "nullptr" ) + ", " + meta + ", " + p.feature_draw.at( f.uid ) + " } );" );
                }
            }
        }
        line( s, 3, "return categories;" );
        line( s, 2, "}" );
        line( s, 1, "}" );
        line( s, 0, "" );
        line( s, 1, "void setup( ImGuiIO& io )" );
        line( s, 1, "{" );
        theme_lines( s, d, true );
        line( s, 1, "}" );
        line( s, 0, "" );
        line( s, 1, "void draw( )" );
        line( s, 1, "{" );
        line( s, 0, "#ifdef KIT_LIVE" );
        line( s, 2, "if ( kit_live::draw( ) )" );
        line( s, 3, "return;" );
        line( s, 0, "#endif" );
        line( s, 2, "static const auto categories = build( );" );
        line( s, 2, "kit::window( \"##" + ns + "\", categories, state );" );
        line( s, 1, "}" );

        line( s, 0, "" );
        line( s, 0, "#ifdef KIT_LIVE" );
        line( s, 1, "void live( const char* project_path )" );
        line( s, 1, "{" );
        line( s, 2, "using kit_live::kind_t;" );
        line( s, 2, "auto& s = settings;" );
        line( s, 2, "kit_live::watch( project_path, {" );
        const auto bind = [&]( const std::string& name, const char* kind, bool pointer = true ) {
            line( s, 3, "{ \"" + name + "\", kind_t::" + kind + ", " + ( pointer ? "&s." : "s." ) + name + " }," );
        };
        for ( const auto& c : d.categories )
            for ( const auto& g : c.groups )
                for ( const auto& f : g.features )
                {
                    if ( f.has_switch )
                        bind( p.feature_on.at( f.uid ), "flag" );
                    if ( f.has_key )
                    {
                        bind( p.feature_key.at( f.uid ), "whole" );
                        bind( p.feature_mode.at( f.uid ), "whole" );
                    }
                    for ( const auto& section : f.sections )
                        for ( const auto& w : section.widgets )
                        {
                            const auto& n = p.widgets.at( w.uid );
                            switch ( w.kind )
                            {
                            case doc::kind_t::toggle: bind( n.main, "flag" ); break;
                            case doc::kind_t::slider: bind( n.main, "number" ); break;
                            case doc::kind_t::slider_int:
                            case doc::kind_t::select:
                            case doc::kind_t::segment: bind( n.main, "whole" ); break;
                            case doc::kind_t::chips:
                                for ( const auto& chip : n.chips )
                                    bind( chip, "flag" );
                                break;
                            case doc::kind_t::color:
                                bind( n.main, "color", false );
                                if ( w.has_switch )
                                    bind( n.on, "flag" );
                                break;
                            case doc::kind_t::keybind:
                                bind( n.key, "whole" );
                                if ( w.has_mode )
                                    bind( n.mode, "whole" );
                                if ( w.has_switch )
                                    bind( n.on, "flag" );
                                break;
                            case doc::kind_t::button: bind( n.main, "callback" ); break;
                            default: break;
                            }
                        }
                }
        line( s, 2, "} );" );
        line( s, 1, "}" );
        line( s, 0, "#endif" );
        line( s, 0, "}" );
        return out;
    }
}

auto codegen::generate( const doc::document_t& d ) -> output_t
{
    std::string ns = doc::identifier( d.theme.namespace_name.empty( ) ? "menu" : d.theme.namespace_name );
    if ( names::keyword( ns ) )
        ns += "_";
    return d.project == doc::project_t::app ? generate_app( d, ns ) : generate_menu( d, ns );
}

auto codegen::export_to( const doc::document_t& d, const std::wstring& folder, bool& ok ) -> std::string
{
    ok = false;
    namespace fs = std::filesystem;
    const auto out = generate( d );
    std::error_code error;
    fs::create_directories( folder, error );
    const auto write = [&]( const std::string& name, const std::string& text ) {
        std::ofstream file( fs::path( folder ) / name, std::ios::binary | std::ios::trunc );
        file << text;
        return static_cast<bool>( file );
    };
    if ( !write( out.header_name, out.header ) || !write( out.source_name, out.source ) ||
         ( !out.images_name.empty( ) && !write( out.images_name, out.images ) ) )
        return "Could not write to that folder.";

    // The kit, and the live-link sources in kit_live/ (only compiled when KIT_LIVE is defined).
    wchar_t exe[MAX_PATH]{ };
    GetModuleFileNameW( nullptr, exe, MAX_PATH );
    const fs::path kit = fs::path( exe ).parent_path( ) / L"kit";
    for ( const wchar_t* name : { L"kit.h", L"kit.cpp", L"inter_font.h" } )
    {
        fs::copy_file( kit / name, fs::path( folder ) / name, fs::copy_options::overwrite_existing, error );
        if ( error )
            return "Exported the menu, but the kit files next to imgui_studio.exe are missing.";
    }
    fs::create_directories( fs::path( folder ) / L"kit_live", error );
    for ( const auto& entry : fs::directory_iterator( kit / L"live", error ) )
        fs::copy_file( entry.path( ), fs::path( folder ) / L"kit_live" / entry.path( ).filename( ), fs::copy_options::overwrite_existing, error );
    ok = true;
    return "Exported " + out.header_name + ", " + out.source_name + ( out.images_name.empty( ) ? "" : ", " + out.images_name ) + " and the kit.";
}
