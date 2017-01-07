// Copyright (c) 2017 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/ColinH/PEGTL/

#ifndef PEGTL_INTERNAL_IF_APPLY_HH
#define PEGTL_INTERNAL_IF_APPLY_HH

#include "skip_control.hh"
#include "rule_match_one.hh"

#include "../action_input.hh"

namespace pegtl
{
   namespace internal
   {
      template< typename Rule, typename ... Actions >
      struct if_apply
      {
         using analyze_t = typename Rule::analyze_t;

         template< apply_mode A, marker_mode M, template< typename ... > class Action, template< typename ... > class Control, typename Input, typename ... States >
         static bool match( Input & in, States && ... st )
         {
            using action_t = typename Input::action_t;

            auto m = in.template mark< marker_mode::ENABLED >();

            if ( rule_match_one< Rule, A, M, Action, Control >::match( in, st ... ) ) {
               const action_t i2( m, in.data() );
               using swallow = bool[];
               (void)swallow{ ( Actions::apply( i2, st ... ), true ) ..., true };
               return m( true );
            }
            return false;
         }
      };

      template< typename Rule, typename ... Actions >
      struct skip_control< if_apply< Rule, Actions ... > > : std::true_type {};

   } // internal

} // pegtl

#endif