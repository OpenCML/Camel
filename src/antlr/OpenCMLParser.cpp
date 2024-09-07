
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
      "waitStmt", "withDef", "funcDef", "retStmt", "lambdaExpr", "carrier", 
      "annotation", "annotations", "modifiers", "keyTypePair", "keyValuePair", 
      "keyParamPair", "indexKTPair", "indexKVPair", "typeList", "identList", 
      "valueList", "pairedTypes", "pairedValues", "pairedParams", "indexKVPairs", 
      "argumentList", "bracedPairedValues", "bracedIdents", "bracedStmts", 
      "bracedHomoValues", "bracedIndexKVPairs", "bracketIdents", "bracketHomoValues", 
      "bracketHeteValues", "parentParams", "parentValues", "parentHomoValues", 
      "parentHeteValues", "angledParams", "angledValues", "primEntity", 
      "memberAccess", "entityLink", "entityUnit", "entityWith", "entityExpr", 
      "relaExpr", "addExpr", "multiExpr", "unaryExpr", "primExpr", "literal", 
      "typeExpr", "unaryType", "atomType", "lambdaType", "primType", "structType", 
      "specialType", "identRef"
    },
    std::vector<std::string>{
      "", "':'", "'='", "':='", "'=>'", "'@'", "'['", "']'", "','", "'{'", 
      "'}'", "'{|'", "'|}'", "'[|'", "'|]'", "'('", "')'", "'(|'", "'|)'", 
      "'<'", "'>'", "'->'", "'.'", "'\\u003F'", "'+='", "'-='", "'*='", 
      "'/='", "'%='", "'^='", "'&='", "'|='", "'<='", "'>='", "'=='", "'!='", 
      "'&&'", "'||'", "'+'", "'-'", "'&'", "'|'", "'^'", "'*'", "'/'", "'%'", 
      "'!'", "'~'", "';'", "'as'", "'is'", "'let'", "'var'", "'use'", "'from'", 
      "'func'", "'type'", "'with'", "'wait'", "'return'", "'inner'", "'outer'", 
      "'atomic'", "'static'", "'null'", "'true'", "'false'", "'Int'", "'Int32'", 
      "'Int64'", "'Real'", "'Float'", "'Double'", "'Number'", "'String'", 
      "'Bool'", "'Char'", "'Set'", "'Map'", "'List'", "'Dict'", "'Array'", 
      "'Tuple'", "'Union'", "'Vector'", "'Tensor'", "'Any'", "'Void'", "'Functor'"
    },
    std::vector<std::string>{
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "SEP", "AS", 
      "IS", "LET", "VAR", "USE", "FROM", "FUNC", "TYPE", "WITH", "WAIT", 
      "RETURN", "INNER", "OUTER", "ATOMIC", "STATIC", "NULL", "TRUE", "FALSE", 
      "INTEGER_TYPE", "INTEGER32_TYPE", "INTEGER64_TYPE", "REAL_TYPE", "FLOAT_TYPE", 
      "DOUBLE_TYPE", "NUMBER_TYPE", "STRING_TYPE", "BOOL_TYPE", "CHAR_TYPE", 
      "SET_TYPE", "MAP_TYPE", "LIST_TYPE", "DICT_TYPE", "ARRAY_TYPE", "TUPLE_TYPE", 
      "UNION_TYPE", "VECTOR_TYPE", "TENSOR_TYPE", "ANY_TYPE", "VOID_TYPE", 
      "FUNCTOR_TYPE", "BLANK", "LIN_CMT_HASH", "LIN_CMT_SLASH", "BLK_CMT", 
      "MULTI_STR", "IDENTIFIER", "UNIT", "STRING", "FSTRING", "INTEGER", 
      "REAL"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,99,816,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,2,55,7,55,2,56,7,
  	56,2,57,7,57,2,58,7,58,2,59,7,59,2,60,7,60,2,61,7,61,2,62,7,62,1,0,3,
  	0,128,8,0,1,0,1,0,1,1,1,1,1,1,5,1,135,8,1,10,1,12,1,138,9,1,1,1,3,1,141,
  	8,1,1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,2,150,8,2,1,3,1,3,1,3,1,3,3,3,156,8,
  	3,1,3,1,3,1,3,1,3,1,3,1,3,3,3,164,8,3,1,3,1,3,1,3,3,3,169,8,3,1,4,1,4,
  	1,4,1,4,1,4,1,4,3,4,177,8,4,1,5,1,5,1,5,1,5,1,5,1,6,3,6,185,8,6,1,6,1,
  	6,1,7,1,7,1,7,1,8,1,8,1,8,1,9,3,9,196,8,9,1,9,3,9,199,8,9,1,9,3,9,202,
  	8,9,1,9,1,9,1,9,1,9,1,9,3,9,209,8,9,1,9,1,9,1,10,1,10,3,10,215,8,10,1,
  	11,3,11,218,8,11,1,11,1,11,1,11,3,11,223,8,11,1,11,1,11,1,11,3,11,228,
  	8,11,1,12,1,12,1,12,3,12,233,8,12,1,13,1,13,1,13,1,14,4,14,239,8,14,11,
  	14,12,14,240,1,15,4,15,244,8,15,11,15,12,15,245,1,16,1,16,1,16,1,16,1,
  	17,1,17,1,17,1,17,1,18,3,18,257,8,18,1,18,1,18,3,18,261,8,18,1,18,1,18,
  	1,18,1,18,3,18,267,8,18,1,19,1,19,1,19,1,19,1,19,1,19,1,20,1,20,1,20,
  	1,20,1,20,1,20,1,21,1,21,1,21,5,21,284,8,21,10,21,12,21,287,9,21,1,22,
  	1,22,1,22,5,22,292,8,22,10,22,12,22,295,9,22,1,23,1,23,1,23,5,23,300,
  	8,23,10,23,12,23,303,9,23,1,24,1,24,1,24,5,24,308,8,24,10,24,12,24,311,
  	9,24,1,25,1,25,1,25,5,25,316,8,25,10,25,12,25,319,9,25,1,26,1,26,1,26,
  	5,26,324,8,26,10,26,12,26,327,9,26,1,27,1,27,1,27,5,27,332,8,27,10,27,
  	12,27,335,9,27,1,28,1,28,1,28,3,28,340,8,28,1,28,3,28,343,8,28,1,29,1,
  	29,3,29,347,8,29,1,29,3,29,350,8,29,1,29,1,29,1,30,1,30,3,30,356,8,30,
  	1,30,3,30,359,8,30,1,30,1,30,1,31,1,31,3,31,365,8,31,1,31,1,31,1,32,1,
  	32,1,32,3,32,372,8,32,1,32,1,32,1,33,1,33,1,33,3,33,379,8,33,1,33,1,33,
  	1,34,1,34,3,34,385,8,34,1,34,3,34,388,8,34,1,34,1,34,1,35,1,35,3,35,394,
  	8,35,1,35,3,35,397,8,35,1,35,1,35,1,36,1,36,3,36,403,8,36,1,36,3,36,406,
  	8,36,1,36,1,36,1,37,1,37,3,37,412,8,37,1,37,3,37,415,8,37,1,37,1,37,1,
  	38,1,38,3,38,421,8,38,1,38,3,38,424,8,38,1,38,1,38,1,39,1,39,3,39,430,
  	8,39,1,39,3,39,433,8,39,1,39,1,39,1,40,1,40,3,40,439,8,40,1,40,3,40,442,
  	8,40,1,40,1,40,1,41,1,41,3,41,448,8,41,1,41,3,41,451,8,41,1,41,1,41,1,
  	42,1,42,3,42,457,8,42,1,42,3,42,460,8,42,1,42,1,42,1,43,1,43,1,43,1,43,
  	1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,3,43,478,8,43,1,44,
  	1,44,1,44,1,44,1,45,1,45,1,45,1,45,1,45,1,45,5,45,490,8,45,10,45,12,45,
  	493,9,45,1,46,1,46,1,46,1,46,3,46,499,8,46,1,46,5,46,502,8,46,10,46,12,
  	46,505,9,46,1,47,1,47,1,47,1,47,1,47,1,47,5,47,513,8,47,10,47,12,47,516,
  	9,47,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,
  	1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,
  	1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,5,48,554,8,48,10,48,12,48,
  	557,9,48,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,
  	1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,
  	1,49,5,49,586,8,49,10,49,12,49,589,9,49,1,50,1,50,1,50,1,50,1,50,1,50,
  	1,50,1,50,1,50,1,50,1,50,1,50,1,50,1,50,1,50,5,50,606,8,50,10,50,12,50,
  	609,9,50,1,51,1,51,1,51,1,51,1,51,1,51,1,51,1,51,1,51,1,51,1,51,1,51,
  	1,51,1,51,1,51,1,51,1,51,1,51,1,51,1,51,1,51,5,51,632,8,51,10,51,12,51,
  	635,9,51,1,52,1,52,1,52,1,52,1,52,3,52,642,8,52,1,53,1,53,1,53,1,53,1,
  	53,3,53,649,8,53,1,54,1,54,3,54,653,8,54,1,54,1,54,3,54,657,8,54,1,54,
  	1,54,1,54,1,54,1,54,1,54,3,54,665,8,54,1,55,1,55,1,55,1,55,1,55,1,55,
  	1,55,1,55,1,55,5,55,676,8,55,10,55,12,55,679,9,55,1,56,1,56,1,56,3,56,
  	684,8,56,1,56,5,56,687,8,56,10,56,12,56,690,9,56,1,57,1,57,1,57,1,57,
  	1,57,1,57,1,57,1,57,1,57,3,57,701,8,57,1,58,1,58,3,58,705,8,58,1,58,3,
  	58,708,8,58,1,58,1,58,3,58,712,8,58,1,58,1,58,1,58,1,58,1,59,1,59,1,60,
  	1,60,1,60,1,60,1,60,3,60,725,8,60,1,60,1,60,1,60,1,60,1,60,1,60,1,60,
  	3,60,734,8,60,1,60,1,60,1,60,1,60,1,60,1,60,1,60,3,60,743,8,60,1,60,1,
  	60,1,60,3,60,748,8,60,1,60,3,60,751,8,60,1,60,3,60,754,8,60,1,60,1,60,
  	1,60,3,60,759,8,60,1,60,3,60,762,8,60,1,60,3,60,765,8,60,1,60,1,60,1,
  	60,1,60,1,60,3,60,772,8,60,1,60,1,60,3,60,776,8,60,1,60,1,60,1,60,1,60,
  	1,60,1,60,1,60,1,60,5,60,786,8,60,10,60,12,60,789,9,60,1,60,3,60,792,
  	8,60,1,60,1,60,3,60,796,8,60,1,60,1,60,3,60,800,8,60,1,60,3,60,803,8,
  	60,1,60,1,60,1,60,1,60,1,60,3,60,810,8,60,1,61,1,61,1,62,1,62,1,62,0,
  	7,90,94,96,98,100,102,110,63,0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,
  	30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,68,70,72,74,
  	76,78,80,82,84,86,88,90,92,94,96,98,100,102,104,106,108,110,112,114,116,
  	118,120,122,124,0,4,1,0,51,52,1,0,60,63,1,0,67,76,1,0,86,88,909,0,127,
  	1,0,0,0,2,131,1,0,0,0,4,149,1,0,0,0,6,168,1,0,0,0,8,170,1,0,0,0,10,178,
  	1,0,0,0,12,184,1,0,0,0,14,188,1,0,0,0,16,191,1,0,0,0,18,195,1,0,0,0,20,
  	212,1,0,0,0,22,217,1,0,0,0,24,232,1,0,0,0,26,234,1,0,0,0,28,238,1,0,0,
  	0,30,243,1,0,0,0,32,247,1,0,0,0,34,251,1,0,0,0,36,256,1,0,0,0,38,268,
  	1,0,0,0,40,274,1,0,0,0,42,280,1,0,0,0,44,288,1,0,0,0,46,296,1,0,0,0,48,
  	304,1,0,0,0,50,312,1,0,0,0,52,320,1,0,0,0,54,328,1,0,0,0,56,342,1,0,0,
  	0,58,344,1,0,0,0,60,353,1,0,0,0,62,362,1,0,0,0,64,368,1,0,0,0,66,375,
  	1,0,0,0,68,382,1,0,0,0,70,391,1,0,0,0,72,400,1,0,0,0,74,409,1,0,0,0,76,
  	418,1,0,0,0,78,427,1,0,0,0,80,436,1,0,0,0,82,445,1,0,0,0,84,454,1,0,0,
  	0,86,477,1,0,0,0,88,479,1,0,0,0,90,483,1,0,0,0,92,494,1,0,0,0,94,506,
  	1,0,0,0,96,517,1,0,0,0,98,558,1,0,0,0,100,590,1,0,0,0,102,610,1,0,0,0,
  	104,641,1,0,0,0,106,648,1,0,0,0,108,664,1,0,0,0,110,666,1,0,0,0,112,680,
  	1,0,0,0,114,700,1,0,0,0,116,707,1,0,0,0,118,717,1,0,0,0,120,809,1,0,0,
  	0,122,811,1,0,0,0,124,813,1,0,0,0,126,128,3,2,1,0,127,126,1,0,0,0,127,
  	128,1,0,0,0,128,129,1,0,0,0,129,130,5,0,0,1,130,1,1,0,0,0,131,136,3,4,
  	2,0,132,133,5,48,0,0,133,135,3,4,2,0,134,132,1,0,0,0,135,138,1,0,0,0,
  	136,134,1,0,0,0,136,137,1,0,0,0,137,140,1,0,0,0,138,136,1,0,0,0,139,141,
  	5,48,0,0,140,139,1,0,0,0,140,141,1,0,0,0,141,3,1,0,0,0,142,150,3,6,3,
  	0,143,150,3,8,4,0,144,150,3,10,5,0,145,150,3,12,6,0,146,150,3,14,7,0,
  	147,150,3,18,9,0,148,150,3,20,10,0,149,142,1,0,0,0,149,143,1,0,0,0,149,
  	144,1,0,0,0,149,145,1,0,0,0,149,146,1,0,0,0,149,147,1,0,0,0,149,148,1,
  	0,0,0,150,5,1,0,0,0,151,152,7,0,0,0,152,155,3,24,12,0,153,154,5,1,0,0,
  	154,156,3,110,55,0,155,153,1,0,0,0,155,156,1,0,0,0,156,157,1,0,0,0,157,
  	158,5,2,0,0,158,159,3,96,48,0,159,169,1,0,0,0,160,163,3,24,12,0,161,162,
  	5,1,0,0,162,164,3,110,55,0,163,161,1,0,0,0,163,164,1,0,0,0,164,165,1,
  	0,0,0,165,166,5,3,0,0,166,167,3,96,48,0,167,169,1,0,0,0,168,151,1,0,0,
  	0,168,160,1,0,0,0,169,7,1,0,0,0,170,171,5,53,0,0,171,176,3,24,12,0,172,
  	173,5,2,0,0,173,177,3,96,48,0,174,175,5,54,0,0,175,177,5,96,0,0,176,172,
  	1,0,0,0,176,174,1,0,0,0,177,9,1,0,0,0,178,179,5,56,0,0,179,180,3,124,
  	62,0,180,181,5,2,0,0,181,182,3,110,55,0,182,11,1,0,0,0,183,185,3,28,14,
  	0,184,183,1,0,0,0,184,185,1,0,0,0,185,186,1,0,0,0,186,187,3,96,48,0,187,
  	13,1,0,0,0,188,189,5,58,0,0,189,190,3,96,48,0,190,15,1,0,0,0,191,192,
  	5,57,0,0,192,193,3,82,41,0,193,17,1,0,0,0,194,196,3,28,14,0,195,194,1,
  	0,0,0,195,196,1,0,0,0,196,198,1,0,0,0,197,199,3,16,8,0,198,197,1,0,0,
  	0,198,199,1,0,0,0,199,201,1,0,0,0,200,202,3,30,15,0,201,200,1,0,0,0,201,
  	202,1,0,0,0,202,203,1,0,0,0,203,204,5,55,0,0,204,205,3,124,62,0,205,208,
  	3,74,37,0,206,207,5,1,0,0,207,209,3,110,55,0,208,206,1,0,0,0,208,209,
  	1,0,0,0,209,210,1,0,0,0,210,211,3,62,31,0,211,19,1,0,0,0,212,214,5,59,
  	0,0,213,215,3,96,48,0,214,213,1,0,0,0,214,215,1,0,0,0,215,21,1,0,0,0,
  	216,218,3,30,15,0,217,216,1,0,0,0,217,218,1,0,0,0,218,219,1,0,0,0,219,
  	222,3,74,37,0,220,221,5,1,0,0,221,223,3,110,55,0,222,220,1,0,0,0,222,
  	223,1,0,0,0,223,224,1,0,0,0,224,227,5,4,0,0,225,228,3,62,31,0,226,228,
  	3,96,48,0,227,225,1,0,0,0,227,226,1,0,0,0,228,23,1,0,0,0,229,233,3,124,
  	62,0,230,233,3,60,30,0,231,233,3,68,34,0,232,229,1,0,0,0,232,230,1,0,
  	0,0,232,231,1,0,0,0,233,25,1,0,0,0,234,235,5,5,0,0,235,236,3,86,43,0,
  	236,27,1,0,0,0,237,239,3,26,13,0,238,237,1,0,0,0,239,240,1,0,0,0,240,
  	238,1,0,0,0,240,241,1,0,0,0,241,29,1,0,0,0,242,244,7,1,0,0,243,242,1,
  	0,0,0,244,245,1,0,0,0,245,243,1,0,0,0,245,246,1,0,0,0,246,31,1,0,0,0,
  	247,248,3,124,62,0,248,249,5,1,0,0,249,250,3,110,55,0,250,33,1,0,0,0,
  	251,252,3,124,62,0,252,253,5,1,0,0,253,254,3,96,48,0,254,35,1,0,0,0,255,
  	257,5,52,0,0,256,255,1,0,0,0,256,257,1,0,0,0,257,258,1,0,0,0,258,260,
  	3,124,62,0,259,261,3,26,13,0,260,259,1,0,0,0,260,261,1,0,0,0,261,262,
  	1,0,0,0,262,263,5,1,0,0,263,266,3,110,55,0,264,265,5,2,0,0,265,267,3,
  	96,48,0,266,264,1,0,0,0,266,267,1,0,0,0,267,37,1,0,0,0,268,269,5,6,0,
  	0,269,270,3,96,48,0,270,271,5,7,0,0,271,272,5,1,0,0,272,273,3,110,55,
  	0,273,39,1,0,0,0,274,275,5,6,0,0,275,276,3,96,48,0,276,277,5,7,0,0,277,
  	278,5,1,0,0,278,279,3,96,48,0,279,41,1,0,0,0,280,285,3,110,55,0,281,282,
  	5,8,0,0,282,284,3,110,55,0,283,281,1,0,0,0,284,287,1,0,0,0,285,283,1,
  	0,0,0,285,286,1,0,0,0,286,43,1,0,0,0,287,285,1,0,0,0,288,293,3,124,62,
  	0,289,290,5,8,0,0,290,292,3,124,62,0,291,289,1,0,0,0,292,295,1,0,0,0,
  	293,291,1,0,0,0,293,294,1,0,0,0,294,45,1,0,0,0,295,293,1,0,0,0,296,301,
  	3,96,48,0,297,298,5,8,0,0,298,300,3,96,48,0,299,297,1,0,0,0,300,303,1,
  	0,0,0,301,299,1,0,0,0,301,302,1,0,0,0,302,47,1,0,0,0,303,301,1,0,0,0,
  	304,309,3,32,16,0,305,306,5,8,0,0,306,308,3,32,16,0,307,305,1,0,0,0,308,
  	311,1,0,0,0,309,307,1,0,0,0,309,310,1,0,0,0,310,49,1,0,0,0,311,309,1,
  	0,0,0,312,317,3,34,17,0,313,314,5,8,0,0,314,316,3,34,17,0,315,313,1,0,
  	0,0,316,319,1,0,0,0,317,315,1,0,0,0,317,318,1,0,0,0,318,51,1,0,0,0,319,
  	317,1,0,0,0,320,325,3,36,18,0,321,322,5,8,0,0,322,324,3,36,18,0,323,321,
  	1,0,0,0,324,327,1,0,0,0,325,323,1,0,0,0,325,326,1,0,0,0,326,53,1,0,0,
  	0,327,325,1,0,0,0,328,333,3,40,20,0,329,330,5,8,0,0,330,332,3,40,20,0,
  	331,329,1,0,0,0,332,335,1,0,0,0,333,331,1,0,0,0,333,334,1,0,0,0,334,55,
  	1,0,0,0,335,333,1,0,0,0,336,339,3,46,23,0,337,338,5,8,0,0,338,340,3,50,
  	25,0,339,337,1,0,0,0,339,340,1,0,0,0,340,343,1,0,0,0,341,343,3,50,25,
  	0,342,336,1,0,0,0,342,341,1,0,0,0,343,57,1,0,0,0,344,346,5,9,0,0,345,
  	347,3,50,25,0,346,345,1,0,0,0,346,347,1,0,0,0,347,349,1,0,0,0,348,350,
  	5,8,0,0,349,348,1,0,0,0,349,350,1,0,0,0,350,351,1,0,0,0,351,352,5,10,
  	0,0,352,59,1,0,0,0,353,355,5,9,0,0,354,356,3,44,22,0,355,354,1,0,0,0,
  	355,356,1,0,0,0,356,358,1,0,0,0,357,359,5,8,0,0,358,357,1,0,0,0,358,359,
  	1,0,0,0,359,360,1,0,0,0,360,361,5,10,0,0,361,61,1,0,0,0,362,364,5,9,0,
  	0,363,365,3,2,1,0,364,363,1,0,0,0,364,365,1,0,0,0,365,366,1,0,0,0,366,
  	367,5,10,0,0,367,63,1,0,0,0,368,369,5,11,0,0,369,371,3,46,23,0,370,372,
  	5,8,0,0,371,370,1,0,0,0,371,372,1,0,0,0,372,373,1,0,0,0,373,374,5,12,
  	0,0,374,65,1,0,0,0,375,376,5,9,0,0,376,378,3,54,27,0,377,379,5,8,0,0,
  	378,377,1,0,0,0,378,379,1,0,0,0,379,380,1,0,0,0,380,381,5,10,0,0,381,
  	67,1,0,0,0,382,384,5,6,0,0,383,385,3,44,22,0,384,383,1,0,0,0,384,385,
  	1,0,0,0,385,387,1,0,0,0,386,388,5,8,0,0,387,386,1,0,0,0,387,388,1,0,0,
  	0,388,389,1,0,0,0,389,390,5,7,0,0,390,69,1,0,0,0,391,393,5,13,0,0,392,
  	394,3,46,23,0,393,392,1,0,0,0,393,394,1,0,0,0,394,396,1,0,0,0,395,397,
  	5,8,0,0,396,395,1,0,0,0,396,397,1,0,0,0,397,398,1,0,0,0,398,399,5,14,
  	0,0,399,71,1,0,0,0,400,402,5,6,0,0,401,403,3,46,23,0,402,401,1,0,0,0,
  	402,403,1,0,0,0,403,405,1,0,0,0,404,406,5,8,0,0,405,404,1,0,0,0,405,406,
  	1,0,0,0,406,407,1,0,0,0,407,408,5,7,0,0,408,73,1,0,0,0,409,411,5,15,0,
  	0,410,412,3,52,26,0,411,410,1,0,0,0,411,412,1,0,0,0,412,414,1,0,0,0,413,
  	415,5,8,0,0,414,413,1,0,0,0,414,415,1,0,0,0,415,416,1,0,0,0,416,417,5,
  	16,0,0,417,75,1,0,0,0,418,420,5,15,0,0,419,421,3,56,28,0,420,419,1,0,
  	0,0,420,421,1,0,0,0,421,423,1,0,0,0,422,424,5,8,0,0,423,422,1,0,0,0,423,
  	424,1,0,0,0,424,425,1,0,0,0,425,426,5,16,0,0,426,77,1,0,0,0,427,429,5,
  	17,0,0,428,430,3,46,23,0,429,428,1,0,0,0,429,430,1,0,0,0,430,432,1,0,
  	0,0,431,433,5,8,0,0,432,431,1,0,0,0,432,433,1,0,0,0,433,434,1,0,0,0,434,
  	435,5,18,0,0,435,79,1,0,0,0,436,438,5,15,0,0,437,439,3,46,23,0,438,437,
  	1,0,0,0,438,439,1,0,0,0,439,441,1,0,0,0,440,442,5,8,0,0,441,440,1,0,0,
  	0,441,442,1,0,0,0,442,443,1,0,0,0,443,444,5,16,0,0,444,81,1,0,0,0,445,
  	447,5,19,0,0,446,448,3,52,26,0,447,446,1,0,0,0,447,448,1,0,0,0,448,450,
  	1,0,0,0,449,451,5,8,0,0,450,449,1,0,0,0,450,451,1,0,0,0,451,452,1,0,0,
  	0,452,453,5,20,0,0,453,83,1,0,0,0,454,456,5,19,0,0,455,457,3,56,28,0,
  	456,455,1,0,0,0,456,457,1,0,0,0,457,459,1,0,0,0,458,460,5,8,0,0,459,458,
  	1,0,0,0,459,460,1,0,0,0,460,461,1,0,0,0,461,462,5,20,0,0,462,85,1,0,0,
  	0,463,478,3,124,62,0,464,478,3,108,54,0,465,478,3,66,33,0,466,478,3,64,
  	32,0,467,478,3,58,29,0,468,478,3,72,36,0,469,478,3,70,35,0,470,471,5,
  	15,0,0,471,472,3,96,48,0,472,473,5,16,0,0,473,478,1,0,0,0,474,478,3,80,
  	40,0,475,478,3,78,39,0,476,478,3,22,11,0,477,463,1,0,0,0,477,464,1,0,
  	0,0,477,465,1,0,0,0,477,466,1,0,0,0,477,467,1,0,0,0,477,468,1,0,0,0,477,
  	469,1,0,0,0,477,470,1,0,0,0,477,474,1,0,0,0,477,475,1,0,0,0,477,476,1,
  	0,0,0,478,87,1,0,0,0,479,480,5,6,0,0,480,481,3,96,48,0,481,482,5,7,0,
  	0,482,89,1,0,0,0,483,484,6,45,-1,0,484,485,3,92,46,0,485,491,1,0,0,0,
  	486,487,10,1,0,0,487,488,5,21,0,0,488,490,3,92,46,0,489,486,1,0,0,0,490,
  	493,1,0,0,0,491,489,1,0,0,0,491,492,1,0,0,0,492,91,1,0,0,0,493,491,1,
  	0,0,0,494,503,3,94,47,0,495,499,3,88,44,0,496,499,3,84,42,0,497,499,3,
  	76,38,0,498,495,1,0,0,0,498,496,1,0,0,0,498,497,1,0,0,0,499,502,1,0,0,
  	0,500,502,3,26,13,0,501,498,1,0,0,0,501,500,1,0,0,0,502,505,1,0,0,0,503,
  	501,1,0,0,0,503,504,1,0,0,0,504,93,1,0,0,0,505,503,1,0,0,0,506,507,6,
  	47,-1,0,507,508,3,86,43,0,508,514,1,0,0,0,509,510,10,1,0,0,510,511,5,
  	22,0,0,511,513,3,86,43,0,512,509,1,0,0,0,513,516,1,0,0,0,514,512,1,0,
  	0,0,514,515,1,0,0,0,515,95,1,0,0,0,516,514,1,0,0,0,517,518,6,48,-1,0,
  	518,519,3,98,49,0,519,555,1,0,0,0,520,521,10,10,0,0,521,522,5,23,0,0,
  	522,523,3,96,48,0,523,524,5,1,0,0,524,525,3,96,48,11,525,554,1,0,0,0,
  	526,527,10,9,0,0,527,528,5,2,0,0,528,554,3,98,49,0,529,530,10,8,0,0,530,
  	531,5,24,0,0,531,554,3,98,49,0,532,533,10,7,0,0,533,534,5,25,0,0,534,
  	554,3,98,49,0,535,536,10,6,0,0,536,537,5,26,0,0,537,554,3,98,49,0,538,
  	539,10,5,0,0,539,540,5,27,0,0,540,554,3,98,49,0,541,542,10,4,0,0,542,
  	543,5,28,0,0,543,554,3,98,49,0,544,545,10,3,0,0,545,546,5,29,0,0,546,
  	554,3,98,49,0,547,548,10,2,0,0,548,549,5,30,0,0,549,554,3,98,49,0,550,
  	551,10,1,0,0,551,552,5,31,0,0,552,554,3,98,49,0,553,520,1,0,0,0,553,526,
  	1,0,0,0,553,529,1,0,0,0,553,532,1,0,0,0,553,535,1,0,0,0,553,538,1,0,0,
  	0,553,541,1,0,0,0,553,544,1,0,0,0,553,547,1,0,0,0,553,550,1,0,0,0,554,
  	557,1,0,0,0,555,553,1,0,0,0,555,556,1,0,0,0,556,97,1,0,0,0,557,555,1,
  	0,0,0,558,559,6,49,-1,0,559,560,3,100,50,0,560,587,1,0,0,0,561,562,10,
  	8,0,0,562,563,5,19,0,0,563,586,3,100,50,0,564,565,10,7,0,0,565,566,5,
  	20,0,0,566,586,3,100,50,0,567,568,10,6,0,0,568,569,5,32,0,0,569,586,3,
  	100,50,0,570,571,10,5,0,0,571,572,5,33,0,0,572,586,3,100,50,0,573,574,
  	10,4,0,0,574,575,5,34,0,0,575,586,3,100,50,0,576,577,10,3,0,0,577,578,
  	5,35,0,0,578,586,3,100,50,0,579,580,10,2,0,0,580,581,5,36,0,0,581,586,
  	3,100,50,0,582,583,10,1,0,0,583,584,5,37,0,0,584,586,3,100,50,0,585,561,
  	1,0,0,0,585,564,1,0,0,0,585,567,1,0,0,0,585,570,1,0,0,0,585,573,1,0,0,
  	0,585,576,1,0,0,0,585,579,1,0,0,0,585,582,1,0,0,0,586,589,1,0,0,0,587,
  	585,1,0,0,0,587,588,1,0,0,0,588,99,1,0,0,0,589,587,1,0,0,0,590,591,6,
  	50,-1,0,591,592,3,102,51,0,592,607,1,0,0,0,593,594,10,4,0,0,594,595,5,
  	38,0,0,595,606,3,102,51,0,596,597,10,3,0,0,597,598,5,39,0,0,598,606,3,
  	102,51,0,599,600,10,2,0,0,600,601,5,40,0,0,601,606,3,102,51,0,602,603,
  	10,1,0,0,603,604,5,41,0,0,604,606,3,102,51,0,605,593,1,0,0,0,605,596,
  	1,0,0,0,605,599,1,0,0,0,605,602,1,0,0,0,606,609,1,0,0,0,607,605,1,0,0,
  	0,607,608,1,0,0,0,608,101,1,0,0,0,609,607,1,0,0,0,610,611,6,51,-1,0,611,
  	612,3,104,52,0,612,633,1,0,0,0,613,614,10,6,0,0,614,615,5,42,0,0,615,
  	632,3,104,52,0,616,617,10,5,0,0,617,618,5,43,0,0,618,632,3,104,52,0,619,
  	620,10,4,0,0,620,621,5,44,0,0,621,632,3,104,52,0,622,623,10,3,0,0,623,
  	624,5,45,0,0,624,632,3,104,52,0,625,626,10,2,0,0,626,627,5,49,0,0,627,
  	632,3,110,55,0,628,629,10,1,0,0,629,630,5,50,0,0,630,632,3,110,55,0,631,
  	613,1,0,0,0,631,616,1,0,0,0,631,619,1,0,0,0,631,622,1,0,0,0,631,625,1,
  	0,0,0,631,628,1,0,0,0,632,635,1,0,0,0,633,631,1,0,0,0,633,634,1,0,0,0,
  	634,103,1,0,0,0,635,633,1,0,0,0,636,642,3,106,53,0,637,638,5,46,0,0,638,
  	642,3,106,53,0,639,640,5,47,0,0,640,642,3,106,53,0,641,636,1,0,0,0,641,
  	637,1,0,0,0,641,639,1,0,0,0,642,105,1,0,0,0,643,649,3,90,45,0,644,645,
  	5,15,0,0,645,646,3,96,48,0,646,647,5,16,0,0,647,649,1,0,0,0,648,643,1,
  	0,0,0,648,644,1,0,0,0,649,107,1,0,0,0,650,652,5,98,0,0,651,653,5,95,0,
  	0,652,651,1,0,0,0,652,653,1,0,0,0,653,665,1,0,0,0,654,656,5,99,0,0,655,
  	657,5,95,0,0,656,655,1,0,0,0,656,657,1,0,0,0,657,665,1,0,0,0,658,665,
  	5,96,0,0,659,665,5,93,0,0,660,665,5,97,0,0,661,665,5,65,0,0,662,665,5,
  	66,0,0,663,665,5,64,0,0,664,650,1,0,0,0,664,654,1,0,0,0,664,658,1,0,0,
  	0,664,659,1,0,0,0,664,660,1,0,0,0,664,661,1,0,0,0,664,662,1,0,0,0,664,
  	663,1,0,0,0,665,109,1,0,0,0,666,667,6,55,-1,0,667,668,3,112,56,0,668,
  	677,1,0,0,0,669,670,10,2,0,0,670,671,5,40,0,0,671,676,3,112,56,0,672,
  	673,10,1,0,0,673,674,5,41,0,0,674,676,3,112,56,0,675,669,1,0,0,0,675,
  	672,1,0,0,0,676,679,1,0,0,0,677,675,1,0,0,0,677,678,1,0,0,0,678,111,1,
  	0,0,0,679,677,1,0,0,0,680,688,3,114,57,0,681,683,5,6,0,0,682,684,5,98,
  	0,0,683,682,1,0,0,0,683,684,1,0,0,0,684,685,1,0,0,0,685,687,5,7,0,0,686,
  	681,1,0,0,0,687,690,1,0,0,0,688,686,1,0,0,0,688,689,1,0,0,0,689,113,1,
  	0,0,0,690,688,1,0,0,0,691,701,3,118,59,0,692,701,3,120,60,0,693,701,3,
  	122,61,0,694,701,3,124,62,0,695,696,5,15,0,0,696,697,3,110,55,0,697,698,
  	5,16,0,0,698,701,1,0,0,0,699,701,3,116,58,0,700,691,1,0,0,0,700,692,1,
  	0,0,0,700,693,1,0,0,0,700,694,1,0,0,0,700,695,1,0,0,0,700,699,1,0,0,0,
  	701,115,1,0,0,0,702,704,5,19,0,0,703,705,3,52,26,0,704,703,1,0,0,0,704,
  	705,1,0,0,0,705,706,1,0,0,0,706,708,5,20,0,0,707,702,1,0,0,0,707,708,
  	1,0,0,0,708,709,1,0,0,0,709,711,5,15,0,0,710,712,3,52,26,0,711,710,1,
  	0,0,0,711,712,1,0,0,0,712,713,1,0,0,0,713,714,5,16,0,0,714,715,5,4,0,
  	0,715,716,3,110,55,0,716,117,1,0,0,0,717,718,7,2,0,0,718,119,1,0,0,0,
  	719,724,5,77,0,0,720,721,5,19,0,0,721,722,3,110,55,0,722,723,5,20,0,0,
  	723,725,1,0,0,0,724,720,1,0,0,0,724,725,1,0,0,0,725,810,1,0,0,0,726,733,
  	5,78,0,0,727,728,5,19,0,0,728,729,3,110,55,0,729,730,5,8,0,0,730,731,
  	3,110,55,0,731,732,5,20,0,0,732,734,1,0,0,0,733,727,1,0,0,0,733,734,1,
  	0,0,0,734,810,1,0,0,0,735,810,5,79,0,0,736,810,5,80,0,0,737,742,5,81,
  	0,0,738,739,5,19,0,0,739,740,3,110,55,0,740,741,5,20,0,0,741,743,1,0,
  	0,0,742,738,1,0,0,0,742,743,1,0,0,0,743,810,1,0,0,0,744,753,5,82,0,0,
  	745,747,5,19,0,0,746,748,3,42,21,0,747,746,1,0,0,0,747,748,1,0,0,0,748,
  	750,1,0,0,0,749,751,5,8,0,0,750,749,1,0,0,0,750,751,1,0,0,0,751,752,1,
  	0,0,0,752,754,5,20,0,0,753,745,1,0,0,0,753,754,1,0,0,0,754,810,1,0,0,
  	0,755,764,5,83,0,0,756,758,5,19,0,0,757,759,3,42,21,0,758,757,1,0,0,0,
  	758,759,1,0,0,0,759,761,1,0,0,0,760,762,5,8,0,0,761,760,1,0,0,0,761,762,
  	1,0,0,0,762,763,1,0,0,0,763,765,5,20,0,0,764,756,1,0,0,0,764,765,1,0,
  	0,0,765,810,1,0,0,0,766,775,5,84,0,0,767,768,5,19,0,0,768,771,3,110,55,
  	0,769,770,5,8,0,0,770,772,5,98,0,0,771,769,1,0,0,0,771,772,1,0,0,0,772,
  	773,1,0,0,0,773,774,5,20,0,0,774,776,1,0,0,0,775,767,1,0,0,0,775,776,
  	1,0,0,0,776,810,1,0,0,0,777,795,5,85,0,0,778,779,5,19,0,0,779,791,3,110,
  	55,0,780,781,5,8,0,0,781,782,5,6,0,0,782,787,5,98,0,0,783,784,5,8,0,0,
  	784,786,5,98,0,0,785,783,1,0,0,0,786,789,1,0,0,0,787,785,1,0,0,0,787,
  	788,1,0,0,0,788,790,1,0,0,0,789,787,1,0,0,0,790,792,5,7,0,0,791,780,1,
  	0,0,0,791,792,1,0,0,0,792,793,1,0,0,0,793,794,5,20,0,0,794,796,1,0,0,
  	0,795,778,1,0,0,0,795,796,1,0,0,0,796,810,1,0,0,0,797,799,5,9,0,0,798,
  	800,3,48,24,0,799,798,1,0,0,0,799,800,1,0,0,0,800,802,1,0,0,0,801,803,
  	5,8,0,0,802,801,1,0,0,0,802,803,1,0,0,0,803,804,1,0,0,0,804,810,5,10,
  	0,0,805,806,5,9,0,0,806,807,3,38,19,0,807,808,5,10,0,0,808,810,1,0,0,
  	0,809,719,1,0,0,0,809,726,1,0,0,0,809,735,1,0,0,0,809,736,1,0,0,0,809,
  	737,1,0,0,0,809,744,1,0,0,0,809,755,1,0,0,0,809,766,1,0,0,0,809,777,1,
  	0,0,0,809,797,1,0,0,0,809,805,1,0,0,0,810,121,1,0,0,0,811,812,7,3,0,0,
  	812,123,1,0,0,0,813,814,5,94,0,0,814,125,1,0,0,0,101,127,136,140,149,
  	155,163,168,176,184,195,198,201,208,214,217,222,227,232,240,245,256,260,
  	266,285,293,301,309,317,325,333,339,342,346,349,355,358,364,371,378,384,
  	387,393,396,402,405,411,414,420,423,429,432,438,441,447,450,456,459,477,
  	491,498,501,503,514,553,555,585,587,605,607,631,633,641,648,652,656,664,
  	675,677,683,688,700,704,707,711,724,733,742,747,750,753,758,761,764,771,
  	775,787,791,795,799,802,809
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
    setState(127);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -20055092090459552) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 66035122183) != 0)) {
      setState(126);
      stmtList();
    }
    setState(129);
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
    setState(131);
    stmt();
    setState(136);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 1, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(132);
        match(OpenCMLParser::SEP);
        setState(133);
        stmt(); 
      }
      setState(138);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 1, _ctx);
    }
    setState(140);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SEP) {
      setState(139);
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

OpenCMLParser::WaitStmtContext* OpenCMLParser::StmtContext::waitStmt() {
  return getRuleContext<OpenCMLParser::WaitStmtContext>(0);
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
    setState(149);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 3, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(142);
      letStmt();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(143);
      useStmt();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(144);
      typeStmt();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(145);
      exprStmt();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(146);
      waitStmt();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(147);
      funcDef();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(148);
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

OpenCMLParser::CarrierContext* OpenCMLParser::LetStmtContext::carrier() {
  return getRuleContext<OpenCMLParser::CarrierContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::LetStmtContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}

tree::TerminalNode* OpenCMLParser::LetStmtContext::LET() {
  return getToken(OpenCMLParser::LET, 0);
}

tree::TerminalNode* OpenCMLParser::LetStmtContext::VAR() {
  return getToken(OpenCMLParser::VAR, 0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::LetStmtContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
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
    setState(168);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::LET:
      case OpenCMLParser::VAR: {
        enterOuterAlt(_localctx, 1);
        setState(151);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::LET

        || _la == OpenCMLParser::VAR)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(152);
        carrier();
        setState(155);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(153);
          match(OpenCMLParser::T__0);
          setState(154);
          typeExpr(0);
        }
        setState(157);
        match(OpenCMLParser::T__1);
        setState(158);
        entityExpr(0);
        break;
      }

      case OpenCMLParser::T__5:
      case OpenCMLParser::T__8:
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(160);
        carrier();
        setState(163);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(161);
          match(OpenCMLParser::T__0);
          setState(162);
          typeExpr(0);
        }
        setState(165);
        match(OpenCMLParser::T__2);
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

tree::TerminalNode* OpenCMLParser::UseStmtContext::FROM() {
  return getToken(OpenCMLParser::FROM, 0);
}

tree::TerminalNode* OpenCMLParser::UseStmtContext::STRING() {
  return getToken(OpenCMLParser::STRING, 0);
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
    match(OpenCMLParser::USE);
    setState(171);
    carrier();
    setState(176);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__1: {
        setState(172);
        match(OpenCMLParser::T__1);
        setState(173);
        entityExpr(0);
        break;
      }

      case OpenCMLParser::FROM: {
        setState(174);
        match(OpenCMLParser::FROM);
        setState(175);
        match(OpenCMLParser::STRING);
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
    match(OpenCMLParser::TYPE);
    setState(179);
    identRef();
    setState(180);
    match(OpenCMLParser::T__1);
    setState(181);
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
    setState(184);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__4) {
      setState(183);
      annotations();
    }
    setState(186);
    entityExpr(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- WaitStmtContext ------------------------------------------------------------------

OpenCMLParser::WaitStmtContext::WaitStmtContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::WaitStmtContext::WAIT() {
  return getToken(OpenCMLParser::WAIT, 0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::WaitStmtContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}


size_t OpenCMLParser::WaitStmtContext::getRuleIndex() const {
  return OpenCMLParser::RuleWaitStmt;
}


std::any OpenCMLParser::WaitStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitWaitStmt(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::WaitStmtContext* OpenCMLParser::waitStmt() {
  WaitStmtContext *_localctx = _tracker.createInstance<WaitStmtContext>(_ctx, getState());
  enterRule(_localctx, 14, OpenCMLParser::RuleWaitStmt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(188);
    match(OpenCMLParser::WAIT);
    setState(189);
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
    setState(191);
    match(OpenCMLParser::WITH);
    setState(192);
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
    setState(195);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__4) {
      setState(194);
      annotations();
    }
    setState(198);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WITH) {
      setState(197);
      withDef();
    }
    setState(201);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -1152921504606846976) != 0)) {
      setState(200);
      modifiers();
    }
    setState(203);
    match(OpenCMLParser::FUNC);
    setState(204);
    identRef();
    setState(205);
    parentParams();
    setState(208);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(206);
      match(OpenCMLParser::T__0);
      setState(207);
      typeExpr(0);
    }
    setState(210);
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
    setState(212);
    match(OpenCMLParser::RETURN);
    setState(214);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -1152710398374139328) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 66035122183) != 0)) {
      setState(213);
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

OpenCMLParser::ParentParamsContext* OpenCMLParser::LambdaExprContext::parentParams() {
  return getRuleContext<OpenCMLParser::ParentParamsContext>(0);
}

OpenCMLParser::BracedStmtsContext* OpenCMLParser::LambdaExprContext::bracedStmts() {
  return getRuleContext<OpenCMLParser::BracedStmtsContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::LambdaExprContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}

OpenCMLParser::ModifiersContext* OpenCMLParser::LambdaExprContext::modifiers() {
  return getRuleContext<OpenCMLParser::ModifiersContext>(0);
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
    setState(217);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -1152921504606846976) != 0)) {
      setState(216);
      modifiers();
    }
    setState(219);
    parentParams();
    setState(222);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(220);
      match(OpenCMLParser::T__0);
      setState(221);
      typeExpr(0);
    }
    setState(224);
    match(OpenCMLParser::T__3);
    setState(227);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 16, _ctx)) {
    case 1: {
      setState(225);
      bracedStmts();
      break;
    }

    case 2: {
      setState(226);
      entityExpr(0);
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
    setState(232);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(229);
        identRef();
        break;
      }

      case OpenCMLParser::T__8: {
        enterOuterAlt(_localctx, 2);
        setState(230);
        bracedIdents();
        break;
      }

      case OpenCMLParser::T__5: {
        enterOuterAlt(_localctx, 3);
        setState(231);
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
    setState(234);
    match(OpenCMLParser::T__4);
    setState(235);
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
    setState(238); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(237);
      annotation();
      setState(240); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == OpenCMLParser::T__4);
   
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
    setState(243); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(242);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & -1152921504606846976) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(245); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -1152921504606846976) != 0));
   
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
    setState(247);
    identRef();
    setState(248);
    match(OpenCMLParser::T__0);
    setState(249);
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
    setState(251);
    identRef();
    setState(252);
    match(OpenCMLParser::T__0);
    setState(253);
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

tree::TerminalNode* OpenCMLParser::KeyParamPairContext::VAR() {
  return getToken(OpenCMLParser::VAR, 0);
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
    setState(256);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR) {
      setState(255);
      match(OpenCMLParser::VAR);
    }
    setState(258);
    identRef();
    setState(260);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__4) {
      setState(259);
      annotation();
    }
    setState(262);
    match(OpenCMLParser::T__0);
    setState(263);
    typeExpr(0);
    setState(266);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(264);
      match(OpenCMLParser::T__1);
      setState(265);
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

//----------------- IndexKTPairContext ------------------------------------------------------------------

OpenCMLParser::IndexKTPairContext::IndexKTPairContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::EntityExprContext* OpenCMLParser::IndexKTPairContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::IndexKTPairContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}


size_t OpenCMLParser::IndexKTPairContext::getRuleIndex() const {
  return OpenCMLParser::RuleIndexKTPair;
}


std::any OpenCMLParser::IndexKTPairContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitIndexKTPair(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::IndexKTPairContext* OpenCMLParser::indexKTPair() {
  IndexKTPairContext *_localctx = _tracker.createInstance<IndexKTPairContext>(_ctx, getState());
  enterRule(_localctx, 38, OpenCMLParser::RuleIndexKTPair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(268);
    match(OpenCMLParser::T__5);
    setState(269);
    entityExpr(0);
    setState(270);
    match(OpenCMLParser::T__6);
    setState(271);
    match(OpenCMLParser::T__0);
    setState(272);
    typeExpr(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- IndexKVPairContext ------------------------------------------------------------------

OpenCMLParser::IndexKVPairContext::IndexKVPairContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::EntityExprContext *> OpenCMLParser::IndexKVPairContext::entityExpr() {
  return getRuleContexts<OpenCMLParser::EntityExprContext>();
}

OpenCMLParser::EntityExprContext* OpenCMLParser::IndexKVPairContext::entityExpr(size_t i) {
  return getRuleContext<OpenCMLParser::EntityExprContext>(i);
}


size_t OpenCMLParser::IndexKVPairContext::getRuleIndex() const {
  return OpenCMLParser::RuleIndexKVPair;
}


std::any OpenCMLParser::IndexKVPairContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitIndexKVPair(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::IndexKVPairContext* OpenCMLParser::indexKVPair() {
  IndexKVPairContext *_localctx = _tracker.createInstance<IndexKVPairContext>(_ctx, getState());
  enterRule(_localctx, 40, OpenCMLParser::RuleIndexKVPair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(274);
    match(OpenCMLParser::T__5);
    setState(275);
    entityExpr(0);
    setState(276);
    match(OpenCMLParser::T__6);
    setState(277);
    match(OpenCMLParser::T__0);
    setState(278);
    entityExpr(0);
   
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
  enterRule(_localctx, 42, OpenCMLParser::RuleTypeList);

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
    typeExpr(0);
    setState(285);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(281);
        match(OpenCMLParser::T__7);
        setState(282);
        typeExpr(0); 
      }
      setState(287);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23, _ctx);
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
  enterRule(_localctx, 44, OpenCMLParser::RuleIdentList);

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
    identRef();
    setState(293);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 24, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(289);
        match(OpenCMLParser::T__7);
        setState(290);
        identRef(); 
      }
      setState(295);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 24, _ctx);
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
  enterRule(_localctx, 46, OpenCMLParser::RuleValueList);

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
    entityExpr(0);
    setState(301);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 25, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(297);
        match(OpenCMLParser::T__7);
        setState(298);
        entityExpr(0); 
      }
      setState(303);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 25, _ctx);
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
  enterRule(_localctx, 48, OpenCMLParser::RulePairedTypes);

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
    keyTypePair();
    setState(309);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(305);
        match(OpenCMLParser::T__7);
        setState(306);
        keyTypePair(); 
      }
      setState(311);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
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
  enterRule(_localctx, 50, OpenCMLParser::RulePairedValues);

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
    keyValuePair();
    setState(317);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(313);
        match(OpenCMLParser::T__7);
        setState(314);
        keyValuePair(); 
      }
      setState(319);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx);
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
  enterRule(_localctx, 52, OpenCMLParser::RulePairedParams);

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
    setState(320);
    keyParamPair();
    setState(325);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(321);
        match(OpenCMLParser::T__7);
        setState(322);
        keyParamPair(); 
      }
      setState(327);
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

//----------------- IndexKVPairsContext ------------------------------------------------------------------

OpenCMLParser::IndexKVPairsContext::IndexKVPairsContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::IndexKVPairContext *> OpenCMLParser::IndexKVPairsContext::indexKVPair() {
  return getRuleContexts<OpenCMLParser::IndexKVPairContext>();
}

OpenCMLParser::IndexKVPairContext* OpenCMLParser::IndexKVPairsContext::indexKVPair(size_t i) {
  return getRuleContext<OpenCMLParser::IndexKVPairContext>(i);
}


size_t OpenCMLParser::IndexKVPairsContext::getRuleIndex() const {
  return OpenCMLParser::RuleIndexKVPairs;
}


std::any OpenCMLParser::IndexKVPairsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitIndexKVPairs(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::IndexKVPairsContext* OpenCMLParser::indexKVPairs() {
  IndexKVPairsContext *_localctx = _tracker.createInstance<IndexKVPairsContext>(_ctx, getState());
  enterRule(_localctx, 54, OpenCMLParser::RuleIndexKVPairs);

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
    setState(328);
    indexKVPair();
    setState(333);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(329);
        match(OpenCMLParser::T__7);
        setState(330);
        indexKVPair(); 
      }
      setState(335);
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
  enterRule(_localctx, 56, OpenCMLParser::RuleArgumentList);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(342);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(336);
      valueList();
      setState(339);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx)) {
      case 1: {
        setState(337);
        match(OpenCMLParser::T__7);
        setState(338);
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
      setState(341);
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
  enterRule(_localctx, 58, OpenCMLParser::RuleBracedPairedValues);
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
    setState(344);
    match(OpenCMLParser::T__8);
    setState(346);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(345);
      pairedValues();
    }
    setState(349);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(348);
      match(OpenCMLParser::T__7);
    }
    setState(351);
    match(OpenCMLParser::T__9);
   
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
  enterRule(_localctx, 60, OpenCMLParser::RuleBracedIdents);
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
    setState(353);
    match(OpenCMLParser::T__8);
    setState(355);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(354);
      identList();
    }
    setState(358);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(357);
      match(OpenCMLParser::T__7);
    }
    setState(360);
    match(OpenCMLParser::T__9);
   
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
  enterRule(_localctx, 62, OpenCMLParser::RuleBracedStmts);
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
    setState(362);
    match(OpenCMLParser::T__8);
    setState(364);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -20055092090459552) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 66035122183) != 0)) {
      setState(363);
      stmtList();
    }
    setState(366);
    match(OpenCMLParser::T__9);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BracedHomoValuesContext ------------------------------------------------------------------

OpenCMLParser::BracedHomoValuesContext::BracedHomoValuesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ValueListContext* OpenCMLParser::BracedHomoValuesContext::valueList() {
  return getRuleContext<OpenCMLParser::ValueListContext>(0);
}


size_t OpenCMLParser::BracedHomoValuesContext::getRuleIndex() const {
  return OpenCMLParser::RuleBracedHomoValues;
}


std::any OpenCMLParser::BracedHomoValuesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBracedHomoValues(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BracedHomoValuesContext* OpenCMLParser::bracedHomoValues() {
  BracedHomoValuesContext *_localctx = _tracker.createInstance<BracedHomoValuesContext>(_ctx, getState());
  enterRule(_localctx, 64, OpenCMLParser::RuleBracedHomoValues);
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
    setState(368);
    match(OpenCMLParser::T__10);
    setState(369);
    valueList();
    setState(371);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(370);
      match(OpenCMLParser::T__7);
    }
    setState(373);
    match(OpenCMLParser::T__11);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BracedIndexKVPairsContext ------------------------------------------------------------------

OpenCMLParser::BracedIndexKVPairsContext::BracedIndexKVPairsContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IndexKVPairsContext* OpenCMLParser::BracedIndexKVPairsContext::indexKVPairs() {
  return getRuleContext<OpenCMLParser::IndexKVPairsContext>(0);
}


size_t OpenCMLParser::BracedIndexKVPairsContext::getRuleIndex() const {
  return OpenCMLParser::RuleBracedIndexKVPairs;
}


std::any OpenCMLParser::BracedIndexKVPairsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBracedIndexKVPairs(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BracedIndexKVPairsContext* OpenCMLParser::bracedIndexKVPairs() {
  BracedIndexKVPairsContext *_localctx = _tracker.createInstance<BracedIndexKVPairsContext>(_ctx, getState());
  enterRule(_localctx, 66, OpenCMLParser::RuleBracedIndexKVPairs);
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
    setState(375);
    match(OpenCMLParser::T__8);
    setState(376);
    indexKVPairs();
    setState(378);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(377);
      match(OpenCMLParser::T__7);
    }
    setState(380);
    match(OpenCMLParser::T__9);
   
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
  enterRule(_localctx, 68, OpenCMLParser::RuleBracketIdents);
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
    setState(382);
    match(OpenCMLParser::T__5);
    setState(384);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(383);
      identList();
    }
    setState(387);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(386);
      match(OpenCMLParser::T__7);
    }
    setState(389);
    match(OpenCMLParser::T__6);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BracketHomoValuesContext ------------------------------------------------------------------

OpenCMLParser::BracketHomoValuesContext::BracketHomoValuesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ValueListContext* OpenCMLParser::BracketHomoValuesContext::valueList() {
  return getRuleContext<OpenCMLParser::ValueListContext>(0);
}


size_t OpenCMLParser::BracketHomoValuesContext::getRuleIndex() const {
  return OpenCMLParser::RuleBracketHomoValues;
}


std::any OpenCMLParser::BracketHomoValuesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBracketHomoValues(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BracketHomoValuesContext* OpenCMLParser::bracketHomoValues() {
  BracketHomoValuesContext *_localctx = _tracker.createInstance<BracketHomoValuesContext>(_ctx, getState());
  enterRule(_localctx, 70, OpenCMLParser::RuleBracketHomoValues);
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
    setState(391);
    match(OpenCMLParser::T__12);
    setState(393);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -1152710398374139328) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 66035122183) != 0)) {
      setState(392);
      valueList();
    }
    setState(396);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(395);
      match(OpenCMLParser::T__7);
    }
    setState(398);
    match(OpenCMLParser::T__13);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BracketHeteValuesContext ------------------------------------------------------------------

OpenCMLParser::BracketHeteValuesContext::BracketHeteValuesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ValueListContext* OpenCMLParser::BracketHeteValuesContext::valueList() {
  return getRuleContext<OpenCMLParser::ValueListContext>(0);
}


size_t OpenCMLParser::BracketHeteValuesContext::getRuleIndex() const {
  return OpenCMLParser::RuleBracketHeteValues;
}


std::any OpenCMLParser::BracketHeteValuesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBracketHeteValues(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BracketHeteValuesContext* OpenCMLParser::bracketHeteValues() {
  BracketHeteValuesContext *_localctx = _tracker.createInstance<BracketHeteValuesContext>(_ctx, getState());
  enterRule(_localctx, 72, OpenCMLParser::RuleBracketHeteValues);
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
    setState(400);
    match(OpenCMLParser::T__5);
    setState(402);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -1152710398374139328) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 66035122183) != 0)) {
      setState(401);
      valueList();
    }
    setState(405);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(404);
      match(OpenCMLParser::T__7);
    }
    setState(407);
    match(OpenCMLParser::T__6);
   
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
  enterRule(_localctx, 74, OpenCMLParser::RuleParentParams);
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
    setState(409);
    match(OpenCMLParser::T__14);
    setState(411);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(410);
      pairedParams();
    }
    setState(414);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(413);
      match(OpenCMLParser::T__7);
    }
    setState(416);
    match(OpenCMLParser::T__15);
   
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
  enterRule(_localctx, 76, OpenCMLParser::RuleParentValues);
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
    setState(418);
    match(OpenCMLParser::T__14);
    setState(420);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -1152710398374139328) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 66035122183) != 0)) {
      setState(419);
      argumentList();
    }
    setState(423);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(422);
      match(OpenCMLParser::T__7);
    }
    setState(425);
    match(OpenCMLParser::T__15);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParentHomoValuesContext ------------------------------------------------------------------

OpenCMLParser::ParentHomoValuesContext::ParentHomoValuesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ValueListContext* OpenCMLParser::ParentHomoValuesContext::valueList() {
  return getRuleContext<OpenCMLParser::ValueListContext>(0);
}


size_t OpenCMLParser::ParentHomoValuesContext::getRuleIndex() const {
  return OpenCMLParser::RuleParentHomoValues;
}


std::any OpenCMLParser::ParentHomoValuesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitParentHomoValues(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ParentHomoValuesContext* OpenCMLParser::parentHomoValues() {
  ParentHomoValuesContext *_localctx = _tracker.createInstance<ParentHomoValuesContext>(_ctx, getState());
  enterRule(_localctx, 78, OpenCMLParser::RuleParentHomoValues);
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
    setState(427);
    match(OpenCMLParser::T__16);
    setState(429);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -1152710398374139328) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 66035122183) != 0)) {
      setState(428);
      valueList();
    }
    setState(432);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(431);
      match(OpenCMLParser::T__7);
    }
    setState(434);
    match(OpenCMLParser::T__17);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParentHeteValuesContext ------------------------------------------------------------------

OpenCMLParser::ParentHeteValuesContext::ParentHeteValuesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ValueListContext* OpenCMLParser::ParentHeteValuesContext::valueList() {
  return getRuleContext<OpenCMLParser::ValueListContext>(0);
}


size_t OpenCMLParser::ParentHeteValuesContext::getRuleIndex() const {
  return OpenCMLParser::RuleParentHeteValues;
}


std::any OpenCMLParser::ParentHeteValuesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitParentHeteValues(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ParentHeteValuesContext* OpenCMLParser::parentHeteValues() {
  ParentHeteValuesContext *_localctx = _tracker.createInstance<ParentHeteValuesContext>(_ctx, getState());
  enterRule(_localctx, 80, OpenCMLParser::RuleParentHeteValues);
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
    setState(436);
    match(OpenCMLParser::T__14);
    setState(438);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -1152710398374139328) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 66035122183) != 0)) {
      setState(437);
      valueList();
    }
    setState(441);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(440);
      match(OpenCMLParser::T__7);
    }
    setState(443);
    match(OpenCMLParser::T__15);
   
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
  enterRule(_localctx, 82, OpenCMLParser::RuleAngledParams);
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
    setState(445);
    match(OpenCMLParser::T__18);
    setState(447);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(446);
      pairedParams();
    }
    setState(450);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(449);
      match(OpenCMLParser::T__7);
    }
    setState(452);
    match(OpenCMLParser::T__19);
   
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
  enterRule(_localctx, 84, OpenCMLParser::RuleAngledValues);
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
    setState(454);
    match(OpenCMLParser::T__18);
    setState(456);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -1152710398374139328) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 66035122183) != 0)) {
      setState(455);
      argumentList();
    }
    setState(459);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(458);
      match(OpenCMLParser::T__7);
    }
    setState(461);
    match(OpenCMLParser::T__19);
   
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

OpenCMLParser::BracedIndexKVPairsContext* OpenCMLParser::PrimEntityContext::bracedIndexKVPairs() {
  return getRuleContext<OpenCMLParser::BracedIndexKVPairsContext>(0);
}

OpenCMLParser::BracedHomoValuesContext* OpenCMLParser::PrimEntityContext::bracedHomoValues() {
  return getRuleContext<OpenCMLParser::BracedHomoValuesContext>(0);
}

OpenCMLParser::BracedPairedValuesContext* OpenCMLParser::PrimEntityContext::bracedPairedValues() {
  return getRuleContext<OpenCMLParser::BracedPairedValuesContext>(0);
}

OpenCMLParser::BracketHeteValuesContext* OpenCMLParser::PrimEntityContext::bracketHeteValues() {
  return getRuleContext<OpenCMLParser::BracketHeteValuesContext>(0);
}

OpenCMLParser::BracketHomoValuesContext* OpenCMLParser::PrimEntityContext::bracketHomoValues() {
  return getRuleContext<OpenCMLParser::BracketHomoValuesContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::PrimEntityContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}

OpenCMLParser::ParentHeteValuesContext* OpenCMLParser::PrimEntityContext::parentHeteValues() {
  return getRuleContext<OpenCMLParser::ParentHeteValuesContext>(0);
}

OpenCMLParser::ParentHomoValuesContext* OpenCMLParser::PrimEntityContext::parentHomoValues() {
  return getRuleContext<OpenCMLParser::ParentHomoValuesContext>(0);
}

OpenCMLParser::LambdaExprContext* OpenCMLParser::PrimEntityContext::lambdaExpr() {
  return getRuleContext<OpenCMLParser::LambdaExprContext>(0);
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
  enterRule(_localctx, 86, OpenCMLParser::RulePrimEntity);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(477);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 57, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(463);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(464);
      literal();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(465);
      bracedIndexKVPairs();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(466);
      bracedHomoValues();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(467);
      bracedPairedValues();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(468);
      bracketHeteValues();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(469);
      bracketHomoValues();
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(470);
      match(OpenCMLParser::T__14);
      setState(471);
      entityExpr(0);
      setState(472);
      match(OpenCMLParser::T__15);
      break;
    }

    case 9: {
      enterOuterAlt(_localctx, 9);
      setState(474);
      parentHeteValues();
      break;
    }

    case 10: {
      enterOuterAlt(_localctx, 10);
      setState(475);
      parentHomoValues();
      break;
    }

    case 11: {
      enterOuterAlt(_localctx, 11);
      setState(476);
      lambdaExpr();
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
  enterRule(_localctx, 88, OpenCMLParser::RuleMemberAccess);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(479);
    match(OpenCMLParser::T__5);
    setState(480);
    entityExpr(0);
    setState(481);
    match(OpenCMLParser::T__6);
   
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
  size_t startState = 90;
  enterRecursionRule(_localctx, 90, OpenCMLParser::RuleEntityLink, precedence);

    

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
    setState(484);
    entityUnit();
    _ctx->stop = _input->LT(-1);
    setState(491);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 58, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EntityLinkContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEntityLink);
        setState(486);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(487);
        match(OpenCMLParser::T__20);
        setState(488);
        entityUnit(); 
      }
      setState(493);
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
  enterRule(_localctx, 92, OpenCMLParser::RuleEntityUnit);

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
    setState(494);
    entityWith(0);
    setState(503);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(501);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case OpenCMLParser::T__5:
          case OpenCMLParser::T__14:
          case OpenCMLParser::T__18: {
            setState(498);
            _errHandler->sync(this);
            switch (_input->LA(1)) {
              case OpenCMLParser::T__5: {
                setState(495);
                memberAccess();
                break;
              }

              case OpenCMLParser::T__18: {
                setState(496);
                angledValues();
                break;
              }

              case OpenCMLParser::T__14: {
                setState(497);
                parentValues();
                break;
              }

            default:
              throw NoViableAltException(this);
            }
            break;
          }

          case OpenCMLParser::T__4: {
            setState(500);
            annotation();
            break;
          }

        default:
          throw NoViableAltException(this);
        } 
      }
      setState(505);
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
  size_t startState = 94;
  enterRecursionRule(_localctx, 94, OpenCMLParser::RuleEntityWith, precedence);

    

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
    setState(507);
    primEntity();
    _ctx->stop = _input->LT(-1);
    setState(514);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 62, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EntityWithContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEntityWith);
        setState(509);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(510);
        match(OpenCMLParser::T__21);
        setState(511);
        primEntity(); 
      }
      setState(516);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 62, _ctx);
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

std::vector<OpenCMLParser::EntityExprContext *> OpenCMLParser::EntityExprContext::entityExpr() {
  return getRuleContexts<OpenCMLParser::EntityExprContext>();
}

OpenCMLParser::EntityExprContext* OpenCMLParser::EntityExprContext::entityExpr(size_t i) {
  return getRuleContext<OpenCMLParser::EntityExprContext>(i);
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
  size_t startState = 96;
  enterRecursionRule(_localctx, 96, OpenCMLParser::RuleEntityExpr, precedence);

    

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
    setState(518);
    relaExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(555);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 64, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(553);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 63, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(520);

          if (!(precpred(_ctx, 10))) throw FailedPredicateException(this, "precpred(_ctx, 10)");
          setState(521);
          match(OpenCMLParser::T__22);
          setState(522);
          entityExpr(0);
          setState(523);
          match(OpenCMLParser::T__0);
          setState(524);
          entityExpr(11);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(526);

          if (!(precpred(_ctx, 9))) throw FailedPredicateException(this, "precpred(_ctx, 9)");
          setState(527);
          match(OpenCMLParser::T__1);
          setState(528);
          relaExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(529);

          if (!(precpred(_ctx, 8))) throw FailedPredicateException(this, "precpred(_ctx, 8)");
          setState(530);
          match(OpenCMLParser::T__23);
          setState(531);
          relaExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(532);

          if (!(precpred(_ctx, 7))) throw FailedPredicateException(this, "precpred(_ctx, 7)");
          setState(533);
          match(OpenCMLParser::T__24);
          setState(534);
          relaExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(535);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(536);
          match(OpenCMLParser::T__25);
          setState(537);
          relaExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(538);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(539);
          match(OpenCMLParser::T__26);
          setState(540);
          relaExpr(0);
          break;
        }

        case 7: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(541);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(542);
          match(OpenCMLParser::T__27);
          setState(543);
          relaExpr(0);
          break;
        }

        case 8: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(544);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(545);
          match(OpenCMLParser::T__28);
          setState(546);
          relaExpr(0);
          break;
        }

        case 9: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(547);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(548);
          match(OpenCMLParser::T__29);
          setState(549);
          relaExpr(0);
          break;
        }

        case 10: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(550);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(551);
          match(OpenCMLParser::T__30);
          setState(552);
          relaExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(557);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 64, _ctx);
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
  size_t startState = 98;
  enterRecursionRule(_localctx, 98, OpenCMLParser::RuleRelaExpr, precedence);

    

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
    setState(559);
    addExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(587);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 66, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(585);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 65, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(561);

          if (!(precpred(_ctx, 8))) throw FailedPredicateException(this, "precpred(_ctx, 8)");
          setState(562);
          match(OpenCMLParser::T__18);
          setState(563);
          addExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(564);

          if (!(precpred(_ctx, 7))) throw FailedPredicateException(this, "precpred(_ctx, 7)");
          setState(565);
          match(OpenCMLParser::T__19);
          setState(566);
          addExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(567);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(568);
          match(OpenCMLParser::T__31);
          setState(569);
          addExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(570);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(571);
          match(OpenCMLParser::T__32);
          setState(572);
          addExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(573);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(574);
          match(OpenCMLParser::T__33);
          setState(575);
          addExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(576);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(577);
          match(OpenCMLParser::T__34);
          setState(578);
          addExpr(0);
          break;
        }

        case 7: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(579);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(580);
          match(OpenCMLParser::T__35);
          setState(581);
          addExpr(0);
          break;
        }

        case 8: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(582);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(583);
          match(OpenCMLParser::T__36);
          setState(584);
          addExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(589);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 66, _ctx);
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
  size_t startState = 100;
  enterRecursionRule(_localctx, 100, OpenCMLParser::RuleAddExpr, precedence);

    

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
    setState(591);
    multiExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(607);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 68, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(605);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 67, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(593);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(594);
          match(OpenCMLParser::T__37);
          setState(595);
          multiExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(596);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(597);
          match(OpenCMLParser::T__38);
          setState(598);
          multiExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(599);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(600);
          match(OpenCMLParser::T__39);
          setState(601);
          multiExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(602);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(603);
          match(OpenCMLParser::T__40);
          setState(604);
          multiExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(609);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 68, _ctx);
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
  size_t startState = 102;
  enterRecursionRule(_localctx, 102, OpenCMLParser::RuleMultiExpr, precedence);

    

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
    setState(611);
    unaryExpr();
    _ctx->stop = _input->LT(-1);
    setState(633);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(631);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(613);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(614);
          match(OpenCMLParser::T__41);
          setState(615);
          unaryExpr();
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(616);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(617);
          match(OpenCMLParser::T__42);
          setState(618);
          unaryExpr();
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(619);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(620);
          match(OpenCMLParser::T__43);
          setState(621);
          unaryExpr();
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(622);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(623);
          match(OpenCMLParser::T__44);
          setState(624);
          unaryExpr();
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(625);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(626);
          match(OpenCMLParser::AS);
          setState(627);
          typeExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(628);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(629);
          match(OpenCMLParser::IS);
          setState(630);
          typeExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(635);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx);
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
  enterRule(_localctx, 104, OpenCMLParser::RuleUnaryExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(641);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__5:
      case OpenCMLParser::T__8:
      case OpenCMLParser::T__10:
      case OpenCMLParser::T__12:
      case OpenCMLParser::T__14:
      case OpenCMLParser::T__16:
      case OpenCMLParser::INNER:
      case OpenCMLParser::OUTER:
      case OpenCMLParser::ATOMIC:
      case OpenCMLParser::STATIC:
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
        setState(636);
        primExpr();
        break;
      }

      case OpenCMLParser::T__45: {
        enterOuterAlt(_localctx, 2);
        setState(637);
        match(OpenCMLParser::T__45);
        setState(638);
        primExpr();
        break;
      }

      case OpenCMLParser::T__46: {
        enterOuterAlt(_localctx, 3);
        setState(639);
        match(OpenCMLParser::T__46);
        setState(640);
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

OpenCMLParser::EntityLinkContext* OpenCMLParser::PrimExprContext::entityLink() {
  return getRuleContext<OpenCMLParser::EntityLinkContext>(0);
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
  enterRule(_localctx, 106, OpenCMLParser::RulePrimExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(648);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 72, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(643);
      entityLink(0);
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(644);
      match(OpenCMLParser::T__14);
      setState(645);
      entityExpr(0);
      setState(646);
      match(OpenCMLParser::T__15);
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
  enterRule(_localctx, 108, OpenCMLParser::RuleLiteral);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(664);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER: {
        enterOuterAlt(_localctx, 1);
        setState(650);
        match(OpenCMLParser::INTEGER);
        setState(652);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 73, _ctx)) {
        case 1: {
          setState(651);
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
        setState(654);
        match(OpenCMLParser::REAL);
        setState(656);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx)) {
        case 1: {
          setState(655);
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
        setState(658);
        match(OpenCMLParser::STRING);
        break;
      }

      case OpenCMLParser::MULTI_STR: {
        enterOuterAlt(_localctx, 4);
        setState(659);
        match(OpenCMLParser::MULTI_STR);
        break;
      }

      case OpenCMLParser::FSTRING: {
        enterOuterAlt(_localctx, 5);
        setState(660);
        match(OpenCMLParser::FSTRING);
        break;
      }

      case OpenCMLParser::TRUE: {
        enterOuterAlt(_localctx, 6);
        setState(661);
        match(OpenCMLParser::TRUE);
        break;
      }

      case OpenCMLParser::FALSE: {
        enterOuterAlt(_localctx, 7);
        setState(662);
        match(OpenCMLParser::FALSE);
        break;
      }

      case OpenCMLParser::NULL_: {
        enterOuterAlt(_localctx, 8);
        setState(663);
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

OpenCMLParser::UnaryTypeContext* OpenCMLParser::TypeExprContext::unaryType() {
  return getRuleContext<OpenCMLParser::UnaryTypeContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::TypeExprContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
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
  size_t startState = 110;
  enterRecursionRule(_localctx, 110, OpenCMLParser::RuleTypeExpr, precedence);

    

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
    setState(667);
    unaryType();
    _ctx->stop = _input->LT(-1);
    setState(677);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 77, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(675);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 76, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<TypeExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleTypeExpr);
          setState(669);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(670);
          match(OpenCMLParser::T__39);
          setState(671);
          unaryType();
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<TypeExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleTypeExpr);
          setState(672);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(673);
          match(OpenCMLParser::T__40);
          setState(674);
          unaryType();
          break;
        }

        default:
          break;
        } 
      }
      setState(679);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 77, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- UnaryTypeContext ------------------------------------------------------------------

OpenCMLParser::UnaryTypeContext::UnaryTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::AtomTypeContext* OpenCMLParser::UnaryTypeContext::atomType() {
  return getRuleContext<OpenCMLParser::AtomTypeContext>(0);
}

std::vector<tree::TerminalNode *> OpenCMLParser::UnaryTypeContext::INTEGER() {
  return getTokens(OpenCMLParser::INTEGER);
}

tree::TerminalNode* OpenCMLParser::UnaryTypeContext::INTEGER(size_t i) {
  return getToken(OpenCMLParser::INTEGER, i);
}


size_t OpenCMLParser::UnaryTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleUnaryType;
}


std::any OpenCMLParser::UnaryTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitUnaryType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::UnaryTypeContext* OpenCMLParser::unaryType() {
  UnaryTypeContext *_localctx = _tracker.createInstance<UnaryTypeContext>(_ctx, getState());
  enterRule(_localctx, 112, OpenCMLParser::RuleUnaryType);
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
    setState(680);
    atomType();
    setState(688);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 79, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(681);
        match(OpenCMLParser::T__5);
        setState(683);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::INTEGER) {
          setState(682);
          match(OpenCMLParser::INTEGER);
        }
        setState(685);
        match(OpenCMLParser::T__6); 
      }
      setState(690);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 79, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AtomTypeContext ------------------------------------------------------------------

OpenCMLParser::AtomTypeContext::AtomTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimTypeContext* OpenCMLParser::AtomTypeContext::primType() {
  return getRuleContext<OpenCMLParser::PrimTypeContext>(0);
}

OpenCMLParser::StructTypeContext* OpenCMLParser::AtomTypeContext::structType() {
  return getRuleContext<OpenCMLParser::StructTypeContext>(0);
}

OpenCMLParser::SpecialTypeContext* OpenCMLParser::AtomTypeContext::specialType() {
  return getRuleContext<OpenCMLParser::SpecialTypeContext>(0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::AtomTypeContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::AtomTypeContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

OpenCMLParser::LambdaTypeContext* OpenCMLParser::AtomTypeContext::lambdaType() {
  return getRuleContext<OpenCMLParser::LambdaTypeContext>(0);
}


size_t OpenCMLParser::AtomTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleAtomType;
}


std::any OpenCMLParser::AtomTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitAtomType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::AtomTypeContext* OpenCMLParser::atomType() {
  AtomTypeContext *_localctx = _tracker.createInstance<AtomTypeContext>(_ctx, getState());
  enterRule(_localctx, 114, OpenCMLParser::RuleAtomType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(700);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 80, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(691);
      primType();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(692);
      structType();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(693);
      specialType();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(694);
      identRef();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(695);
      match(OpenCMLParser::T__14);
      setState(696);
      typeExpr(0);
      setState(697);
      match(OpenCMLParser::T__15);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(699);
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
  enterRule(_localctx, 116, OpenCMLParser::RuleLambdaType);
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
    setState(707);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__18) {
      setState(702);
      match(OpenCMLParser::T__18);
      setState(704);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::VAR

      || _la == OpenCMLParser::IDENTIFIER) {
        setState(703);
        pairedParams();
      }
      setState(706);
      match(OpenCMLParser::T__19);
    }
    setState(709);
    match(OpenCMLParser::T__14);
    setState(711);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(710);
      pairedParams();
    }
    setState(713);
    match(OpenCMLParser::T__15);
    setState(714);
    match(OpenCMLParser::T__3);
    setState(715);
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
  enterRule(_localctx, 118, OpenCMLParser::RulePrimType);
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
    setState(717);
    _la = _input->LA(1);
    if (!(((((_la - 67) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 67)) & 1023) != 0))) {
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

OpenCMLParser::PairedTypesContext* OpenCMLParser::StructTypeContext::pairedTypes() {
  return getRuleContext<OpenCMLParser::PairedTypesContext>(0);
}

OpenCMLParser::IndexKTPairContext* OpenCMLParser::StructTypeContext::indexKTPair() {
  return getRuleContext<OpenCMLParser::IndexKTPairContext>(0);
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
  enterRule(_localctx, 120, OpenCMLParser::RuleStructType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(809);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 100, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(719);
      match(OpenCMLParser::SET_TYPE);
      setState(724);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 84, _ctx)) {
      case 1: {
        setState(720);
        match(OpenCMLParser::T__18);
        setState(721);
        typeExpr(0);
        setState(722);
        match(OpenCMLParser::T__19);
        break;
      }

      default:
        break;
      }
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(726);
      match(OpenCMLParser::MAP_TYPE);
      setState(733);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 85, _ctx)) {
      case 1: {
        setState(727);
        match(OpenCMLParser::T__18);
        setState(728);
        typeExpr(0);
        setState(729);
        match(OpenCMLParser::T__7);
        setState(730);
        typeExpr(0);
        setState(731);
        match(OpenCMLParser::T__19);
        break;
      }

      default:
        break;
      }
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(735);
      match(OpenCMLParser::LIST_TYPE);
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(736);
      match(OpenCMLParser::DICT_TYPE);
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(737);
      match(OpenCMLParser::ARRAY_TYPE);
      setState(742);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 86, _ctx)) {
      case 1: {
        setState(738);
        match(OpenCMLParser::T__18);
        setState(739);
        typeExpr(0);
        setState(740);
        match(OpenCMLParser::T__19);
        break;
      }

      default:
        break;
      }
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(744);
      match(OpenCMLParser::TUPLE_TYPE);
      setState(753);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 89, _ctx)) {
      case 1: {
        setState(745);
        match(OpenCMLParser::T__18);
        setState(747);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 557568) != 0) || ((((_la - 67) & ~ 0x3fULL) == 0) &&
          ((1ULL << (_la - 67)) & 138412031) != 0)) {
          setState(746);
          typeList();
        }
        setState(750);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__7) {
          setState(749);
          match(OpenCMLParser::T__7);
        }
        setState(752);
        match(OpenCMLParser::T__19);
        break;
      }

      default:
        break;
      }
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(755);
      match(OpenCMLParser::UNION_TYPE);
      setState(764);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 92, _ctx)) {
      case 1: {
        setState(756);
        match(OpenCMLParser::T__18);
        setState(758);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 557568) != 0) || ((((_la - 67) & ~ 0x3fULL) == 0) &&
          ((1ULL << (_la - 67)) & 138412031) != 0)) {
          setState(757);
          typeList();
        }
        setState(761);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__7) {
          setState(760);
          match(OpenCMLParser::T__7);
        }
        setState(763);
        match(OpenCMLParser::T__19);
        break;
      }

      default:
        break;
      }
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(766);
      match(OpenCMLParser::VECTOR_TYPE);
      setState(775);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 94, _ctx)) {
      case 1: {
        setState(767);
        match(OpenCMLParser::T__18);
        setState(768);
        typeExpr(0);
        setState(771);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__7) {
          setState(769);
          match(OpenCMLParser::T__7);
          setState(770);
          match(OpenCMLParser::INTEGER);
        }
        setState(773);
        match(OpenCMLParser::T__19);
        break;
      }

      default:
        break;
      }
      break;
    }

    case 9: {
      enterOuterAlt(_localctx, 9);
      setState(777);
      match(OpenCMLParser::TENSOR_TYPE);
      setState(795);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 97, _ctx)) {
      case 1: {
        setState(778);
        match(OpenCMLParser::T__18);
        setState(779);
        typeExpr(0);
        setState(791);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__7) {
          setState(780);
          match(OpenCMLParser::T__7);
          setState(781);
          match(OpenCMLParser::T__5);
          setState(782);
          match(OpenCMLParser::INTEGER);
          setState(787);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == OpenCMLParser::T__7) {
            setState(783);
            match(OpenCMLParser::T__7);
            setState(784);
            match(OpenCMLParser::INTEGER);
            setState(789);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
          setState(790);
          match(OpenCMLParser::T__6);
        }
        setState(793);
        match(OpenCMLParser::T__19);
        break;
      }

      default:
        break;
      }
      break;
    }

    case 10: {
      enterOuterAlt(_localctx, 10);
      setState(797);
      match(OpenCMLParser::T__8);
      setState(799);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::IDENTIFIER) {
        setState(798);
        pairedTypes();
      }
      setState(802);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__7) {
        setState(801);
        match(OpenCMLParser::T__7);
      }
      setState(804);
      match(OpenCMLParser::T__9);
      break;
    }

    case 11: {
      enterOuterAlt(_localctx, 11);
      setState(805);
      match(OpenCMLParser::T__8);
      setState(806);
      indexKTPair();
      setState(807);
      match(OpenCMLParser::T__9);
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
  enterRule(_localctx, 122, OpenCMLParser::RuleSpecialType);
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
    setState(811);
    _la = _input->LA(1);
    if (!(((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 7) != 0))) {
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
  enterRule(_localctx, 124, OpenCMLParser::RuleIdentRef);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(813);
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
    case 45: return entityLinkSempred(antlrcpp::downCast<EntityLinkContext *>(context), predicateIndex);
    case 47: return entityWithSempred(antlrcpp::downCast<EntityWithContext *>(context), predicateIndex);
    case 48: return entityExprSempred(antlrcpp::downCast<EntityExprContext *>(context), predicateIndex);
    case 49: return relaExprSempred(antlrcpp::downCast<RelaExprContext *>(context), predicateIndex);
    case 50: return addExprSempred(antlrcpp::downCast<AddExprContext *>(context), predicateIndex);
    case 51: return multiExprSempred(antlrcpp::downCast<MultiExprContext *>(context), predicateIndex);
    case 55: return typeExprSempred(antlrcpp::downCast<TypeExprContext *>(context), predicateIndex);

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

bool OpenCMLParser::entityWithSempred(EntityWithContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 1: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::entityExprSempred(EntityExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 2: return precpred(_ctx, 10);
    case 3: return precpred(_ctx, 9);
    case 4: return precpred(_ctx, 8);
    case 5: return precpred(_ctx, 7);
    case 6: return precpred(_ctx, 6);
    case 7: return precpred(_ctx, 5);
    case 8: return precpred(_ctx, 4);
    case 9: return precpred(_ctx, 3);
    case 10: return precpred(_ctx, 2);
    case 11: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::relaExprSempred(RelaExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 12: return precpred(_ctx, 8);
    case 13: return precpred(_ctx, 7);
    case 14: return precpred(_ctx, 6);
    case 15: return precpred(_ctx, 5);
    case 16: return precpred(_ctx, 4);
    case 17: return precpred(_ctx, 3);
    case 18: return precpred(_ctx, 2);
    case 19: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::addExprSempred(AddExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 20: return precpred(_ctx, 4);
    case 21: return precpred(_ctx, 3);
    case 22: return precpred(_ctx, 2);
    case 23: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::multiExprSempred(MultiExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 24: return precpred(_ctx, 6);
    case 25: return precpred(_ctx, 5);
    case 26: return precpred(_ctx, 4);
    case 27: return precpred(_ctx, 3);
    case 28: return precpred(_ctx, 2);
    case 29: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::typeExprSempred(TypeExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 30: return precpred(_ctx, 2);
    case 31: return precpred(_ctx, 1);

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
