#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

// A small JSON value with a reader and a pretty writer; enough for project files.
namespace json
{
    struct value_t
    {
        enum class kind_t
        {
            null,
            boolean,
            number,
            string,
            array,
            object
        } kind = kind_t::null;

        bool b = false;
        double n = 0;
        std::string s;
        std::vector<value_t> items;
        std::vector<std::pair<std::string, value_t>> fields; // keeps key order

        value_t( ) = default;
        value_t( bool v ) : kind( kind_t::boolean ), b( v ) { }
        value_t( int v ) : kind( kind_t::number ), n( v ) { }
        value_t( float v ) : kind( kind_t::number ), n( v ) { }
        value_t( double v ) : kind( kind_t::number ), n( v ) { }
        value_t( const char* v ) : kind( kind_t::string ), s( v ) { }
        value_t( std::string v ) : kind( kind_t::string ), s( std::move( v ) ) { }

        static auto array( ) -> value_t;
        static auto object( ) -> value_t;

        auto operator[]( const std::string& key ) -> value_t&; // adds the field if missing (objects)
        auto get( const std::string& key ) const -> const value_t*;
        void push( value_t v );

        auto as_bool( bool fallback = false ) const -> bool;
        auto as_float( float fallback = 0 ) const -> float;
        auto as_int( int fallback = 0 ) const -> int;
        auto as_string( const std::string& fallback = { } ) const -> std::string;
    };

    auto parse( const std::string& text, value_t& out, std::string* error = nullptr ) -> bool;
    auto write( const value_t& value, int indent = 2 ) -> std::string;
}
