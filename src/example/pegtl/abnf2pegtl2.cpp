// Copyright (c) 2018 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/PEGTL/

#include <algorithm>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include <tao/pegtl.hpp>
#include <tao/pegtl/analyze.hpp>
#include <tao/pegtl/contrib/abnf.hpp>
#include <tao/pegtl/contrib/parse_tree.hpp>

namespace tao
{
   namespace TAO_PEGTL_NAMESPACE
   {
      namespace abnf
      {
         namespace
         {
            std::string prefix = "tao::pegtl::";  // NOLINT

            // clang-format off
            std::set< std::string > keywords = {  // NOLINT
               "alignas", "alignof", "and", "and_eq",
               "asm", "auto", "bitand", "bitor",
               "bool", "break", "case", "catch",
               "char", "char16_t", "char32_t", "class",
               "compl", "const", "constexpr", "const_cast",
               "continue", "decltype", "default", "delete",
               "do", "double", "dynamic_cast", "else",
               "enum", "explicit", "export", "extern",
               "false", "float", "for", "friend",
               "goto", "if", "inline", "int",
               "long", "mutable", "namespace", "new",
               "noexcept", "not", "not_eq", "nullptr",
               "operator", "or", "or_eq", "private",
               "protected", "public", "register", "reinterpret_cast",
               "return", "short", "signed", "sizeof",
               "static", "static_assert", "static_cast", "struct",
               "switch", "template", "this", "thread_local",
               "throw", "true", "try", "typedef",
               "typeid", "typename", "union", "unsigned",
               "using", "virtual", "void", "volatile",
               "wchar_t", "while", "xor", "xor_eq"
            };
            // clang-format on

            using rules_t = std::vector< std::string >;
            rules_t rules_defined;  // NOLINT
            rules_t rules;          // NOLINT

            // clang-format off
            struct one_tag {};
            struct string_tag {};
            struct istring_tag {};
            // clang-format on

            rules_t::reverse_iterator find_rule( rules_t& r, const std::string& v, const rules_t::reverse_iterator& rbegin )
            {
               return std::find_if( rbegin, r.rend(), [&]( const rules_t::value_type& p ) { return ::strcasecmp( p.c_str(), v.c_str() ) == 0; } );
            }

            rules_t::reverse_iterator find_rule( rules_t& r, const std::string& v )
            {
               return find_rule( r, v, r.rbegin() );
            }

            bool append_char( std::string& s, const char c )
            {
               if( !s.empty() ) {
                  s += ", ";
               }
               s += '\'';
               if( c == '\'' || c == '\\' ) {
                  s += '\\';
               }
               s += c;
               s += '\'';
               return std::isalpha( c ) != 0;
            }

            std::string remove_leading_zeroes( const std::string& v )
            {
               const auto pos = v.find_first_not_of( '0' );
               if( pos == std::string::npos ) {
                  return "";
               }
               return v.substr( pos );
            }

            void shift( internal::iterator& it, int delta )
            {
               it.data += delta;
               it.byte += delta;
               it.byte_in_line += delta;
            }

         }  // namespace

         struct node;

         std::string to_string( const std::unique_ptr< node >& n );
         std::string to_string( const std::vector< std::unique_ptr< node > >& v );

         namespace grammar
         {
            // ABNF grammar according to RFC 5234, updated by RFC 7405, with
            // the following differences:
            //
            // To form a C++ identifier from a rulename, all minuses are
            // replaced with underscores.
            //
            // As C++ identifiers are case-sensitive, we remember the "correct"
            // spelling from the first occurrence of a rulename, all other
            // occurrences are automatically changed to that.
            //
            // Certain rulenames are reserved as their equivalent C++ identifier is
            // reserved as a keyword, an alternative token, by the standard or
            // for other, special reasons.
            //
            // When using numerical values (num-val, repeat), the values
            // must be in the range of the corresponsing C++ data type.
            //
            // Remember we are defining a PEG, not a CFG. Simply copying some
            // ABNF from somewhere might lead to surprising results as the
            // alternations are now sequential, using the sor<> rule.
            //
            // PEG also require two extensions: the and-predicate and the
            // not-predicate. They are expressed by '&' and '!' respectively,
            // being allowed (optionally, only one of them) before the
            // repetition. You can use braces for more complex expressions.
            //
            // Finally, instead of the pre-defined CRLF sequence, we accept
            // any type of line ending as a convencience extension:

            // clang-format off
            struct CRLF : sor< abnf::CRLF, CR, LF > {};

            // The rest is according to the RFC(s):
            struct comment_cont : until< CRLF, sor< WSP, VCHAR > > {};
            struct comment : if_must< one< ';' >, comment_cont > {};
            struct c_nl : sor< comment, CRLF > {};
            struct c_wsp : sor< WSP, seq< c_nl, WSP > > {};

            struct rulename : seq< ALPHA, star< ranges< 'a', 'z', 'A', 'Z', '0', '9', '-' > > > {};

            struct quoted_string_cont : until< DQUOTE, print > {};
            struct quoted_string : if_must< DQUOTE, quoted_string_cont > {};
            struct case_insensitive_string : seq< opt< istring< '%', 'i' > >, quoted_string > {};
            struct case_sensitive_string : seq< istring< '%', 's' >, quoted_string > {};
            struct char_val : sor< case_insensitive_string, case_sensitive_string > {};

            struct prose_val_cont : until< one< '>' >, print > {};
            struct prose_val : if_must< one< '<' >, prose_val_cont > {};

            template< char First, typename Digit >
            struct gen_val
            {
               struct value : plus< Digit > {};
               struct range : if_must< one< '-' >, value > {};
               struct next_value : must< value > {};
               struct type : seq< istring< First >, must< value >, sor< range, star< one< '.' >, next_value > > > {};
            };

            using hex_val = gen_val< 'x', HEXDIG >;
            using dec_val = gen_val< 'd', DIGIT >;
            using bin_val = gen_val< 'b', BIT >;

            struct num_val_choice : sor< bin_val::type, dec_val::type, hex_val::type > {};
            struct num_val : if_must< one< '%' >, num_val_choice > {};

            struct alternation;
            struct option_close : one< ']' > {};
            struct option : seq< one< '[' >, pad< must< alternation >, c_wsp >, must< option_close > > {};
            struct group_close : one< ')' > {};
            struct group : seq< one< '(' >, pad< must< alternation >, c_wsp >, must< group_close > > {};
            struct element : sor< rulename, group, option, char_val, num_val, prose_val > {};

            struct repeat : sor< seq< star< DIGIT >, one< '*' >, star< DIGIT > >, plus< DIGIT > > {};
            struct repetition : seq< opt< repeat >, element > {};

            struct and_predicate : if_must< one< '&' >, repetition > {};
            struct not_predicate : if_must< one< '!' >, repetition > {};
            struct predicate : sor< and_predicate, not_predicate, repetition > {};

            struct concatenation : list< predicate, plus< c_wsp > > {};
            struct alternation : list_must< concatenation, pad< one< '/' >, c_wsp > > {};

            struct defined_as_op : sor< string< '=', '/' >, one< '=' > > {};
            struct defined_as : pad< defined_as_op, c_wsp > {};
            struct rule : seq< if_must< rulename, defined_as, alternation >, star< c_wsp >, must< c_nl > > {};
            struct rulelist : until< eof, sor< seq< star< c_wsp >, c_nl >, must< rule > > > {};

            // end of grammar

            template< typename Rule >
            struct error_control : normal< Rule >
            {
               static const std::string error_message;

               template< typename Input, typename... States >
               static void raise( const Input& in, States&&... /*unused*/ )
               {
                  throw parse_error( error_message, in );
               }
            };

            template<> const std::string error_control< comment_cont >::error_message = "unterminated comment";  // NOLINT

            template<> const std::string error_control< quoted_string_cont >::error_message = "unterminated string (missing '\"')";  // NOLINT
            template<> const std::string error_control< prose_val_cont >::error_message = "unterminated prose description (missing '>')";  // NOLINT

            template<> const std::string error_control< hex_val::value >::error_message = "expected hexadecimal value";  // NOLINT
            template<> const std::string error_control< dec_val::value >::error_message = "expected decimal value";  // NOLINT
            template<> const std::string error_control< bin_val::value >::error_message = "expected binary value";  // NOLINT
            template<> const std::string error_control< num_val_choice >::error_message = "expected base specifier (one of 'bBdDxX')";  // NOLINT

            template<> const std::string error_control< option_close >::error_message = "unterminated option (missing ']')";  // NOLINT
            template<> const std::string error_control< group_close >::error_message = "unterminated group (missing ')')";  // NOLINT

            template<> const std::string error_control< repetition >::error_message = "expected element";  // NOLINT
            template<> const std::string error_control< concatenation >::error_message = "expected element";  // NOLINT
            template<> const std::string error_control< alternation >::error_message = "expected element";  // NOLINT

            template<> const std::string error_control< defined_as >::error_message = "expected '=' or '=/'";  // NOLINT
            template<> const std::string error_control< c_nl >::error_message = "unterminated rule";  // NOLINT
            template<> const std::string error_control< rule >::error_message = "expected rule";  // NOLINT

         }  // namespace grammar

         struct node
            : tao::TAO_PEGTL_NAMESPACE::parse_tree::basic_node< node >
         {
            template< typename... States >
            void emplace_back( std::unique_ptr< node > child, States&&... st );
         };

         struct fold_one : std::true_type
         {
            static void transform( std::unique_ptr< node >& n )
            {
               if( n->size() == 1 ) {
                  n = std::move( n->front() );
               }
            }
         };

         template< typename Rule > struct selector : std::false_type {};
         template<> struct selector< grammar::rulename > : std::true_type {};

         template<> struct selector< grammar::quoted_string > : std::true_type
         {
            static void transform( std::unique_ptr< node >& n )
            {
               shift( n->begin_, 1 );
               shift( n->end_, -1 );

               const std::string content = n->content();
               for( const auto c : content ) {
                  if( std::isalpha( c ) != 0 ) {
                     n->id_ = &typeid( istring_tag );
                     return;
                  }
               }
               if( content.size() == 1 ) {
                  n->id_ = &typeid( one_tag );
               }
               else {
                  n->id_ = &typeid( string_tag );
               }
            }
         };

         template<> struct selector< grammar::case_sensitive_string > : std::true_type
         {
            static void transform( std::unique_ptr< node >& n )
            {
               n = std::move( n->back() );
               if( n->content().size() == 1 ) {
                  n->id_ = &typeid( one_tag );
               }
               else {
                  n->id_ = &typeid( string_tag );
               }
            }
         };

         template<> struct selector< grammar::prose_val > : std::true_type {};
         template<> struct selector< grammar::hex_val::value > : std::true_type {};
         template<> struct selector< grammar::dec_val::value > : std::true_type {};
         template<> struct selector< grammar::bin_val::value > : std::true_type {};
         template<> struct selector< grammar::hex_val::range > : std::true_type {};
         template<> struct selector< grammar::dec_val::range > : std::true_type {};
         template<> struct selector< grammar::bin_val::range > : std::true_type {};
         template<> struct selector< grammar::hex_val::type > : std::true_type {};
         template<> struct selector< grammar::dec_val::type > : std::true_type {};
         template<> struct selector< grammar::bin_val::type > : std::true_type {};
         template<> struct selector< grammar::alternation > : fold_one {};
         template<> struct selector< grammar::option > : std::true_type {};
         template<> struct selector< grammar::group > : fold_one {};
         template<> struct selector< grammar::repeat > : std::true_type {};
         template<> struct selector< grammar::repetition > : fold_one {};
         template<> struct selector< grammar::and_predicate > : std::true_type {};
         template<> struct selector< grammar::not_predicate > : std::true_type {};
         template<> struct selector< grammar::concatenation > : fold_one {};
         template<> struct selector< grammar::defined_as_op > : std::true_type {};
         template<> struct selector< grammar::rule > : std::true_type {};
         // clang-format on

         namespace
         {
            std::string get_rulename( const std::unique_ptr< node >& n )
            {
               assert( n->is< grammar::rulename >() );
               std::string v = n->content();
               std::replace( v.begin(), v.end(), '-', '_' );
               return v;
            }

            std::string get_rulename( const std::unique_ptr< node >& n, const bool print_forward_declarations )
            {
               std::string v = get_rulename( n );
               const auto it = find_rule( rules, v );
               if( it != rules.rend() ) {
                  return *it;
               }
               if( keywords.count( v ) != 0 || v.find( "__" ) != std::string::npos ) {
                  throw std::runtime_error( to_string( n->begin() ) + ": '" + v + "' is a reserved rulename" );  // NOLINT
               }
               if( print_forward_declarations && find_rule( rules_defined, v ) != rules_defined.rend() ) {
                  std::cout << "struct " << v << ";\n";
               }
               rules.push_back( v );
               return v;
            }

            template< typename T >
            std::string gen_val( const std::unique_ptr< node >& n )
            {
               if( n->size() == 2 ) {
                  if( n->back()->is< T >() ) {
                     return prefix + "range< " + to_string( n->front() ) + ", " + to_string( n->back()->front() ) + " >";
                  }
               }
               if( n->size() == 1 ) {
                  return prefix + "one< " + to_string( n->children ) + " >";
               }
               return prefix + "string< " + to_string( n->children ) + " >";
            }

         }  // namespace

         template< typename... States >
         void node::emplace_back( std::unique_ptr< node > child, States&&... st )
         {
            // inserting a rule is handled here since we need access to all previously inserted rules
            if( child->is< grammar::rule >() ) {
               const auto rname = get_rulename( child->front() );
               assert( child->at( 1 )->is< grammar::defined_as_op >() );
               const auto op = child->at( 1 )->content();
               // when we insert a normal rule, we need to check for duplicates
               if( op == "=" ) {
                  for( const auto& n : children ) {
                     if(::strcasecmp( rname.c_str(), abnf::get_rulename( n->front() ).c_str() ) == 0 ) {
                        throw std::runtime_error( to_string( child->begin() ) + ": rule '" + rname + "' is already defined" );  // NOLINT
                     }
                  }
               }
               // if it is an "incremental alternation", we need to consolidate the assigned alternations
               else if( op == "=/" ) {
                  std::size_t i = 0;
                  while( i < this->size() ) {
                     if(::strcasecmp( rname.c_str(), abnf::get_rulename( this->at( i )->front() ).c_str() ) == 0 ) {
                        auto& previous = this->at( i )->back();

                        // if the previous rule does not assign an alternation, create an intermediate alternation and move its assignee into it.
                        if( !previous->is< abnf::grammar::alternation >() ) {
                           std::unique_ptr< node > s( new node );
                           s->id_ = &typeid( abnf::grammar::alternation );
                           s->source_ = previous->source_;
                           s->begin_ = previous->begin_;
                           s->end_ = previous->end_;
                           s->children.emplace_back( std::move( previous ) );
                           previous = std::move( s );
                        }

                        // append all new options to the previous rule's assignee (which now always an alternation)
                        previous->end_ = child->back()->end_;

                        // if the new rule itself contains an alternation, append the individual entries...
                        if( child->back()->is< abnf::grammar::alternation >() ) {
                           for( auto& n : child->back()->children ) {
                              previous->children.emplace_back( std::move( n ) );
                           }
                        }
                        // ...otherwise add the node itself as another option.
                        else {
                           previous->children.emplace_back( std::move( child->back() ) );
                        }

                        // finally, move the previous rule to the current position...
                        child = std::move( this->at( i ) );

                        // ...and remove the previous rule from the list.
                        this->children.erase( this->children.begin() + i );

                        // all OK now
                        break;
                     }
                     ++i;
                  }
                  if( i == this->size() ) {
                     throw std::runtime_error( to_string( child->begin() ) + ": incremental alternation '" + rname + "' without previous rule definition" );  // NOLINT
                  }
               }
               else {
                  throw std::runtime_error( to_string( child->begin() ) + ": invalid operator '" + op + "', this should not happen!" );  // NOLINT
               }
            }

            // perform the normal emplace_back operation by forwarding to the original method
            tao::TAO_PEGTL_NAMESPACE::parse_tree::basic_node< node >::emplace_back( std::move( child ), st... );
         }

         std::string to_string( const std::unique_ptr< node >& n )
         {
            // rulename
            if( n->is< grammar::rulename >() ) {
               return get_rulename( n, true );
            }

            // string
            if( n->is< string_tag >() ) {
               const std::string content = n->content();
               std::string s;
               for( const auto c : content ) {
                  append_char( s, c );
               }
               return prefix + "string< " + s + " >";
            }

            // istring
            if( n->is< istring_tag >() ) {
               const std::string content = n->content();
               std::string s;
               for( const auto c : content ) {
                  append_char( s, c );
               }
               return prefix + "istring< " + s + " >";
            }

            // one
            if( n->is< one_tag >() ) {
               const std::string content = n->content();
               std::string s;
               for( const auto c : content ) {
                  append_char( s, c );
               }
               return prefix + "one< " + s + " >";
            }

            // prose_val
            if( n->is< grammar::prose_val >() ) {
               return "/* " + n->content() + " */";
            }

            // hex_val::value
            if( n->is< grammar::hex_val::value >() ) {
               return "0x" + n->content();
            }

            // hex_val::type
            if( n->is< grammar::hex_val::type >() ) {
               return gen_val< grammar::hex_val::range >( n );
            }

            // dec_val::value
            if( n->is< grammar::dec_val::value >() ) {
               return n->content();
            }

            // dec_val::type
            if( n->is< grammar::dec_val::type >() ) {
               return gen_val< grammar::dec_val::range >( n );
            }

            // bin_val::value
            if( n->is< grammar::bin_val::value >() ) {
               return std::to_string( std::strtoull( n->content().c_str(), nullptr, 2 ) );
            }

            // bin_val::type
            if( n->is< grammar::bin_val::type >() ) {
               return gen_val< grammar::bin_val::range >( n );
            }

            // alternation
            if( n->is< grammar::alternation >() ) {
               return prefix + "sor< " + to_string( n->children ) + " >";
            }

            // option
            if( n->is< grammar::option >() ) {
               return prefix + "opt< " + to_string( n->children ) + " >";
            }

            // group
            if( n->is< grammar::group >() ) {
               return prefix + "seq< " + to_string( n->children ) + " >";
            }

            // repetition
            if( n->is< grammar::repetition >() ) {
               assert( n->size() == 2 );
               const auto content = to_string( n->back() );
               const auto rep = n->front()->content();
               const auto star = rep.find( '*' );
               if( star == std::string::npos ) {
                  const auto v = remove_leading_zeroes( rep );
                  if( v.empty() ) {
                     throw std::runtime_error( to_string( n->begin() ) + ": repetition of zero not allowed" );  // NOLINT
                  }
                  return prefix + "rep< " + v + ", " + content + " >";
               }
               const auto min = remove_leading_zeroes( rep.substr( 0, star ) );
               const auto max = remove_leading_zeroes( rep.substr( star + 1 ) );
               if( ( star != rep.size() - 1 ) && max.empty() ) {
                  throw std::runtime_error( to_string( n->begin() ) + ": repetition maximum of zero not allowed" );  // NOLINT
               }
               if( min.empty() && max.empty() ) {
                  return prefix + "star< " + content + " >";
               }
               if( !min.empty() && max.empty() ) {
                  if( min == "1" ) {
                     return prefix + "plus< " + content + " >";
                  }
                  return prefix + "rep_min< " + min + ", " + content + " >";
               }
               if( min.empty() && !max.empty() ) {
                  if( max == "1" ) {
                     return prefix + "opt< " + content + " >";
                  }
                  return prefix + "rep_max< " + max + ", " + content + " >";
               }
               const auto min_val = std::strtoull( min.c_str(), nullptr, 10 );
               const auto max_val = std::strtoull( max.c_str(), nullptr, 10 );
               if( min_val > max_val ) {
                  throw std::runtime_error( to_string( n->begin() ) + ": repetition minimum which is greater than the repetition maximum not allowed" );  // NOLINT
               }
               const auto min_element = ( min_val == 1 ) ? content : ( prefix + "rep< " + min + ", " + content + " >" );
               if( min_val == max_val ) {
                  return min_element;
               }
               const auto max_element = prefix + ( ( max_val - min_val == 1 ) ? "opt< " : ( "rep_opt< " + std::to_string( max_val - min_val ) + ", " ) ) + content + " >";
               return prefix + "seq< " + min_element + ", " + max_element + " >";
            }

            // and_predicate
            if( n->is< grammar::and_predicate >() ) {
               assert( n->size() == 1 );
               return prefix + "at< " + to_string( n->front() ) + " >";
            }

            // not_predicate
            if( n->is< grammar::not_predicate >() ) {
               assert( n->size() == 1 );
               return prefix + "not_at< " + to_string( n->front() ) + " >";
            }

            // concatenation
            if( n->is< grammar::concatenation >() ) {
               assert( !n->empty() );
               return prefix + "seq< " + to_string( n->children ) + " >";
            }

            // rule
            if( n->is< grammar::rule >() ) {
               return "struct " + get_rulename( n->front(), false ) + " : " + to_string( n->back() ) + " {};";
            }

            throw std::runtime_error( to_string( n->begin() ) + ": missing to_string() for " + n->name() );  // NOLINT
         }

         std::string to_string( const std::vector< std::unique_ptr< node > >& v )
         {
            std::string result;
            for( const auto& c : v ) {
               if( !result.empty() ) {
                  result += ", ";
               }
               result += to_string( c );
            }
            return result;
         }

      }  // namespace abnf

   }  // namespace TAO_PEGTL_NAMESPACE

}  // namespace tao

int main( int argc, char** argv )
{
   using namespace tao::TAO_PEGTL_NAMESPACE;  // NOLINT

   if( argc != 2 ) {
      analyze< abnf::grammar::rulelist >();
      std::cerr << "Usage: " << argv[ 0 ] << " SOURCE" << std::endl;
      return 1;
   }

   file_input<> in( argv[ 1 ] );
   const auto root = parse_tree::parse< abnf::grammar::rulelist, abnf::node, abnf::selector >( in );

   for( const auto& rule : root->children ) {
      abnf::rules_defined.push_back( abnf::get_rulename( rule->front() ) );
   }

   for( const auto& rule : root->children ) {
      std::cout << abnf::to_string( rule ) << std::endl;
   }

   return 0;
}
