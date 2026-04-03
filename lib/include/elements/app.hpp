/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License (https://opensource.org/licenses/MIT)
=============================================================================*/
#if !defined(ELEMENTS_APP_MARCH_6_2019)
#define ELEMENTS_APP_MARCH_6_2019

#include <string>
#include <infra/support.hpp>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   // Application class
   ////////////////////////////////////////////////////////////////////////////
   class app : non_copyable
   {
   public:
                           [[deprecated("We no longer use argc, argv and the app id")]]
                           app(int /*argc*/, char* /*argv*/[], std::string name, std::string /*id*/)
                            : app{name}
                           {}

                           app(std::string name);
                           ~app();

      std::string const&   name() const { return _app_name; }
      void                 run();
      void                 stop();

   private:

      bool                 _running = true;
      std::string          _app_name;
   };
}

#endif
