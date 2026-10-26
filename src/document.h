#pragma once

#include <string>
#include <vector>

// A project is either a menu (theme, then categories (tabs) > groups > features > sections > widgets) or an app: a
// plain window with screens of free-placed elements, for loaders and launchers. Values live here too: what you set in
// the editor's preview becomes the exported default.
namespace doc
{
    enum class kind_t
    {
        toggle,
        slider,
        slider_int,
        select,
        segment,
        chips,
        color,
        keybind,
        button,
        value,
        note,
        count
    };

    auto kind_name( kind_t kind ) -> const char*;

    struct widget_t
    {
        int uid = 0;
        kind_t kind = kind_t::toggle;
        std::string label = "Setting", hint, id; // id: the C++ field name
        bool on = false;                       // toggle; the switch beside a colour / keybind
        bool has_switch = false;               // colour and keybind rows: show a switch
        float number = 0, min = 0, max = 100;
        int choice = 0;
        std::string format = "%.0f", unit;
        std::vector<std::string> options;  // select, segment, chips
        std::vector<bool> chip_values;     // chips
        float rgba[4]{ 1, 1, 1, 1 };
        int key = 0, mode = 0;
        bool has_mode = true;
        std::string text = "Run"; // button caption, value text, note text
    };

    struct section_t
    {
        int uid = 0;
        std::string title = "Section";
        int column = 0;
        std::vector<widget_t> widgets;
    };

    struct feature_t
    {
        int uid = 0;
        std::string name = "Feature", description, id;
        bool has_switch = true, enabled = false;
        bool has_key = false;
        int key = 0, mode = 0;
        std::string meta; // list subtitle; empty = the key, or nothing
        std::vector<section_t> sections;
    };

    struct group_t
    {
        int uid = 0;
        std::string title = "Group";
        std::vector<feature_t> features;
    };

    struct category_t
    {
        int uid = 0;
        std::string name = "Category";
        std::vector<group_t> groups;
    };

    // ---- apps ----

    enum class element_kind_t
    {
        text,
        button,
        input,
        checkbox,
        slider,
        dropdown,
        progress,
        list,
        panel,
        divider,
        spinner,
        tabs,
        image,
        icon,
        instance, // a placed copy of a component
        custom,   // drawn by your own C++ function
        count
    };

    auto element_name( element_kind_t kind ) -> const char*;

    struct element_t
    {
        int uid = 0;
        element_kind_t kind = element_kind_t::text;
        std::string text = "Text"; // text, caption, label, or an input's placeholder
        std::string id;            // the C++ name
        float x = 32, y = 32, w = 160, h = 32;
        float size = 13.f;                 // text
        int align = 0;                     // text: left, centre, right
        int role = 1;                      // text: title, body, muted, accent
        bool primary = false;              // button
        int target = 0;                    // button: the uid of the screen it opens; -1 closes the window
        bool password = false;             // input
        std::string value;                 // input contents
        bool on = false;                   // checkbox
        float number = 0, min = 0, max = 100; // slider; progress (0..1)
        bool endless = false;              // progress: an endless sweep
        std::string format = "%.0f";       // slider
        int choice = 0;                    // dropdown and list selection
        std::vector<std::string> options;  // dropdown items, list rows
        std::vector<std::string> details;  // list rows' second lines
        int icon = 0;                      // button, icon: a kit::icon_t
        int pin_x = 0, pin_y = 0;          // kit::el::pin_t: how it follows a resized window
        bool dynamic = false;              // text: your code can change it
        bool hideable = false, shown = true;   // your code can hide it; shown is the start value
        bool lockable = false, enabled = true; // your code can disable it
        int tab_owner = 0, tab_index = 0;  // shown only while that tabs element is on this tab
        bool tint = false;                 // use rgba instead of the theme's colour
        float rgba[4]{ 76 / 255.f, 125 / 255.f, 240 / 255.f, 1.f };
        std::string toast;                 // button: a notification when pressed
        int image = 0;                     // image: the project image's uid
        float rounding = 0;                // image corners
        int component = 0;                 // instance: the component's uid
        std::string function;              // custom: the C++ function that draws it
    };

    struct image_t
    {
        int uid = 0;
        std::string name, id;
        std::string data; // the file's bytes (PNG or JPEG)
        int width = 0, height = 0;
    };

    // A group of elements designed once and placed as instances; editing it updates every instance.
    struct component_t
    {
        int uid = 0;
        std::string name = "Component", id;
        float w = 240, h = 120;
        std::vector<element_t> elements;
    };

    struct screen_t
    {
        int uid = 0;
        std::string name = "Screen", id;
        std::vector<element_t> elements; // back to front
    };

    enum class project_t
    {
        menu,
        app
    };

    struct theme_t
    {
        std::string title = "base", footer = "Ready", namespace_name = "menu";
        float accent[4]{ 76 / 255.f, 125 / 255.f, 240 / 255.f, 1.f };
        float window_alpha = .74f, rounding = 14.f, width = 1040, height = 640, list_width = 252;
        int columns = 2;
        int palette = 0;                       // kit::palette_id
        int toggles = 0, sliders = 0, buttons = 0, fields = 0; // kit widget styles
        float control_rounding = 7.f;
        bool resizable = false; // apps: the user can resize the window; elements follow their pins
        int transition = 0;     // apps: between screens; none, fade, slide
        // Palette 4: your own colours.
        float custom_window[4]{ 20 / 255.f, 20 / 255.f, 23 / 255.f, 1.f };
        float custom_text[4]{ 236 / 255.f, 236 / 255.f, 239 / 255.f, 1.f };
        float custom_muted[4]{ 139 / 255.f, 139 / 255.f, 149 / 255.f, 1.f };
        bool custom_light = false; // dark lines and fills, for a light window
        std::string plugin;        // a widget plugin DLL for custom elements
    };

    struct document_t
    {
        std::string name = "Untitled menu";
        project_t project = project_t::menu;
        theme_t theme;
        std::vector<category_t> categories;
        std::vector<screen_t> screens;
        std::vector<component_t> components;
        std::vector<image_t> images;
        int next_uid = 1;
    };

    // An element as drawn: instances are expanded into their component's elements, moved to the instance.
    struct flat_t
    {
        element_t* element;
        float dx = 0, dy = 0;
        int instance = 0;          // the instance's uid, for elements inside one
        unsigned long long key;    // unique per drawn element: (instance << 32) | uid
        element_t* owner = nullptr; // that instance: its tab, visibility and enabled state apply to everything inside
    };
    auto flatten( document_t& d, std::vector<element_t>& elements ) -> std::vector<flat_t>;
    auto find_component( document_t& d, int uid ) -> component_t*;
    auto find_image( document_t& d, int uid ) -> image_t*;

    auto make_widget( document_t& d, kind_t kind ) -> widget_t;
    auto make_element( document_t& d, element_kind_t kind ) -> element_t;
    auto make_screen( document_t& d, const std::string& name ) -> screen_t;

    // Templates for File > New.
    auto starter( ) -> document_t;   // an example menu
    auto loader( ) -> document_t;    // a three-screen loader
    auto blank_menu( ) -> document_t;
    auto blank_app( ) -> document_t;

    auto to_json( const document_t& d ) -> std::string;
    auto from_json( const std::string& text, document_t& out, std::string* error = nullptr ) -> bool;

    // The clipboard format: elements or menu rows, as JSON with a marker so other text is ignored.
    auto clip_elements( const std::vector<element_t>& elements ) -> std::string;
    auto clip_widgets( const std::vector<widget_t>& widgets ) -> std::string;
    // Parses a clip; new uids are taken from `d`. False when the text isn't a clip of that kind.
    auto paste_elements( const std::string& text, document_t& d, std::vector<element_t>& out ) -> bool;
    auto paste_widgets( const std::string& text, document_t& d, std::vector<widget_t>& out ) -> bool;

    auto base64( const std::string& bytes ) -> std::string;
    auto unbase64( const std::string& text ) -> std::string;

    // A valid, unique-looking C++ identifier from free text ("Max distance" -> "max_distance").
    auto identifier( const std::string& text ) -> std::string;
}
