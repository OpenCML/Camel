
/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Supported by: National Key Research and Development Program of China
 */


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
    T__38 = 39, T__39 = 40, SEP = 41, AS = 42, IS = 43, LET = 44, VAR = 45, 
    USE = 46, FROM = 47, FUNC = 48, TYPE = 49, WITH = 50, WAIT = 51, RETURN = 52, 
    INNER = 53, OUTER = 54, ATOMIC = 55, STATIC = 56, SYNC = 57, NULL_ = 58, 
    TRUE = 59, FALSE = 60, INTEGER_TYPE = 61, INTEGER32_TYPE = 62, INTEGER64_TYPE = 63, 
    REAL_TYPE = 64, FLOAT_TYPE = 65, DOUBLE_TYPE = 66, NUMBER_TYPE = 67, 
    STRING_TYPE = 68, BOOL_TYPE = 69, CHAR_TYPE = 70, SET_TYPE = 71, MAP_TYPE = 72, 
    LIST_TYPE = 73, DICT_TYPE = 74, ARRAY_TYPE = 75, TUPLE_TYPE = 76, UNION_TYPE = 77, 
    VECTOR_TYPE = 78, TENSOR_TYPE = 79, ANY_TYPE = 80, VOID_TYPE = 81, FUNCTOR_TYPE = 82, 
    BLANK = 83, LIN_CMT_HASH = 84, LIN_CMT_SLASH = 85, BLK_CMT = 86, MULTI_STR = 87, 
    IDENTIFIER = 88, UNIT = 89, STRING = 90, FSTRING = 91, INTEGER = 92, 
    REAL = 93
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

