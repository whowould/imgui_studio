#pragma once

#include "document.h"

#include <map>
#include <string>
#include <vector>

// The C++ names of a document's values, shared by the code generator and the live link so both always agree.
namespace names
{
    auto keyword( const std::string& word ) -> bool;

    struct widget_t
    {
        std::string main, key, mode, on;
        std::vector<std::string> chips;
    };

    struct menu_t
    {
        std::map<int, std::string> feature_on, feature_key, feature_mode, feature_draw;
        std::map<int, widget_t> widgets;
    };

    // Apps: drawn elements are keyed by doc::flat_t::key.
    struct app_t
    {
        std::map<int, std::string> screen, draw; // screen uid: enum value, draw function
        std::map<unsigned long long, std::string> value;   // inputs, checkboxes, sliders, dropdowns, lists, tabs, progress
        std::map<unsigned long long, std::string> text;    // text your code changes
        std::map<unsigned long long, std::string> visible, enabled;
        std::map<unsigned long long, std::string> event;   // buttons
        std::map<unsigned long long, std::string> label;   // ImGui labels for buttons and checkboxes, unique per screen
        std::map<int, std::string> image;                  // image uid: the byte array's name
        std::vector<std::string> functions;                // custom widgets' functions, once each
    };

    auto menu( const doc::document_t& d ) -> menu_t;
    auto app( const doc::document_t& d ) -> app_t;
}
