
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
    T__38 = 39, SEP = 40, AS = 41, LET = 42, USE = 43, FUNC = 44, TYPE = 45, 
    ENUM = 46, WITH = 47, RETURN = 48, INNER = 49, OUTER = 50, SYNC = 51, 
    SCOPED = 52, STATIC = 53, ATOMIC = 54, NULL_ = 55, TRUE = 56, FALSE = 57, 
    ANY_TYPE = 58, VOID_TYPE = 59, INTEGER_TYPE = 60, INTEGER32_TYPE = 61, 
    INTEGER64_TYPE = 62, REAL_TYPE = 63, FLOAT_TYPE = 64, DOUBLE_TYPE = 65, 
    NUMBER_TYPE = 66, STRING_TYPE = 67, BOOLEAN_TYPE = 68, FUNCTOR_TYPE = 69, 
    ARRAY_TYPE = 70, MATRIX_TYPE = 71, LIST_TYPE = 72, DICT_TYPE = 73, SKIP_ = 74, 
    MULTI_STR = 75, IDENTIFIER = 76, UNIT = 77, STRING = 78, FSTRING = 79, 
    INTEGER = 80, REAL = 81
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

