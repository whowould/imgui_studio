#pragma once

#include "document.h"

#include <string>

// C++ for a document. Menus: a settings struct with every value at its current default, and draw( ) built from kit
// calls. Apps: screens, a state struct, button events, and a draw function per screen.
namespace codegen
{
    struct output_t
    {
        std::string header_name, source_name;
        std::string header, source;
        std::string images_name, images; // embedded images, when the project has any
    };

    auto generate( const doc::document_t& d ) -> output_t;
    // Writes the generated files, the kit, and the live-link sources next to them. Returns a message for the status bar.
    auto export_to( const doc::document_t& d, const std::wstring& folder, bool& ok ) -> std::string;
}
