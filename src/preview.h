#pragma once

#include "document.h"

#include <kit.h>

#include <functional>
#include <string>
#include <vector>

// Draws a document with the real kit widgets, exactly as the exported code will.
namespace preview
{
    struct hit_t
    {
        int uid;
        ImRect rect;
    };

    // Puts the caller's look back when it goes out of scope (the canvas draws with the project's).
    struct scoped_theme_t
    {
        kit::theme_t theme = kit::theme;
        kit::palette_t palette = kit::palette;
        ~scoped_theme_t( )
        {
            kit::theme = theme;
            kit::use_palette( palette );
        }
    };

    // Draws a custom element with your function; return false to get a labelled placeholder.
    inline std::function<bool( const std::string& function, const ImRect& rect )> custom;
    // Called with a button's flat key when it is pressed (not while inert).
    inline std::function<void( unsigned long long key )> pressed;

    auto palette_of( const doc::theme_t& t ) -> kit::palette_t;
    void apply_theme( const doc::document_t& d );
    // Draws the menu at the cursor; `hits` gets every section and row with its node's uid.
    void draw( doc::document_t& d, kit::shell_state_t& state, std::vector<hit_t>& hits );

    struct app_options_t
    {
        bool inert = false;  // elements draw but don't react (design mode)
        bool ghosts = false; // hidden elements still draw, faintly, so they can be picked
        ImVec2 size{ 0, 0 }; // the window's size if not the designed one (a resizable window)
    };
    // Draws an app's screen (`screen` is its uid) at the cursor; buttons that open a screen change it unless inert.
    void draw_app( doc::document_t& d, int& screen, const app_options_t& options, std::vector<hit_t>& hits );
    // Draws a component on its own, at its size.
    void draw_component( doc::document_t& d, int uid, const app_options_t& options, std::vector<hit_t>& hits );
    // The uid of the feature the shell is showing, or 0.
    auto shown_feature( const doc::document_t& d, const kit::shell_state_t& state ) -> int;
    // Shows the feature that holds node `uid` (any depth).
    void focus( const doc::document_t& d, kit::shell_state_t& state, int uid );
}
