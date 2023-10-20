#ifndef BEE_FISH__JSON_H
#define BEE_FISH__JSON_H

#include "../Parser/Parser.hpp"
#include "../PowerEncoding/PowerEncoding.hpp"
#include "Type.hpp"
#include "Version.hpp"
#include "Object.hpp"
#include "Array.hpp"
#include "Number.hpp"
#include "String.hpp"
#include "BlankSpace.hpp"

namespace BeeFishJSON {

   using namespace BeeFishParser;
  
   class JSON : public Parser
   {
   protected:
      Parser* _params {nullptr};
      And _parser;

   public:
      JSON(Parser* params = nullptr) :
         _params(params),
         _parser(createParser(params))
      {

      }

      JSON(const JSON& source) :
          _params(source._params),
          _parser(createParser(_params))
      {
      }

      Parser* copy() const override {
         return new JSON(*this);
      }

      virtual bool read(char c)
      override
      {

         if (_parser._result != nullopt)
            return false;

         Parser::read(c);

         bool matched = _parser.read(c);

         if (_parser._result != nullopt)
            setResult(_parser._result);

         return matched;
      }

      And createParser(Parser* params) {
      

         static const auto undef =
            Word("undefined");

         static const auto _null =
            Word("null");

         static const auto _true =
            Word("true");

         static const auto _false =
            Word("false");

         static const auto boolean =
            _true or _false;

         return
            blankSpaces and
            (
               undef or
               _null or
               boolean or
               number or
               _string or
               array or
               Object(params)
            );
      }

   };

   Parser* _JSON(Parser* params) {
      return JSON(params).copy();
   }


}

#endif