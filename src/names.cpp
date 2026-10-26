#include "names.h"

#include <set>

namespace
{
    struct pool_t
    {
        std::set<std::string> used;

        auto take( std::string base ) -> std::string
        {
            if ( base.empty( ) )
                base = "value";
            if ( names::keyword( base ) )
                base += "_";
            std::string name = base;
            for ( int i = 2; used.count( name ); ++i )
                name = base + "_" + std::to_string( i );
            used.insert( name );
            return name;
        }
    };

    auto id_of( const std::string& id, const std::string& fallback ) -> std::string
    {
        return doc::identifier( id.empty( ) ? fallback : id );
    }
}

auto names::keyword( const std::string& word ) -> bool
{
    static const std::set<std::string> words = { "alignas", "auto", "bool", "break", "case", "catch", "char", "class", "const",
                                                 "continue", "default", "delete", "do", "double", "else", "enum", "explicit",
                                                 "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline",
                                                 "int", "long", "namespace", "new", "operator", "private", "protected", "public",
                                                 "register", "return", "short", "signed", "sizeof", "static", "struct", "switch",
                                                 "template", "this", "throw", "true", "try", "typedef", "union", "unsigned",
                                                 "using", "virtual", "void", "volatile", "while" };
    return words.count( word ) > 0;
}

auto names::menu( const doc::document_t& d ) -> menu_t
{
    menu_t p;
    pool_t fields, functions;
    fields.used = { "settings", "setup", "draw", "state" };
    for ( const auto& c : d.categories )
        for ( const auto& g : c.groups )
            for ( const auto& f : g.features )
            {
                const std::string base = id_of( f.id, f.name );
                if ( f.has_switch )
                    p.feature_on[f.uid] = fields.take( base );
                if ( f.has_key )
                {
                    p.feature_key[f.uid] = fields.take( base + "_key" );
                    p.feature_mode[f.uid] = fields.take( base + "_mode" );
                }
                p.feature_draw[f.uid] = functions.take( "draw_" + base );
                for ( const auto& s : f.sections )
                    for ( const auto& w : s.widgets )
                    {
                        widget_t n;
                        const std::string name = base + "_" + id_of( w.id, w.label );
                        switch ( w.kind )
                        {
                        case doc::kind_t::chips:
                            for ( const auto& option : w.options )
                                n.chips.push_back( fields.take( name + "_" + doc::identifier( option ) ) );
                            break;
                        case doc::kind_t::keybind:
                            n.key = fields.take( name + "_key" );
                            if ( w.has_mode )
                                n.mode = fields.take( name + "_mode" );
                            if ( w.has_switch )
                                n.on = fields.take( name );
                            break;
                        case doc::kind_t::color:
                            n.main = fields.take( name + "_color" );
                            if ( w.has_switch )
                                n.on = fields.take( name );
                            break;
                        case doc::kind_t::value:
                        case doc::kind_t::note: break;
                        default: n.main = fields.take( name ); break;
                        }
                        p.widgets[w.uid] = n;
                    }
            }
    return p;
}

auto names::app( const doc::document_t& source ) -> app_t
{
    // flatten( ) only syncs instance sizes; nothing here changes the document.
    auto& d = const_cast<doc::document_t&>( source );
    app_t p;
    pool_t screens, functions, fields, events, images;
    functions.used = { "setup", "draw", "live" };
    fields.used = { "screen", "open" };
    for ( const auto& image : d.images )
        p.image[image.uid] = images.take( id_of( image.id, image.name ) + "_image" );
    std::set<std::string> customs;
    for ( auto& screen : d.screens )
    {
        const std::string base = id_of( screen.id, screen.name );
        p.screen[screen.uid] = screens.take( base );
        p.draw[screen.uid] = functions.take( "draw_" + base );
        std::set<std::string> labels;
        for ( const auto& e : screen.elements )
            if ( e.kind == doc::element_kind_t::instance )
            {
                const std::string name = base + "_" + id_of( e.id, e.text );
                if ( e.hideable )
                    p.visible[static_cast<unsigned long long>( e.uid )] = fields.take( name + "_visible" );
                if ( e.lockable )
                    p.enabled[static_cast<unsigned long long>( e.uid )] = fields.take( name + "_enabled" );
            }
        for ( const auto& flat : doc::flatten( d, screen.elements ) )
        {
            const auto& e = *flat.element;
            std::string prefix = base + "_";
            std::string event_prefix;
            if ( flat.instance )
                for ( const auto& owner : screen.elements )
                    if ( owner.uid == flat.instance )
                    {
                        prefix += id_of( owner.id, owner.text ) + "_";
                        event_prefix = id_of( owner.id, owner.text ) + "_";
                    }
            const std::string name = prefix + id_of( e.id, e.text );
            using k = doc::element_kind_t;
            const bool has_value = e.kind == k::input || e.kind == k::checkbox || e.kind == k::slider || e.kind == k::dropdown ||
                                   e.kind == k::list || e.kind == k::tabs || ( e.kind == k::progress && !e.endless );
            if ( has_value )
                p.value[flat.key] = fields.take( name );
            if ( e.kind == k::text && e.dynamic )
                p.text[flat.key] = fields.take( name );
            if ( e.hideable )
                p.visible[flat.key] = fields.take( name + "_visible" );
            if ( e.lockable )
                p.enabled[flat.key] = fields.take( name + "_enabled" );
            if ( e.kind == k::button )
                p.event[flat.key] = events.take( event_prefix + id_of( e.id, e.text ) );
            if ( e.kind == k::button || e.kind == k::checkbox )
            {
                std::string label = e.text;
                if ( labels.count( label ) )
                    label += "##" + std::to_string( flat.key );
                labels.insert( e.text );
                p.label[flat.key] = label;
            }
            if ( e.kind == k::custom )
            {
                const auto fn = doc::identifier( e.function.empty( ) ? "draw_custom" : e.function );
                if ( customs.insert( fn ).second )
                    p.functions.push_back( fn );
            }
        }
    }
    return p;
}
