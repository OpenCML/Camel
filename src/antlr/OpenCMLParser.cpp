
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


#include "OpenCMLVisitor.h"

#include "OpenCMLParser.h"


using namespace antlrcpp;

using namespace antlr4;

namespace {

struct OpenCMLParserStaticData final {
  OpenCMLParserStaticData(std::vector<std::string> ruleNames,
                        std::vector<std::string> literalNames,
                        std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  OpenCMLParserStaticData(const OpenCMLParserStaticData&) = delete;
  OpenCMLParserStaticData(OpenCMLParserStaticData&&) = delete;
  OpenCMLParserStaticData& operator=(const OpenCMLParserStaticData&) = delete;
  OpenCMLParserStaticData& operator=(OpenCMLParserStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag opencmlParserOnceFlag;
#if ANTLR4_USE_THREAD_LOCAL_CACHE
static thread_local
#endif
OpenCMLParserStaticData *opencmlParserStaticData = nullptr;

void opencmlParserInitialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  if (opencmlParserStaticData != nullptr) {
    return;
  }
#else
  assert(opencmlParserStaticData == nullptr);
#endif
  auto staticData = std::make_unique<OpenCMLParserStaticData>(
    std::vector<std::string>{
      "program", "stmtList", "stmt", "letStmt", "useStmt", "typeStmt", "exprStmt", 
      "assignStmt", "withDef", "funcDef", "retStmt", "lambdaExpr", "carrier", 
      "annotation", "annotations", "modifiers", "keyTypePair", "keyValuePair", 
      "keyParamPair", "typeList", "identList", "valueList", "pairedTypes", 
      "pairedValues", "pairedParams", "argumentList", "bracedPairedValues", 
      "bracedIdents", "bracedStmts", "bracketIdents", "bracketValues", "parentParams", 
      "parentValues", "angledParams", "angledValues", "memberAccess", "primEntity", 
      "entityChain", "entityLink", "entityUnit", "entityWith", "entityExpr", 
      "relaExpr", "addExpr", "multiExpr", "unaryExpr", "primExpr", "literal", 
      "typeExpr", "type", "lambdaType", "primType", "structType", "specialType", 
      "identRef"
    },
    std::vector<std::string>{
      "", "':'", "'='", "':='", "'::'", "'=>'", "'{'", "'}'", "'@'", "','", 
      "'['", "']'", "'('", "')'", "'<'", "'>'", "'->'", "'.'", "'+='", "'-='", 
      "'*='", "'/='", "'%='", "'^='", "'&='", "'|='", "'<='", "'>='", "'=='", 
      "'!='", "'&&'", "'||'", "'+'", "'-'", "'&'", "'|'", "'^'", "'*'", 
      "'/'", "'%'", "'!'", "'~'", "';'", "'as'", "'is'", "'let'", "'use'", 
      "'func'", "'type'", "'with'", "'return'", "'inner'", "'outer'", "'atomic'", 
      "'static'", "'sync'", "'null'", "'true'", "'false'", "'Int'", "'Int32'", 
      "'Int64'", "'Real'", "'Float'", "'Double'", "'Number'", "'String'", 
      "'Bool'", "'Char'", "'Set'", "'Map'", "'List'", "'Dict'", "'Array'", 
      "'Tuple'", "'Union'", "'Vector'", "'Tensor'", "'Any'", "'Void'", "'Functor'"
    },
    std::vector<std::string>{
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "SEP", "AS", "IS", "LET", "USE", "FUNC", 
      "TYPE", "WITH", "RETURN", "INNER", "OUTER", "ATOMIC", "STATIC", "SYNC", 
      "NULL", "TRUE", "FALSE", "INTEGER_TYPE", "INTEGER32_TYPE", "INTEGER64_TYPE", 
      "REAL_TYPE", "FLOAT_TYPE", "DOUBLE_TYPE", "NUMBER_TYPE", "STRING_TYPE", 
      "BOOL_TYPE", "CHAR_TYPE", "SET_TYPE", "MAP_TYPE", "LIST_TYPE", "DICT_TYPE", 
      "ARRAY_TYPE", "TUPLE_TYPE", "UNION_TYPE", "VECTOR_TYPE", "TENSOR_TYPE", 
      "ANY_TYPE", "VOID_TYPE", "FUNCTOR_TYPE", "BLANK", "LIN_CMT_HASH", 
      "LIN_CMT_SLASH", "BLK_CMT", "MULTI_STR", "IDENTIFIER", "UNIT", "STRING", 
      "FSTRING", "INTEGER", "REAL"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,91,745,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,1,0,3,0,112,8,0,
  	1,0,1,0,1,1,1,1,1,1,5,1,119,8,1,10,1,12,1,122,9,1,1,1,3,1,125,8,1,1,2,
  	1,2,1,2,1,2,1,2,1,2,1,2,3,2,134,8,2,1,3,1,3,1,3,1,3,3,3,140,8,3,1,3,3,
  	3,143,8,3,1,3,3,3,146,8,3,1,3,1,3,1,3,3,3,151,8,3,1,3,1,3,1,3,3,3,156,
  	8,3,1,4,1,4,1,4,3,4,161,8,4,1,4,1,4,1,4,1,4,1,4,1,4,3,4,169,8,4,1,5,1,
  	5,1,5,3,5,174,8,5,1,5,1,5,1,6,3,6,179,8,6,1,6,1,6,1,7,1,7,5,7,185,8,7,
  	10,7,12,7,188,9,7,1,7,1,7,1,7,1,8,1,8,1,8,1,9,3,9,197,8,9,1,9,3,9,200,
  	8,9,1,9,3,9,203,8,9,1,9,1,9,1,9,1,9,1,9,3,9,210,8,9,1,9,1,9,1,10,1,10,
  	3,10,216,8,10,1,11,3,11,219,8,11,1,11,1,11,1,11,3,11,224,8,11,1,11,1,
  	11,1,11,3,11,229,8,11,1,11,1,11,1,11,1,11,3,11,235,8,11,1,12,1,12,1,12,
  	3,12,240,8,12,1,13,1,13,1,13,1,14,4,14,246,8,14,11,14,12,14,247,1,15,
  	4,15,251,8,15,11,15,12,15,252,1,16,1,16,1,16,1,16,1,17,1,17,1,17,1,17,
  	1,18,1,18,3,18,265,8,18,1,18,1,18,1,18,1,18,3,18,271,8,18,1,19,1,19,1,
  	19,5,19,276,8,19,10,19,12,19,279,9,19,1,20,1,20,1,20,5,20,284,8,20,10,
  	20,12,20,287,9,20,1,21,1,21,1,21,5,21,292,8,21,10,21,12,21,295,9,21,1,
  	22,1,22,1,22,5,22,300,8,22,10,22,12,22,303,9,22,1,23,1,23,1,23,5,23,308,
  	8,23,10,23,12,23,311,9,23,1,24,1,24,1,24,5,24,316,8,24,10,24,12,24,319,
  	9,24,1,25,1,25,1,25,3,25,324,8,25,1,25,3,25,327,8,25,1,26,1,26,3,26,331,
  	8,26,1,26,3,26,334,8,26,1,26,1,26,1,27,1,27,3,27,340,8,27,1,27,3,27,343,
  	8,27,1,27,1,27,1,28,1,28,3,28,349,8,28,1,28,1,28,1,29,1,29,3,29,355,8,
  	29,1,29,3,29,358,8,29,1,29,1,29,1,30,1,30,3,30,364,8,30,1,30,3,30,367,
  	8,30,1,30,1,30,1,31,1,31,3,31,373,8,31,1,31,3,31,376,8,31,1,31,1,31,1,
  	32,1,32,3,32,382,8,32,1,32,3,32,385,8,32,1,32,1,32,1,33,1,33,3,33,391,
  	8,33,1,33,3,33,394,8,33,1,33,1,33,1,34,1,34,3,34,400,8,34,1,34,3,34,403,
  	8,34,1,34,1,34,1,35,1,35,1,35,1,35,1,36,1,36,1,36,1,36,1,36,1,36,1,36,
  	1,36,1,36,3,36,420,8,36,1,37,4,37,423,8,37,11,37,12,37,424,1,38,1,38,
  	1,38,1,38,1,38,1,38,5,38,433,8,38,10,38,12,38,436,9,38,1,39,1,39,1,39,
  	1,39,1,39,3,39,443,8,39,1,39,5,39,446,8,39,10,39,12,39,449,9,39,1,40,
  	1,40,1,40,1,40,1,40,1,40,5,40,457,8,40,10,40,12,40,460,9,40,1,41,1,41,
  	1,41,1,41,1,41,1,41,1,41,1,41,1,41,1,41,1,41,1,41,1,41,1,41,1,41,1,41,
  	1,41,1,41,1,41,1,41,1,41,1,41,1,41,1,41,1,41,1,41,1,41,5,41,489,8,41,
  	10,41,12,41,492,9,41,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,
  	1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,
  	1,42,1,42,1,42,5,42,521,8,42,10,42,12,42,524,9,42,1,43,1,43,1,43,1,43,
  	1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,5,43,541,8,43,
  	10,43,12,43,544,9,43,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,
  	1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,5,44,567,8,44,
  	10,44,12,44,570,9,44,1,45,1,45,1,45,1,45,1,45,3,45,577,8,45,1,46,1,46,
  	1,46,1,46,1,46,3,46,584,8,46,1,47,1,47,3,47,588,8,47,1,47,1,47,3,47,592,
  	8,47,1,47,1,47,1,47,1,47,1,47,1,47,3,47,600,8,47,1,48,1,48,1,48,1,48,
  	3,48,606,8,48,1,48,5,48,609,8,48,10,48,12,48,612,9,48,1,48,1,48,1,48,
  	1,48,1,48,1,48,5,48,620,8,48,10,48,12,48,623,9,48,1,49,1,49,1,49,1,49,
  	1,49,1,49,1,49,1,49,1,49,3,49,634,8,49,1,50,1,50,3,50,638,8,50,1,50,3,
  	50,641,8,50,1,50,1,50,3,50,645,8,50,1,50,1,50,1,50,1,50,1,51,1,51,1,52,
  	1,52,1,52,1,52,1,52,3,52,658,8,52,1,52,1,52,1,52,1,52,1,52,1,52,1,52,
  	3,52,667,8,52,1,52,1,52,1,52,1,52,3,52,673,8,52,1,52,3,52,676,8,52,1,
  	52,1,52,1,52,1,52,1,52,1,52,3,52,684,8,52,1,52,1,52,1,52,3,52,689,8,52,
  	1,52,3,52,692,8,52,1,52,3,52,695,8,52,1,52,1,52,1,52,3,52,700,8,52,1,
  	52,3,52,703,8,52,1,52,3,52,706,8,52,1,52,1,52,1,52,1,52,1,52,3,52,713,
  	8,52,1,52,1,52,3,52,717,8,52,1,52,1,52,1,52,1,52,1,52,1,52,1,52,1,52,
  	5,52,727,8,52,10,52,12,52,730,9,52,1,52,3,52,733,8,52,1,52,1,52,3,52,
  	737,8,52,3,52,739,8,52,1,53,1,53,1,54,1,54,1,54,0,7,76,80,82,84,86,88,
  	96,55,0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,
  	46,48,50,52,54,56,58,60,62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,
  	92,94,96,98,100,102,104,106,108,0,3,1,0,51,55,1,0,59,68,1,0,78,80,835,
  	0,111,1,0,0,0,2,115,1,0,0,0,4,133,1,0,0,0,6,155,1,0,0,0,8,168,1,0,0,0,
  	10,170,1,0,0,0,12,178,1,0,0,0,14,182,1,0,0,0,16,192,1,0,0,0,18,196,1,
  	0,0,0,20,213,1,0,0,0,22,218,1,0,0,0,24,239,1,0,0,0,26,241,1,0,0,0,28,
  	245,1,0,0,0,30,250,1,0,0,0,32,254,1,0,0,0,34,258,1,0,0,0,36,262,1,0,0,
  	0,38,272,1,0,0,0,40,280,1,0,0,0,42,288,1,0,0,0,44,296,1,0,0,0,46,304,
  	1,0,0,0,48,312,1,0,0,0,50,326,1,0,0,0,52,328,1,0,0,0,54,337,1,0,0,0,56,
  	346,1,0,0,0,58,352,1,0,0,0,60,361,1,0,0,0,62,370,1,0,0,0,64,379,1,0,0,
  	0,66,388,1,0,0,0,68,397,1,0,0,0,70,406,1,0,0,0,72,419,1,0,0,0,74,422,
  	1,0,0,0,76,426,1,0,0,0,78,437,1,0,0,0,80,450,1,0,0,0,82,461,1,0,0,0,84,
  	493,1,0,0,0,86,525,1,0,0,0,88,545,1,0,0,0,90,576,1,0,0,0,92,583,1,0,0,
  	0,94,599,1,0,0,0,96,601,1,0,0,0,98,633,1,0,0,0,100,640,1,0,0,0,102,650,
  	1,0,0,0,104,738,1,0,0,0,106,740,1,0,0,0,108,742,1,0,0,0,110,112,3,2,1,
  	0,111,110,1,0,0,0,111,112,1,0,0,0,112,113,1,0,0,0,113,114,5,0,0,1,114,
  	1,1,0,0,0,115,120,3,4,2,0,116,117,5,42,0,0,117,119,3,4,2,0,118,116,1,
  	0,0,0,119,122,1,0,0,0,120,118,1,0,0,0,120,121,1,0,0,0,121,124,1,0,0,0,
  	122,120,1,0,0,0,123,125,5,42,0,0,124,123,1,0,0,0,124,125,1,0,0,0,125,
  	3,1,0,0,0,126,134,3,6,3,0,127,134,3,8,4,0,128,134,3,10,5,0,129,134,3,
  	12,6,0,130,134,3,14,7,0,131,134,3,18,9,0,132,134,3,20,10,0,133,126,1,
  	0,0,0,133,127,1,0,0,0,133,128,1,0,0,0,133,129,1,0,0,0,133,130,1,0,0,0,
  	133,131,1,0,0,0,133,132,1,0,0,0,134,5,1,0,0,0,135,136,5,45,0,0,136,139,
  	3,24,12,0,137,138,5,1,0,0,138,140,3,96,48,0,139,137,1,0,0,0,139,140,1,
  	0,0,0,140,145,1,0,0,0,141,143,5,2,0,0,142,141,1,0,0,0,142,143,1,0,0,0,
  	143,144,1,0,0,0,144,146,3,82,41,0,145,142,1,0,0,0,145,146,1,0,0,0,146,
  	156,1,0,0,0,147,150,3,24,12,0,148,149,5,1,0,0,149,151,3,96,48,0,150,148,
  	1,0,0,0,150,151,1,0,0,0,151,152,1,0,0,0,152,153,5,3,0,0,153,154,3,82,
  	41,0,154,156,1,0,0,0,155,135,1,0,0,0,155,147,1,0,0,0,156,7,1,0,0,0,157,
  	158,5,46,0,0,158,160,3,24,12,0,159,161,5,2,0,0,160,159,1,0,0,0,160,161,
  	1,0,0,0,161,162,1,0,0,0,162,163,3,82,41,0,163,169,1,0,0,0,164,165,3,24,
  	12,0,165,166,5,4,0,0,166,167,3,82,41,0,167,169,1,0,0,0,168,157,1,0,0,
  	0,168,164,1,0,0,0,169,9,1,0,0,0,170,171,5,48,0,0,171,173,3,108,54,0,172,
  	174,5,2,0,0,173,172,1,0,0,0,173,174,1,0,0,0,174,175,1,0,0,0,175,176,3,
  	96,48,0,176,11,1,0,0,0,177,179,3,28,14,0,178,177,1,0,0,0,178,179,1,0,
  	0,0,179,180,1,0,0,0,180,181,3,82,41,0,181,13,1,0,0,0,182,186,3,108,54,
  	0,183,185,3,70,35,0,184,183,1,0,0,0,185,188,1,0,0,0,186,184,1,0,0,0,186,
  	187,1,0,0,0,187,189,1,0,0,0,188,186,1,0,0,0,189,190,5,2,0,0,190,191,3,
  	82,41,0,191,15,1,0,0,0,192,193,5,49,0,0,193,194,3,66,33,0,194,17,1,0,
  	0,0,195,197,3,28,14,0,196,195,1,0,0,0,196,197,1,0,0,0,197,199,1,0,0,0,
  	198,200,3,16,8,0,199,198,1,0,0,0,199,200,1,0,0,0,200,202,1,0,0,0,201,
  	203,3,30,15,0,202,201,1,0,0,0,202,203,1,0,0,0,203,204,1,0,0,0,204,205,
  	5,47,0,0,205,206,3,108,54,0,206,209,3,62,31,0,207,208,5,1,0,0,208,210,
  	3,96,48,0,209,207,1,0,0,0,209,210,1,0,0,0,210,211,1,0,0,0,211,212,3,56,
  	28,0,212,19,1,0,0,0,213,215,5,50,0,0,214,216,3,82,41,0,215,214,1,0,0,
  	0,215,216,1,0,0,0,216,21,1,0,0,0,217,219,3,30,15,0,218,217,1,0,0,0,218,
  	219,1,0,0,0,219,234,1,0,0,0,220,223,3,62,31,0,221,222,5,1,0,0,222,224,
  	3,96,48,0,223,221,1,0,0,0,223,224,1,0,0,0,224,225,1,0,0,0,225,228,5,5,
  	0,0,226,229,3,56,28,0,227,229,3,82,41,0,228,226,1,0,0,0,228,227,1,0,0,
  	0,229,235,1,0,0,0,230,231,5,6,0,0,231,232,3,2,1,0,232,233,5,7,0,0,233,
  	235,1,0,0,0,234,220,1,0,0,0,234,230,1,0,0,0,235,23,1,0,0,0,236,240,3,
  	108,54,0,237,240,3,54,27,0,238,240,3,58,29,0,239,236,1,0,0,0,239,237,
  	1,0,0,0,239,238,1,0,0,0,240,25,1,0,0,0,241,242,5,8,0,0,242,243,3,72,36,
  	0,243,27,1,0,0,0,244,246,3,26,13,0,245,244,1,0,0,0,246,247,1,0,0,0,247,
  	245,1,0,0,0,247,248,1,0,0,0,248,29,1,0,0,0,249,251,7,0,0,0,250,249,1,
  	0,0,0,251,252,1,0,0,0,252,250,1,0,0,0,252,253,1,0,0,0,253,31,1,0,0,0,
  	254,255,3,108,54,0,255,256,5,1,0,0,256,257,3,96,48,0,257,33,1,0,0,0,258,
  	259,3,108,54,0,259,260,5,1,0,0,260,261,3,82,41,0,261,35,1,0,0,0,262,264,
  	3,108,54,0,263,265,3,26,13,0,264,263,1,0,0,0,264,265,1,0,0,0,265,266,
  	1,0,0,0,266,267,5,1,0,0,267,270,3,96,48,0,268,269,5,2,0,0,269,271,3,82,
  	41,0,270,268,1,0,0,0,270,271,1,0,0,0,271,37,1,0,0,0,272,277,3,96,48,0,
  	273,274,5,9,0,0,274,276,3,96,48,0,275,273,1,0,0,0,276,279,1,0,0,0,277,
  	275,1,0,0,0,277,278,1,0,0,0,278,39,1,0,0,0,279,277,1,0,0,0,280,285,3,
  	108,54,0,281,282,5,9,0,0,282,284,3,108,54,0,283,281,1,0,0,0,284,287,1,
  	0,0,0,285,283,1,0,0,0,285,286,1,0,0,0,286,41,1,0,0,0,287,285,1,0,0,0,
  	288,293,3,82,41,0,289,290,5,9,0,0,290,292,3,82,41,0,291,289,1,0,0,0,292,
  	295,1,0,0,0,293,291,1,0,0,0,293,294,1,0,0,0,294,43,1,0,0,0,295,293,1,
  	0,0,0,296,301,3,32,16,0,297,298,5,9,0,0,298,300,3,32,16,0,299,297,1,0,
  	0,0,300,303,1,0,0,0,301,299,1,0,0,0,301,302,1,0,0,0,302,45,1,0,0,0,303,
  	301,1,0,0,0,304,309,3,34,17,0,305,306,5,9,0,0,306,308,3,34,17,0,307,305,
  	1,0,0,0,308,311,1,0,0,0,309,307,1,0,0,0,309,310,1,0,0,0,310,47,1,0,0,
  	0,311,309,1,0,0,0,312,317,3,36,18,0,313,314,5,9,0,0,314,316,3,36,18,0,
  	315,313,1,0,0,0,316,319,1,0,0,0,317,315,1,0,0,0,317,318,1,0,0,0,318,49,
  	1,0,0,0,319,317,1,0,0,0,320,323,3,42,21,0,321,322,5,9,0,0,322,324,3,46,
  	23,0,323,321,1,0,0,0,323,324,1,0,0,0,324,327,1,0,0,0,325,327,3,46,23,
  	0,326,320,1,0,0,0,326,325,1,0,0,0,327,51,1,0,0,0,328,330,5,6,0,0,329,
  	331,3,46,23,0,330,329,1,0,0,0,330,331,1,0,0,0,331,333,1,0,0,0,332,334,
  	5,9,0,0,333,332,1,0,0,0,333,334,1,0,0,0,334,335,1,0,0,0,335,336,5,7,0,
  	0,336,53,1,0,0,0,337,339,5,6,0,0,338,340,3,40,20,0,339,338,1,0,0,0,339,
  	340,1,0,0,0,340,342,1,0,0,0,341,343,5,9,0,0,342,341,1,0,0,0,342,343,1,
  	0,0,0,343,344,1,0,0,0,344,345,5,7,0,0,345,55,1,0,0,0,346,348,5,6,0,0,
  	347,349,3,2,1,0,348,347,1,0,0,0,348,349,1,0,0,0,349,350,1,0,0,0,350,351,
  	5,7,0,0,351,57,1,0,0,0,352,354,5,10,0,0,353,355,3,40,20,0,354,353,1,0,
  	0,0,354,355,1,0,0,0,355,357,1,0,0,0,356,358,5,9,0,0,357,356,1,0,0,0,357,
  	358,1,0,0,0,358,359,1,0,0,0,359,360,5,11,0,0,360,59,1,0,0,0,361,363,5,
  	10,0,0,362,364,3,42,21,0,363,362,1,0,0,0,363,364,1,0,0,0,364,366,1,0,
  	0,0,365,367,5,9,0,0,366,365,1,0,0,0,366,367,1,0,0,0,367,368,1,0,0,0,368,
  	369,5,11,0,0,369,61,1,0,0,0,370,372,5,12,0,0,371,373,3,48,24,0,372,371,
  	1,0,0,0,372,373,1,0,0,0,373,375,1,0,0,0,374,376,5,9,0,0,375,374,1,0,0,
  	0,375,376,1,0,0,0,376,377,1,0,0,0,377,378,5,13,0,0,378,63,1,0,0,0,379,
  	381,5,12,0,0,380,382,3,50,25,0,381,380,1,0,0,0,381,382,1,0,0,0,382,384,
  	1,0,0,0,383,385,5,9,0,0,384,383,1,0,0,0,384,385,1,0,0,0,385,386,1,0,0,
  	0,386,387,5,13,0,0,387,65,1,0,0,0,388,390,5,14,0,0,389,391,3,48,24,0,
  	390,389,1,0,0,0,390,391,1,0,0,0,391,393,1,0,0,0,392,394,5,9,0,0,393,392,
  	1,0,0,0,393,394,1,0,0,0,394,395,1,0,0,0,395,396,5,15,0,0,396,67,1,0,0,
  	0,397,399,5,14,0,0,398,400,3,50,25,0,399,398,1,0,0,0,399,400,1,0,0,0,
  	400,402,1,0,0,0,401,403,5,9,0,0,402,401,1,0,0,0,402,403,1,0,0,0,403,404,
  	1,0,0,0,404,405,5,15,0,0,405,69,1,0,0,0,406,407,5,10,0,0,407,408,3,82,
  	41,0,408,409,5,11,0,0,409,71,1,0,0,0,410,420,3,108,54,0,411,420,3,94,
  	47,0,412,420,3,60,30,0,413,420,3,52,26,0,414,420,3,22,11,0,415,416,5,
  	12,0,0,416,417,3,82,41,0,417,418,5,13,0,0,418,420,1,0,0,0,419,410,1,0,
  	0,0,419,411,1,0,0,0,419,412,1,0,0,0,419,413,1,0,0,0,419,414,1,0,0,0,419,
  	415,1,0,0,0,420,73,1,0,0,0,421,423,3,76,38,0,422,421,1,0,0,0,423,424,
  	1,0,0,0,424,422,1,0,0,0,424,425,1,0,0,0,425,75,1,0,0,0,426,427,6,38,-1,
  	0,427,428,3,78,39,0,428,434,1,0,0,0,429,430,10,1,0,0,430,431,5,16,0,0,
  	431,433,3,78,39,0,432,429,1,0,0,0,433,436,1,0,0,0,434,432,1,0,0,0,434,
  	435,1,0,0,0,435,77,1,0,0,0,436,434,1,0,0,0,437,447,3,80,40,0,438,442,
  	4,39,1,0,439,443,3,70,35,0,440,443,3,68,34,0,441,443,3,64,32,0,442,439,
  	1,0,0,0,442,440,1,0,0,0,442,441,1,0,0,0,443,446,1,0,0,0,444,446,3,26,
  	13,0,445,438,1,0,0,0,445,444,1,0,0,0,446,449,1,0,0,0,447,445,1,0,0,0,
  	447,448,1,0,0,0,448,79,1,0,0,0,449,447,1,0,0,0,450,451,6,40,-1,0,451,
  	452,3,72,36,0,452,458,1,0,0,0,453,454,10,1,0,0,454,455,5,17,0,0,455,457,
  	3,72,36,0,456,453,1,0,0,0,457,460,1,0,0,0,458,456,1,0,0,0,458,459,1,0,
  	0,0,459,81,1,0,0,0,460,458,1,0,0,0,461,462,6,41,-1,0,462,463,3,84,42,
  	0,463,490,1,0,0,0,464,465,10,8,0,0,465,466,5,18,0,0,466,489,3,84,42,0,
  	467,468,10,7,0,0,468,469,5,19,0,0,469,489,3,84,42,0,470,471,10,6,0,0,
  	471,472,5,20,0,0,472,489,3,84,42,0,473,474,10,5,0,0,474,475,5,21,0,0,
  	475,489,3,84,42,0,476,477,10,4,0,0,477,478,5,22,0,0,478,489,3,84,42,0,
  	479,480,10,3,0,0,480,481,5,23,0,0,481,489,3,84,42,0,482,483,10,2,0,0,
  	483,484,5,24,0,0,484,489,3,84,42,0,485,486,10,1,0,0,486,487,5,25,0,0,
  	487,489,3,84,42,0,488,464,1,0,0,0,488,467,1,0,0,0,488,470,1,0,0,0,488,
  	473,1,0,0,0,488,476,1,0,0,0,488,479,1,0,0,0,488,482,1,0,0,0,488,485,1,
  	0,0,0,489,492,1,0,0,0,490,488,1,0,0,0,490,491,1,0,0,0,491,83,1,0,0,0,
  	492,490,1,0,0,0,493,494,6,42,-1,0,494,495,3,86,43,0,495,522,1,0,0,0,496,
  	497,10,8,0,0,497,498,5,14,0,0,498,521,3,86,43,0,499,500,10,7,0,0,500,
  	501,5,15,0,0,501,521,3,86,43,0,502,503,10,6,0,0,503,504,5,26,0,0,504,
  	521,3,86,43,0,505,506,10,5,0,0,506,507,5,27,0,0,507,521,3,86,43,0,508,
  	509,10,4,0,0,509,510,5,28,0,0,510,521,3,86,43,0,511,512,10,3,0,0,512,
  	513,5,29,0,0,513,521,3,86,43,0,514,515,10,2,0,0,515,516,5,30,0,0,516,
  	521,3,86,43,0,517,518,10,1,0,0,518,519,5,31,0,0,519,521,3,86,43,0,520,
  	496,1,0,0,0,520,499,1,0,0,0,520,502,1,0,0,0,520,505,1,0,0,0,520,508,1,
  	0,0,0,520,511,1,0,0,0,520,514,1,0,0,0,520,517,1,0,0,0,521,524,1,0,0,0,
  	522,520,1,0,0,0,522,523,1,0,0,0,523,85,1,0,0,0,524,522,1,0,0,0,525,526,
  	6,43,-1,0,526,527,3,88,44,0,527,542,1,0,0,0,528,529,10,4,0,0,529,530,
  	5,32,0,0,530,541,3,88,44,0,531,532,10,3,0,0,532,533,5,33,0,0,533,541,
  	3,88,44,0,534,535,10,2,0,0,535,536,5,34,0,0,536,541,3,88,44,0,537,538,
  	10,1,0,0,538,539,5,35,0,0,539,541,3,88,44,0,540,528,1,0,0,0,540,531,1,
  	0,0,0,540,534,1,0,0,0,540,537,1,0,0,0,541,544,1,0,0,0,542,540,1,0,0,0,
  	542,543,1,0,0,0,543,87,1,0,0,0,544,542,1,0,0,0,545,546,6,44,-1,0,546,
  	547,3,90,45,0,547,568,1,0,0,0,548,549,10,6,0,0,549,550,5,36,0,0,550,567,
  	3,90,45,0,551,552,10,5,0,0,552,553,5,37,0,0,553,567,3,90,45,0,554,555,
  	10,4,0,0,555,556,5,38,0,0,556,567,3,90,45,0,557,558,10,3,0,0,558,559,
  	5,39,0,0,559,567,3,90,45,0,560,561,10,2,0,0,561,562,5,43,0,0,562,567,
  	3,96,48,0,563,564,10,1,0,0,564,565,5,44,0,0,565,567,3,96,48,0,566,548,
  	1,0,0,0,566,551,1,0,0,0,566,554,1,0,0,0,566,557,1,0,0,0,566,560,1,0,0,
  	0,566,563,1,0,0,0,567,570,1,0,0,0,568,566,1,0,0,0,568,569,1,0,0,0,569,
  	89,1,0,0,0,570,568,1,0,0,0,571,577,3,92,46,0,572,573,5,40,0,0,573,577,
  	3,92,46,0,574,575,5,41,0,0,575,577,3,92,46,0,576,571,1,0,0,0,576,572,
  	1,0,0,0,576,574,1,0,0,0,577,91,1,0,0,0,578,584,3,74,37,0,579,580,5,12,
  	0,0,580,581,3,82,41,0,581,582,5,13,0,0,582,584,1,0,0,0,583,578,1,0,0,
  	0,583,579,1,0,0,0,584,93,1,0,0,0,585,587,5,90,0,0,586,588,5,87,0,0,587,
  	586,1,0,0,0,587,588,1,0,0,0,588,600,1,0,0,0,589,591,5,91,0,0,590,592,
  	5,87,0,0,591,590,1,0,0,0,591,592,1,0,0,0,592,600,1,0,0,0,593,600,5,88,
  	0,0,594,600,5,85,0,0,595,600,5,89,0,0,596,600,5,57,0,0,597,600,5,58,0,
  	0,598,600,5,56,0,0,599,585,1,0,0,0,599,589,1,0,0,0,599,593,1,0,0,0,599,
  	594,1,0,0,0,599,595,1,0,0,0,599,596,1,0,0,0,599,597,1,0,0,0,599,598,1,
  	0,0,0,600,95,1,0,0,0,601,602,6,48,-1,0,602,610,3,98,49,0,603,605,5,10,
  	0,0,604,606,5,90,0,0,605,604,1,0,0,0,605,606,1,0,0,0,606,607,1,0,0,0,
  	607,609,5,11,0,0,608,603,1,0,0,0,609,612,1,0,0,0,610,608,1,0,0,0,610,
  	611,1,0,0,0,611,621,1,0,0,0,612,610,1,0,0,0,613,614,10,2,0,0,614,615,
  	5,35,0,0,615,620,3,96,48,3,616,617,10,1,0,0,617,618,5,34,0,0,618,620,
  	3,96,48,2,619,613,1,0,0,0,619,616,1,0,0,0,620,623,1,0,0,0,621,619,1,0,
  	0,0,621,622,1,0,0,0,622,97,1,0,0,0,623,621,1,0,0,0,624,634,3,102,51,0,
  	625,634,3,104,52,0,626,634,3,106,53,0,627,634,3,108,54,0,628,629,5,12,
  	0,0,629,630,3,96,48,0,630,631,5,13,0,0,631,634,1,0,0,0,632,634,3,100,
  	50,0,633,624,1,0,0,0,633,625,1,0,0,0,633,626,1,0,0,0,633,627,1,0,0,0,
  	633,628,1,0,0,0,633,632,1,0,0,0,634,99,1,0,0,0,635,637,5,14,0,0,636,638,
  	3,48,24,0,637,636,1,0,0,0,637,638,1,0,0,0,638,639,1,0,0,0,639,641,5,15,
  	0,0,640,635,1,0,0,0,640,641,1,0,0,0,641,642,1,0,0,0,642,644,5,12,0,0,
  	643,645,3,48,24,0,644,643,1,0,0,0,644,645,1,0,0,0,645,646,1,0,0,0,646,
  	647,5,13,0,0,647,648,5,5,0,0,648,649,3,96,48,0,649,101,1,0,0,0,650,651,
  	7,1,0,0,651,103,1,0,0,0,652,657,5,69,0,0,653,654,5,14,0,0,654,655,3,96,
  	48,0,655,656,5,15,0,0,656,658,1,0,0,0,657,653,1,0,0,0,657,658,1,0,0,0,
  	658,739,1,0,0,0,659,666,5,70,0,0,660,661,5,14,0,0,661,662,3,96,48,0,662,
  	663,5,9,0,0,663,664,3,96,48,0,664,665,5,15,0,0,665,667,1,0,0,0,666,660,
  	1,0,0,0,666,667,1,0,0,0,667,739,1,0,0,0,668,739,5,71,0,0,669,739,5,72,
  	0,0,670,672,5,6,0,0,671,673,3,44,22,0,672,671,1,0,0,0,672,673,1,0,0,0,
  	673,675,1,0,0,0,674,676,5,9,0,0,675,674,1,0,0,0,675,676,1,0,0,0,676,677,
  	1,0,0,0,677,739,5,7,0,0,678,683,5,73,0,0,679,680,5,14,0,0,680,681,3,96,
  	48,0,681,682,5,15,0,0,682,684,1,0,0,0,683,679,1,0,0,0,683,684,1,0,0,0,
  	684,739,1,0,0,0,685,694,5,74,0,0,686,688,5,14,0,0,687,689,3,38,19,0,688,
  	687,1,0,0,0,688,689,1,0,0,0,689,691,1,0,0,0,690,692,5,9,0,0,691,690,1,
  	0,0,0,691,692,1,0,0,0,692,693,1,0,0,0,693,695,5,15,0,0,694,686,1,0,0,
  	0,694,695,1,0,0,0,695,739,1,0,0,0,696,705,5,75,0,0,697,699,5,14,0,0,698,
  	700,3,38,19,0,699,698,1,0,0,0,699,700,1,0,0,0,700,702,1,0,0,0,701,703,
  	5,9,0,0,702,701,1,0,0,0,702,703,1,0,0,0,703,704,1,0,0,0,704,706,5,15,
  	0,0,705,697,1,0,0,0,705,706,1,0,0,0,706,739,1,0,0,0,707,716,5,76,0,0,
  	708,709,5,14,0,0,709,712,3,96,48,0,710,711,5,9,0,0,711,713,5,90,0,0,712,
  	710,1,0,0,0,712,713,1,0,0,0,713,714,1,0,0,0,714,715,5,15,0,0,715,717,
  	1,0,0,0,716,708,1,0,0,0,716,717,1,0,0,0,717,739,1,0,0,0,718,736,5,77,
  	0,0,719,720,5,14,0,0,720,732,3,96,48,0,721,722,5,9,0,0,722,723,5,10,0,
  	0,723,728,5,90,0,0,724,725,5,9,0,0,725,727,5,90,0,0,726,724,1,0,0,0,727,
  	730,1,0,0,0,728,726,1,0,0,0,728,729,1,0,0,0,729,731,1,0,0,0,730,728,1,
  	0,0,0,731,733,5,11,0,0,732,721,1,0,0,0,732,733,1,0,0,0,733,734,1,0,0,
  	0,734,735,5,15,0,0,735,737,1,0,0,0,736,719,1,0,0,0,736,737,1,0,0,0,737,
  	739,1,0,0,0,738,652,1,0,0,0,738,659,1,0,0,0,738,668,1,0,0,0,738,669,1,
  	0,0,0,738,670,1,0,0,0,738,678,1,0,0,0,738,685,1,0,0,0,738,696,1,0,0,0,
  	738,707,1,0,0,0,738,718,1,0,0,0,739,105,1,0,0,0,740,741,7,2,0,0,741,107,
  	1,0,0,0,742,743,5,86,0,0,743,109,1,0,0,0,98,111,120,124,133,139,142,145,
  	150,155,160,168,173,178,186,196,199,202,209,215,218,223,228,234,239,247,
  	252,264,270,277,285,293,301,309,317,323,326,330,333,339,342,348,354,357,
  	363,366,372,375,381,384,390,393,399,402,419,424,434,442,445,447,458,488,
  	490,520,522,540,542,566,568,576,583,587,591,599,605,610,619,621,633,637,
  	640,644,657,666,672,675,683,688,691,694,699,702,705,712,716,728,732,736,
  	738
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  opencmlParserStaticData = staticData.release();
}

}

OpenCMLParser::OpenCMLParser(TokenStream *input) : OpenCMLParser(input, antlr4::atn::ParserATNSimulatorOptions()) {}

OpenCMLParser::OpenCMLParser(TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options) : Parser(input) {
  OpenCMLParser::initialize();
  _interpreter = new atn::ParserATNSimulator(this, *opencmlParserStaticData->atn, opencmlParserStaticData->decisionToDFA, opencmlParserStaticData->sharedContextCache, options);
}

OpenCMLParser::~OpenCMLParser() {
  delete _interpreter;
}

const atn::ATN& OpenCMLParser::getATN() const {
  return *opencmlParserStaticData->atn;
}

std::string OpenCMLParser::getGrammarFileName() const {
  return "OpenCML.g4";
}

const std::vector<std::string>& OpenCMLParser::getRuleNames() const {
  return opencmlParserStaticData->ruleNames;
}

const dfa::Vocabulary& OpenCMLParser::getVocabulary() const {
  return opencmlParserStaticData->vocabulary;
}

antlr4::atn::SerializedATNView OpenCMLParser::getSerializedATN() const {
  return opencmlParserStaticData->serializedATN;
}


//----------------- ProgramContext ------------------------------------------------------------------

OpenCMLParser::ProgramContext::ProgramContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::ProgramContext::EOF() {
  return getToken(OpenCMLParser::EOF, 0);
}

OpenCMLParser::StmtListContext* OpenCMLParser::ProgramContext::stmtList() {
  return getRuleContext<OpenCMLParser::StmtListContext>(0);
}


size_t OpenCMLParser::ProgramContext::getRuleIndex() const {
  return OpenCMLParser::RuleProgram;
}


std::any OpenCMLParser::ProgramContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitProgram(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ProgramContext* OpenCMLParser::program() {
  ProgramContext *_localctx = _tracker.createInstance<ProgramContext>(_ctx, getState());
  enterRule(_localctx, 0, OpenCMLParser::RuleProgram);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(111);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 576428866466223424) != 0) || ((((_la - 85) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 85)) & 123) != 0)) {
      setState(110);
      stmtList();
    }
    setState(113);
    match(OpenCMLParser::EOF);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StmtListContext ------------------------------------------------------------------

OpenCMLParser::StmtListContext::StmtListContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::StmtContext *> OpenCMLParser::StmtListContext::stmt() {
  return getRuleContexts<OpenCMLParser::StmtContext>();
}

OpenCMLParser::StmtContext* OpenCMLParser::StmtListContext::stmt(size_t i) {
  return getRuleContext<OpenCMLParser::StmtContext>(i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::StmtListContext::SEP() {
  return getTokens(OpenCMLParser::SEP);
}

tree::TerminalNode* OpenCMLParser::StmtListContext::SEP(size_t i) {
  return getToken(OpenCMLParser::SEP, i);
}


size_t OpenCMLParser::StmtListContext::getRuleIndex() const {
  return OpenCMLParser::RuleStmtList;
}


std::any OpenCMLParser::StmtListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitStmtList(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::StmtListContext* OpenCMLParser::stmtList() {
  StmtListContext *_localctx = _tracker.createInstance<StmtListContext>(_ctx, getState());
  enterRule(_localctx, 2, OpenCMLParser::RuleStmtList);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(115);
    stmt();
    setState(120);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 1, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(116);
        match(OpenCMLParser::SEP);
        setState(117);
        stmt(); 
      }
      setState(122);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 1, _ctx);
    }
    setState(124);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SEP) {
      setState(123);
      match(OpenCMLParser::SEP);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StmtContext ------------------------------------------------------------------

OpenCMLParser::StmtContext::StmtContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::LetStmtContext* OpenCMLParser::StmtContext::letStmt() {
  return getRuleContext<OpenCMLParser::LetStmtContext>(0);
}

OpenCMLParser::UseStmtContext* OpenCMLParser::StmtContext::useStmt() {
  return getRuleContext<OpenCMLParser::UseStmtContext>(0);
}

OpenCMLParser::TypeStmtContext* OpenCMLParser::StmtContext::typeStmt() {
  return getRuleContext<OpenCMLParser::TypeStmtContext>(0);
}

OpenCMLParser::ExprStmtContext* OpenCMLParser::StmtContext::exprStmt() {
  return getRuleContext<OpenCMLParser::ExprStmtContext>(0);
}

OpenCMLParser::AssignStmtContext* OpenCMLParser::StmtContext::assignStmt() {
  return getRuleContext<OpenCMLParser::AssignStmtContext>(0);
}

OpenCMLParser::FuncDefContext* OpenCMLParser::StmtContext::funcDef() {
  return getRuleContext<OpenCMLParser::FuncDefContext>(0);
}

OpenCMLParser::RetStmtContext* OpenCMLParser::StmtContext::retStmt() {
  return getRuleContext<OpenCMLParser::RetStmtContext>(0);
}


size_t OpenCMLParser::StmtContext::getRuleIndex() const {
  return OpenCMLParser::RuleStmt;
}


std::any OpenCMLParser::StmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitStmt(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::StmtContext* OpenCMLParser::stmt() {
  StmtContext *_localctx = _tracker.createInstance<StmtContext>(_ctx, getState());
  enterRule(_localctx, 4, OpenCMLParser::RuleStmt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(133);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 3, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(126);
      letStmt();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(127);
      useStmt();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(128);
      typeStmt();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(129);
      exprStmt();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(130);
      assignStmt();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(131);
      funcDef();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(132);
      retStmt();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LetStmtContext ------------------------------------------------------------------

OpenCMLParser::LetStmtContext::LetStmtContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::LetStmtContext::LET() {
  return getToken(OpenCMLParser::LET, 0);
}

OpenCMLParser::CarrierContext* OpenCMLParser::LetStmtContext::carrier() {
  return getRuleContext<OpenCMLParser::CarrierContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::LetStmtContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::LetStmtContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}


size_t OpenCMLParser::LetStmtContext::getRuleIndex() const {
  return OpenCMLParser::RuleLetStmt;
}


std::any OpenCMLParser::LetStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitLetStmt(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::LetStmtContext* OpenCMLParser::letStmt() {
  LetStmtContext *_localctx = _tracker.createInstance<LetStmtContext>(_ctx, getState());
  enterRule(_localctx, 6, OpenCMLParser::RuleLetStmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(155);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::LET: {
        enterOuterAlt(_localctx, 1);
        setState(135);
        match(OpenCMLParser::LET);
        setState(136);
        carrier();
        setState(139);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(137);
          match(OpenCMLParser::T__0);
          setState(138);
          typeExpr(0);
        }
        setState(145);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 574212251024626756) != 0) || ((((_la - 85) & ~ 0x3fULL) == 0) &&
          ((1ULL << (_la - 85)) & 123) != 0)) {
          setState(142);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == OpenCMLParser::T__1) {
            setState(141);
            match(OpenCMLParser::T__1);
          }
          setState(144);
          entityExpr(0);
        }
        break;
      }

      case OpenCMLParser::T__5:
      case OpenCMLParser::T__9:
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(147);
        carrier();
        setState(150);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(148);
          match(OpenCMLParser::T__0);
          setState(149);
          typeExpr(0);
        }
        setState(152);
        match(OpenCMLParser::T__2);
        setState(153);
        entityExpr(0);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UseStmtContext ------------------------------------------------------------------

OpenCMLParser::UseStmtContext::UseStmtContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::UseStmtContext::USE() {
  return getToken(OpenCMLParser::USE, 0);
}

OpenCMLParser::CarrierContext* OpenCMLParser::UseStmtContext::carrier() {
  return getRuleContext<OpenCMLParser::CarrierContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::UseStmtContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}


size_t OpenCMLParser::UseStmtContext::getRuleIndex() const {
  return OpenCMLParser::RuleUseStmt;
}


std::any OpenCMLParser::UseStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitUseStmt(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::UseStmtContext* OpenCMLParser::useStmt() {
  UseStmtContext *_localctx = _tracker.createInstance<UseStmtContext>(_ctx, getState());
  enterRule(_localctx, 8, OpenCMLParser::RuleUseStmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(168);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::USE: {
        enterOuterAlt(_localctx, 1);
        setState(157);
        match(OpenCMLParser::USE);
        setState(158);
        carrier();
        setState(160);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__1) {
          setState(159);
          match(OpenCMLParser::T__1);
        }
        setState(162);
        entityExpr(0);
        break;
      }

      case OpenCMLParser::T__5:
      case OpenCMLParser::T__9:
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(164);
        carrier();
        setState(165);
        match(OpenCMLParser::T__3);
        setState(166);
        entityExpr(0);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TypeStmtContext ------------------------------------------------------------------

OpenCMLParser::TypeStmtContext::TypeStmtContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::TypeStmtContext::TYPE() {
  return getToken(OpenCMLParser::TYPE, 0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::TypeStmtContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::TypeStmtContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}


size_t OpenCMLParser::TypeStmtContext::getRuleIndex() const {
  return OpenCMLParser::RuleTypeStmt;
}


std::any OpenCMLParser::TypeStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitTypeStmt(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::TypeStmtContext* OpenCMLParser::typeStmt() {
  TypeStmtContext *_localctx = _tracker.createInstance<TypeStmtContext>(_ctx, getState());
  enterRule(_localctx, 10, OpenCMLParser::RuleTypeStmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(170);
    match(OpenCMLParser::TYPE);
    setState(171);
    identRef();
    setState(173);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(172);
      match(OpenCMLParser::T__1);
    }
    setState(175);
    typeExpr(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExprStmtContext ------------------------------------------------------------------

OpenCMLParser::ExprStmtContext::ExprStmtContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::EntityExprContext* OpenCMLParser::ExprStmtContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}

OpenCMLParser::AnnotationsContext* OpenCMLParser::ExprStmtContext::annotations() {
  return getRuleContext<OpenCMLParser::AnnotationsContext>(0);
}


size_t OpenCMLParser::ExprStmtContext::getRuleIndex() const {
  return OpenCMLParser::RuleExprStmt;
}


std::any OpenCMLParser::ExprStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitExprStmt(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ExprStmtContext* OpenCMLParser::exprStmt() {
  ExprStmtContext *_localctx = _tracker.createInstance<ExprStmtContext>(_ctx, getState());
  enterRule(_localctx, 12, OpenCMLParser::RuleExprStmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(178);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(177);
      annotations();
    }
    setState(180);
    entityExpr(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AssignStmtContext ------------------------------------------------------------------

OpenCMLParser::AssignStmtContext::AssignStmtContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::AssignStmtContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::AssignStmtContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}

std::vector<OpenCMLParser::MemberAccessContext *> OpenCMLParser::AssignStmtContext::memberAccess() {
  return getRuleContexts<OpenCMLParser::MemberAccessContext>();
}

OpenCMLParser::MemberAccessContext* OpenCMLParser::AssignStmtContext::memberAccess(size_t i) {
  return getRuleContext<OpenCMLParser::MemberAccessContext>(i);
}


size_t OpenCMLParser::AssignStmtContext::getRuleIndex() const {
  return OpenCMLParser::RuleAssignStmt;
}


std::any OpenCMLParser::AssignStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitAssignStmt(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::AssignStmtContext* OpenCMLParser::assignStmt() {
  AssignStmtContext *_localctx = _tracker.createInstance<AssignStmtContext>(_ctx, getState());
  enterRule(_localctx, 14, OpenCMLParser::RuleAssignStmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(182);
    identRef();
    setState(186);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__9) {
      setState(183);
      memberAccess();
      setState(188);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(189);
    match(OpenCMLParser::T__1);
    setState(190);
    entityExpr(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- WithDefContext ------------------------------------------------------------------

OpenCMLParser::WithDefContext::WithDefContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::WithDefContext::WITH() {
  return getToken(OpenCMLParser::WITH, 0);
}

OpenCMLParser::AngledParamsContext* OpenCMLParser::WithDefContext::angledParams() {
  return getRuleContext<OpenCMLParser::AngledParamsContext>(0);
}


size_t OpenCMLParser::WithDefContext::getRuleIndex() const {
  return OpenCMLParser::RuleWithDef;
}


std::any OpenCMLParser::WithDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitWithDef(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::WithDefContext* OpenCMLParser::withDef() {
  WithDefContext *_localctx = _tracker.createInstance<WithDefContext>(_ctx, getState());
  enterRule(_localctx, 16, OpenCMLParser::RuleWithDef);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(192);
    match(OpenCMLParser::WITH);
    setState(193);
    angledParams();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncDefContext ------------------------------------------------------------------

OpenCMLParser::FuncDefContext::FuncDefContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::FuncDefContext::FUNC() {
  return getToken(OpenCMLParser::FUNC, 0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::FuncDefContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::ParentParamsContext* OpenCMLParser::FuncDefContext::parentParams() {
  return getRuleContext<OpenCMLParser::ParentParamsContext>(0);
}

OpenCMLParser::BracedStmtsContext* OpenCMLParser::FuncDefContext::bracedStmts() {
  return getRuleContext<OpenCMLParser::BracedStmtsContext>(0);
}

OpenCMLParser::AnnotationsContext* OpenCMLParser::FuncDefContext::annotations() {
  return getRuleContext<OpenCMLParser::AnnotationsContext>(0);
}

OpenCMLParser::WithDefContext* OpenCMLParser::FuncDefContext::withDef() {
  return getRuleContext<OpenCMLParser::WithDefContext>(0);
}

OpenCMLParser::ModifiersContext* OpenCMLParser::FuncDefContext::modifiers() {
  return getRuleContext<OpenCMLParser::ModifiersContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::FuncDefContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}


size_t OpenCMLParser::FuncDefContext::getRuleIndex() const {
  return OpenCMLParser::RuleFuncDef;
}


std::any OpenCMLParser::FuncDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitFuncDef(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::FuncDefContext* OpenCMLParser::funcDef() {
  FuncDefContext *_localctx = _tracker.createInstance<FuncDefContext>(_ctx, getState());
  enterRule(_localctx, 18, OpenCMLParser::RuleFuncDef);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(196);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(195);
      annotations();
    }
    setState(199);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WITH) {
      setState(198);
      withDef();
    }
    setState(202);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 69805794224242688) != 0)) {
      setState(201);
      modifiers();
    }
    setState(204);
    match(OpenCMLParser::FUNC);
    setState(205);
    identRef();
    setState(206);
    parentParams();
    setState(209);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(207);
      match(OpenCMLParser::T__0);
      setState(208);
      typeExpr(0);
    }
    setState(211);
    bracedStmts();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RetStmtContext ------------------------------------------------------------------

OpenCMLParser::RetStmtContext::RetStmtContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::RetStmtContext::RETURN() {
  return getToken(OpenCMLParser::RETURN, 0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::RetStmtContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}


size_t OpenCMLParser::RetStmtContext::getRuleIndex() const {
  return OpenCMLParser::RuleRetStmt;
}


std::any OpenCMLParser::RetStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitRetStmt(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::RetStmtContext* OpenCMLParser::retStmt() {
  RetStmtContext *_localctx = _tracker.createInstance<RetStmtContext>(_ctx, getState());
  enterRule(_localctx, 20, OpenCMLParser::RuleRetStmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(213);
    match(OpenCMLParser::RETURN);
    setState(215);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 574212251024626752) != 0) || ((((_la - 85) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 85)) & 123) != 0)) {
      setState(214);
      entityExpr(0);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LambdaExprContext ------------------------------------------------------------------

OpenCMLParser::LambdaExprContext::LambdaExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::StmtListContext* OpenCMLParser::LambdaExprContext::stmtList() {
  return getRuleContext<OpenCMLParser::StmtListContext>(0);
}

OpenCMLParser::ModifiersContext* OpenCMLParser::LambdaExprContext::modifiers() {
  return getRuleContext<OpenCMLParser::ModifiersContext>(0);
}

OpenCMLParser::ParentParamsContext* OpenCMLParser::LambdaExprContext::parentParams() {
  return getRuleContext<OpenCMLParser::ParentParamsContext>(0);
}

OpenCMLParser::BracedStmtsContext* OpenCMLParser::LambdaExprContext::bracedStmts() {
  return getRuleContext<OpenCMLParser::BracedStmtsContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::LambdaExprContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::LambdaExprContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}


size_t OpenCMLParser::LambdaExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleLambdaExpr;
}


std::any OpenCMLParser::LambdaExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitLambdaExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::LambdaExprContext* OpenCMLParser::lambdaExpr() {
  LambdaExprContext *_localctx = _tracker.createInstance<LambdaExprContext>(_ctx, getState());
  enterRule(_localctx, 22, OpenCMLParser::RuleLambdaExpr);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(218);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 69805794224242688) != 0)) {
      setState(217);
      modifiers();
    }
    setState(234);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__11: {
        setState(220);
        parentParams();
        setState(223);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(221);
          match(OpenCMLParser::T__0);
          setState(222);
          typeExpr(0);
        }
        setState(225);
        match(OpenCMLParser::T__4);
        setState(228);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 21, _ctx)) {
        case 1: {
          setState(226);
          bracedStmts();
          break;
        }

        case 2: {
          setState(227);
          entityExpr(0);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::T__5: {
        setState(230);
        match(OpenCMLParser::T__5);
        setState(231);
        stmtList();
        setState(232);
        match(OpenCMLParser::T__6);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- CarrierContext ------------------------------------------------------------------

OpenCMLParser::CarrierContext::CarrierContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::CarrierContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::BracedIdentsContext* OpenCMLParser::CarrierContext::bracedIdents() {
  return getRuleContext<OpenCMLParser::BracedIdentsContext>(0);
}

OpenCMLParser::BracketIdentsContext* OpenCMLParser::CarrierContext::bracketIdents() {
  return getRuleContext<OpenCMLParser::BracketIdentsContext>(0);
}


size_t OpenCMLParser::CarrierContext::getRuleIndex() const {
  return OpenCMLParser::RuleCarrier;
}


std::any OpenCMLParser::CarrierContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitCarrier(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::CarrierContext* OpenCMLParser::carrier() {
  CarrierContext *_localctx = _tracker.createInstance<CarrierContext>(_ctx, getState());
  enterRule(_localctx, 24, OpenCMLParser::RuleCarrier);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(239);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(236);
        identRef();
        break;
      }

      case OpenCMLParser::T__5: {
        enterOuterAlt(_localctx, 2);
        setState(237);
        bracedIdents();
        break;
      }

      case OpenCMLParser::T__9: {
        enterOuterAlt(_localctx, 3);
        setState(238);
        bracketIdents();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AnnotationContext ------------------------------------------------------------------

OpenCMLParser::AnnotationContext::AnnotationContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimEntityContext* OpenCMLParser::AnnotationContext::primEntity() {
  return getRuleContext<OpenCMLParser::PrimEntityContext>(0);
}


size_t OpenCMLParser::AnnotationContext::getRuleIndex() const {
  return OpenCMLParser::RuleAnnotation;
}


std::any OpenCMLParser::AnnotationContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitAnnotation(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::AnnotationContext* OpenCMLParser::annotation() {
  AnnotationContext *_localctx = _tracker.createInstance<AnnotationContext>(_ctx, getState());
  enterRule(_localctx, 26, OpenCMLParser::RuleAnnotation);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(241);
    match(OpenCMLParser::T__7);
    setState(242);
    primEntity();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AnnotationsContext ------------------------------------------------------------------

OpenCMLParser::AnnotationsContext::AnnotationsContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::AnnotationContext *> OpenCMLParser::AnnotationsContext::annotation() {
  return getRuleContexts<OpenCMLParser::AnnotationContext>();
}

OpenCMLParser::AnnotationContext* OpenCMLParser::AnnotationsContext::annotation(size_t i) {
  return getRuleContext<OpenCMLParser::AnnotationContext>(i);
}


size_t OpenCMLParser::AnnotationsContext::getRuleIndex() const {
  return OpenCMLParser::RuleAnnotations;
}


std::any OpenCMLParser::AnnotationsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitAnnotations(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::AnnotationsContext* OpenCMLParser::annotations() {
  AnnotationsContext *_localctx = _tracker.createInstance<AnnotationsContext>(_ctx, getState());
  enterRule(_localctx, 28, OpenCMLParser::RuleAnnotations);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(245); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(244);
      annotation();
      setState(247); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == OpenCMLParser::T__7);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ModifiersContext ------------------------------------------------------------------

OpenCMLParser::ModifiersContext::ModifiersContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::INNER() {
  return getTokens(OpenCMLParser::INNER);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::INNER(size_t i) {
  return getToken(OpenCMLParser::INNER, i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::OUTER() {
  return getTokens(OpenCMLParser::OUTER);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::OUTER(size_t i) {
  return getToken(OpenCMLParser::OUTER, i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::ATOMIC() {
  return getTokens(OpenCMLParser::ATOMIC);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::ATOMIC(size_t i) {
  return getToken(OpenCMLParser::ATOMIC, i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::STATIC() {
  return getTokens(OpenCMLParser::STATIC);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::STATIC(size_t i) {
  return getToken(OpenCMLParser::STATIC, i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::SYNC() {
  return getTokens(OpenCMLParser::SYNC);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::SYNC(size_t i) {
  return getToken(OpenCMLParser::SYNC, i);
}


size_t OpenCMLParser::ModifiersContext::getRuleIndex() const {
  return OpenCMLParser::RuleModifiers;
}


std::any OpenCMLParser::ModifiersContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitModifiers(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ModifiersContext* OpenCMLParser::modifiers() {
  ModifiersContext *_localctx = _tracker.createInstance<ModifiersContext>(_ctx, getState());
  enterRule(_localctx, 30, OpenCMLParser::RuleModifiers);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(250); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(249);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 69805794224242688) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(252); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 69805794224242688) != 0));
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- KeyTypePairContext ------------------------------------------------------------------

OpenCMLParser::KeyTypePairContext::KeyTypePairContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::KeyTypePairContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::KeyTypePairContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}


size_t OpenCMLParser::KeyTypePairContext::getRuleIndex() const {
  return OpenCMLParser::RuleKeyTypePair;
}


std::any OpenCMLParser::KeyTypePairContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitKeyTypePair(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::KeyTypePairContext* OpenCMLParser::keyTypePair() {
  KeyTypePairContext *_localctx = _tracker.createInstance<KeyTypePairContext>(_ctx, getState());
  enterRule(_localctx, 32, OpenCMLParser::RuleKeyTypePair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(254);
    identRef();
    setState(255);
    match(OpenCMLParser::T__0);
    setState(256);
    typeExpr(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- KeyValuePairContext ------------------------------------------------------------------

OpenCMLParser::KeyValuePairContext::KeyValuePairContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::KeyValuePairContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::KeyValuePairContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}


size_t OpenCMLParser::KeyValuePairContext::getRuleIndex() const {
  return OpenCMLParser::RuleKeyValuePair;
}


std::any OpenCMLParser::KeyValuePairContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitKeyValuePair(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::KeyValuePairContext* OpenCMLParser::keyValuePair() {
  KeyValuePairContext *_localctx = _tracker.createInstance<KeyValuePairContext>(_ctx, getState());
  enterRule(_localctx, 34, OpenCMLParser::RuleKeyValuePair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(258);
    identRef();
    setState(259);
    match(OpenCMLParser::T__0);
    setState(260);
    entityExpr(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- KeyParamPairContext ------------------------------------------------------------------

OpenCMLParser::KeyParamPairContext::KeyParamPairContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::KeyParamPairContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::KeyParamPairContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

OpenCMLParser::AnnotationContext* OpenCMLParser::KeyParamPairContext::annotation() {
  return getRuleContext<OpenCMLParser::AnnotationContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::KeyParamPairContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}


size_t OpenCMLParser::KeyParamPairContext::getRuleIndex() const {
  return OpenCMLParser::RuleKeyParamPair;
}


std::any OpenCMLParser::KeyParamPairContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitKeyParamPair(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::KeyParamPairContext* OpenCMLParser::keyParamPair() {
  KeyParamPairContext *_localctx = _tracker.createInstance<KeyParamPairContext>(_ctx, getState());
  enterRule(_localctx, 36, OpenCMLParser::RuleKeyParamPair);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(262);
    identRef();
    setState(264);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(263);
      annotation();
    }
    setState(266);
    match(OpenCMLParser::T__0);
    setState(267);
    typeExpr(0);
    setState(270);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(268);
      match(OpenCMLParser::T__1);
      setState(269);
      entityExpr(0);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TypeListContext ------------------------------------------------------------------

OpenCMLParser::TypeListContext::TypeListContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::TypeExprContext *> OpenCMLParser::TypeListContext::typeExpr() {
  return getRuleContexts<OpenCMLParser::TypeExprContext>();
}

OpenCMLParser::TypeExprContext* OpenCMLParser::TypeListContext::typeExpr(size_t i) {
  return getRuleContext<OpenCMLParser::TypeExprContext>(i);
}


size_t OpenCMLParser::TypeListContext::getRuleIndex() const {
  return OpenCMLParser::RuleTypeList;
}


std::any OpenCMLParser::TypeListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitTypeList(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::TypeListContext* OpenCMLParser::typeList() {
  TypeListContext *_localctx = _tracker.createInstance<TypeListContext>(_ctx, getState());
  enterRule(_localctx, 38, OpenCMLParser::RuleTypeList);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(272);
    typeExpr(0);
    setState(277);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(273);
        match(OpenCMLParser::T__8);
        setState(274);
        typeExpr(0); 
      }
      setState(279);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- IdentListContext ------------------------------------------------------------------

OpenCMLParser::IdentListContext::IdentListContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::IdentRefContext *> OpenCMLParser::IdentListContext::identRef() {
  return getRuleContexts<OpenCMLParser::IdentRefContext>();
}

OpenCMLParser::IdentRefContext* OpenCMLParser::IdentListContext::identRef(size_t i) {
  return getRuleContext<OpenCMLParser::IdentRefContext>(i);
}


size_t OpenCMLParser::IdentListContext::getRuleIndex() const {
  return OpenCMLParser::RuleIdentList;
}


std::any OpenCMLParser::IdentListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitIdentList(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::IdentListContext* OpenCMLParser::identList() {
  IdentListContext *_localctx = _tracker.createInstance<IdentListContext>(_ctx, getState());
  enterRule(_localctx, 40, OpenCMLParser::RuleIdentList);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(280);
    identRef();
    setState(285);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(281);
        match(OpenCMLParser::T__8);
        setState(282);
        identRef(); 
      }
      setState(287);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ValueListContext ------------------------------------------------------------------

OpenCMLParser::ValueListContext::ValueListContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::EntityExprContext *> OpenCMLParser::ValueListContext::entityExpr() {
  return getRuleContexts<OpenCMLParser::EntityExprContext>();
}

OpenCMLParser::EntityExprContext* OpenCMLParser::ValueListContext::entityExpr(size_t i) {
  return getRuleContext<OpenCMLParser::EntityExprContext>(i);
}


size_t OpenCMLParser::ValueListContext::getRuleIndex() const {
  return OpenCMLParser::RuleValueList;
}


std::any OpenCMLParser::ValueListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitValueList(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ValueListContext* OpenCMLParser::valueList() {
  ValueListContext *_localctx = _tracker.createInstance<ValueListContext>(_ctx, getState());
  enterRule(_localctx, 42, OpenCMLParser::RuleValueList);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(288);
    entityExpr(0);
    setState(293);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(289);
        match(OpenCMLParser::T__8);
        setState(290);
        entityExpr(0); 
      }
      setState(295);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PairedTypesContext ------------------------------------------------------------------

OpenCMLParser::PairedTypesContext::PairedTypesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::KeyTypePairContext *> OpenCMLParser::PairedTypesContext::keyTypePair() {
  return getRuleContexts<OpenCMLParser::KeyTypePairContext>();
}

OpenCMLParser::KeyTypePairContext* OpenCMLParser::PairedTypesContext::keyTypePair(size_t i) {
  return getRuleContext<OpenCMLParser::KeyTypePairContext>(i);
}


size_t OpenCMLParser::PairedTypesContext::getRuleIndex() const {
  return OpenCMLParser::RulePairedTypes;
}


std::any OpenCMLParser::PairedTypesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitPairedTypes(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::PairedTypesContext* OpenCMLParser::pairedTypes() {
  PairedTypesContext *_localctx = _tracker.createInstance<PairedTypesContext>(_ctx, getState());
  enterRule(_localctx, 44, OpenCMLParser::RulePairedTypes);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(296);
    keyTypePair();
    setState(301);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(297);
        match(OpenCMLParser::T__8);
        setState(298);
        keyTypePair(); 
      }
      setState(303);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PairedValuesContext ------------------------------------------------------------------

OpenCMLParser::PairedValuesContext::PairedValuesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::KeyValuePairContext *> OpenCMLParser::PairedValuesContext::keyValuePair() {
  return getRuleContexts<OpenCMLParser::KeyValuePairContext>();
}

OpenCMLParser::KeyValuePairContext* OpenCMLParser::PairedValuesContext::keyValuePair(size_t i) {
  return getRuleContext<OpenCMLParser::KeyValuePairContext>(i);
}


size_t OpenCMLParser::PairedValuesContext::getRuleIndex() const {
  return OpenCMLParser::RulePairedValues;
}


std::any OpenCMLParser::PairedValuesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitPairedValues(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::PairedValuesContext* OpenCMLParser::pairedValues() {
  PairedValuesContext *_localctx = _tracker.createInstance<PairedValuesContext>(_ctx, getState());
  enterRule(_localctx, 46, OpenCMLParser::RulePairedValues);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(304);
    keyValuePair();
    setState(309);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(305);
        match(OpenCMLParser::T__8);
        setState(306);
        keyValuePair(); 
      }
      setState(311);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PairedParamsContext ------------------------------------------------------------------

OpenCMLParser::PairedParamsContext::PairedParamsContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::KeyParamPairContext *> OpenCMLParser::PairedParamsContext::keyParamPair() {
  return getRuleContexts<OpenCMLParser::KeyParamPairContext>();
}

OpenCMLParser::KeyParamPairContext* OpenCMLParser::PairedParamsContext::keyParamPair(size_t i) {
  return getRuleContext<OpenCMLParser::KeyParamPairContext>(i);
}


size_t OpenCMLParser::PairedParamsContext::getRuleIndex() const {
  return OpenCMLParser::RulePairedParams;
}


std::any OpenCMLParser::PairedParamsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitPairedParams(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::PairedParamsContext* OpenCMLParser::pairedParams() {
  PairedParamsContext *_localctx = _tracker.createInstance<PairedParamsContext>(_ctx, getState());
  enterRule(_localctx, 48, OpenCMLParser::RulePairedParams);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(312);
    keyParamPair();
    setState(317);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(313);
        match(OpenCMLParser::T__8);
        setState(314);
        keyParamPair(); 
      }
      setState(319);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ArgumentListContext ------------------------------------------------------------------

OpenCMLParser::ArgumentListContext::ArgumentListContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ValueListContext* OpenCMLParser::ArgumentListContext::valueList() {
  return getRuleContext<OpenCMLParser::ValueListContext>(0);
}

OpenCMLParser::PairedValuesContext* OpenCMLParser::ArgumentListContext::pairedValues() {
  return getRuleContext<OpenCMLParser::PairedValuesContext>(0);
}


size_t OpenCMLParser::ArgumentListContext::getRuleIndex() const {
  return OpenCMLParser::RuleArgumentList;
}


std::any OpenCMLParser::ArgumentListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitArgumentList(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ArgumentListContext* OpenCMLParser::argumentList() {
  ArgumentListContext *_localctx = _tracker.createInstance<ArgumentListContext>(_ctx, getState());
  enterRule(_localctx, 50, OpenCMLParser::RuleArgumentList);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(326);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(320);
      valueList();
      setState(323);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 34, _ctx)) {
      case 1: {
        setState(321);
        match(OpenCMLParser::T__8);
        setState(322);
        pairedValues();
        break;
      }

      default:
        break;
      }
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(325);
      pairedValues();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BracedPairedValuesContext ------------------------------------------------------------------

OpenCMLParser::BracedPairedValuesContext::BracedPairedValuesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PairedValuesContext* OpenCMLParser::BracedPairedValuesContext::pairedValues() {
  return getRuleContext<OpenCMLParser::PairedValuesContext>(0);
}


size_t OpenCMLParser::BracedPairedValuesContext::getRuleIndex() const {
  return OpenCMLParser::RuleBracedPairedValues;
}


std::any OpenCMLParser::BracedPairedValuesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBracedPairedValues(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BracedPairedValuesContext* OpenCMLParser::bracedPairedValues() {
  BracedPairedValuesContext *_localctx = _tracker.createInstance<BracedPairedValuesContext>(_ctx, getState());
  enterRule(_localctx, 52, OpenCMLParser::RuleBracedPairedValues);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(328);
    match(OpenCMLParser::T__5);
    setState(330);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(329);
      pairedValues();
    }
    setState(333);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(332);
      match(OpenCMLParser::T__8);
    }
    setState(335);
    match(OpenCMLParser::T__6);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BracedIdentsContext ------------------------------------------------------------------

OpenCMLParser::BracedIdentsContext::BracedIdentsContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentListContext* OpenCMLParser::BracedIdentsContext::identList() {
  return getRuleContext<OpenCMLParser::IdentListContext>(0);
}


size_t OpenCMLParser::BracedIdentsContext::getRuleIndex() const {
  return OpenCMLParser::RuleBracedIdents;
}


std::any OpenCMLParser::BracedIdentsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBracedIdents(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BracedIdentsContext* OpenCMLParser::bracedIdents() {
  BracedIdentsContext *_localctx = _tracker.createInstance<BracedIdentsContext>(_ctx, getState());
  enterRule(_localctx, 54, OpenCMLParser::RuleBracedIdents);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(337);
    match(OpenCMLParser::T__5);
    setState(339);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(338);
      identList();
    }
    setState(342);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(341);
      match(OpenCMLParser::T__8);
    }
    setState(344);
    match(OpenCMLParser::T__6);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BracedStmtsContext ------------------------------------------------------------------

OpenCMLParser::BracedStmtsContext::BracedStmtsContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::StmtListContext* OpenCMLParser::BracedStmtsContext::stmtList() {
  return getRuleContext<OpenCMLParser::StmtListContext>(0);
}


size_t OpenCMLParser::BracedStmtsContext::getRuleIndex() const {
  return OpenCMLParser::RuleBracedStmts;
}


std::any OpenCMLParser::BracedStmtsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBracedStmts(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BracedStmtsContext* OpenCMLParser::bracedStmts() {
  BracedStmtsContext *_localctx = _tracker.createInstance<BracedStmtsContext>(_ctx, getState());
  enterRule(_localctx, 56, OpenCMLParser::RuleBracedStmts);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(346);
    match(OpenCMLParser::T__5);
    setState(348);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 576428866466223424) != 0) || ((((_la - 85) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 85)) & 123) != 0)) {
      setState(347);
      stmtList();
    }
    setState(350);
    match(OpenCMLParser::T__6);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BracketIdentsContext ------------------------------------------------------------------

OpenCMLParser::BracketIdentsContext::BracketIdentsContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentListContext* OpenCMLParser::BracketIdentsContext::identList() {
  return getRuleContext<OpenCMLParser::IdentListContext>(0);
}


size_t OpenCMLParser::BracketIdentsContext::getRuleIndex() const {
  return OpenCMLParser::RuleBracketIdents;
}


std::any OpenCMLParser::BracketIdentsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBracketIdents(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BracketIdentsContext* OpenCMLParser::bracketIdents() {
  BracketIdentsContext *_localctx = _tracker.createInstance<BracketIdentsContext>(_ctx, getState());
  enterRule(_localctx, 58, OpenCMLParser::RuleBracketIdents);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(352);
    match(OpenCMLParser::T__9);
    setState(354);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(353);
      identList();
    }
    setState(357);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(356);
      match(OpenCMLParser::T__8);
    }
    setState(359);
    match(OpenCMLParser::T__10);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BracketValuesContext ------------------------------------------------------------------

OpenCMLParser::BracketValuesContext::BracketValuesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ValueListContext* OpenCMLParser::BracketValuesContext::valueList() {
  return getRuleContext<OpenCMLParser::ValueListContext>(0);
}


size_t OpenCMLParser::BracketValuesContext::getRuleIndex() const {
  return OpenCMLParser::RuleBracketValues;
}


std::any OpenCMLParser::BracketValuesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBracketValues(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BracketValuesContext* OpenCMLParser::bracketValues() {
  BracketValuesContext *_localctx = _tracker.createInstance<BracketValuesContext>(_ctx, getState());
  enterRule(_localctx, 60, OpenCMLParser::RuleBracketValues);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(361);
    match(OpenCMLParser::T__9);
    setState(363);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 574212251024626752) != 0) || ((((_la - 85) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 85)) & 123) != 0)) {
      setState(362);
      valueList();
    }
    setState(366);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(365);
      match(OpenCMLParser::T__8);
    }
    setState(368);
    match(OpenCMLParser::T__10);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParentParamsContext ------------------------------------------------------------------

OpenCMLParser::ParentParamsContext::ParentParamsContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PairedParamsContext* OpenCMLParser::ParentParamsContext::pairedParams() {
  return getRuleContext<OpenCMLParser::PairedParamsContext>(0);
}


size_t OpenCMLParser::ParentParamsContext::getRuleIndex() const {
  return OpenCMLParser::RuleParentParams;
}


std::any OpenCMLParser::ParentParamsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitParentParams(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ParentParamsContext* OpenCMLParser::parentParams() {
  ParentParamsContext *_localctx = _tracker.createInstance<ParentParamsContext>(_ctx, getState());
  enterRule(_localctx, 62, OpenCMLParser::RuleParentParams);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(370);
    match(OpenCMLParser::T__11);
    setState(372);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(371);
      pairedParams();
    }
    setState(375);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(374);
      match(OpenCMLParser::T__8);
    }
    setState(377);
    match(OpenCMLParser::T__12);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParentValuesContext ------------------------------------------------------------------

OpenCMLParser::ParentValuesContext::ParentValuesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ArgumentListContext* OpenCMLParser::ParentValuesContext::argumentList() {
  return getRuleContext<OpenCMLParser::ArgumentListContext>(0);
}


size_t OpenCMLParser::ParentValuesContext::getRuleIndex() const {
  return OpenCMLParser::RuleParentValues;
}


std::any OpenCMLParser::ParentValuesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitParentValues(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ParentValuesContext* OpenCMLParser::parentValues() {
  ParentValuesContext *_localctx = _tracker.createInstance<ParentValuesContext>(_ctx, getState());
  enterRule(_localctx, 64, OpenCMLParser::RuleParentValues);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(379);
    match(OpenCMLParser::T__11);
    setState(381);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 574212251024626752) != 0) || ((((_la - 85) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 85)) & 123) != 0)) {
      setState(380);
      argumentList();
    }
    setState(384);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(383);
      match(OpenCMLParser::T__8);
    }
    setState(386);
    match(OpenCMLParser::T__12);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AngledParamsContext ------------------------------------------------------------------

OpenCMLParser::AngledParamsContext::AngledParamsContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PairedParamsContext* OpenCMLParser::AngledParamsContext::pairedParams() {
  return getRuleContext<OpenCMLParser::PairedParamsContext>(0);
}


size_t OpenCMLParser::AngledParamsContext::getRuleIndex() const {
  return OpenCMLParser::RuleAngledParams;
}


std::any OpenCMLParser::AngledParamsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitAngledParams(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::AngledParamsContext* OpenCMLParser::angledParams() {
  AngledParamsContext *_localctx = _tracker.createInstance<AngledParamsContext>(_ctx, getState());
  enterRule(_localctx, 66, OpenCMLParser::RuleAngledParams);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(388);
    match(OpenCMLParser::T__13);
    setState(390);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(389);
      pairedParams();
    }
    setState(393);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(392);
      match(OpenCMLParser::T__8);
    }
    setState(395);
    match(OpenCMLParser::T__14);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AngledValuesContext ------------------------------------------------------------------

OpenCMLParser::AngledValuesContext::AngledValuesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ArgumentListContext* OpenCMLParser::AngledValuesContext::argumentList() {
  return getRuleContext<OpenCMLParser::ArgumentListContext>(0);
}


size_t OpenCMLParser::AngledValuesContext::getRuleIndex() const {
  return OpenCMLParser::RuleAngledValues;
}


std::any OpenCMLParser::AngledValuesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitAngledValues(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::AngledValuesContext* OpenCMLParser::angledValues() {
  AngledValuesContext *_localctx = _tracker.createInstance<AngledValuesContext>(_ctx, getState());
  enterRule(_localctx, 68, OpenCMLParser::RuleAngledValues);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(397);
    match(OpenCMLParser::T__13);
    setState(399);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 574212251024626752) != 0) || ((((_la - 85) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 85)) & 123) != 0)) {
      setState(398);
      argumentList();
    }
    setState(402);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(401);
      match(OpenCMLParser::T__8);
    }
    setState(404);
    match(OpenCMLParser::T__14);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MemberAccessContext ------------------------------------------------------------------

OpenCMLParser::MemberAccessContext::MemberAccessContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::EntityExprContext* OpenCMLParser::MemberAccessContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}


size_t OpenCMLParser::MemberAccessContext::getRuleIndex() const {
  return OpenCMLParser::RuleMemberAccess;
}


std::any OpenCMLParser::MemberAccessContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitMemberAccess(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::MemberAccessContext* OpenCMLParser::memberAccess() {
  MemberAccessContext *_localctx = _tracker.createInstance<MemberAccessContext>(_ctx, getState());
  enterRule(_localctx, 70, OpenCMLParser::RuleMemberAccess);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(406);
    match(OpenCMLParser::T__9);
    setState(407);
    entityExpr(0);
    setState(408);
    match(OpenCMLParser::T__10);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrimEntityContext ------------------------------------------------------------------

OpenCMLParser::PrimEntityContext::PrimEntityContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::PrimEntityContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::LiteralContext* OpenCMLParser::PrimEntityContext::literal() {
  return getRuleContext<OpenCMLParser::LiteralContext>(0);
}

OpenCMLParser::BracketValuesContext* OpenCMLParser::PrimEntityContext::bracketValues() {
  return getRuleContext<OpenCMLParser::BracketValuesContext>(0);
}

OpenCMLParser::BracedPairedValuesContext* OpenCMLParser::PrimEntityContext::bracedPairedValues() {
  return getRuleContext<OpenCMLParser::BracedPairedValuesContext>(0);
}

OpenCMLParser::LambdaExprContext* OpenCMLParser::PrimEntityContext::lambdaExpr() {
  return getRuleContext<OpenCMLParser::LambdaExprContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::PrimEntityContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}


size_t OpenCMLParser::PrimEntityContext::getRuleIndex() const {
  return OpenCMLParser::RulePrimEntity;
}


std::any OpenCMLParser::PrimEntityContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitPrimEntity(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::PrimEntityContext* OpenCMLParser::primEntity() {
  PrimEntityContext *_localctx = _tracker.createInstance<PrimEntityContext>(_ctx, getState());
  enterRule(_localctx, 72, OpenCMLParser::RulePrimEntity);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(419);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 53, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(410);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(411);
      literal();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(412);
      bracketValues();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(413);
      bracedPairedValues();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(414);
      lambdaExpr();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(415);
      match(OpenCMLParser::T__11);
      setState(416);
      entityExpr(0);
      setState(417);
      match(OpenCMLParser::T__12);
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- EntityChainContext ------------------------------------------------------------------

OpenCMLParser::EntityChainContext::EntityChainContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::EntityLinkContext *> OpenCMLParser::EntityChainContext::entityLink() {
  return getRuleContexts<OpenCMLParser::EntityLinkContext>();
}

OpenCMLParser::EntityLinkContext* OpenCMLParser::EntityChainContext::entityLink(size_t i) {
  return getRuleContext<OpenCMLParser::EntityLinkContext>(i);
}


size_t OpenCMLParser::EntityChainContext::getRuleIndex() const {
  return OpenCMLParser::RuleEntityChain;
}


std::any OpenCMLParser::EntityChainContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEntityChain(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::EntityChainContext* OpenCMLParser::entityChain() {
  EntityChainContext *_localctx = _tracker.createInstance<EntityChainContext>(_ctx, getState());
  enterRule(_localctx, 74, OpenCMLParser::RuleEntityChain);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(422); 
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(421);
              entityLink(0);
              break;
            }

      default:
        throw NoViableAltException(this);
      }
      setState(424); 
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 54, _ctx);
    } while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- EntityLinkContext ------------------------------------------------------------------

OpenCMLParser::EntityLinkContext::EntityLinkContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::EntityUnitContext* OpenCMLParser::EntityLinkContext::entityUnit() {
  return getRuleContext<OpenCMLParser::EntityUnitContext>(0);
}

OpenCMLParser::EntityLinkContext* OpenCMLParser::EntityLinkContext::entityLink() {
  return getRuleContext<OpenCMLParser::EntityLinkContext>(0);
}


size_t OpenCMLParser::EntityLinkContext::getRuleIndex() const {
  return OpenCMLParser::RuleEntityLink;
}


std::any OpenCMLParser::EntityLinkContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEntityLink(this);
  else
    return visitor->visitChildren(this);
}


OpenCMLParser::EntityLinkContext* OpenCMLParser::entityLink() {
   return entityLink(0);
}

OpenCMLParser::EntityLinkContext* OpenCMLParser::entityLink(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::EntityLinkContext *_localctx = _tracker.createInstance<EntityLinkContext>(_ctx, parentState);
  OpenCMLParser::EntityLinkContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 76;
  enterRecursionRule(_localctx, 76, OpenCMLParser::RuleEntityLink, precedence);

    

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(427);
    entityUnit();
    _ctx->stop = _input->LT(-1);
    setState(434);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 55, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EntityLinkContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEntityLink);
        setState(429);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(430);
        match(OpenCMLParser::T__15);
        setState(431);
        entityUnit(); 
      }
      setState(436);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 55, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- EntityUnitContext ------------------------------------------------------------------

OpenCMLParser::EntityUnitContext::EntityUnitContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::EntityWithContext* OpenCMLParser::EntityUnitContext::entityWith() {
  return getRuleContext<OpenCMLParser::EntityWithContext>(0);
}

std::vector<OpenCMLParser::AnnotationContext *> OpenCMLParser::EntityUnitContext::annotation() {
  return getRuleContexts<OpenCMLParser::AnnotationContext>();
}

OpenCMLParser::AnnotationContext* OpenCMLParser::EntityUnitContext::annotation(size_t i) {
  return getRuleContext<OpenCMLParser::AnnotationContext>(i);
}

std::vector<OpenCMLParser::MemberAccessContext *> OpenCMLParser::EntityUnitContext::memberAccess() {
  return getRuleContexts<OpenCMLParser::MemberAccessContext>();
}

OpenCMLParser::MemberAccessContext* OpenCMLParser::EntityUnitContext::memberAccess(size_t i) {
  return getRuleContext<OpenCMLParser::MemberAccessContext>(i);
}

std::vector<OpenCMLParser::AngledValuesContext *> OpenCMLParser::EntityUnitContext::angledValues() {
  return getRuleContexts<OpenCMLParser::AngledValuesContext>();
}

OpenCMLParser::AngledValuesContext* OpenCMLParser::EntityUnitContext::angledValues(size_t i) {
  return getRuleContext<OpenCMLParser::AngledValuesContext>(i);
}

std::vector<OpenCMLParser::ParentValuesContext *> OpenCMLParser::EntityUnitContext::parentValues() {
  return getRuleContexts<OpenCMLParser::ParentValuesContext>();
}

OpenCMLParser::ParentValuesContext* OpenCMLParser::EntityUnitContext::parentValues(size_t i) {
  return getRuleContext<OpenCMLParser::ParentValuesContext>(i);
}


size_t OpenCMLParser::EntityUnitContext::getRuleIndex() const {
  return OpenCMLParser::RuleEntityUnit;
}


std::any OpenCMLParser::EntityUnitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEntityUnit(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::EntityUnitContext* OpenCMLParser::entityUnit() {
  EntityUnitContext *_localctx = _tracker.createInstance<EntityUnitContext>(_ctx, getState());
  enterRule(_localctx, 78, OpenCMLParser::RuleEntityUnit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(437);
    entityWith(0);
    setState(447);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 58, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(445);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 57, _ctx)) {
        case 1: {
          setState(438);

          if (!(isAdjacent())) throw FailedPredicateException(this, "isAdjacent()");
          setState(442);
          _errHandler->sync(this);
          switch (_input->LA(1)) {
            case OpenCMLParser::T__9: {
              setState(439);
              memberAccess();
              break;
            }

            case OpenCMLParser::T__13: {
              setState(440);
              angledValues();
              break;
            }

            case OpenCMLParser::T__11: {
              setState(441);
              parentValues();
              break;
            }

          default:
            throw NoViableAltException(this);
          }
          break;
        }

        case 2: {
          setState(444);
          annotation();
          break;
        }

        default:
          break;
        } 
      }
      setState(449);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 58, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- EntityWithContext ------------------------------------------------------------------

OpenCMLParser::EntityWithContext::EntityWithContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimEntityContext* OpenCMLParser::EntityWithContext::primEntity() {
  return getRuleContext<OpenCMLParser::PrimEntityContext>(0);
}

OpenCMLParser::EntityWithContext* OpenCMLParser::EntityWithContext::entityWith() {
  return getRuleContext<OpenCMLParser::EntityWithContext>(0);
}


size_t OpenCMLParser::EntityWithContext::getRuleIndex() const {
  return OpenCMLParser::RuleEntityWith;
}


std::any OpenCMLParser::EntityWithContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEntityWith(this);
  else
    return visitor->visitChildren(this);
}


OpenCMLParser::EntityWithContext* OpenCMLParser::entityWith() {
   return entityWith(0);
}

OpenCMLParser::EntityWithContext* OpenCMLParser::entityWith(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::EntityWithContext *_localctx = _tracker.createInstance<EntityWithContext>(_ctx, parentState);
  OpenCMLParser::EntityWithContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 80;
  enterRecursionRule(_localctx, 80, OpenCMLParser::RuleEntityWith, precedence);

    

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(451);
    primEntity();
    _ctx->stop = _input->LT(-1);
    setState(458);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 59, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EntityWithContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEntityWith);
        setState(453);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(454);
        match(OpenCMLParser::T__16);
        setState(455);
        primEntity(); 
      }
      setState(460);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 59, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- EntityExprContext ------------------------------------------------------------------

OpenCMLParser::EntityExprContext::EntityExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::RelaExprContext* OpenCMLParser::EntityExprContext::relaExpr() {
  return getRuleContext<OpenCMLParser::RelaExprContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::EntityExprContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}


size_t OpenCMLParser::EntityExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleEntityExpr;
}


std::any OpenCMLParser::EntityExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEntityExpr(this);
  else
    return visitor->visitChildren(this);
}


OpenCMLParser::EntityExprContext* OpenCMLParser::entityExpr() {
   return entityExpr(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::entityExpr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::EntityExprContext *_localctx = _tracker.createInstance<EntityExprContext>(_ctx, parentState);
  OpenCMLParser::EntityExprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 82;
  enterRecursionRule(_localctx, 82, OpenCMLParser::RuleEntityExpr, precedence);

    

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(462);
    relaExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(490);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(488);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 60, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(464);

          if (!(precpred(_ctx, 8))) throw FailedPredicateException(this, "precpred(_ctx, 8)");
          setState(465);
          match(OpenCMLParser::T__17);
          setState(466);
          relaExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(467);

          if (!(precpred(_ctx, 7))) throw FailedPredicateException(this, "precpred(_ctx, 7)");
          setState(468);
          match(OpenCMLParser::T__18);
          setState(469);
          relaExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(470);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(471);
          match(OpenCMLParser::T__19);
          setState(472);
          relaExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(473);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(474);
          match(OpenCMLParser::T__20);
          setState(475);
          relaExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(476);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(477);
          match(OpenCMLParser::T__21);
          setState(478);
          relaExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(479);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(480);
          match(OpenCMLParser::T__22);
          setState(481);
          relaExpr(0);
          break;
        }

        case 7: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(482);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(483);
          match(OpenCMLParser::T__23);
          setState(484);
          relaExpr(0);
          break;
        }

        case 8: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(485);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(486);
          match(OpenCMLParser::T__24);
          setState(487);
          relaExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(492);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- RelaExprContext ------------------------------------------------------------------

OpenCMLParser::RelaExprContext::RelaExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::AddExprContext* OpenCMLParser::RelaExprContext::addExpr() {
  return getRuleContext<OpenCMLParser::AddExprContext>(0);
}

OpenCMLParser::RelaExprContext* OpenCMLParser::RelaExprContext::relaExpr() {
  return getRuleContext<OpenCMLParser::RelaExprContext>(0);
}


size_t OpenCMLParser::RelaExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleRelaExpr;
}


std::any OpenCMLParser::RelaExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitRelaExpr(this);
  else
    return visitor->visitChildren(this);
}


OpenCMLParser::RelaExprContext* OpenCMLParser::relaExpr() {
   return relaExpr(0);
}

OpenCMLParser::RelaExprContext* OpenCMLParser::relaExpr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::RelaExprContext *_localctx = _tracker.createInstance<RelaExprContext>(_ctx, parentState);
  OpenCMLParser::RelaExprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 84;
  enterRecursionRule(_localctx, 84, OpenCMLParser::RuleRelaExpr, precedence);

    

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(494);
    addExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(522);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 63, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(520);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 62, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(496);

          if (!(precpred(_ctx, 8))) throw FailedPredicateException(this, "precpred(_ctx, 8)");
          setState(497);
          match(OpenCMLParser::T__13);
          setState(498);
          addExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(499);

          if (!(precpred(_ctx, 7))) throw FailedPredicateException(this, "precpred(_ctx, 7)");
          setState(500);
          match(OpenCMLParser::T__14);
          setState(501);
          addExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(502);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(503);
          match(OpenCMLParser::T__25);
          setState(504);
          addExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(505);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(506);
          match(OpenCMLParser::T__26);
          setState(507);
          addExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(508);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(509);
          match(OpenCMLParser::T__27);
          setState(510);
          addExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(511);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(512);
          match(OpenCMLParser::T__28);
          setState(513);
          addExpr(0);
          break;
        }

        case 7: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(514);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(515);
          match(OpenCMLParser::T__29);
          setState(516);
          addExpr(0);
          break;
        }

        case 8: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(517);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(518);
          match(OpenCMLParser::T__30);
          setState(519);
          addExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(524);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 63, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- AddExprContext ------------------------------------------------------------------

OpenCMLParser::AddExprContext::AddExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::MultiExprContext* OpenCMLParser::AddExprContext::multiExpr() {
  return getRuleContext<OpenCMLParser::MultiExprContext>(0);
}

OpenCMLParser::AddExprContext* OpenCMLParser::AddExprContext::addExpr() {
  return getRuleContext<OpenCMLParser::AddExprContext>(0);
}


size_t OpenCMLParser::AddExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleAddExpr;
}


std::any OpenCMLParser::AddExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitAddExpr(this);
  else
    return visitor->visitChildren(this);
}


OpenCMLParser::AddExprContext* OpenCMLParser::addExpr() {
   return addExpr(0);
}

OpenCMLParser::AddExprContext* OpenCMLParser::addExpr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::AddExprContext *_localctx = _tracker.createInstance<AddExprContext>(_ctx, parentState);
  OpenCMLParser::AddExprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 86;
  enterRecursionRule(_localctx, 86, OpenCMLParser::RuleAddExpr, precedence);

    

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(526);
    multiExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(542);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 65, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(540);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 64, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(528);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(529);
          match(OpenCMLParser::T__31);
          setState(530);
          multiExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(531);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(532);
          match(OpenCMLParser::T__32);
          setState(533);
          multiExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(534);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(535);
          match(OpenCMLParser::T__33);
          setState(536);
          multiExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(537);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(538);
          match(OpenCMLParser::T__34);
          setState(539);
          multiExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(544);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 65, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- MultiExprContext ------------------------------------------------------------------

OpenCMLParser::MultiExprContext::MultiExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::UnaryExprContext* OpenCMLParser::MultiExprContext::unaryExpr() {
  return getRuleContext<OpenCMLParser::UnaryExprContext>(0);
}

OpenCMLParser::MultiExprContext* OpenCMLParser::MultiExprContext::multiExpr() {
  return getRuleContext<OpenCMLParser::MultiExprContext>(0);
}

tree::TerminalNode* OpenCMLParser::MultiExprContext::AS() {
  return getToken(OpenCMLParser::AS, 0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::MultiExprContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

tree::TerminalNode* OpenCMLParser::MultiExprContext::IS() {
  return getToken(OpenCMLParser::IS, 0);
}


size_t OpenCMLParser::MultiExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleMultiExpr;
}


std::any OpenCMLParser::MultiExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitMultiExpr(this);
  else
    return visitor->visitChildren(this);
}


OpenCMLParser::MultiExprContext* OpenCMLParser::multiExpr() {
   return multiExpr(0);
}

OpenCMLParser::MultiExprContext* OpenCMLParser::multiExpr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::MultiExprContext *_localctx = _tracker.createInstance<MultiExprContext>(_ctx, parentState);
  OpenCMLParser::MultiExprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 88;
  enterRecursionRule(_localctx, 88, OpenCMLParser::RuleMultiExpr, precedence);

    

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(546);
    unaryExpr();
    _ctx->stop = _input->LT(-1);
    setState(568);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 67, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(566);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 66, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(548);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(549);
          match(OpenCMLParser::T__35);
          setState(550);
          unaryExpr();
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(551);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(552);
          match(OpenCMLParser::T__36);
          setState(553);
          unaryExpr();
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(554);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(555);
          match(OpenCMLParser::T__37);
          setState(556);
          unaryExpr();
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(557);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(558);
          match(OpenCMLParser::T__38);
          setState(559);
          unaryExpr();
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(560);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(561);
          match(OpenCMLParser::AS);
          setState(562);
          typeExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(563);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(564);
          match(OpenCMLParser::IS);
          setState(565);
          typeExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(570);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 67, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- UnaryExprContext ------------------------------------------------------------------

OpenCMLParser::UnaryExprContext::UnaryExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimExprContext* OpenCMLParser::UnaryExprContext::primExpr() {
  return getRuleContext<OpenCMLParser::PrimExprContext>(0);
}


size_t OpenCMLParser::UnaryExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleUnaryExpr;
}


std::any OpenCMLParser::UnaryExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitUnaryExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::UnaryExprContext* OpenCMLParser::unaryExpr() {
  UnaryExprContext *_localctx = _tracker.createInstance<UnaryExprContext>(_ctx, getState());
  enterRule(_localctx, 90, OpenCMLParser::RuleUnaryExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(576);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__5:
      case OpenCMLParser::T__9:
      case OpenCMLParser::T__11:
      case OpenCMLParser::INNER:
      case OpenCMLParser::OUTER:
      case OpenCMLParser::ATOMIC:
      case OpenCMLParser::STATIC:
      case OpenCMLParser::SYNC:
      case OpenCMLParser::NULL_:
      case OpenCMLParser::TRUE:
      case OpenCMLParser::FALSE:
      case OpenCMLParser::MULTI_STR:
      case OpenCMLParser::IDENTIFIER:
      case OpenCMLParser::STRING:
      case OpenCMLParser::FSTRING:
      case OpenCMLParser::INTEGER:
      case OpenCMLParser::REAL: {
        enterOuterAlt(_localctx, 1);
        setState(571);
        primExpr();
        break;
      }

      case OpenCMLParser::T__39: {
        enterOuterAlt(_localctx, 2);
        setState(572);
        match(OpenCMLParser::T__39);
        setState(573);
        primExpr();
        break;
      }

      case OpenCMLParser::T__40: {
        enterOuterAlt(_localctx, 3);
        setState(574);
        match(OpenCMLParser::T__40);
        setState(575);
        primExpr();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrimExprContext ------------------------------------------------------------------

OpenCMLParser::PrimExprContext::PrimExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::EntityChainContext* OpenCMLParser::PrimExprContext::entityChain() {
  return getRuleContext<OpenCMLParser::EntityChainContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::PrimExprContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}


size_t OpenCMLParser::PrimExprContext::getRuleIndex() const {
  return OpenCMLParser::RulePrimExpr;
}


std::any OpenCMLParser::PrimExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitPrimExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::PrimExprContext* OpenCMLParser::primExpr() {
  PrimExprContext *_localctx = _tracker.createInstance<PrimExprContext>(_ctx, getState());
  enterRule(_localctx, 92, OpenCMLParser::RulePrimExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(583);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(578);
      entityChain();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(579);
      match(OpenCMLParser::T__11);
      setState(580);
      entityExpr(0);
      setState(581);
      match(OpenCMLParser::T__12);
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LiteralContext ------------------------------------------------------------------

OpenCMLParser::LiteralContext::LiteralContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::LiteralContext::INTEGER() {
  return getToken(OpenCMLParser::INTEGER, 0);
}

tree::TerminalNode* OpenCMLParser::LiteralContext::UNIT() {
  return getToken(OpenCMLParser::UNIT, 0);
}

tree::TerminalNode* OpenCMLParser::LiteralContext::REAL() {
  return getToken(OpenCMLParser::REAL, 0);
}

tree::TerminalNode* OpenCMLParser::LiteralContext::STRING() {
  return getToken(OpenCMLParser::STRING, 0);
}

tree::TerminalNode* OpenCMLParser::LiteralContext::MULTI_STR() {
  return getToken(OpenCMLParser::MULTI_STR, 0);
}

tree::TerminalNode* OpenCMLParser::LiteralContext::FSTRING() {
  return getToken(OpenCMLParser::FSTRING, 0);
}

tree::TerminalNode* OpenCMLParser::LiteralContext::TRUE() {
  return getToken(OpenCMLParser::TRUE, 0);
}

tree::TerminalNode* OpenCMLParser::LiteralContext::FALSE() {
  return getToken(OpenCMLParser::FALSE, 0);
}

tree::TerminalNode* OpenCMLParser::LiteralContext::NULL_() {
  return getToken(OpenCMLParser::NULL_, 0);
}


size_t OpenCMLParser::LiteralContext::getRuleIndex() const {
  return OpenCMLParser::RuleLiteral;
}


std::any OpenCMLParser::LiteralContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitLiteral(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::LiteralContext* OpenCMLParser::literal() {
  LiteralContext *_localctx = _tracker.createInstance<LiteralContext>(_ctx, getState());
  enterRule(_localctx, 94, OpenCMLParser::RuleLiteral);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(599);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER: {
        enterOuterAlt(_localctx, 1);
        setState(585);
        match(OpenCMLParser::INTEGER);
        setState(587);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx)) {
        case 1: {
          setState(586);
          match(OpenCMLParser::UNIT);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::REAL: {
        enterOuterAlt(_localctx, 2);
        setState(589);
        match(OpenCMLParser::REAL);
        setState(591);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 71, _ctx)) {
        case 1: {
          setState(590);
          match(OpenCMLParser::UNIT);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::STRING: {
        enterOuterAlt(_localctx, 3);
        setState(593);
        match(OpenCMLParser::STRING);
        break;
      }

      case OpenCMLParser::MULTI_STR: {
        enterOuterAlt(_localctx, 4);
        setState(594);
        match(OpenCMLParser::MULTI_STR);
        break;
      }

      case OpenCMLParser::FSTRING: {
        enterOuterAlt(_localctx, 5);
        setState(595);
        match(OpenCMLParser::FSTRING);
        break;
      }

      case OpenCMLParser::TRUE: {
        enterOuterAlt(_localctx, 6);
        setState(596);
        match(OpenCMLParser::TRUE);
        break;
      }

      case OpenCMLParser::FALSE: {
        enterOuterAlt(_localctx, 7);
        setState(597);
        match(OpenCMLParser::FALSE);
        break;
      }

      case OpenCMLParser::NULL_: {
        enterOuterAlt(_localctx, 8);
        setState(598);
        match(OpenCMLParser::NULL_);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TypeExprContext ------------------------------------------------------------------

OpenCMLParser::TypeExprContext::TypeExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::TypeContext* OpenCMLParser::TypeExprContext::type() {
  return getRuleContext<OpenCMLParser::TypeContext>(0);
}

std::vector<tree::TerminalNode *> OpenCMLParser::TypeExprContext::INTEGER() {
  return getTokens(OpenCMLParser::INTEGER);
}

tree::TerminalNode* OpenCMLParser::TypeExprContext::INTEGER(size_t i) {
  return getToken(OpenCMLParser::INTEGER, i);
}

std::vector<OpenCMLParser::TypeExprContext *> OpenCMLParser::TypeExprContext::typeExpr() {
  return getRuleContexts<OpenCMLParser::TypeExprContext>();
}

OpenCMLParser::TypeExprContext* OpenCMLParser::TypeExprContext::typeExpr(size_t i) {
  return getRuleContext<OpenCMLParser::TypeExprContext>(i);
}


size_t OpenCMLParser::TypeExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleTypeExpr;
}


std::any OpenCMLParser::TypeExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitTypeExpr(this);
  else
    return visitor->visitChildren(this);
}


OpenCMLParser::TypeExprContext* OpenCMLParser::typeExpr() {
   return typeExpr(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::typeExpr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::TypeExprContext *_localctx = _tracker.createInstance<TypeExprContext>(_ctx, parentState);
  OpenCMLParser::TypeExprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 96;
  enterRecursionRule(_localctx, 96, OpenCMLParser::RuleTypeExpr, precedence);

    size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(602);
    type();
    setState(610);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(603);
        match(OpenCMLParser::T__9);
        setState(605);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::INTEGER) {
          setState(604);
          match(OpenCMLParser::INTEGER);
        }
        setState(607);
        match(OpenCMLParser::T__10); 
      }
      setState(612);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx);
    }
    _ctx->stop = _input->LT(-1);
    setState(621);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 76, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(619);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 75, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<TypeExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleTypeExpr);
          setState(613);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(614);
          match(OpenCMLParser::T__34);
          setState(615);
          typeExpr(3);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<TypeExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleTypeExpr);
          setState(616);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(617);
          match(OpenCMLParser::T__33);
          setState(618);
          typeExpr(2);
          break;
        }

        default:
          break;
        } 
      }
      setState(623);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 76, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- TypeContext ------------------------------------------------------------------

OpenCMLParser::TypeContext::TypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimTypeContext* OpenCMLParser::TypeContext::primType() {
  return getRuleContext<OpenCMLParser::PrimTypeContext>(0);
}

OpenCMLParser::StructTypeContext* OpenCMLParser::TypeContext::structType() {
  return getRuleContext<OpenCMLParser::StructTypeContext>(0);
}

OpenCMLParser::SpecialTypeContext* OpenCMLParser::TypeContext::specialType() {
  return getRuleContext<OpenCMLParser::SpecialTypeContext>(0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::TypeContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::TypeContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

OpenCMLParser::LambdaTypeContext* OpenCMLParser::TypeContext::lambdaType() {
  return getRuleContext<OpenCMLParser::LambdaTypeContext>(0);
}


size_t OpenCMLParser::TypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleType;
}


std::any OpenCMLParser::TypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::TypeContext* OpenCMLParser::type() {
  TypeContext *_localctx = _tracker.createInstance<TypeContext>(_ctx, getState());
  enterRule(_localctx, 98, OpenCMLParser::RuleType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(633);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 77, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(624);
      primType();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(625);
      structType();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(626);
      specialType();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(627);
      identRef();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(628);
      match(OpenCMLParser::T__11);
      setState(629);
      typeExpr(0);
      setState(630);
      match(OpenCMLParser::T__12);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(632);
      lambdaType();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LambdaTypeContext ------------------------------------------------------------------

OpenCMLParser::LambdaTypeContext::LambdaTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::TypeExprContext* OpenCMLParser::LambdaTypeContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

std::vector<OpenCMLParser::PairedParamsContext *> OpenCMLParser::LambdaTypeContext::pairedParams() {
  return getRuleContexts<OpenCMLParser::PairedParamsContext>();
}

OpenCMLParser::PairedParamsContext* OpenCMLParser::LambdaTypeContext::pairedParams(size_t i) {
  return getRuleContext<OpenCMLParser::PairedParamsContext>(i);
}


size_t OpenCMLParser::LambdaTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleLambdaType;
}


std::any OpenCMLParser::LambdaTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitLambdaType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::LambdaTypeContext* OpenCMLParser::lambdaType() {
  LambdaTypeContext *_localctx = _tracker.createInstance<LambdaTypeContext>(_ctx, getState());
  enterRule(_localctx, 100, OpenCMLParser::RuleLambdaType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(640);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__13) {
      setState(635);
      match(OpenCMLParser::T__13);
      setState(637);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::IDENTIFIER) {
        setState(636);
        pairedParams();
      }
      setState(639);
      match(OpenCMLParser::T__14);
    }
    setState(642);
    match(OpenCMLParser::T__11);
    setState(644);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(643);
      pairedParams();
    }
    setState(646);
    match(OpenCMLParser::T__12);
    setState(647);
    match(OpenCMLParser::T__4);
    setState(648);
    typeExpr(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrimTypeContext ------------------------------------------------------------------

OpenCMLParser::PrimTypeContext::PrimTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::INTEGER_TYPE() {
  return getToken(OpenCMLParser::INTEGER_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::INTEGER32_TYPE() {
  return getToken(OpenCMLParser::INTEGER32_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::INTEGER64_TYPE() {
  return getToken(OpenCMLParser::INTEGER64_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::REAL_TYPE() {
  return getToken(OpenCMLParser::REAL_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::FLOAT_TYPE() {
  return getToken(OpenCMLParser::FLOAT_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::DOUBLE_TYPE() {
  return getToken(OpenCMLParser::DOUBLE_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::NUMBER_TYPE() {
  return getToken(OpenCMLParser::NUMBER_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::STRING_TYPE() {
  return getToken(OpenCMLParser::STRING_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::BOOL_TYPE() {
  return getToken(OpenCMLParser::BOOL_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::CHAR_TYPE() {
  return getToken(OpenCMLParser::CHAR_TYPE, 0);
}


size_t OpenCMLParser::PrimTypeContext::getRuleIndex() const {
  return OpenCMLParser::RulePrimType;
}


std::any OpenCMLParser::PrimTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitPrimType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::PrimTypeContext* OpenCMLParser::primType() {
  PrimTypeContext *_localctx = _tracker.createInstance<PrimTypeContext>(_ctx, getState());
  enterRule(_localctx, 102, OpenCMLParser::RulePrimType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(650);
    _la = _input->LA(1);
    if (!(((((_la - 59) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 59)) & 1023) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StructTypeContext ------------------------------------------------------------------

OpenCMLParser::StructTypeContext::StructTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::SET_TYPE() {
  return getToken(OpenCMLParser::SET_TYPE, 0);
}

std::vector<OpenCMLParser::TypeExprContext *> OpenCMLParser::StructTypeContext::typeExpr() {
  return getRuleContexts<OpenCMLParser::TypeExprContext>();
}

OpenCMLParser::TypeExprContext* OpenCMLParser::StructTypeContext::typeExpr(size_t i) {
  return getRuleContext<OpenCMLParser::TypeExprContext>(i);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::MAP_TYPE() {
  return getToken(OpenCMLParser::MAP_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::LIST_TYPE() {
  return getToken(OpenCMLParser::LIST_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::DICT_TYPE() {
  return getToken(OpenCMLParser::DICT_TYPE, 0);
}

OpenCMLParser::PairedTypesContext* OpenCMLParser::StructTypeContext::pairedTypes() {
  return getRuleContext<OpenCMLParser::PairedTypesContext>(0);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::ARRAY_TYPE() {
  return getToken(OpenCMLParser::ARRAY_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::TUPLE_TYPE() {
  return getToken(OpenCMLParser::TUPLE_TYPE, 0);
}

OpenCMLParser::TypeListContext* OpenCMLParser::StructTypeContext::typeList() {
  return getRuleContext<OpenCMLParser::TypeListContext>(0);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::UNION_TYPE() {
  return getToken(OpenCMLParser::UNION_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::VECTOR_TYPE() {
  return getToken(OpenCMLParser::VECTOR_TYPE, 0);
}

std::vector<tree::TerminalNode *> OpenCMLParser::StructTypeContext::INTEGER() {
  return getTokens(OpenCMLParser::INTEGER);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::INTEGER(size_t i) {
  return getToken(OpenCMLParser::INTEGER, i);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::TENSOR_TYPE() {
  return getToken(OpenCMLParser::TENSOR_TYPE, 0);
}


size_t OpenCMLParser::StructTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleStructType;
}


std::any OpenCMLParser::StructTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitStructType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::StructTypeContext* OpenCMLParser::structType() {
  StructTypeContext *_localctx = _tracker.createInstance<StructTypeContext>(_ctx, getState());
  enterRule(_localctx, 104, OpenCMLParser::RuleStructType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(738);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::SET_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(652);
        match(OpenCMLParser::SET_TYPE);
        setState(657);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 81, _ctx)) {
        case 1: {
          setState(653);
          match(OpenCMLParser::T__13);
          setState(654);
          typeExpr(0);
          setState(655);
          match(OpenCMLParser::T__14);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::MAP_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(659);
        match(OpenCMLParser::MAP_TYPE);
        setState(666);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 82, _ctx)) {
        case 1: {
          setState(660);
          match(OpenCMLParser::T__13);
          setState(661);
          typeExpr(0);
          setState(662);
          match(OpenCMLParser::T__8);
          setState(663);
          typeExpr(0);
          setState(664);
          match(OpenCMLParser::T__14);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::LIST_TYPE: {
        enterOuterAlt(_localctx, 3);
        setState(668);
        match(OpenCMLParser::LIST_TYPE);
        break;
      }

      case OpenCMLParser::DICT_TYPE: {
        enterOuterAlt(_localctx, 4);
        setState(669);
        match(OpenCMLParser::DICT_TYPE);
        break;
      }

      case OpenCMLParser::T__5: {
        enterOuterAlt(_localctx, 5);
        setState(670);
        match(OpenCMLParser::T__5);
        setState(672);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::IDENTIFIER) {
          setState(671);
          pairedTypes();
        }
        setState(675);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__8) {
          setState(674);
          match(OpenCMLParser::T__8);
        }
        setState(677);
        match(OpenCMLParser::T__6);
        break;
      }

      case OpenCMLParser::ARRAY_TYPE: {
        enterOuterAlt(_localctx, 6);
        setState(678);
        match(OpenCMLParser::ARRAY_TYPE);
        setState(683);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 85, _ctx)) {
        case 1: {
          setState(679);
          match(OpenCMLParser::T__13);
          setState(680);
          typeExpr(0);
          setState(681);
          match(OpenCMLParser::T__14);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::TUPLE_TYPE: {
        enterOuterAlt(_localctx, 7);
        setState(685);
        match(OpenCMLParser::TUPLE_TYPE);
        setState(694);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 88, _ctx)) {
        case 1: {
          setState(686);
          match(OpenCMLParser::T__13);
          setState(688);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if ((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & -576460752303402944) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
            ((1ULL << (_la - 64)) & 4325375) != 0)) {
            setState(687);
            typeList();
          }
          setState(691);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == OpenCMLParser::T__8) {
            setState(690);
            match(OpenCMLParser::T__8);
          }
          setState(693);
          match(OpenCMLParser::T__14);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::UNION_TYPE: {
        enterOuterAlt(_localctx, 8);
        setState(696);
        match(OpenCMLParser::UNION_TYPE);
        setState(705);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 91, _ctx)) {
        case 1: {
          setState(697);
          match(OpenCMLParser::T__13);
          setState(699);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if ((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & -576460752303402944) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
            ((1ULL << (_la - 64)) & 4325375) != 0)) {
            setState(698);
            typeList();
          }
          setState(702);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == OpenCMLParser::T__8) {
            setState(701);
            match(OpenCMLParser::T__8);
          }
          setState(704);
          match(OpenCMLParser::T__14);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::VECTOR_TYPE: {
        enterOuterAlt(_localctx, 9);
        setState(707);
        match(OpenCMLParser::VECTOR_TYPE);
        setState(716);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 93, _ctx)) {
        case 1: {
          setState(708);
          match(OpenCMLParser::T__13);
          setState(709);
          typeExpr(0);
          setState(712);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == OpenCMLParser::T__8) {
            setState(710);
            match(OpenCMLParser::T__8);
            setState(711);
            match(OpenCMLParser::INTEGER);
          }
          setState(714);
          match(OpenCMLParser::T__14);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::TENSOR_TYPE: {
        enterOuterAlt(_localctx, 10);
        setState(718);
        match(OpenCMLParser::TENSOR_TYPE);
        setState(736);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 96, _ctx)) {
        case 1: {
          setState(719);
          match(OpenCMLParser::T__13);
          setState(720);
          typeExpr(0);
          setState(732);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == OpenCMLParser::T__8) {
            setState(721);
            match(OpenCMLParser::T__8);
            setState(722);
            match(OpenCMLParser::T__9);
            setState(723);
            match(OpenCMLParser::INTEGER);
            setState(728);
            _errHandler->sync(this);
            _la = _input->LA(1);
            while (_la == OpenCMLParser::T__8) {
              setState(724);
              match(OpenCMLParser::T__8);
              setState(725);
              match(OpenCMLParser::INTEGER);
              setState(730);
              _errHandler->sync(this);
              _la = _input->LA(1);
            }
            setState(731);
            match(OpenCMLParser::T__10);
          }
          setState(734);
          match(OpenCMLParser::T__14);
          break;
        }

        default:
          break;
        }
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- SpecialTypeContext ------------------------------------------------------------------

OpenCMLParser::SpecialTypeContext::SpecialTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::SpecialTypeContext::ANY_TYPE() {
  return getToken(OpenCMLParser::ANY_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::SpecialTypeContext::VOID_TYPE() {
  return getToken(OpenCMLParser::VOID_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::SpecialTypeContext::FUNCTOR_TYPE() {
  return getToken(OpenCMLParser::FUNCTOR_TYPE, 0);
}


size_t OpenCMLParser::SpecialTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleSpecialType;
}


std::any OpenCMLParser::SpecialTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitSpecialType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::SpecialTypeContext* OpenCMLParser::specialType() {
  SpecialTypeContext *_localctx = _tracker.createInstance<SpecialTypeContext>(_ctx, getState());
  enterRule(_localctx, 106, OpenCMLParser::RuleSpecialType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(740);
    _la = _input->LA(1);
    if (!(((((_la - 78) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 78)) & 7) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- IdentRefContext ------------------------------------------------------------------

OpenCMLParser::IdentRefContext::IdentRefContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::IdentRefContext::IDENTIFIER() {
  return getToken(OpenCMLParser::IDENTIFIER, 0);
}


size_t OpenCMLParser::IdentRefContext::getRuleIndex() const {
  return OpenCMLParser::RuleIdentRef;
}


std::any OpenCMLParser::IdentRefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitIdentRef(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::identRef() {
  IdentRefContext *_localctx = _tracker.createInstance<IdentRefContext>(_ctx, getState());
  enterRule(_localctx, 108, OpenCMLParser::RuleIdentRef);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(742);
    match(OpenCMLParser::IDENTIFIER);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

bool OpenCMLParser::sempred(RuleContext *context, size_t ruleIndex, size_t predicateIndex) {
  switch (ruleIndex) {
    case 38: return entityLinkSempred(antlrcpp::downCast<EntityLinkContext *>(context), predicateIndex);
    case 39: return entityUnitSempred(antlrcpp::downCast<EntityUnitContext *>(context), predicateIndex);
    case 40: return entityWithSempred(antlrcpp::downCast<EntityWithContext *>(context), predicateIndex);
    case 41: return entityExprSempred(antlrcpp::downCast<EntityExprContext *>(context), predicateIndex);
    case 42: return relaExprSempred(antlrcpp::downCast<RelaExprContext *>(context), predicateIndex);
    case 43: return addExprSempred(antlrcpp::downCast<AddExprContext *>(context), predicateIndex);
    case 44: return multiExprSempred(antlrcpp::downCast<MultiExprContext *>(context), predicateIndex);
    case 48: return typeExprSempred(antlrcpp::downCast<TypeExprContext *>(context), predicateIndex);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::entityLinkSempred(EntityLinkContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 0: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::entityUnitSempred(EntityUnitContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 1: return isAdjacent();

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::entityWithSempred(EntityWithContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 2: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::entityExprSempred(EntityExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 3: return precpred(_ctx, 8);
    case 4: return precpred(_ctx, 7);
    case 5: return precpred(_ctx, 6);
    case 6: return precpred(_ctx, 5);
    case 7: return precpred(_ctx, 4);
    case 8: return precpred(_ctx, 3);
    case 9: return precpred(_ctx, 2);
    case 10: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::relaExprSempred(RelaExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 11: return precpred(_ctx, 8);
    case 12: return precpred(_ctx, 7);
    case 13: return precpred(_ctx, 6);
    case 14: return precpred(_ctx, 5);
    case 15: return precpred(_ctx, 4);
    case 16: return precpred(_ctx, 3);
    case 17: return precpred(_ctx, 2);
    case 18: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::addExprSempred(AddExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 19: return precpred(_ctx, 4);
    case 20: return precpred(_ctx, 3);
    case 21: return precpred(_ctx, 2);
    case 22: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::multiExprSempred(MultiExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 23: return precpred(_ctx, 6);
    case 24: return precpred(_ctx, 5);
    case 25: return precpred(_ctx, 4);
    case 26: return precpred(_ctx, 3);
    case 27: return precpred(_ctx, 2);
    case 28: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::typeExprSempred(TypeExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 29: return precpred(_ctx, 2);
    case 30: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

void OpenCMLParser::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  opencmlParserInitialize();
#else
  ::antlr4::internal::call_once(opencmlParserOnceFlag, opencmlParserInitialize);
#endif
}
