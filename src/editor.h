#pragma once

#include "codegen.h"
#include "document.h"
#include "preview.h"

#include <kit.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

// Monospace font for the code drawer (falls back to the menu font).
inline ImFont* code_font = nullptr;

class editor_c
{
public:
    editor_c( );
    void frame( );
    void open_path( const std::wstring& path );
    // Starts from a template: "example", "loader", "blank-menu" or "blank-app". False for an unknown name.
    auto use_template( const std::wstring& name ) -> bool;
    // Writes the C++ without a dialog (for --export); the message goes to `message`.
    auto export_to( const std::wstring& folder, std::string& message ) -> bool;
    // Writes the project without a dialog (for --save).
    auto save_to( const std::wstring& path ) -> bool;
    // A clean exit: the autosave is no longer needed.
    void shutdown( );

private:
    // A node's place in the document; the deepest non-null pointer is the node.
    struct where_t
    {
        doc::category_t* category = nullptr;
        doc::group_t* group = nullptr;
        doc::feature_t* feature = nullptr;
        doc::section_t* section = nullptr;
        doc::widget_t* widget = nullptr;
        size_t ci = 0, gi = 0, fi = 0, si = 0, wi = 0;
        doc::screen_t* screen = nullptr;       // apps: a screen, or the screen holding the element
        doc::component_t* component = nullptr; // or a component, or the component holding the element
        doc::element_t* element = nullptr;
        size_t sci = 0, coi = 0, ei = 0;
        explicit operator bool( ) const
        {
            return category || screen || component;
        }
        auto elements( ) const -> std::vector<doc::element_t>*
        {
            return screen ? &screen->elements : component ? &component->elements : nullptr;
        }
    };

    // A drag on the canvas: moving the selected elements, resizing one by a handle, or resizing the window.
    struct drag_t
    {
        enum class kind_t
        {
            none,
            move,
            resize,
            window
        } kind = kind_t::none;
        int uid = 0;
        int handle = -1; // resize: 0..7 clockwise from the top-left corner; window: 0 right, 1 bottom, 2 corner
        ImVec2 mouse;
        ImVec4 start;    // the dragged element's x, y, w, h, or the window's size
        std::vector<std::pair<int, ImVec4>> group; // every element that moves, with where it started
    };

    struct guide_t
    {
        bool vertical;
        float at; // surface coordinates
    };

    // Where the canvas draws the window and at what zoom.
    struct view_t
    {
        ImVec2 origin;
        float scale = 1;
        auto to_screen( ImVec2 p ) const -> ImVec2
        {
            return origin + ( p - origin ) * scale;
        }
        auto to_doc( ImVec2 p ) const -> ImVec2
        {
            return origin + ( p - origin ) / scale;
        }
    };

    auto app( ) const -> bool
    {
        return m_doc.project == doc::project_t::app;
    }
    auto locate( int uid ) -> where_t;
    auto shown_screen( ) -> doc::screen_t*;
    auto shown_component( ) -> doc::component_t*;
    auto selected_elements( ) -> std::vector<doc::element_t*>;
    auto multi( int uid ) const -> bool;

    void title_bar( );
    void structure( );
    void tree( );
    void canvas( );
    void canvas_menu( ImVec2 o, float width, float height );
    void canvas_app( ImVec2 o, float width, float height );
    void zoom_controls( ImVec2 o, float width, float height, ImVec2 surface );
    void recover_banner( ImVec2 o, float width );
    void scaled( const view_t& view, const ImRect& clip, const std::function<void( )>& body );
    void properties( );
    void properties_project( );
    void properties_element( doc::element_t& e, const where_t& at );
    void properties_multi( );
    void code_panel( );
    void status_bar( );
    void shortcuts( );
    void track_history( );
    void autosave( );

    void new_menu( );
    void open_menu( );
    void add_menu( );
    // Menus: 0 category, 1 group, 2 feature, 3 section, 4+ widget kind. Apps: 100 screen, 101 component, 200+ element kind.
    void add( int what );
    void remove( int uid );
    void remove_selection( );
    void duplicate( int uid );
    void duplicate_selection( );
    void move( int uid, int delta );
    void drop( int source, int target );
    void select( int uid, bool add = false );
    void copy( bool cut );
    void paste( );
    void align( int how );      // 0 left, 1 centre, 2 right, 3 top, 4 middle, 5 bottom
    void distribute( bool horizontal );
    void match( bool width );
    void make_component( );
    void detach( int instance );
    void resize_window( float width, float height, const std::vector<std::pair<int, ImVec4>>& from, ImVec2 from_size );
    void import_image( doc::element_t* into );
    void renumber( doc::feature_t& f );
    void renumber( doc::section_t& s );
    void renumber( doc::group_t& g );
    void renumber( doc::category_t& c );
    void renumber( std::vector<doc::element_t>& elements );

    void start( doc::document_t d, const char* message );
    void open( );
    void save( bool as );
    void export_code( );
    void undo( );
    void redo( );
    void notify( std::string message );
    void remember( const std::wstring& path );

    doc::document_t m_doc;
    std::string m_last, m_saved;
    std::vector<std::string> m_undo, m_redo;
    std::wstring m_path;
    std::vector<std::wstring> m_recent;

    int m_selected = 0; // 0 = the project itself
    std::vector<int> m_multi; // apps: every selected element (the selected one included)
    int m_hovered = 0;
    int m_last_shown = 0;
    int m_screen = 0; // the app screen, or component, on the canvas
    bool m_design = true, m_code_open = false;
    int m_code_tab = 1;
    float m_zoom = 1.f;
    bool m_fit = true;   // fit the window to the canvas on the next frame
    bool m_marquee = false;
    ImVec2 m_marquee_from;
    std::set<int> m_collapsed;
    kit::shell_state_t m_shell;
    std::vector<preview::hit_t> m_hits;
    drag_t m_drag;
    std::vector<guide_t> m_guides;
    codegen::output_t m_code;
    std::string m_code_for;
    std::string m_status;
    double m_status_at = -100;
    double m_autosaved_at = 0;
    std::string m_autosaved;
    std::string m_recovered; // a previous session's unsaved project, offered on start
    ImVec2 m_size;
};
