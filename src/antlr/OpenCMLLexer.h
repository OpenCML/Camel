
// Generated from OpenCML.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"




class  OpenCMLLexer : public antlr4::Lexer {
public:
  enum {
    T__0 = 1, T__1 = 2, T__2 = 3, T__3 = 4, T__4 = 5, T__5 = 6, T__6 = 7, 
    T__7 = 8, T__8 = 9, T__9 = 10, T__10 = 11, T__11 = 12, T__12 = 13, T__13 = 14, 
    T__14 = 15, T__15 = 16, T__16 = 17, T__17 = 18, T__18 = 19, T__19 = 20, 
    T__20 = 21, T__21 = 22, T__22 = 23, T__23 = 24, T__24 = 25, T__25 = 26, 
    T__26 = 27, T__27 = 28, T__28 = 29, T__29 = 30, T__30 = 31, T__31 = 32, 
    T__32 = 33, T__33 = 34, T__34 = 35, T__35 = 36, T__36 = 37, T__37 = 38, 
    T__38 = 39, T__39 = 40, T__40 = 41, T__41 = 42, SEP = 43, AS = 44, IS = 45, 
    LET = 46, USE = 47, FUNC = 48, TYPE = 49, WITH = 50, RETURN = 51, INNER = 52, 
    OUTER = 53, ATOMIC = 54, STATIC = 55, SYNC = 56, NULL_ = 57, TRUE = 58, 
    FALSE = 59, INTEGER_TYPE = 60, INTEGER32_TYPE = 61, INTEGER64_TYPE = 62, 
    REAL_TYPE = 63, FLOAT_TYPE = 64, DOUBLE_TYPE = 65, NUMBER_TYPE = 66, 
    STRING_TYPE = 67, BOOL_TYPE = 68, CHAR_TYPE = 69, SET_TYPE = 70, MAP_TYPE = 71, 
    LIST_TYPE = 72, DICT_TYPE = 73, ARRAY_TYPE = 74, TUPLE_TYPE = 75, UNION_TYPE = 76, 
    VECTOR_TYPE = 77, TENSOR_TYPE = 78, ANY_TYPE = 79, VOID_TYPE = 80, FUNCTOR_TYPE = 81, 
    BLANK = 82, LIN_CMT_HASH = 83, LIN_CMT_SLASH = 84, BLK_CMT = 85, MULTI_STR = 86, 
    IDENTIFIER = 87, UNIT = 88, STRING = 89, FSTRING = 90, INTEGER = 91, 
    REAL = 92
  };

  explicit OpenCMLLexer(antlr4::CharStream *input);

  ~OpenCMLLexer() override;


  std::string getGrammarFileName() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const std::vector<std::string>& getChannelNames() const override;

  const std::vector<std::string>& getModeNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  const antlr4::atn::ATN& getATN() const override;

  // By default the static state used to implement the lexer is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:

  // Individual action functions triggered by action() above.

  // Individual semantic predicate functions triggered by sempred() above.

};

