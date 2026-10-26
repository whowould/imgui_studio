#include "preview.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>

namespace
{
    std::vector<preview::hit_t>* recording = nullptr;

    void record( int tag, const ImRect& rect )
    {
        if ( recording )
            recording->push_back( { tag, rect } );
    }

    void draw_widget( doc::widget_t& w )
    {
        kit::tag = w.uid;
        ImGui::PushID( w.uid );
        const char* hint = w.hint.empty( ) ? nullptr : w.hint.c_str( );
        std::vector<const char*> options;
        for ( const auto& o : w.options )
            options.push_back( o.c_str( ) );
        switch ( w.kind )
        {
        case doc::kind_t::toggle: kit::toggle( w.label.c_str( ), w.on, hint ); break;
        case doc::kind_t::slider:
            kit::slider( w.label.c_str( ), w.number, w.min, w.max, w.format.c_str( ), w.unit.c_str( ), hint );
            break;
        case doc::kind_t::slider_int:
        {
            int v = static_cast<int>( w.number );
            kit::slider( w.label.c_str( ), v, static_cast<int>( w.min ), static_cast<int>( w.max ), w.unit.c_str( ), hint );
            w.number = static_cast<float>( v );
            break;
        }
        case doc::kind_t::select: kit::select( w.label.c_str( ), w.choice, options, hint ); break;
        case doc::kind_t::segment: kit::segment( w.label.c_str( ), w.choice, options ); break;
        case doc::kind_t::chips:
        {
            w.chip_values.resize( w.options.size( ), false );
            std::unique_ptr<bool[]> values( new bool[std::max<size_t>( 1, w.options.size( ) )] );
            std::vector<kit::chip_t> chips;
            for ( size_t i = 0; i < w.options.size( ); ++i )
            {
                values[i] = w.chip_values[i];
                chips.push_back( { w.options[i].c_str( ), &values[i] } );
            }
            kit::chips( w.label.c_str( ), std::span<const kit::chip_t>( chips ), hint );
            for ( size_t i = 0; i < w.options.size( ); ++i )
                w.chip_values[i] = values[i];
            break;
        }
        case doc::kind_t::color: kit::color( w.label.c_str( ), w.rgba, w.has_switch ? &w.on : nullptr, hint ); break;
        case doc::kind_t::keybind:
            kit::keybind( w.label.c_str( ), w.key, w.has_mode ? &w.mode : nullptr, w.has_switch ? &w.on : nullptr, hint );
            break;
        case doc::kind_t::button: kit::button( w.label.c_str( ), w.text.c_str( ) ); break;
        case doc::kind_t::value: kit::value( w.label.c_str( ), w.text.c_str( ) ); break;
        case doc::kind_t::note: kit::note( w.text.c_str( ) ); break;
        default: break;
        }
        ImGui::PopID( );
        kit::tag = -1;
    }

    void draw_feature( doc::feature_t& f, int columns )
    {
        kit::begin_columns( columns );
        for ( int column = 0; column < columns; ++column )
        {
            if ( column > 0 )
                kit::next_column( );
            for ( auto& s : f.sections )
            {
                if ( std::clamp( s.column, 0, columns - 1 ) != column )
                    continue;
                kit::tag = s.uid;
                kit::section( s.title.c_str( ) );
                kit::tag = -1;
                for ( auto& w : s.widgets )
                    draw_widget( w );
            }
        }
        kit::end_columns( );
    }

    auto rgba_u32( const float* c ) -> ImU32
    {
        return ImGui::ColorConvertFloat4ToU32( { c[0], c[1], c[2], c[3] } );
    }

    auto names( const std::vector<std::string>& list ) -> std::vector<const char*>
    {
        std::vector<const char*> out;
        for ( const auto& s : list )
            out.push_back( s.c_str( ) );
        return out;
    }

    // Draws one element; true when it was a button that was pressed.
    auto draw_element( doc::document_t& d, doc::element_t& e, const ImRect& r ) -> bool
    {
        const ImU32 tint = e.tint ? rgba_u32( e.rgba ) : 0;
        switch ( e.kind )
        {
        case doc::element_kind_t::text:
            kit::el::text( e.text.c_str( ), r,
                           { e.size, static_cast<kit::el::align_t>( std::clamp( e.align, 0, 2 ) ),
                             static_cast<kit::el::role_t>( std::clamp( e.role, 0, 3 ) ), tint } );
            break;
        case doc::element_kind_t::button:
            return kit::el::button( e.text.c_str( ), r, e.primary, static_cast<kit::icon_t>( std::clamp( e.icon, 0, static_cast<int>( kit::icon_t::count ) - 1 ) ),
                                    tint );
        case doc::element_kind_t::input:
        {
            char buffer[256];
            std::snprintf( buffer, sizeof( buffer ), "%s", e.value.c_str( ) );
            if ( kit::el::input( "##input", buffer, sizeof( buffer ), r, e.text.c_str( ), e.password ) )
                e.value = buffer;
            break;
        }
        case doc::element_kind_t::checkbox: kit::el::checkbox( e.text.c_str( ), e.on, r ); break;
        case doc::element_kind_t::slider: kit::el::slider( "##slider", e.number, e.min, e.max, r, e.format.c_str( ) ); break;
        case doc::element_kind_t::dropdown: kit::el::dropdown( "##dropdown", e.choice, names( e.options ), r ); break;
        case doc::element_kind_t::progress: kit::el::progress( e.endless ? -1.f : e.number, r ); break;
        case doc::element_kind_t::list: kit::el::list( "##list", e.choice, names( e.options ), r, names( e.details ) ); break;
        case doc::element_kind_t::panel: kit::el::panel( r, tint ); break;
        case doc::element_kind_t::divider: kit::el::divider( r ); break;
        case doc::element_kind_t::spinner: kit::el::spinner( r ); break;
        case doc::element_kind_t::tabs: kit::el::tabs( "##tabs", e.choice, names( e.options ), r ); break;
        case doc::element_kind_t::image:
        {
            const auto* image = doc::find_image( d, e.image );
            kit::el::image( image ? reinterpret_cast<const unsigned char*>( image->data.data( ) ) : nullptr, image ? image->data.size( ) : 0,
                            r, e.rounding );
            break;
        }
        case doc::element_kind_t::icon:
            kit::el::icon( static_cast<kit::icon_t>( std::clamp( e.icon, 0, static_cast<int>( kit::icon_t::count ) - 1 ) ), r, tint );
            break;
        case doc::element_kind_t::custom:
            if ( !preview::custom || !preview::custom( e.function, r ) )
            {
                // A placeholder until the function is available: a hatched box with its name.
                ImDrawList* draw = ImGui::GetWindowDrawList( );
                draw->AddRectFilled( r.Min, r.Max, kit::faded( kit::tone::overlay( 6 ) ), 6 );
                draw->PushClipRect( r.Min, r.Max, true );
                for ( float x = r.Min.x - r.GetHeight( ); x < r.Max.x; x += 10 )
                    draw->AddLine( { x, r.Max.y }, { x + r.GetHeight( ), r.Min.y }, kit::faded( kit::tone::overlay( 9 ) ) );
                draw->PopClipRect( );
                draw->AddRect( r.Min + ImVec2( .5f, .5f ), r.Max - ImVec2( .5f, .5f ), kit::faded( kit::tone::overlay( 30 ) ), 6 );
                const std::string name = ( e.function.empty( ) ? std::string( "draw_custom" ) : e.function ) + "( )";
                kit::text( draw, { std::floor( r.GetCenter( ).x - kit::text_width( name.c_str( ), 12 ) * .5f ), std::floor( r.GetCenter( ).y - 7 ) },
                           kit::tone::muted, name.c_str( ), 12 );
            }
            record( kit::tag, r );
            break;
        default: break;
        }
        return false;
    }

    // Draws a list of elements (a screen, or a component on its own); returns the pressed button's target.
    auto draw_elements( doc::document_t& d, std::vector<doc::element_t>& elements, const preview::app_options_t& options, bool pins ) -> int
    {
        const auto flats = doc::flatten( d, elements );
        // Which tab every tabs element shows, for the elements that live on a tab.
        std::map<unsigned long long, int> tabs;
        for ( const auto& f : flats )
            if ( f.element->kind == doc::element_kind_t::tabs )
                tabs[f.key] = f.element->choice;
        int next = 0;
        int last_instance = 0;
        for ( const auto& f : flats )
        {
            auto& e = *f.element;
            if ( e.tab_owner )
            {
                const unsigned long long owner =
                    f.instance ? ( static_cast<unsigned long long>( f.instance ) << 32 ) | static_cast<unsigned long long>( e.tab_owner )
                               : static_cast<unsigned long long>( e.tab_owner );
                if ( tabs.count( owner ) && tabs[owner] != e.tab_index )
                    continue;
            }
            if ( f.owner && f.owner->tab_owner && tabs.count( static_cast<unsigned long long>( f.owner->tab_owner ) ) &&
                 tabs[static_cast<unsigned long long>( f.owner->tab_owner )] != f.owner->tab_index )
                continue;
            const bool hidden = ( e.hideable && !e.shown ) || ( f.owner && f.owner->hideable && !f.owner->shown );
            if ( hidden && !options.ghosts )
                continue;
            // An instance is picked as one piece: its whole rect, then its elements, all under its uid.
            if ( f.instance && f.instance != last_instance )
                for ( const auto& owner : elements )
                    if ( owner.uid == f.instance )
                        record( owner.uid, kit::el::at( owner.x, owner.y, owner.w, owner.h ) );
            last_instance = f.instance;
            kit::tag = f.instance ? f.instance : e.uid;
            const ImRect r = pins && !f.instance ? kit::el::at( e.x, e.y, e.w, e.h, static_cast<kit::el::pin_t>( std::clamp( e.pin_x, 0, 3 ) ),
                                                                static_cast<kit::el::pin_t>( std::clamp( e.pin_y, 0, 3 ) ) )
                                                 : kit::el::at( e.x + f.dx, e.y + f.dy, e.w, e.h );
            ImGui::PushID( static_cast<int>( f.key ) );
            ImGui::PushID( static_cast<int>( f.key >> 32 ) );
            if ( hidden )
                ImGui::PushStyleVar( ImGuiStyleVar_Alpha, ImGui::GetStyle( ).Alpha * .3f );
            const bool locked = ( e.lockable && !e.enabled ) || ( f.owner && f.owner->lockable && !f.owner->enabled );
            if ( locked )
                ImGui::BeginDisabled( );
            if ( draw_element( d, e, r ) && !options.inert )
            {
                if ( !e.toast.empty( ) )
                    kit::toast( e.toast.c_str( ) );
                if ( preview::pressed )
                    preview::pressed( f.key );
                if ( e.target != 0 )
                    next = e.target;
            }
            if ( locked )
                ImGui::EndDisabled( );
            if ( hidden )
                ImGui::PopStyleVar( );
            ImGui::PopID( );
            ImGui::PopID( );
        }
        kit::tag = -1;
        return next;
    }

    struct recording_t
    {
        explicit recording_t( std::vector<preview::hit_t>& hits )
        {
            hits.clear( );
            recording = &hits;
            kit::on_row = []( int tag, const ImRect& rect ) { record( tag, rect ); };
        }
        ~recording_t( )
        {
            kit::on_row = nullptr;
            recording = nullptr;
        }
    };

    struct inert_t
    {
        bool on;
        explicit inert_t( bool value ) : on( value )
        {
            if ( on )
            {
                ImGui::PushStyleVar( ImGuiStyleVar_DisabledAlpha, 1.f );
                ImGui::BeginDisabled( );
            }
        }
        ~inert_t( )
        {
            if ( on )
            {
                ImGui::EndDisabled( );
                ImGui::PopStyleVar( );
            }
        }
    };
}

// Palette 4 is built from three colours: the window, the text and the muted text; the rest is mixed from them.
auto preview::palette_of( const doc::theme_t& t ) -> kit::palette_t
{
    if ( t.palette != 4 )
        return kit::preset( static_cast<kit::palette_id>( std::clamp( t.palette, 0, 3 ) ) );
    const ImVec4 window( t.custom_window[0], t.custom_window[1], t.custom_window[2], 1 );
    const ImVec4 text( t.custom_text[0], t.custom_text[1], t.custom_text[2], 1 );
    const ImVec4 muted( t.custom_muted[0], t.custom_muted[1], t.custom_muted[2], 1 );
    const auto u32 = []( ImVec4 c, float a = 1.f ) { return ImGui::ColorConvertFloat4ToU32( { c.x, c.y, c.z, a } ); };
    kit::palette_t p;
    p.window = window;
    p.overlay = t.custom_light ? ImVec4( 0, 0, 0, 1 ) : ImVec4( 1, 1, 1, 1 );
    p.overlay_gain = t.custom_light ? 1.5f : 1.f;
    p.title = u32( ImLerp( text, t.custom_light ? ImVec4( 0, 0, 0, 1 ) : ImVec4( 1, 1, 1, 1 ), .3f ) );
    p.primary = u32( text );
    p.label = u32( ImLerp( text, muted, .35f ) );
    p.secondary = u32( ImLerp( text, muted, .75f ) );
    p.muted = u32( muted );
    p.knob_off = t.custom_light ? IM_COL32( 255, 255, 255, 255 ) : u32( ImLerp( text, muted, .2f ) );
    p.popup = u32( ImLerp( window, t.custom_light ? ImVec4( 1, 1, 1, 1 ) : ImVec4( 1, 1, 1, 1 ), t.custom_light ? .6f : .05f ), .98f );
    p.button = u32( text );
    p.ink = u32( window );
    return p;
}

void preview::apply_theme( const doc::document_t& d )
{
    const auto& t = d.theme;
    kit::theme.accent = { t.accent[0], t.accent[1], t.accent[2], 1.f };
    kit::theme.window_alpha = t.window_alpha;
    kit::theme.rounding = t.rounding;
    kit::theme.size = { t.width, t.height };
    kit::theme.list_width = t.list_width;
    kit::theme.columns = t.columns;
    kit::theme.title = t.title;
    kit::theme.footer = t.footer;
    kit::theme.control_rounding = t.control_rounding;
    kit::theme.toggles = static_cast<kit::toggle_t>( std::clamp( t.toggles, 0, 1 ) );
    kit::theme.sliders = static_cast<kit::slider_t>( std::clamp( t.sliders, 0, 1 ) );
    kit::theme.buttons = static_cast<kit::button_t>( std::clamp( t.buttons, 0, 2 ) );
    kit::theme.fields = static_cast<kit::field_t>( std::clamp( t.fields, 0, 1 ) );
    kit::theme.resizable = t.resizable;
    kit::theme.transition = t.transition;
    kit::use_palette( palette_of( t ) );
}

void preview::draw( doc::document_t& d, kit::shell_state_t& state, std::vector<hit_t>& hits )
{
    apply_theme( d );
    const recording_t rec( hits );

    std::vector<kit::category_t> categories;
    for ( auto& c : d.categories )
    {
        kit::category_t kc{ c.name, { } };
        for ( auto& g : c.groups )
        {
            kit::group_t kg{ g.title, { } };
            for ( auto& f : g.features )
            {
                kit::feature_t kf;
                kf.name = f.name;
                kf.description = f.description;
                kf.enabled = f.has_switch ? &f.enabled : nullptr;
                kf.key = f.has_key ? &f.key : nullptr;
                kf.mode = f.has_key ? &f.mode : nullptr;
                doc::feature_t* feature = &f;
                kf.meta = [feature] {
                    if ( !feature->meta.empty( ) )
                        return feature->meta;
                    return feature->has_key ? kit::keyed( feature->key, feature->mode ) : std::string( );
                };
                const int columns = d.theme.columns;
                kf.draw = [feature, columns] { draw_feature( *feature, columns ); };
                kg.features.push_back( std::move( kf ) );
            }
            kc.groups.push_back( std::move( kg ) );
        }
        categories.push_back( std::move( kc ) );
    }
    kit::shell( categories, state );
}

void preview::draw_app( doc::document_t& d, int& screen, const app_options_t& options, std::vector<hit_t>& hits )
{
    apply_theme( d );
    hits.clear( );
    if ( d.screens.empty( ) )
        return;
    auto found = std::find_if( d.screens.begin( ), d.screens.end( ), [&]( const doc::screen_t& s ) { return s.uid == screen; } );
    if ( found == d.screens.end( ) )
        found = d.screens.begin( );
    screen = found->uid;

    const recording_t rec( hits );
    kit::begin_surface( options.size );
    kit::screen( static_cast<int>( found - d.screens.begin( ) ) );
    ImGui::PushID( found->uid );
    int next = 0;
    {
        const inert_t inert( options.inert );
        next = draw_elements( d, found->elements, options, true );
    }
    ImGui::PopID( );
    kit::end_surface( );
    if ( next > 0 && !options.inert )
        screen = next;
}

void preview::draw_component( doc::document_t& d, int uid, const app_options_t& options, std::vector<hit_t>& hits )
{
    apply_theme( d );
    hits.clear( );
    auto* c = doc::find_component( d, uid );
    if ( !c )
        return;
    const recording_t rec( hits );
    // A component is designed at its own size, so nothing moves by its pins here.
    kit::theme.size = { c->w, c->h };
    kit::begin_surface( { c->w, c->h } );
    ImGui::PushID( c->uid );
    {
        const inert_t inert( options.inert );
        draw_elements( d, c->elements, options, false );
    }
    ImGui::PopID( );
    kit::end_surface( );
}

auto preview::shown_feature( const doc::document_t& d, const kit::shell_state_t& state ) -> int
{
    if ( state.category < 0 || state.category >= static_cast<int>( d.categories.size( ) ) )
        return 0;
    const int wanted = state.selected[static_cast<size_t>( state.category ) % std::size( state.selected )];
    int index = 0;
    for ( const auto& g : d.categories[static_cast<size_t>( state.category )].groups )
        for ( const auto& f : g.features )
        {
            if ( index == wanted )
                return f.uid;
            ++index;
        }
    return 0;
}

void preview::focus( const doc::document_t& d, kit::shell_state_t& state, int uid )
{
    for ( size_t c = 0; c < d.categories.size( ); ++c )
    {
        const auto& category = d.categories[c];
        if ( category.uid == uid )
        {
            state.category = static_cast<int>( c );
            return;
        }
        int index = 0;
        for ( const auto& g : category.groups )
        {
            bool group_hit = g.uid == uid;
            for ( const auto& f : g.features )
            {
                bool hit = group_hit || f.uid == uid;
                for ( const auto& s : f.sections )
                {
                    hit = hit || s.uid == uid;
                    for ( const auto& w : s.widgets )
                        hit = hit || w.uid == uid;
                }
                if ( hit )
                {
                    state.category = static_cast<int>( c );
                    state.selected[c % std::size( state.selected )] = index;
                    return;
                }
                group_hit = false;
                ++index;
            }
        }
    }
}
