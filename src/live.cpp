#include "live.h"
#include "document.h"
#include "names.h"
#include "preview.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>

namespace
{
    struct live_t
    {
        std::filesystem::path path;
        std::map<std::string, kit_live::binding_t> bindings;
        bool loaded = false;
        doc::document_t doc;
        names::app_t app;
        names::menu_t menu;
        std::filesystem::file_time_type stamp{ };
        double checked = -10;
        int screen = 0;
        kit::shell_state_t shell;
        std::vector<preview::hit_t> hits;
    } live;

    auto bound( const std::string& name, kit_live::kind_t kind ) -> void*
    {
        const auto it = live.bindings.find( name );
        return it != live.bindings.end( ) && it->second.kind == kind ? it->second.value : nullptr;
    }

    // Reloads the project when its file changes (checked a few times a second).
    void refresh( )
    {
        const double now = ImGui::GetTime( );
        if ( now - live.checked < .25 )
            return;
        live.checked = now;
        std::error_code error;
        const auto stamp = std::filesystem::last_write_time( live.path, error );
        if ( error || ( live.loaded && stamp == live.stamp ) )
            return;
        std::ifstream file( live.path, std::ios::binary );
        std::stringstream text;
        text << file.rdbuf( );
        doc::document_t next;
        if ( !file || !doc::from_json( text.str( ), next ) )
            return; // keep the last good version (the file may be mid-save)
        live.doc = std::move( next );
        live.stamp = stamp;
        live.loaded = true;
        if ( live.doc.project == doc::project_t::app )
            live.app = names::app( live.doc );
        else
            live.menu = names::menu( live.doc );
    }

    // ---- apps ----

    // Copies between your state and the document, in either direction.
    void sync_app( bool into_document )
    {
        using k = kit_live::kind_t;
        for ( auto& screen : live.doc.screens )
        {
            // An instance's own flags.
            for ( auto& e : screen.elements )
            {
                const auto key = static_cast<unsigned long long>( e.uid );
                if ( const auto it = live.app.visible.find( key ); it != live.app.visible.end( ) && e.kind == doc::element_kind_t::instance )
                    if ( auto* v = static_cast<bool*>( bound( it->second, k::flag ) ) )
                        into_document ? void( e.shown = *v ) : void( *v = e.shown );
                if ( const auto it = live.app.enabled.find( key ); it != live.app.enabled.end( ) && e.kind == doc::element_kind_t::instance )
                    if ( auto* v = static_cast<bool*>( bound( it->second, k::flag ) ) )
                        into_document ? void( e.enabled = *v ) : void( *v = e.enabled );
            }
            for ( const auto& f : doc::flatten( live.doc, screen.elements ) )
            {
                auto& e = *f.element;
                if ( const auto it = live.app.value.find( f.key ); it != live.app.value.end( ) )
                {
                    const auto& name = it->second;
                    switch ( e.kind )
                    {
                    case doc::element_kind_t::input:
                        if ( auto b = live.bindings.find( name ); b != live.bindings.end( ) && b->second.kind == k::buffer && b->second.size )
                        {
                            auto* text = static_cast<char*>( b->second.value );
                            if ( into_document )
                                e.value = text;
                            else
                            {
                                const size_t n = std::min( e.value.size( ), b->second.size - 1 );
                                std::memcpy( text, e.value.data( ), n );
                                text[n] = 0;
                            }
                        }
                        break;
                    case doc::element_kind_t::checkbox:
                        if ( auto* v = static_cast<bool*>( bound( name, k::flag ) ) )
                            into_document ? void( e.on = *v ) : void( *v = e.on );
                        break;
                    case doc::element_kind_t::slider:
                    case doc::element_kind_t::progress:
                        if ( auto* v = static_cast<float*>( bound( name, k::number ) ) )
                            into_document ? void( e.number = *v ) : void( *v = e.number );
                        break;
                    default:
                        if ( auto* v = static_cast<int*>( bound( name, k::whole ) ) )
                            into_document ? void( e.choice = *v ) : void( *v = e.choice );
                        break;
                    }
                }
                if ( const auto it = live.app.text.find( f.key ); it != live.app.text.end( ) && into_document )
                    if ( auto* v = static_cast<std::string*>( bound( it->second, k::string ) ) )
                        e.text = *v;
                if ( const auto it = live.app.visible.find( f.key ); it != live.app.visible.end( ) )
                    if ( auto* v = static_cast<bool*>( bound( it->second, k::flag ) ) )
                        into_document ? void( e.shown = *v ) : void( *v = e.shown );
                if ( const auto it = live.app.enabled.find( f.key ); it != live.app.enabled.end( ) )
                    if ( auto* v = static_cast<bool*>( bound( it->second, k::flag ) ) )
                        into_document ? void( e.enabled = *v ) : void( *v = e.enabled );
            }
        }
    }

    auto draw_app( ) -> bool
    {
        using k = kit_live::kind_t;
        auto& d = live.doc;
        if ( d.screens.empty( ) )
            return false;
        if ( auto* open = static_cast<bool*>( bound( "open", k::flag ) ); open && !*open )
            return true;
        // Your screen enum counts screens in project order.
        auto* index = static_cast<int*>( bound( "screen", k::whole ) );
        if ( index )
            live.screen = d.screens[static_cast<size_t>( std::clamp( *index, 0, static_cast<int>( d.screens.size( ) ) - 1 ) )].uid;

        sync_app( true );
        std::map<unsigned long long, const doc::element_t*> elements;
        for ( auto& screen : d.screens )
            for ( const auto& f : doc::flatten( d, screen.elements ) )
                elements[f.key] = f.element;
        preview::pressed = [&]( unsigned long long key ) {
            if ( const auto it = elements.find( key ); it != elements.end( ) && it->second->target == -1 )
                if ( auto* open = static_cast<bool*>( bound( "open", k::flag ) ) )
                    *open = false;
            if ( const auto it = live.app.event.find( key ); it != live.app.event.end( ) )
                if ( auto* event = static_cast<std::function<void( )>*>( bound( it->second, k::event ) ); event && *event )
                    ( *event )( );
        };
        preview::custom = []( const std::string& function, const ImRect& rect ) {
            auto* fn = reinterpret_cast<void ( * )( const ImRect& )>( bound( doc::identifier( function ), k::widget ) );
            if ( fn )
                fn( rect );
            return fn != nullptr;
        };

        preview::apply_theme( d );
        kit::begin_host( "##kit_live" );
        ImGui::SetCursorScreenPos( ImGui::GetWindowPos( ) );
        preview::app_options_t options;
        if ( d.theme.resizable )
            options.size = ImGui::GetWindowSize( );
        preview::draw_app( d, live.screen, options, live.hits );
        kit::end_host( );
        preview::pressed = nullptr;
        preview::custom = nullptr;

        sync_app( false );
        if ( index )
            for ( size_t i = 0; i < d.screens.size( ); ++i )
                if ( d.screens[i].uid == live.screen )
                    *index = static_cast<int>( i );
        return true;
    }

    // ---- menus ----

    void sync_menu( bool into_document )
    {
        using k = kit_live::kind_t;
        const auto flag = [&]( const std::string& name, bool& value ) {
            if ( auto* v = static_cast<bool*>( bound( name, k::flag ) ) )
                into_document ? void( value = *v ) : void( *v = value );
        };
        const auto whole = [&]( const std::string& name, int& value ) {
            if ( auto* v = static_cast<int*>( bound( name, k::whole ) ) )
                into_document ? void( value = *v ) : void( *v = value );
        };
        for ( auto& c : live.doc.categories )
            for ( auto& g : c.groups )
                for ( auto& f : g.features )
                {
                    if ( f.has_switch )
                        flag( live.menu.feature_on[f.uid], f.enabled );
                    if ( f.has_key )
                    {
                        whole( live.menu.feature_key[f.uid], f.key );
                        whole( live.menu.feature_mode[f.uid], f.mode );
                    }
                    for ( auto& s : f.sections )
                        for ( auto& w : s.widgets )
                        {
                            const auto& n = live.menu.widgets[w.uid];
                            switch ( w.kind )
                            {
                            case doc::kind_t::toggle: flag( n.main, w.on ); break;
                            case doc::kind_t::slider:
                                if ( auto* v = static_cast<float*>( bound( n.main, k::number ) ) )
                                    into_document ? void( w.number = *v ) : void( *v = w.number );
                                break;
                            case doc::kind_t::slider_int:
                                if ( auto* v = static_cast<int*>( bound( n.main, k::whole ) ) )
                                    into_document ? void( w.number = static_cast<float>( *v ) ) : void( *v = static_cast<int>( w.number ) );
                                break;
                            case doc::kind_t::select:
                            case doc::kind_t::segment: whole( n.main, w.choice ); break;
                            case doc::kind_t::chips:
                                w.chip_values.resize( n.chips.size( ), false );
                                for ( size_t i = 0; i < n.chips.size( ); ++i )
                                {
                                    bool v = w.chip_values[i];
                                    flag( n.chips[i], v );
                                    w.chip_values[i] = v;
                                }
                                break;
                            case doc::kind_t::color:
                                if ( auto* v = static_cast<float*>( bound( n.main, k::color ) ) )
                                    for ( int i = 0; i < 4; ++i )
                                        into_document ? void( w.rgba[i] = v[i] ) : void( v[i] = w.rgba[i] );
                                if ( w.has_switch )
                                    flag( n.on, w.on );
                                break;
                            case doc::kind_t::keybind:
                                whole( n.key, w.key );
                                if ( w.has_mode )
                                    whole( n.mode, w.mode );
                                if ( w.has_switch )
                                    flag( n.on, w.on );
                                break;
                            default: break;
                            }
                        }
                }
    }

    auto draw_menu( ) -> bool
    {
        sync_menu( true );
        preview::apply_theme( live.doc );
        kit::begin_host( "##kit_live" );
        ImGui::SetCursorScreenPos( ImGui::GetWindowPos( ) );
        preview::draw( live.doc, live.shell, live.hits );
        kit::end_host( );
        sync_menu( false );
        return true;
    }
}

void kit_live::watch( const char* path, std::vector<binding_t> bindings )
{
    live.path = path ? path : "";
    live.bindings.clear( );
    for ( const auto& b : bindings )
        live.bindings[b.name] = b;
    live.loaded = false;
    live.checked = -10;
}

bool kit_live::draw( )
{
    if ( live.path.empty( ) )
        return false;
    refresh( );
    if ( !live.loaded )
        return false;
    return live.doc.project == doc::project_t::app ? draw_app( ) : draw_menu( );
}
