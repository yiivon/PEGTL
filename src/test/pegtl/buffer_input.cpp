// Copyright (c) 2019 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/PEGTL/

#include <string>

#include "test.hpp"

#include <tao/pegtl/internal/cstring_reader.hpp>

namespace TAO_PEGTL_NAMESPACE
{

   template< typename Rule,
             template< typename... > class Action = nothing,
             typename... States >
   bool parse_cstring( const char* string, const unsigned line, const std::size_t maximum, States&&... st )
   {
      buffer_input< internal::cstring_reader > in( std::to_string( line ), maximum, string );
      return parse< Rule, Action >( in, st... );
   }

   void unit_test()
   {
      static constexpr std::size_t chunk_size = buffer_input< internal::cstring_reader >::chunk_size;

      static_assert( chunk_size >= 2 );
      TAO_PEGTL_TEST_ASSERT( parse_cstring< seq< string< 'a', 'b', 'c' >, eof > >( "abc", __LINE__, 1 ) );
      TAO_PEGTL_TEST_ASSERT( parse_cstring< seq< string< 'a', 'b', 'c' >, eof > >( "abc", __LINE__, 128 ) );

      // We need one extra byte in the buffer so that eof calling in.empty() calling in.require( 1 ) does not throw a "require beyond end of buffer" exception.
      TAO_PEGTL_TEST_THROWS( parse_cstring< seq< rep< chunk_size + 2, one< 'a' > >, eof > >( std::string( std::size_t( chunk_size + 2 ), 'a' ).c_str(), __LINE__, 2 ) );
      TAO_PEGTL_TEST_ASSERT( parse_cstring< seq< rep< chunk_size + 2, one< 'a' > >, eof > >( std::string( std::size_t( chunk_size + 2 ), 'a' ).c_str(), __LINE__, 3 ) );

      TAO_PEGTL_TEST_ASSERT( parse_cstring< rep< chunk_size + 9, one< 'a' > > >( std::string( std::size_t( chunk_size + 9 ), 'a' ).c_str(), __LINE__, 9 ) );
      TAO_PEGTL_TEST_ASSERT( parse_cstring< rep< chunk_size + 9, one< 'a' > > >( std::string( std::size_t( chunk_size + 10 ), 'a' ).c_str(), __LINE__, 9 ) );
      TAO_PEGTL_TEST_THROWS( parse_cstring< rep< chunk_size + 10, one< 'a' > > >( std::string( std::size_t( chunk_size + 10 ), 'a' ).c_str(), __LINE__, 9 ) );
      TAO_PEGTL_TEST_THROWS( parse_cstring< rep< chunk_size + 10, one< 'a' > > >( std::string( std::size_t( chunk_size + 11 ), 'a' ).c_str(), __LINE__, 9 ) );
      TAO_PEGTL_TEST_THROWS( parse_cstring< seq< rep< chunk_size + 10, one< 'a' > >, eof > >( std::string( std::size_t( chunk_size + 10 ), 'a' ).c_str(), __LINE__, 9 ) );
      TAO_PEGTL_TEST_THROWS( parse_cstring< seq< rep< chunk_size + 10, one< 'a' > >, eof > >( std::string( std::size_t( chunk_size + 10 ), 'a' ).c_str(), __LINE__, 10 ) );
   }

}  // namespace TAO_PEGTL_NAMESPACE

#include "main.hpp"