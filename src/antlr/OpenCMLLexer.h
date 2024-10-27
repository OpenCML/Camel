
/**
 * Copyright (c) 2024 Beijing Jiaotong University
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
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
    INNER = 53, OUTER = 54, ATOMIC = 55, SHARED = 56, SYNC = 57, MACRO = 58, 
    NULL_ = 59, TRUE = 60, FALSE = 61, INTEGER_TYPE = 62, INTEGER32_TYPE = 63, 
    INTEGER64_TYPE = 64, REAL_TYPE = 65, FLOAT_TYPE = 66, DOUBLE_TYPE = 67, 
    NUMBER_TYPE = 68, STRING_TYPE = 69, BOOL_TYPE = 70, CHAR_TYPE = 71, 
    SET_TYPE = 72, MAP_TYPE = 73, LIST_TYPE = 74, DICT_TYPE = 75, ARRAY_TYPE = 76, 
    TUPLE_TYPE = 77, UNION_TYPE = 78, VECTOR_TYPE = 79, TENSOR_TYPE = 80, 
    ANY_TYPE = 81, VOID_TYPE = 82, FUNCTOR_TYPE = 83, BLANK = 84, LIN_CMT_HASH = 85, 
    LIN_CMT_SLASH = 86, BLK_CMT = 87, MULTI_STR = 88, IDENTIFIER = 89, UNIT = 90, 
    STRING = 91, FSTRING = 92, INTEGER = 93, REAL = 94
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

