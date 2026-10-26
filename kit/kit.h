#pragma once
// kit: an ImGui widget library for menus and small apps (ImGui 1.92+). The editor renders its canvas with it, and every
// export ships it next to the generated code, so the exported UI draws exactly what the editor showed.
//
// Use in your own project: add kit.cpp to your build, call kit::load_font( io ) before the font atlas is built, set
// kit::theme (and kit::use_palette) as you like, then draw between ImGui::NewFrame( ) and ImGui::Render( ).

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include "imgui_internal.h"

#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace kit
{
    using items = std::span<const char* const>;

    // Widget looks, chosen per project.
    enum class toggle_t
    {
        pill, // a sliding switch
        check // a checkbox
    };
    enum class slider_t
    {
        bar,  // a filled box with the value inside
        track // a thin track with a knob
    };
    enum class button_t
    {
        soft,   // a light fill with a hairline
        solid,  // a stronger fill, no border
        outline // a border only
    };
    enum class field_t
    {
        box, // a filled box
        line // an underline
    };

    struct theme_t
    {
        ImVec4 accent{ 76 / 255.f, 125 / 255.f, 240 / 255.f, 1.f };
        float window_alpha = .74f; // window fill opacity; put a blur behind the window for frosted glass
        float rounding = 14.f;     // the window's corners
        float control_rounding = 7.f;
        ImVec2 size{ 1040, 640 };
        float list_width = 252.f;
        int columns = 2;
        std::string title = "base";
        std::string footer = "Ready";
        toggle_t toggles = toggle_t::pill;
        slider_t sliders = slider_t::bar;
        button_t buttons = button_t::soft;
        field_t fields = field_t::box;
        bool resizable = false;        // free-placed windows: the user can drag the edges; elements follow their pins
        ImVec2 min_size{ 200, 160 };
        int transition = 0;            // between screens: 0 none, 1 fade, 2 slide
    };
    inline theme_t theme;

    // The neutrals every widget is drawn with. Translucent fills and lines use `overlay` (white on dark palettes,
    // black on light ones), so one palette swap recolours everything.
    struct palette_t
    {
        ImVec4 window{ 20 / 255.f, 20 / 255.f, 23 / 255.f, 1.f };
        ImVec4 overlay{ 1.f, 1.f, 1.f, 1.f };
        float overlay_gain = 1.f;
        ImU32 title = IM_COL32( 244, 244, 246, 255 );
        ImU32 primary = IM_COL32( 236, 236, 239, 255 );
        ImU32 label = IM_COL32( 201, 201, 208, 255 );
        ImU32 secondary = IM_COL32( 154, 154, 164, 255 );
        ImU32 muted = IM_COL32( 139, 139, 149, 255 );
        ImU32 knob_off = IM_COL32( 217, 217, 222, 255 );
        ImU32 popup = IM_COL32( 28, 28, 31, 250 );
        ImU32 button = IM_COL32( 242, 242, 244, 255 ); // the primary action button
        ImU32 ink = IM_COL32( 20, 20, 22, 255 );       // text on it
    };
    enum class palette_id
    {
        graphite,
        midnight,
        light,
        oled
    };
    palette_t preset( palette_id id );
    void use_palette( const palette_t& value );
    inline palette_t palette;

    namespace tone
    {
        constexpr ImU32 white( int alpha )
        {
            return IM_COL32( 255, 255, 255, alpha );
        }
        // A translucent fill in the palette's overlay colour.
        ImU32 overlay( int alpha );

        // Kept in step with the palette by use_palette.
        inline ImU32 hairline = white( 18 );
        inline ImU32 row_line = white( 15 );
        inline ImU32 list_fill = white( 4 );
        inline ImU32 control = white( 13 );
        inline ImU32 control_hover = white( 20 );
        inline ImU32 control_border = white( 20 );
        inline ImU32 control_border_hover = white( 36 );
        inline ImU32 selected = white( 18 );
        inline ImU32 hover = white( 10 );
        inline ImU32 track_off = white( 41 );
        inline ImU32 window_border = white( 23 );
        inline ImU32 title = IM_COL32( 244, 244, 246, 255 );
        inline ImU32 primary = IM_COL32( 236, 236, 239, 255 );
        inline ImU32 label = IM_COL32( 201, 201, 208, 255 );
        inline ImU32 secondary = IM_COL32( 154, 154, 164, 255 );
        inline ImU32 muted = IM_COL32( 139, 139, 149, 255 );
        inline ImU32 knob_off = IM_COL32( 217, 217, 222, 255 );
        inline ImU32 popup = IM_COL32( 28, 28, 31, 250 );
        inline ImU32 ink = IM_COL32( 20, 20, 22, 255 );
        constexpr ImU32 warning = IM_COL32( 232, 168, 92, 255 );
        constexpr ImU32 live = IM_COL32( 77, 184, 132, 255 );
        ImU32 accent( int alpha = 255 );
        ImU32 window( );
    }

    namespace metric
    {
        constexpr float header = 52, footer = 32, head = 76, inset = 28;
        constexpr float row = 44, section = 40, list_row = 48, list_section = 32, column_gap = 40;
        constexpr float control_h = 28, select_w = 152, slider_w = 192, color_w = 100;
    }

    // Fonts: Inter (SIL OFL). Call before the atlas is built; any size can be drawn from it.
    ImFont* load_font( ImGuiIO& io, float size = 13.f );
    void set_font( ImFont* font );
    ImFont* font( );

    // Drawing helpers; colours go through the style alpha so disabled rows fade.
    float text_width( const char* text, float size = 13.f );
    void text( ImDrawList* draw, ImVec2 pos, ImU32 color, const char* text, float size = 13.f );
    void caps( ImDrawList* draw, ImVec2 pos, ImU32 color, const char* text );
    ImU32 faded( ImU32 color );
    float approach( float speed );

    // Primitives.
    bool switch_at( ImGuiID id, ImVec2 pos, bool& value, ImVec2 size = { 30, 18 } );
    float keycap( int& vk, int* mode, float right_x, float mid_y, bool* changed = nullptr );
    const char* key_name( int vk );
    const char* mode_name( int mode );
    std::string keyed( int vk, int mode ); // "Hold · Mouse 2" for list subtitles
    bool action_button( const char* label, ImVec2 pos, bool primary = false, float height = 32 );
    float action_width( const char* label );
    bool color_popup( ImGuiID id, float* rgba, const ImRect& anchor );

    // Settings rows, in columns of the current window from the cursor down. Rows are 44 px with a hairline above;
    // controls have fixed widths and sit flush right.
    void begin_columns( int count = 2 );
    void next_column( );
    void end_columns( );
    void section( const char* title );
    bool toggle( const char* label, bool& value, const char* hint = nullptr );
    bool select( const char* label, int& value, items options, const char* hint = nullptr );
    bool segment( const char* label, int& value, items options );
    bool slider( const char* label, float& value, float min, float max, const char* format = "%.0f",
                 const char* unit = "", const char* hint = nullptr );
    bool slider( const char* label, int& value, int min, int max, const char* unit = "", const char* hint = nullptr );

    struct chip_t
    {
        const char* name;
        bool* value;
    };
    bool chips( const char* label, std::initializer_list<chip_t> list, const char* hint = nullptr );
    bool chips( const char* label, std::span<const chip_t> list, const char* hint = nullptr );
    bool color( const char* label, float* rgba, bool* enabled = nullptr, const char* hint = nullptr );
    bool keybind( const char* label, int& vk, int* mode, bool* enabled = nullptr, const char* hint = nullptr );
    void value( const char* label, const char* text, ImU32 color = 0 ); // 0: the palette's secondary
    bool button( const char* label, const char* text, bool primary = false );
    void note( const char* text, ImU32 color = 0 ); // 0: the palette's muted
    bool text_field( const char* label, char* buffer, size_t capacity, const char* hint = nullptr, float width = 192 );
    ImRect block( float height );

    // Hooks for tools: `tag` is reported with the rect of every row, section and element drawn after it is set.
    inline int tag = -1;
    inline std::function<void( int tag, const ImRect& rect )> on_row;
    // For a tool that draws kit scaled: maps a drawn position to the screen (popups open there), and switches the mouse
    // to screen space while a popup is handled.
    inline std::function<ImVec2( ImVec2 )> to_screen;
    inline std::function<void( bool screen )> popup_mouse;

    // Vector icons in the kit's line style, drawn at any size.
    enum class icon_t
    {
        none,
        user,
        lock,
        key,
        mail,
        eye,
        eye_off,
        play,
        download,
        upload,
        settings,
        close,
        check,
        plus,
        minus,
        arrow_right,
        arrow_left,
        chevron_right,
        home,
        shield,
        bell,
        search,
        star,
        heart,
        info,
        warning,
        refresh,
        power,
        folder,
        log_out,
        globe,
        cpu,
        zap,
        trash,
        copy,
        external,
        message,
        calendar,
        clock,
        crosshair,
        count
    };
    void icon( ImDrawList* draw, icon_t id, ImVec2 centre, float size, ImU32 color );
    items icon_names( );

    // Notifications that stack at the bottom of the current window and fade after a few seconds.
    enum class toast_t
    {
        info,
        success,
        error
    };
    void toast( const char* text, toast_t kind = toast_t::info );

    // PNG or JPEG bytes decoded once (Windows Imaging Component) and uploaded through ImGui's texture system, so any
    // renderer backend with ImGuiBackendFlags_RendererHasTextures works. Returns false for data that won't decode.
    bool image_size( const unsigned char* data, size_t size, int& width, int& height );

    // The menu shell: category tabs, a feature list and an inspector.
    struct feature_t
    {
        std::string name, description;
        bool* enabled = nullptr;
        int* key = nullptr;
        int* mode = nullptr;
        std::function<std::string( )> meta;
        std::function<void( )> draw;
    };

    struct group_t
    {
        std::string title;
        std::vector<feature_t> features;
    };

    struct category_t
    {
        std::string name;
        std::vector<group_t> groups;
    };

    struct shell_state_t
    {
        int category = 0;
        int selected[32]{ };
        bool inert = false; // inspector rows draw but don't react (an editor's design mode)
        double changed_at = 0;
        int shown_category = -1, shown_feature = -1;
    };

    // Draws the shell into the current window at the cursor, at theme.size.
    void shell( const std::vector<category_t>& categories, shell_state_t& state );
    // A borderless, draggable ImGui window holding the shell.
    void window( const char* id, const std::vector<category_t>& categories, shell_state_t& state );

    // Free-placed UI for loaders, launchers and small tools: a plain window of theme.size with elements at fixed
    // positions inside it. Drag any empty spot to move the window.
    void begin_surface( ImVec2 size = { 0, 0 } ); // draws the window at the cursor (theme.size by default); el::at is relative to it
    void end_surface( );
    void begin_window( const char* id ); // an ImGui window holding a surface
    void end_window( );
    void begin_host( const char* id );   // the same window, empty: draw a surface or shell in it yourself
    void end_host( );
    // Call after begin_window/begin_surface with the shown screen's index: plays theme.transition when it changes.
    void screen( int index );

    namespace el
    {
        // How an element follows a window that is larger or smaller than designed.
        enum class pin_t
        {
            start,   // keeps its distance to the left / top
            end,     // keeps its distance to the right / bottom
            stretch, // keeps both distances and grows
            centre   // stays centred
        };
        enum class align_t
        {
            left,
            centre,
            right
        };
        enum class role_t
        {
            title,
            body,
            muted,
            accent
        };
        struct text_style_t
        {
            float size = 13.f;
            align_t align = align_t::left;
            role_t role = role_t::body;
            ImU32 color = 0; // overrides the role's colour
        };

        // A rect relative to the surface's top-left corner, designed for theme.size; pins move it when the window is
        // another size.
        ImRect at( float x, float y, float w, float h, pin_t x_pin = pin_t::start, pin_t y_pin = pin_t::start );

        void text( const char* text, const ImRect& r, text_style_t style = { } );
        bool button( const char* label, const ImRect& r, bool primary = false, icon_t icon = icon_t::none, ImU32 fill = 0 );
        bool tabs( const char* id, int& value, items options, const ImRect& r );
        void image( const unsigned char* data, size_t size, const ImRect& r, float rounding = 0 );
        void icon( icon_t id, const ImRect& r, ImU32 color = 0 );
        bool input( const char* id, char* buffer, size_t capacity, const ImRect& r, const char* placeholder = "",
                    bool password = false );
        bool checkbox( const char* label, bool& value, const ImRect& r );
        bool slider( const char* id, float& value, float min, float max, const ImRect& r, const char* format = "%.0f" );
        bool dropdown( const char* id, int& value, items options, const ImRect& r );
        void progress( float value, const ImRect& r ); // 0..1; below 0 runs an endless sweep
        bool list( const char* id, int& selected, items rows, const ImRect& r, items details = { } );
        void panel( const ImRect& r, ImU32 fill = 0 );
        void divider( const ImRect& r );
        void spinner( const ImRect& r );
    }
}
