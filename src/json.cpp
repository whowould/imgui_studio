#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using json::value_t;

auto value_t::array( ) -> value_t
{
    value_t v;
    v.kind = kind_t::array;
    return v;
}

auto value_t::object( ) -> value_t
{
    value_t v;
    v.kind = kind_t::object;
    return v;
}

auto value_t::operator[]( const std::string& key ) -> value_t&
{
    kind = kind_t::object;
    for ( auto& [name, field] : fields )
        if ( name == key )
            return field;
    fields.emplace_back( key, value_t{ } );
    return fields.back( ).second;
}

auto value_t::get( const std::string& key ) const -> const value_t*
{
    for ( const auto& [name, field] : fields )
        if ( name == key )
            return &field;
    return nullptr;
}

void value_t::push( value_t v )
{
    kind = kind_t::array;
    items.push_back( std::move( v ) );
}

auto value_t::as_bool( bool fallback ) const -> bool
{
    return kind == kind_t::boolean ? b : fallback;
}

auto value_t::as_float( float fallback ) const -> float
{
    return kind == kind_t::number ? static_cast<float>( n ) : fallback;
}

auto value_t::as_int( int fallback ) const -> int
{
    return kind == kind_t::number ? static_cast<int>( std::lround( n ) ) : fallback;
}

auto value_t::as_string( const std::string& fallback ) const -> std::string
{
    return kind == kind_t::string ? s : fallback;
}

namespace
{
    struct reader_t
    {
        const std::string& text;
        size_t at = 0;
        std::string error;

        void space( )
        {
            while ( at < text.size( ) && ( text[at] == ' ' || text[at] == '\n' || text[at] == '\r' || text[at] == '\t' ) )
                ++at;
        }

        auto fail( const char* what ) -> bool
        {
            if ( error.empty( ) )
                error = std::string( what ) + " at offset " + std::to_string( at );
            return false;
        }

        auto literal( const char* word ) -> bool
        {
            size_t i = 0;
            while ( word[i] )
            {
                if ( at + i >= text.size( ) || text[at + i] != word[i] )
                    return false;
                ++i;
            }
            at += i;
            return true;
        }

        auto string( std::string& out ) -> bool
        {
            if ( text[at] != '"' )
                return fail( "expected a string" );
            ++at;
            while ( at < text.size( ) && text[at] != '"' )
            {
                char c = text[at++];
                if ( c == '\\' && at < text.size( ) )
                {
                    const char e = text[at++];
                    switch ( e )
                    {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'u':
                    {
                        if ( at + 4 > text.size( ) )
                            return fail( "bad escape" );
                        const unsigned code = static_cast<unsigned>( std::strtoul( text.substr( at, 4 ).c_str( ), nullptr, 16 ) );
                        at += 4;
                        if ( code < 0x80 )
                            out += static_cast<char>( code );
                        else if ( code < 0x800 )
                        {
                            out += static_cast<char>( 0xC0 | ( code >> 6 ) );
                            out += static_cast<char>( 0x80 | ( code & 0x3F ) );
                        }
                        else
                        {
                            out += static_cast<char>( 0xE0 | ( code >> 12 ) );
                            out += static_cast<char>( 0x80 | ( ( code >> 6 ) & 0x3F ) );
                            out += static_cast<char>( 0x80 | ( code & 0x3F ) );
                        }
                        continue;
                    }
                    default: c = e; break;
                    }
                }
                out += c;
            }
            if ( at >= text.size( ) )
                return fail( "unterminated string" );
            ++at;
            return true;
        }

        auto value( value_t& out ) -> bool
        {
            space( );
            if ( at >= text.size( ) )
                return fail( "unexpected end" );
            const char c = text[at];
            if ( c == '{' )
            {
                ++at;
                out = value_t::object( );
                space( );
                if ( at < text.size( ) && text[at] == '}' )
                {
                    ++at;
                    return true;
                }
                while ( true )
                {
                    space( );
                    std::string key;
                    if ( !string( key ) )
                        return false;
                    space( );
                    if ( at >= text.size( ) || text[at] != ':' )
                        return fail( "expected ':'" );
                    ++at;
                    value_t field;
                    if ( !value( field ) )
                        return false;
                    out.fields.emplace_back( std::move( key ), std::move( field ) );
                    space( );
                    if ( at < text.size( ) && text[at] == ',' )
                    {
                        ++at;
                        continue;
                    }
                    if ( at < text.size( ) && text[at] == '}' )
                    {
                        ++at;
                        return true;
                    }
                    return fail( "expected ',' or '}'" );
                }
            }
            if ( c == '[' )
            {
                ++at;
                out = value_t::array( );
                space( );
                if ( at < text.size( ) && text[at] == ']' )
                {
                    ++at;
                    return true;
                }
                while ( true )
                {
                    value_t item;
                    if ( !value( item ) )
                        return false;
                    out.items.push_back( std::move( item ) );
                    space( );
                    if ( at < text.size( ) && text[at] == ',' )
                    {
                        ++at;
                        continue;
                    }
                    if ( at < text.size( ) && text[at] == ']' )
                    {
                        ++at;
                        return true;
                    }
                    return fail( "expected ',' or ']'" );
                }
            }
            if ( c == '"' )
            {
                out = value_t( std::string( ) );
                return string( out.s );
            }
            if ( literal( "true" ) )
            {
                out = value_t( true );
                return true;
            }
            if ( literal( "false" ) )
            {
                out = value_t( false );
                return true;
            }
            if ( literal( "null" ) )
            {
                out = value_t( );
                return true;
            }
            char* end = nullptr;
            const double number = std::strtod( text.c_str( ) + at, &end );
            if ( end == text.c_str( ) + at )
                return fail( "unexpected character" );
            at = static_cast<size_t>( end - text.c_str( ) );
            out = value_t( number );
            return true;
        }
    };

    void escape( std::string& out, const std::string& s )
    {
        out += '"';
        for ( const char c : s )
        {
            switch ( c )
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ( static_cast<unsigned char>( c ) < 0x20 )
                {
                    char code[8];
                    std::snprintf( code, sizeof( code ), "\\u%04x", c );
                    out += code;
                }
                else
                    out += c;
            }
        }
        out += '"';
    }

    void emit( std::string& out, const value_t& v, int indent, int depth )
    {
        const std::string pad( static_cast<size_t>( indent * ( depth + 1 ) ), ' ' ), close( static_cast<size_t>( indent * depth ), ' ' );
        switch ( v.kind )
        {
        case value_t::kind_t::null: out += "null"; break;
        case value_t::kind_t::boolean: out += v.b ? "true" : "false"; break;
        case value_t::kind_t::number:
        {
            char number[32];
            if ( v.n == std::floor( v.n ) && std::fabs( v.n ) < 1e15 )
                std::snprintf( number, sizeof( number ), "%.0f", v.n );
            else
                std::snprintf( number, sizeof( number ), "%.6g", v.n );
            out += number;
            break;
        }
        case value_t::kind_t::string: escape( out, v.s ); break;
        case value_t::kind_t::array:
        {
            if ( v.items.empty( ) )
            {
                out += "[]";
                break;
            }
            // Short arrays of plain values stay on one line (colours, option lists).
            bool flat = v.items.size( ) <= 8;
            for ( const auto& item : v.items )
                flat = flat && item.kind != value_t::kind_t::array && item.kind != value_t::kind_t::object;
            out += '[';
            for ( size_t i = 0; i < v.items.size( ); ++i )
            {
                out += flat ? ( i ? " " : "" ) : "\n" + pad;
                emit( out, v.items[i], indent, depth + 1 );
                if ( i + 1 < v.items.size( ) )
                    out += ',';
            }
            out += flat ? "]" : "\n" + close + "]";
            break;
        }
        case value_t::kind_t::object:
        {
            if ( v.fields.empty( ) )
            {
                out += "{}";
                break;
            }
            out += '{';
            for ( size_t i = 0; i < v.fields.size( ); ++i )
            {
                out += "\n" + pad;
                escape( out, v.fields[i].first );
                out += ": ";
                emit( out, v.fields[i].second, indent, depth + 1 );
                if ( i + 1 < v.fields.size( ) )
                    out += ',';
            }
            out += "\n" + close + "}";
            break;
        }
        }
    }
}

auto json::parse( const std::string& text, value_t& out, std::string* error ) -> bool
{
    reader_t reader{ text };
    const bool ok = reader.value( out );
    if ( !ok && error )
        *error = reader.error;
    return ok;
}

auto json::write( const value_t& value, int indent ) -> std::string
{
    std::string out;
    emit( out, value, indent, 0 );
    out += '\n';
    return out;
}
