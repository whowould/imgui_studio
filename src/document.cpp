#include "document.h"
#include "json.h"

#include <algorithm>
#include <map>
#include <set>
#include <cctype>

namespace
{
    using doc::kind_t;
    constexpr const char* kind_names[] = { "toggle", "slider", "slider_int", "select", "segment", "chips",
                                           "color",  "keybind", "button",    "value",  "note" };

    auto kind_from( const std::string& name ) -> kind_t
    {
        for ( int i = 0; i < static_cast<int>( kind_t::count ); ++i )
            if ( name == kind_names[i] )
                return static_cast<kind_t>( i );
        return kind_t::toggle;
    }

    using doc::element_kind_t;
    constexpr const char* element_keys[] = { "text",    "button", "input", "checkbox", "slider",   "dropdown",
                                             "progress", "list",  "panel", "divider",  "spinner",  "tabs",
                                             "image",   "icon",   "instance", "custom" };

    auto element_from( const std::string& name ) -> element_kind_t
    {
        for ( int i = 0; i < static_cast<int>( element_kind_t::count ); ++i )
            if ( name == element_keys[i] )
                return static_cast<element_kind_t>( i );
        return element_kind_t::text;
    }

    auto strings( const std::vector<std::string>& list ) -> json::value_t
    {
        auto out = json::value_t::array( );
        for ( const auto& s : list )
            out.push( s );
        return out;
    }

    auto read_strings( const json::value_t* v ) -> std::vector<std::string>
    {
        std::vector<std::string> out;
        if ( v )
            for ( const auto& item : v->items )
                out.push_back( item.as_string( ) );
        return out;
    }

    auto color( const float* rgba ) -> json::value_t
    {
        auto out = json::value_t::array( );
        for ( int i = 0; i < 4; ++i )
            out.push( rgba[i] );
        return out;
    }

    void read_color( const json::value_t* v, float* rgba )
    {
        if ( v )
            for ( size_t i = 0; i < 4 && i < v->items.size( ); ++i )
                rgba[i] = v->items[i].as_float( rgba[i] );
    }

    auto field( const json::value_t& o, const char* key, const std::string& fallback ) -> std::string
    {
        const auto* v = o.get( key );
        return v ? v->as_string( fallback ) : fallback;
    }

    auto field( const json::value_t& o, const char* key, float fallback ) -> float
    {
        const auto* v = o.get( key );
        return v ? v->as_float( fallback ) : fallback;
    }

    auto field_i( const json::value_t& o, const char* key, int fallback ) -> int
    {
        const auto* v = o.get( key );
        return v ? v->as_int( fallback ) : fallback;
    }

    auto field_b( const json::value_t& o, const char* key, bool fallback ) -> bool
    {
        const auto* v = o.get( key );
        return v ? v->as_bool( fallback ) : fallback;
    }

    struct builder_t
    {
        doc::document_t& d;

        auto w( kind_t kind, const char* label ) -> doc::widget_t
        {
            auto widget = doc::make_widget( d, kind );
            widget.label = label;
            widget.id = doc::identifier( label );
            return widget;
        }
    };
}

auto doc::kind_name( kind_t kind ) -> const char*
{
    constexpr const char* titles[] = { "Toggle", "Slider", "Whole-number slider", "Dropdown", "Segmented", "Chips",
                                       "Colour", "Keybind", "Button", "Value",  "Note" };
    return titles[static_cast<int>( kind )];
}

auto doc::make_widget( document_t& d, kind_t kind ) -> widget_t
{
    widget_t w;
    w.uid = d.next_uid++;
    w.kind = kind;
    switch ( kind )
    {
    case kind_t::toggle: w.label = "Toggle"; break;
    case kind_t::slider: w.label = "Amount"; w.number = 50; w.max = 100; w.format = "%.0f"; break;
    case kind_t::slider_int: w.label = "Count"; w.number = 5; w.max = 10; break;
    case kind_t::select: w.label = "Mode"; w.options = { "First", "Second", "Third" }; break;
    case kind_t::segment: w.label = "Style"; w.options = { "Full", "Corner" }; break;
    case kind_t::chips:
        w.label = "Skip";
        w.options = { "Team", "Dead" };
        w.chip_values = { true, false };
        break;
    case kind_t::color: w.label = "Colour"; w.has_switch = true; w.on = true; break;
    case kind_t::keybind: w.label = "Key"; w.key = 0x02; break;
    case kind_t::button: w.label = "Action"; w.text = "Run"; break;
    case kind_t::value: w.label = "Status"; w.text = "Ready"; break;
    case kind_t::note: w.label = ""; w.text = "A short explanation for the rows above."; break;
    default: break;
    }
    w.id = identifier( w.label.empty( ) ? "note" : w.label );
    return w;
}

auto doc::element_name( element_kind_t kind ) -> const char*
{
    constexpr const char* titles[] = { "Text",    "Button", "Input", "Checkbox", "Slider",    "Dropdown",
                                       "Progress", "List",  "Panel", "Divider",  "Spinner",   "Tabs",
                                       "Image",   "Icon",   "Component", "Custom" };
    return titles[static_cast<int>( kind )];
}

auto doc::make_element( document_t& d, element_kind_t kind ) -> element_t
{
    element_t e;
    e.uid = d.next_uid++;
    e.kind = kind;
    switch ( kind )
    {
    case element_kind_t::text: e.text = "Text"; e.w = 200, e.h = 20; break;
    case element_kind_t::button: e.text = "Button"; e.w = 140, e.h = 36; break;
    case element_kind_t::input: e.text = "Type here"; e.w = 240, e.h = 40; break;
    case element_kind_t::checkbox: e.text = "Option"; e.w = 160, e.h = 22; break;
    case element_kind_t::slider: e.text = "Amount"; e.w = 220, e.h = 28, e.number = 50; break;
    case element_kind_t::dropdown: e.text = "Choice"; e.w = 160, e.h = 32, e.options = { "First", "Second", "Third" }; break;
    case element_kind_t::progress: e.text = "Progress"; e.w = 240, e.h = 6, e.number = .5f, e.max = 1; break;
    case element_kind_t::list:
        e.text = "List";
        e.w = 260, e.h = 150;
        e.options = { "First", "Second", "Third" };
        break;
    case element_kind_t::panel: e.text = "Panel"; e.w = 260, e.h = 160; break;
    case element_kind_t::divider: e.text = "Divider"; e.w = 260, e.h = 9; break;
    case element_kind_t::spinner: e.text = "Spinner"; e.w = 32, e.h = 32; break;
    case element_kind_t::tabs: e.text = "Tabs"; e.w = 336, e.h = 36, e.options = { "General", "Advanced" }; break;
    case element_kind_t::image: e.text = "Image"; e.w = 120, e.h = 120, e.rounding = 8; break;
    case element_kind_t::icon: e.text = "Icon"; e.w = 20, e.h = 20, e.icon = 22; break;
    case element_kind_t::instance:
        e.text = "Component";
        if ( !d.components.empty( ) )
        {
            e.component = d.components.front( ).uid;
            e.text = d.components.front( ).name;
            e.w = d.components.front( ).w, e.h = d.components.front( ).h;
        }
        break;
    case element_kind_t::custom: e.text = "Custom widget"; e.w = 160, e.h = 80, e.function = "draw_custom"; break;
    default: break;
    }
    e.id = identifier( e.text );
    return e;
}

auto doc::find_component( document_t& d, int uid ) -> component_t*
{
    for ( auto& c : d.components )
        if ( c.uid == uid )
            return &c;
    return nullptr;
}

auto doc::find_image( document_t& d, int uid ) -> image_t*
{
    for ( auto& i : d.images )
        if ( i.uid == uid )
            return &i;
    return nullptr;
}

auto doc::flatten( document_t& d, std::vector<element_t>& elements ) -> std::vector<flat_t>
{
    std::vector<flat_t> out;
    for ( auto& e : elements )
    {
        if ( e.kind != element_kind_t::instance )
        {
            out.push_back( { &e, 0, 0, 0, static_cast<unsigned long long>( e.uid ), nullptr } );
            continue;
        }
        auto* c = find_component( d, e.component );
        if ( !c )
            continue;
        e.w = c->w, e.h = c->h; // instances are their component's size
        for ( auto& inner : c->elements )
            if ( inner.kind != element_kind_t::instance ) // components don't nest
                out.push_back( { &inner, e.x, e.y, e.uid,
                                 ( static_cast<unsigned long long>( e.uid ) << 32 ) | static_cast<unsigned long long>( inner.uid ), &e } );
    }
    return out;
}

auto doc::base64( const std::string& bytes ) -> std::string
{
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve( ( bytes.size( ) + 2 ) / 3 * 4 );
    for ( size_t i = 0; i < bytes.size( ); i += 3 )
    {
        const unsigned v = static_cast<unsigned char>( bytes[i] ) << 16 |
                           ( i + 1 < bytes.size( ) ? static_cast<unsigned char>( bytes[i + 1] ) << 8 : 0u ) |
                           ( i + 2 < bytes.size( ) ? static_cast<unsigned char>( bytes[i + 2] ) : 0u );
        out += table[( v >> 18 ) & 63];
        out += table[( v >> 12 ) & 63];
        out += i + 1 < bytes.size( ) ? table[( v >> 6 ) & 63] : '=';
        out += i + 2 < bytes.size( ) ? table[v & 63] : '=';
    }
    return out;
}

auto doc::unbase64( const std::string& text ) -> std::string
{
    std::string out;
    unsigned v = 0;
    int bits = -8;
    for ( const char c : text )
    {
        int d = -1;
        if ( c >= 'A' && c <= 'Z' ) d = c - 'A';
        else if ( c >= 'a' && c <= 'z' ) d = c - 'a' + 26;
        else if ( c >= '0' && c <= '9' ) d = c - '0' + 52;
        else if ( c == '+' ) d = 62;
        else if ( c == '/' ) d = 63;
        if ( d < 0 )
            continue;
        v = ( v << 6 ) | static_cast<unsigned>( d );
        bits += 6;
        if ( bits >= 0 )
        {
            out += static_cast<char>( ( v >> bits ) & 0xFF );
            bits -= 8;
        }
    }
    return out;
}

auto doc::make_screen( document_t& d, const std::string& name ) -> screen_t
{
    screen_t s;
    s.uid = d.next_uid++;
    s.name = name;
    s.id = identifier( name );
    return s;
}

auto doc::identifier( const std::string& text ) -> std::string
{
    std::string out;
    bool gap = false;
    for ( const char c : text )
    {
        if ( std::isalnum( static_cast<unsigned char>( c ) ) )
        {
            if ( gap && !out.empty( ) )
                out += '_';
            out += static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
            gap = false;
        }
        else
            gap = true;
    }
    if ( out.empty( ) )
        out = "value";
    if ( std::isdigit( static_cast<unsigned char>( out[0] ) ) )
        out = "_" + out;
    return out;
}

auto doc::starter( ) -> document_t
{
    document_t d;
    d.name = "Example menu";
    builder_t b{ d };
    const auto feature = [&]( const char* name, const char* description, bool key ) {
        feature_t f;
        f.uid = d.next_uid++;
        f.name = name;
        f.description = description;
        f.id = identifier( name );
        f.has_key = key;
        f.key = key ? 0x02 : 0;
        return f;
    };
    const auto section = [&]( const char* title, int column ) {
        section_t s;
        s.uid = d.next_uid++;
        s.title = title;
        s.column = column;
        return s;
    };

    // Combat
    auto aimbot = feature( "Aimbot", "Pulls your aim onto the nearest target.", true );
    aimbot.enabled = true;
    {
        auto s = section( "Targeting", 0 );
        auto part = b.w( kind_t::select, "Hit part" );
        part.options = { "Head", "Torso", "Nearest" };
        s.widgets.push_back( part );
        auto distance = b.w( kind_t::slider, "Max distance" );
        distance.number = 600, distance.max = 2000, distance.unit = " st";
        s.widgets.push_back( distance );
        auto sticky = b.w( kind_t::toggle, "Sticky aim" );
        sticky.hint = "Keep the lock outside the FOV";
        sticky.on = true;
        s.widgets.push_back( sticky );
        aimbot.sections.push_back( s );
        auto checks = section( "Checks", 0 );
        auto skip = b.w( kind_t::chips, "Skip" );
        skip.options = { "Team", "Dead", "Hidden" };
        skip.chip_values = { true, true, false };
        checks.widgets.push_back( skip );
        aimbot.sections.push_back( checks );
        auto fov = section( "Field of view", 1 );
        auto circle = b.w( kind_t::color, "Show circle" );
        fov.widgets.push_back( circle );
        auto radius = b.w( kind_t::slider, "Radius" );
        radius.number = 180, radius.min = 10, radius.max = 600, radius.unit = " px";
        fov.widgets.push_back( radius );
        aimbot.sections.push_back( fov );
        auto smooth = section( "Smoothing", 1 );
        auto amount = b.w( kind_t::slider, "Smoothness" );
        amount.number = 3, amount.min = 1, amount.max = 10, amount.format = "%.1f";
        smooth.widgets.push_back( amount );
        aimbot.sections.push_back( smooth );
    }
    auto trigger = feature( "Trigger Bot", "Fires when your crosshair is on a target.", true );
    {
        auto s = section( "Firing", 0 );
        auto delay = b.w( kind_t::slider, "Delay" );
        delay.number = 40, delay.max = 500, delay.unit = " ms";
        s.widgets.push_back( delay );
        trigger.sections.push_back( s );
    }

    // Visuals
    auto esp = feature( "ESP", "Boxes, health and names over players.", false );
    esp.enabled = true;
    {
        auto box = section( "Box", 0 );
        box.widgets.push_back( b.w( kind_t::color, "Box" ) );
        auto style = b.w( kind_t::segment, "Style" );
        style.options = { "Full", "Corner" };
        box.widgets.push_back( style );
        auto thickness = b.w( kind_t::slider, "Thickness" );
        thickness.number = 1.5f, thickness.min = 1, thickness.max = 4, thickness.format = "%.1f";
        box.widgets.push_back( thickness );
        esp.sections.push_back( box );
        auto labels = section( "Labels", 1 );
        auto show = b.w( kind_t::chips, "Show" );
        show.options = { "Name", "Distance", "Weapon" };
        show.chip_values = { true, true, false };
        labels.widgets.push_back( show );
        auto note = b.w( kind_t::note, "" );
        note.id = "labels_note";
        note.text = "Labels draw above and below each box.";
        labels.widgets.push_back( note );
        esp.sections.push_back( labels );
    }

    // Settings
    auto menu = feature( "Menu", "How the menu opens.", false );
    menu.has_switch = false;
    {
        auto s = section( "Menu", 0 );
        auto key = b.w( kind_t::keybind, "Open menu" );
        key.key = 0x2D;
        key.has_mode = false;
        s.widgets.push_back( key );
        auto status = b.w( kind_t::value, "Version" );
        status.text = "1.0";
        s.widgets.push_back( status );
        menu.sections.push_back( s );
    }

    const auto group = [&]( const char* title, std::vector<feature_t> list ) {
        group_t g;
        g.uid = d.next_uid++;
        g.title = title;
        g.features = std::move( list );
        return g;
    };
    const auto category = [&]( const char* name, std::vector<group_t> list ) {
        category_t c;
        c.uid = d.next_uid++;
        c.name = name;
        c.groups = std::move( list );
        return c;
    };
    d.categories.push_back( category( "Combat", { group( "Aim", { aimbot, trigger } ) } ) );
    d.categories.push_back( category( "Visuals", { group( "Players", { esp } ) } ) );
    d.categories.push_back( category( "Settings", { group( "General", { menu } ) } ) );
    return d;
}

auto doc::blank_menu( ) -> document_t
{
    document_t d;
    d.name = "Untitled menu";
    category_t c;
    c.uid = d.next_uid++;
    c.name = "General";
    d.categories.push_back( c );
    return d;
}

auto doc::blank_app( ) -> document_t
{
    document_t d;
    d.name = "Untitled app";
    d.project = project_t::app;
    d.theme.namespace_name = "app";
    d.theme.width = 480, d.theme.height = 320;
    d.screens.push_back( make_screen( d, "Main" ) );
    return d;
}

// Sign in, pick a product, then a loading screen; 400 x 500 with a 32 px margin.
auto doc::loader( ) -> document_t
{
    document_t d;
    d.name = "Loader";
    d.project = project_t::app;
    d.theme.namespace_name = "loader";
    d.theme.width = 400, d.theme.height = 500;
    d.theme.window_alpha = .92f;
    d.theme.transition = 1;

    const auto put = [&]( screen_t& s, element_kind_t kind, const char* text, float x, float y, float w, float h ) -> element_t& {
        auto e = make_element( d, kind );
        e.text = text;
        e.id = identifier( text );
        e.x = x, e.y = y, e.w = w, e.h = h;
        s.elements.push_back( std::move( e ) );
        return s.elements.back( );
    };
    const auto header = [&]( screen_t& s ) {
        auto& name = put( s, element_kind_t::text, "base", 32, 22, 160, 20 );
        name.size = 15.f, name.role = 0, name.id = "brand";
        auto& version = put( s, element_kind_t::text, "v1.0", 208, 22, 160, 20 );
        version.size = 12.f, version.align = 2, version.role = 2, version.id = "version";
        put( s, element_kind_t::divider, "Divider", 0, 58, 400, 9 ).id = "rule";
    };

    auto sign_in = make_screen( d, "Sign in" );
    auto products = make_screen( d, "Products" );
    auto loading = make_screen( d, "Loading" );

    header( sign_in );
    {
        auto& title = put( sign_in, element_kind_t::text, "Welcome back", 32, 100, 336, 30 );
        title.size = 22.f, title.role = 0;
        auto& subtitle = put( sign_in, element_kind_t::text, "Sign in with your license to continue.", 32, 134, 336, 18 );
        subtitle.role = 2, subtitle.id = "subtitle";
        put( sign_in, element_kind_t::input, "Username", 32, 180, 336, 40 );
        put( sign_in, element_kind_t::input, "Password", 32, 232, 336, 40 ).password = true;
        put( sign_in, element_kind_t::checkbox, "Remember me", 32, 288, 200, 22 ).on = true;
        auto& go = put( sign_in, element_kind_t::button, "Sign in", 32, 332, 336, 42 );
        go.primary = true, go.target = products.uid, go.icon = 15, go.toast = "Signed in";
        auto& help = put( sign_in, element_kind_t::text, "Need a key? Ask in the Discord.", 32, 446, 336, 16 );
        help.size = 12.f, help.align = 1, help.role = 2, help.id = "help";
    }

    header( products );
    {
        auto& title = put( products, element_kind_t::text, "Your products", 32, 88, 336, 24 );
        title.size = 18.f, title.role = 0;
        auto& list = put( products, element_kind_t::list, "Products", 32, 124, 336, 152 );
        list.options = { "Roblox External", "Roblox Internal", "Spoofer" };
        list.details = { "29 days left", "Expired", "Lifetime" };
        auto& channel = put( products, element_kind_t::text, "Channel", 32, 298, 160, 20 );
        channel.role = 1, channel.id = "channel_label";
        auto& pick = put( products, element_kind_t::dropdown, "Channel", 220, 292, 148, 32 );
        pick.options = { "Stable", "Beta" };
        auto& launch = put( products, element_kind_t::button, "Launch", 32, 388, 336, 42 );
        launch.primary = true, launch.target = loading.uid, launch.icon = 7;
        auto& out = put( products, element_kind_t::button, "Sign out", 32, 438, 336, 36 );
        out.target = sign_in.uid, out.icon = 29;
    }

    header( loading );
    {
        put( loading, element_kind_t::spinner, "Spinner", 184, 168, 32, 32 );
        auto& title = put( loading, element_kind_t::text, "Injecting", 32, 220, 336, 24 );
        title.size = 18.f, title.align = 1, title.role = 0, title.id = "status", title.dynamic = true;
        auto& detail = put( loading, element_kind_t::text, "Waiting for the game window", 32, 250, 336, 18 );
        detail.align = 1, detail.role = 2, detail.id = "detail", detail.dynamic = true;
        auto& bar = put( loading, element_kind_t::progress, "Progress", 72, 292, 256, 6 );
        bar.number = .45f;
        put( loading, element_kind_t::button, "Cancel", 140, 430, 120, 36 ).target = products.uid;
    }

    d.screens = { std::move( sign_in ), std::move( products ), std::move( loading ) };
    return d;
}

namespace
{
    using doc::document_t;
    using doc::element_t;
    using doc::widget_t;

    auto write_widget( const widget_t& w ) -> json::value_t
    {
        auto wo = json::value_t::object( );
        wo["kind"] = kind_names[static_cast<int>( w.kind )];
        wo["label"] = w.label;
        wo["hint"] = w.hint;
        wo["id"] = w.id;
        wo["on"] = w.on;
        wo["switch"] = w.has_switch;
        wo["number"] = w.number;
        wo["min"] = w.min;
        wo["max"] = w.max;
        wo["choice"] = w.choice;
        wo["format"] = w.format;
        wo["unit"] = w.unit;
        wo["options"] = strings( w.options );
        auto chips = json::value_t::array( );
        for ( const bool v : w.chip_values )
            chips.push( v );
        wo["chips"] = chips;
        wo["rgba"] = color( w.rgba );
        wo["key"] = w.key;
        wo["mode"] = w.mode;
        wo["modes"] = w.has_mode;
        wo["text"] = w.text;
        return wo;
    }

    auto read_widget( const json::value_t& wo, document_t& d ) -> widget_t
    {
        widget_t w;
        w.uid = d.next_uid++;
        w.kind = kind_from( field( wo, "kind", std::string( "toggle" ) ) );
        w.label = field( wo, "label", w.label );
        w.hint = field( wo, "hint", w.hint );
        w.id = field( wo, "id", doc::identifier( w.label ) );
        w.on = field_b( wo, "on", w.on );
        w.has_switch = field_b( wo, "switch", w.has_switch );
        w.number = field( wo, "number", w.number );
        w.min = field( wo, "min", w.min );
        w.max = field( wo, "max", w.max );
        w.choice = field_i( wo, "choice", w.choice );
        w.format = field( wo, "format", w.format );
        w.unit = field( wo, "unit", w.unit );
        w.options = read_strings( wo.get( "options" ) );
        if ( const auto* chips = wo.get( "chips" ) )
            for ( const auto& v : chips->items )
                w.chip_values.push_back( v.as_bool( ) );
        w.chip_values.resize( w.options.size( ), false );
        read_color( wo.get( "rgba" ), w.rgba );
        w.key = field_i( wo, "key", w.key );
        w.mode = field_i( wo, "mode", w.mode );
        w.has_mode = field_b( wo, "modes", w.has_mode );
        w.text = field( wo, "text", w.text );
        return w;
    }

    auto write_element( const element_t& e ) -> json::value_t
    {
        auto eo = json::value_t::object( );
        eo["uid"] = e.uid;
        eo["kind"] = element_keys[static_cast<int>( e.kind )];
        eo["text"] = e.text;
        eo["id"] = e.id;
        auto box = json::value_t::array( );
        for ( const float v : { e.x, e.y, e.w, e.h } )
            box.push( v );
        eo["rect"] = box;
        eo["size"] = e.size;
        eo["align"] = e.align;
        eo["role"] = e.role;
        eo["primary"] = e.primary;
        eo["target"] = e.target;
        eo["password"] = e.password;
        eo["value"] = e.value;
        eo["on"] = e.on;
        eo["number"] = e.number;
        eo["min"] = e.min;
        eo["max"] = e.max;
        eo["endless"] = e.endless;
        eo["format"] = e.format;
        eo["choice"] = e.choice;
        eo["options"] = strings( e.options );
        eo["details"] = strings( e.details );
        eo["icon"] = e.icon;
        eo["pin_x"] = e.pin_x;
        eo["pin_y"] = e.pin_y;
        eo["dynamic"] = e.dynamic;
        eo["hideable"] = e.hideable;
        eo["shown"] = e.shown;
        eo["lockable"] = e.lockable;
        eo["enabled"] = e.enabled;
        eo["tab_owner"] = e.tab_owner;
        eo["tab_index"] = e.tab_index;
        eo["tint"] = e.tint;
        eo["rgba"] = color( e.rgba );
        eo["toast"] = e.toast;
        eo["image"] = e.image;
        eo["rounding"] = e.rounding;
        eo["component"] = e.component;
        eo["function"] = e.function;
        return eo;
    }

    // `taken` holds the uids already in use; a stored uid is kept unless it clashes.
    auto read_element( const json::value_t& eo, document_t& d, std::set<int>* taken ) -> element_t
    {
        element_t e;
        const int stored = field_i( eo, "uid", 0 );
        if ( taken && stored > 0 && !taken->count( stored ) )
            e.uid = stored;
        else
            e.uid = d.next_uid++;
        if ( taken )
            taken->insert( e.uid );
        e.kind = element_from( field( eo, "kind", std::string( "text" ) ) );
        e.text = field( eo, "text", e.text );
        e.id = field( eo, "id", doc::identifier( e.text ) );
        if ( const auto* box = eo.get( "rect" ); box && box->items.size( ) == 4 )
        {
            e.x = box->items[0].as_float( e.x );
            e.y = box->items[1].as_float( e.y );
            e.w = box->items[2].as_float( e.w );
            e.h = box->items[3].as_float( e.h );
        }
        e.size = field( eo, "size", e.size );
        e.align = field_i( eo, "align", e.align );
        e.role = field_i( eo, "role", e.role );
        e.primary = field_b( eo, "primary", e.primary );
        e.target = field_i( eo, "target", e.target );
        e.password = field_b( eo, "password", e.password );
        e.value = field( eo, "value", e.value );
        e.on = field_b( eo, "on", e.on );
        e.number = field( eo, "number", e.number );
        e.min = field( eo, "min", e.min );
        e.max = field( eo, "max", e.max );
        e.endless = field_b( eo, "endless", e.endless );
        e.format = field( eo, "format", e.format );
        e.choice = field_i( eo, "choice", e.choice );
        e.options = read_strings( eo.get( "options" ) );
        e.details = read_strings( eo.get( "details" ) );
        e.icon = field_i( eo, "icon", e.icon );
        e.pin_x = field_i( eo, "pin_x", e.pin_x );
        e.pin_y = field_i( eo, "pin_y", e.pin_y );
        e.dynamic = field_b( eo, "dynamic", e.dynamic );
        e.hideable = field_b( eo, "hideable", e.hideable );
        e.shown = field_b( eo, "shown", e.shown );
        e.lockable = field_b( eo, "lockable", e.lockable );
        e.enabled = field_b( eo, "enabled", e.enabled );
        e.tab_owner = field_i( eo, "tab_owner", e.tab_owner );
        e.tab_index = field_i( eo, "tab_index", e.tab_index );
        e.tint = field_b( eo, "tint", e.tint );
        read_color( eo.get( "rgba" ), e.rgba );
        e.toast = field( eo, "toast", e.toast );
        e.image = field_i( eo, "image", e.image );
        e.rounding = field( eo, "rounding", e.rounding );
        e.component = field_i( eo, "component", e.component );
        e.function = field( eo, "function", e.function );
        return e;
    }

    // Raises next_uid past every stored uid, so new nodes never reuse one.
    void reserve_uids( const json::value_t& node, document_t& d )
    {
        if ( node.kind == json::value_t::kind_t::object )
        {
            if ( const auto* uid = node.get( "uid" ) )
                d.next_uid = std::max( d.next_uid, uid->as_int( 0 ) + 1 );
            for ( const auto& f : node.fields )
                reserve_uids( f.second, d );
        }
        else if ( node.kind == json::value_t::kind_t::array )
            for ( const auto& item : node.items )
                reserve_uids( item, d );
    }

    constexpr const char* clip_marker = "imgui-studio/clip";

    auto clip_items( const std::string& text, const char* key ) -> const json::value_t*
    {
        static json::value_t root;
        root = { };
        if ( text.find( clip_marker ) == std::string::npos || !json::parse( text, root ) || root.kind != json::value_t::kind_t::object )
            return nullptr;
        if ( field( root, "format", std::string( ) ) != clip_marker )
            return nullptr;
        return root.get( key );
    }
}

auto doc::to_json( const document_t& d ) -> std::string
{
    auto root = json::value_t::object( );
    root["format"] = "imgui-studio/3";
    root["name"] = d.name;
    root["project"] = d.project == project_t::app ? "app" : "menu";
    auto& theme = root["theme"];
    theme = json::value_t::object( );
    const auto& t = d.theme;
    theme["title"] = t.title;
    theme["footer"] = t.footer;
    theme["namespace"] = t.namespace_name;
    theme["accent"] = color( t.accent );
    theme["window_alpha"] = t.window_alpha;
    theme["rounding"] = t.rounding;
    theme["width"] = t.width;
    theme["height"] = t.height;
    theme["list_width"] = t.list_width;
    theme["columns"] = t.columns;
    theme["palette"] = t.palette;
    theme["toggles"] = t.toggles;
    theme["sliders"] = t.sliders;
    theme["buttons"] = t.buttons;
    theme["fields"] = t.fields;
    theme["control_rounding"] = t.control_rounding;
    theme["resizable"] = t.resizable;
    theme["transition"] = t.transition;
    theme["custom_window"] = color( t.custom_window );
    theme["custom_text"] = color( t.custom_text );
    theme["custom_muted"] = color( t.custom_muted );
    theme["custom_light"] = t.custom_light;
    theme["plugin"] = t.plugin;

    auto screens = json::value_t::array( );
    for ( const auto& s : d.screens )
    {
        auto so = json::value_t::object( );
        so["uid"] = s.uid;
        so["name"] = s.name;
        so["id"] = s.id;
        auto elements = json::value_t::array( );
        for ( const auto& e : s.elements )
            elements.push( write_element( e ) );
        so["elements"] = elements;
        screens.push( so );
    }
    root["screens"] = screens;

    auto components = json::value_t::array( );
    for ( const auto& c : d.components )
    {
        auto co = json::value_t::object( );
        co["uid"] = c.uid;
        co["name"] = c.name;
        co["id"] = c.id;
        co["w"] = c.w;
        co["h"] = c.h;
        auto elements = json::value_t::array( );
        for ( const auto& e : c.elements )
            elements.push( write_element( e ) );
        co["elements"] = elements;
        components.push( co );
    }
    root["components"] = components;

    auto images = json::value_t::array( );
    for ( const auto& i : d.images )
    {
        auto io = json::value_t::object( );
        io["uid"] = i.uid;
        io["name"] = i.name;
        io["id"] = i.id;
        io["width"] = i.width;
        io["height"] = i.height;
        io["data"] = base64( i.data );
        images.push( io );
    }
    root["images"] = images;

    auto categories = json::value_t::array( );
    for ( const auto& c : d.categories )
    {
        auto co = json::value_t::object( );
        co["name"] = c.name;
        auto groups = json::value_t::array( );
        for ( const auto& g : c.groups )
        {
            auto go = json::value_t::object( );
            go["title"] = g.title;
            auto features = json::value_t::array( );
            for ( const auto& f : g.features )
            {
                auto fo = json::value_t::object( );
                fo["name"] = f.name;
                fo["description"] = f.description;
                fo["id"] = f.id;
                fo["switch"] = f.has_switch;
                fo["enabled"] = f.enabled;
                fo["keyed"] = f.has_key;
                fo["key"] = f.key;
                fo["mode"] = f.mode;
                fo["meta"] = f.meta;
                auto sections = json::value_t::array( );
                for ( const auto& s : f.sections )
                {
                    auto so = json::value_t::object( );
                    so["title"] = s.title;
                    so["column"] = s.column;
                    auto widgets = json::value_t::array( );
                    for ( const auto& w : s.widgets )
                        widgets.push( write_widget( w ) );
                    so["widgets"] = widgets;
                    sections.push( so );
                }
                fo["sections"] = sections;
                features.push( fo );
            }
            go["features"] = features;
            groups.push( go );
        }
        co["groups"] = groups;
        categories.push( co );
    }
    root["categories"] = categories;
    return json::write( root );
}

auto doc::from_json( const std::string& text, document_t& out, std::string* error ) -> bool
{
    json::value_t root;
    if ( !json::parse( text, root, error ) )
        return false;
    if ( root.kind != json::value_t::kind_t::object )
    {
        if ( error )
            *error = "not a project file";
        return false;
    }
    document_t d;
    d.name = field( root, "name", d.name );
    if ( const auto* theme = root.get( "theme" ) )
    {
        auto& t = d.theme;
        t.title = field( *theme, "title", t.title );
        t.footer = field( *theme, "footer", t.footer );
        t.namespace_name = field( *theme, "namespace", t.namespace_name );
        read_color( theme->get( "accent" ), t.accent );
        t.window_alpha = field( *theme, "window_alpha", t.window_alpha );
        t.rounding = field( *theme, "rounding", t.rounding );
        t.width = field( *theme, "width", t.width );
        t.height = field( *theme, "height", t.height );
        t.list_width = field( *theme, "list_width", t.list_width );
        t.columns = field_i( *theme, "columns", t.columns );
        t.palette = field_i( *theme, "palette", t.palette );
        t.toggles = field_i( *theme, "toggles", t.toggles );
        t.sliders = field_i( *theme, "sliders", t.sliders );
        t.buttons = field_i( *theme, "buttons", t.buttons );
        t.fields = field_i( *theme, "fields", t.fields );
        t.control_rounding = field( *theme, "control_rounding", t.control_rounding );
        t.resizable = field_b( *theme, "resizable", t.resizable );
        t.transition = field_i( *theme, "transition", t.transition );
        read_color( theme->get( "custom_window" ), t.custom_window );
        read_color( theme->get( "custom_text" ), t.custom_text );
        read_color( theme->get( "custom_muted" ), t.custom_muted );
        t.custom_light = field_b( *theme, "custom_light", t.custom_light );
        t.plugin = field( *theme, "plugin", t.plugin );
    }
    d.project = field( root, "project", std::string( "menu" ) ) == "app" ? project_t::app : project_t::menu;

    // Screens, components, images and elements keep their uids (buttons, tabs and instances refer to them).
    reserve_uids( root, d );
    std::set<int> taken;
    const auto keep = [&]( const json::value_t& o ) {
        int uid = field_i( o, "uid", 0 );
        if ( uid <= 0 || taken.count( uid ) )
            uid = d.next_uid++;
        taken.insert( uid );
        return uid;
    };
    if ( const auto* screens = root.get( "screens" ) )
        for ( const auto& so : screens->items )
        {
            screen_t s;
            s.uid = keep( so );
            s.name = field( so, "name", s.name );
            s.id = field( so, "id", identifier( s.name ) );
            if ( const auto* elements = so.get( "elements" ) )
                for ( const auto& eo : elements->items )
                    s.elements.push_back( read_element( eo, d, &taken ) );
            d.screens.push_back( std::move( s ) );
        }
    if ( const auto* components = root.get( "components" ) )
        for ( const auto& co : components->items )
        {
            component_t c;
            c.uid = keep( co );
            c.name = field( co, "name", c.name );
            c.id = field( co, "id", identifier( c.name ) );
            c.w = field( co, "w", c.w );
            c.h = field( co, "h", c.h );
            if ( const auto* elements = co.get( "elements" ) )
                for ( const auto& eo : elements->items )
                    c.elements.push_back( read_element( eo, d, &taken ) );
            d.components.push_back( std::move( c ) );
        }
    if ( const auto* images = root.get( "images" ) )
        for ( const auto& io : images->items )
        {
            image_t i;
            i.uid = keep( io );
            i.name = field( io, "name", std::string( "image" ) );
            i.id = field( io, "id", identifier( i.name ) );
            i.width = field_i( io, "width", 0 );
            i.height = field_i( io, "height", 0 );
            i.data = unbase64( field( io, "data", std::string( ) ) );
            d.images.push_back( std::move( i ) );
        }

    if ( const auto* categories = root.get( "categories" ) )
        for ( const auto& co : categories->items )
        {
            category_t c;
            c.uid = d.next_uid++;
            c.name = field( co, "name", c.name );
            if ( const auto* groups = co.get( "groups" ) )
                for ( const auto& go : groups->items )
                {
                    group_t g;
                    g.uid = d.next_uid++;
                    g.title = field( go, "title", g.title );
                    if ( const auto* features = go.get( "features" ) )
                        for ( const auto& fo : features->items )
                        {
                            feature_t f;
                            f.uid = d.next_uid++;
                            f.name = field( fo, "name", f.name );
                            f.description = field( fo, "description", f.description );
                            f.id = field( fo, "id", identifier( f.name ) );
                            f.has_switch = field_b( fo, "switch", f.has_switch );
                            f.enabled = field_b( fo, "enabled", f.enabled );
                            f.has_key = field_b( fo, "keyed", f.has_key );
                            f.key = field_i( fo, "key", f.key );
                            f.mode = field_i( fo, "mode", f.mode );
                            f.meta = field( fo, "meta", f.meta );
                            if ( const auto* sections = fo.get( "sections" ) )
                                for ( const auto& so : sections->items )
                                {
                                    section_t s;
                                    s.uid = d.next_uid++;
                                    s.title = field( so, "title", s.title );
                                    s.column = field_i( so, "column", s.column );
                                    if ( const auto* widgets = so.get( "widgets" ) )
                                        for ( const auto& wo : widgets->items )
                                            s.widgets.push_back( read_widget( wo, d ) );
                                    f.sections.push_back( std::move( s ) );
                                }
                            g.features.push_back( std::move( f ) );
                        }
                    c.groups.push_back( std::move( g ) );
                }
            d.categories.push_back( std::move( c ) );
        }
    out = std::move( d );
    return true;
}

auto doc::clip_elements( const std::vector<element_t>& elements ) -> std::string
{
    auto root = json::value_t::object( );
    root["format"] = clip_marker;
    auto list = json::value_t::array( );
    for ( const auto& e : elements )
        list.push( write_element( e ) );
    root["elements"] = list;
    return json::write( root );
}

auto doc::clip_widgets( const std::vector<widget_t>& widgets ) -> std::string
{
    auto root = json::value_t::object( );
    root["format"] = clip_marker;
    auto list = json::value_t::array( );
    for ( const auto& w : widgets )
        list.push( write_widget( w ) );
    root["widgets"] = list;
    return json::write( root );
}

// Pasted elements get new uids; tab links between pasted elements follow, links to anything else are dropped.
auto doc::paste_elements( const std::string& text, document_t& d, std::vector<element_t>& out ) -> bool
{
    const auto* list = clip_items( text, "elements" );
    if ( !list )
        return false;
    std::map<int, int> renamed;
    for ( const auto& eo : list->items )
    {
        auto e = read_element( eo, d, nullptr );
        renamed[field_i( eo, "uid", 0 )] = e.uid;
        out.push_back( std::move( e ) );
    }
    for ( auto& e : out )
        e.tab_owner = renamed.count( e.tab_owner ) ? renamed[e.tab_owner] : 0;
    return !out.empty( );
}

auto doc::paste_widgets( const std::string& text, document_t& d, std::vector<widget_t>& out ) -> bool
{
    const auto* list = clip_items( text, "widgets" );
    if ( !list )
        return false;
    for ( const auto& wo : list->items )
        out.push_back( read_widget( wo, d ) );
    return !out.empty( );
}
