
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
    T__38 = 39, SEP = 40, AS = 41, IS = 42, LET = 43, USE = 44, FUNC = 45, 
    TYPE = 46, ENUM = 47, WITH = 48, RETURN = 49, INNER = 50, OUTER = 51, 
    SYNC = 52, SCOPED = 53, STATIC = 54, ATOMIC = 55, NULL_ = 56, TRUE = 57, 
    FALSE = 58, ANY_TYPE = 59, VOID_TYPE = 60, INTEGER_TYPE = 61, INTEGER32_TYPE = 62, 
    INTEGER64_TYPE = 63, REAL_TYPE = 64, FLOAT_TYPE = 65, DOUBLE_TYPE = 66, 
    NUMBER_TYPE = 67, STRING_TYPE = 68, BOOLEAN_TYPE = 69, FUNCTOR_TYPE = 70, 
    ARRAY_TYPE = 71, MATRIX_TYPE = 72, LIST_TYPE = 73, DICT_TYPE = 74, SKIP_ = 75, 
    MULTI_STR = 76, IDENTIFIER = 77, UNIT = 78, STRING = 79, FSTRING = 80, 
    INTEGER = 81, REAL = 82
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

