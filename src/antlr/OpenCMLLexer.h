
// Generated from OpenCML.g4 by ANTLR 4.13.0

#pragma once


#include "antlr4-runtime.h"




class  OpenCMLLexer : public antlr4::Lexer {
public:
  enum {
    T__0 = 1, T__1 = 2, T__2 = 3, T__3 = 4, T__4 = 5, T__5 = 6, T__6 = 7, 
    T__7 = 8, T__8 = 9, T__9 = 10, T__10 = 11, T__11 = 12, T__12 = 13, T__13 = 14, 
    T__14 = 15, T__15 = 16, T__16 = 17, T__17 = 18, T__18 = 19, AS = 20, 
    LET = 21, USE = 22, FUNC = 23, TYPE = 24, ENUM = 25, WITH = 26, RETURN = 27, 
    INNER = 28, OUTER = 29, SYNC = 30, SCOPED = 31, STATIC = 32, ATOMIC = 33, 
    NULL_ = 34, TRUE = 35, FALSE = 36, ANY_TYPE = 37, NUMBER_TYPE = 38, 
    STRING_TYPE = 39, BOOLEAN_TYPE = 40, FUNCTOR_TYPE = 41, BLOCK_TYPE = 42, 
    INTEGER_TYPE = 43, REAL_TYPE = 44, COMPLEX_TYPE = 45, ARRAY_TYPE = 46, 
    MATRIX_TYPE = 47, LIST_TYPE = 48, DICT_TYPE = 49, SKIP_ = 50, LINE_END = 51, 
    MULTI_STR = 52, IDENTIFIER = 53, UNIT = 54, STRING = 55, FSTRING = 56, 
    INTEGER = 57, REAL = 58
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

