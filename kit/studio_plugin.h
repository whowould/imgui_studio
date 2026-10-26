#pragma once
// Widget plugins: a DLL that ImGui Studio loads to draw your custom widgets on its canvas, so a "Custom" element shows
// the real thing instead of a placeholder. Build it against the same ImGui version and kit as the editor (see
// examples/widget_plugin), then pick it under Project settings > Custom widgets. The editor reloads it when you rebuild.
//
// In the plugin's source, after your widget functions:
//
//     STUDIO_WIDGETS( { "ring_meter", ring_meter }, { "status_badge", status_badge } )
//
// Each widget is a plain function, void name( const ImRect& r ), so the same file also compiles into your app, where
// the exported code calls it by that name.

#include "kit.h"

#include <cstring>

struct studio_widget_t
{
    const char* name;
    void ( *draw )( const ImRect& r );
};

#define STUDIO_WIDGETS( ... )                                                                                                  \
    static const studio_widget_t studio_widget_table[] = { __VA_ARGS__ };                                                    \
    extern "C" __declspec( dllexport ) void studio_attach( ImGuiContext* context, ImGuiMemAllocFunc alloc, ImGuiMemFreeFunc free, \
                                                           void* user )                                                    \
    {                                                                                                                          \
        ImGui::SetAllocatorFunctions( alloc, free, user );                                                                     \
        ImGui::SetCurrentContext( context );                                                                                   \
    }                                                                                                                          \
    extern "C" __declspec( dllexport ) int studio_widgets( const char** names, int capacity )                                 \
    {                                                                                                                          \
        int count = 0;                                                                                                         \
        for ( const auto& widget : studio_widget_table )                                                                       \
        {                                                                                                                      \
            if ( count < capacity )                                                                                            \
                names[count] = widget.name;                                                                                    \
            ++count;                                                                                                           \
        }                                                                                                                      \
        return count;                                                                                                          \
    }                                                                                                                          \
    extern "C" __declspec( dllexport ) bool studio_draw( const char* name, const ImRect* rect, const kit::theme_t* theme,      \
                                                         const kit::palette_t* palette, ImFont* font )                         \
    {                                                                                                                          \
        for ( const auto& widget : studio_widget_table )                                                                       \
            if ( std::strcmp( widget.name, name ) == 0 )                                                                       \
            {                                                                                                                  \
                kit::theme = *theme;                                                                                           \
                kit::use_palette( *palette );                                                                                  \
                kit::set_font( font );                                                                                         \
                widget.draw( *rect );                                                                                          \
                return true;                                                                                                   \
            }                                                                                                                  \
        return false;                                                                                                          \
    }
