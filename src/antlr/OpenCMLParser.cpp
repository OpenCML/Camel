
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
      "bracedValues", "bracedIndexKVPairs", "bracketIdents", "bracketValues", 
      "memberAccess", "parentParams", "parentArgues", "parentValues", "angledParams", 
      "angledValues", "entityExpr", "ternaryExpr", "logicalOrExpr", "logicalAndExpr", 
      "equalityExpr", "relationalExpr", "additiveExpr", "multiplicativeExpr", 
      "unaryExpr", "linkExpr", "withExpr", "annotatedExpr", "primaryExpr", 
      "literal", "typeExpr", "arrayType", "atomType", "lambdaType", "primaryType", 
      "structType", "specialType", "identRef"
    },
    std::vector<std::string>{
      "", "':'", "'='", "':='", "'*'", "'=>'", "'@'", "'['", "']'", "','", 
      "'{'", "'}'", "'('", "')'", "'<'", "'>'", "'+='", "'-='", "'*='", 
      "'/='", "'%='", "'^='", "'&='", "'|='", "'\\u003F'", "'||'", "'&&'", 
      "'=='", "'!='", "'<='", "'>='", "'+'", "'-'", "'^'", "'/'", "'%'", 
      "'!'", "'~'", "'->'", "'.'", "'&'", "'|'", "';'", "'as'", "'is'", 
      "'let'", "'var'", "'use'", "'from'", "'func'", "'type'", "'with'", 
      "'wait'", "'return'", "'inner'", "'outer'", "'atomic'", "'static'", 
      "'null'", "'true'", "'false'", "'Int'", "'Int32'", "'Int64'", "'Real'", 
      "'Float'", "'Double'", "'Number'", "'String'", "'Bool'", "'Char'", 
      "'Set'", "'Map'", "'List'", "'Dict'", "'Array'", "'Tuple'", "'Union'", 
      "'Vector'", "'Tensor'", "'Any'", "'Void'", "'Functor'"
    },
    std::vector<std::string>{
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "SEP", "AS", "IS", "LET", "VAR", "USE", 
      "FROM", "FUNC", "TYPE", "WITH", "WAIT", "RETURN", "INNER", "OUTER", 
      "ATOMIC", "STATIC", "NULL", "TRUE", "FALSE", "INTEGER_TYPE", "INTEGER32_TYPE", 
      "INTEGER64_TYPE", "REAL_TYPE", "FLOAT_TYPE", "DOUBLE_TYPE", "NUMBER_TYPE", 
      "STRING_TYPE", "BOOL_TYPE", "CHAR_TYPE", "SET_TYPE", "MAP_TYPE", "LIST_TYPE", 
      "DICT_TYPE", "ARRAY_TYPE", "TUPLE_TYPE", "UNION_TYPE", "VECTOR_TYPE", 
      "TENSOR_TYPE", "ANY_TYPE", "VOID_TYPE", "FUNCTOR_TYPE", "BLANK", "LIN_CMT_HASH", 
      "LIN_CMT_SLASH", "BLK_CMT", "MULTI_STR", "IDENTIFIER", "UNIT", "STRING", 
      "FSTRING", "INTEGER", "REAL"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,93,747,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,2,55,7,55,2,56,7,
  	56,2,57,7,57,2,58,7,58,2,59,7,59,2,60,7,60,2,61,7,61,2,62,7,62,2,63,7,
  	63,1,0,3,0,130,8,0,1,0,1,0,1,1,1,1,1,1,5,1,137,8,1,10,1,12,1,140,9,1,
  	1,1,3,1,143,8,1,1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,2,152,8,2,1,3,1,3,1,3,1,
  	3,3,3,158,8,3,1,3,1,3,1,3,1,3,1,3,1,3,3,3,166,8,3,1,3,1,3,1,3,3,3,171,
  	8,3,1,4,1,4,1,4,3,4,176,8,4,1,4,1,4,1,4,1,4,3,4,182,8,4,1,5,1,5,1,5,1,
  	5,1,5,1,6,3,6,190,8,6,1,6,1,6,1,7,1,7,1,7,1,8,1,8,1,8,1,9,3,9,201,8,9,
  	1,9,3,9,204,8,9,1,9,3,9,207,8,9,1,9,1,9,1,9,1,9,1,9,3,9,214,8,9,1,9,1,
  	9,1,10,1,10,3,10,220,8,10,1,11,3,11,223,8,11,1,11,1,11,1,11,3,11,228,
  	8,11,1,11,1,11,1,11,3,11,233,8,11,1,12,1,12,1,12,3,12,238,8,12,1,13,1,
  	13,1,13,1,14,4,14,244,8,14,11,14,12,14,245,1,15,4,15,249,8,15,11,15,12,
  	15,250,1,16,1,16,1,16,1,16,1,17,1,17,1,17,1,17,1,18,3,18,262,8,18,1,18,
  	1,18,3,18,266,8,18,1,18,1,18,1,18,1,18,3,18,272,8,18,1,19,1,19,1,19,1,
  	19,1,19,1,19,1,20,1,20,1,20,1,20,1,20,1,20,1,21,1,21,1,21,5,21,289,8,
  	21,10,21,12,21,292,9,21,1,22,1,22,1,22,5,22,297,8,22,10,22,12,22,300,
  	9,22,1,23,1,23,1,23,5,23,305,8,23,10,23,12,23,308,9,23,1,24,1,24,1,24,
  	5,24,313,8,24,10,24,12,24,316,9,24,1,25,1,25,1,25,5,25,321,8,25,10,25,
  	12,25,324,9,25,1,26,1,26,1,26,5,26,329,8,26,10,26,12,26,332,9,26,1,27,
  	1,27,1,27,5,27,337,8,27,10,27,12,27,340,9,27,1,28,1,28,1,28,3,28,345,
  	8,28,1,28,3,28,348,8,28,1,29,1,29,3,29,352,8,29,1,29,3,29,355,8,29,1,
  	29,1,29,1,30,1,30,3,30,361,8,30,1,30,3,30,364,8,30,1,30,1,30,1,31,1,31,
  	3,31,370,8,31,1,31,1,31,1,32,1,32,1,32,3,32,377,8,32,1,32,1,32,1,33,1,
  	33,1,33,3,33,384,8,33,1,33,1,33,1,34,1,34,3,34,390,8,34,1,34,3,34,393,
  	8,34,1,34,1,34,1,35,1,35,3,35,399,8,35,1,35,3,35,402,8,35,1,35,1,35,1,
  	36,1,36,1,36,1,36,1,37,1,37,3,37,412,8,37,1,37,3,37,415,8,37,1,37,1,37,
  	1,38,1,38,3,38,421,8,38,1,38,3,38,424,8,38,1,38,1,38,1,39,1,39,3,39,430,
  	8,39,1,39,3,39,433,8,39,1,39,1,39,1,40,1,40,3,40,439,8,40,1,40,3,40,442,
  	8,40,1,40,1,40,1,41,1,41,3,41,448,8,41,1,41,3,41,451,8,41,1,41,1,41,1,
  	42,1,42,1,42,5,42,458,8,42,10,42,12,42,461,9,42,1,43,1,43,1,43,1,43,1,
  	43,1,43,3,43,469,8,43,1,44,1,44,1,44,5,44,474,8,44,10,44,12,44,477,9,
  	44,1,45,1,45,1,45,5,45,482,8,45,10,45,12,45,485,9,45,1,46,1,46,1,46,5,
  	46,490,8,46,10,46,12,46,493,9,46,1,47,1,47,1,47,5,47,498,8,47,10,47,12,
  	47,501,9,47,1,48,1,48,1,48,5,48,506,8,48,10,48,12,48,509,9,48,1,49,1,
  	49,1,49,5,49,514,8,49,10,49,12,49,517,9,49,1,50,1,50,1,50,3,50,522,8,
  	50,1,51,1,51,1,51,5,51,527,8,51,10,51,12,51,530,9,51,1,52,1,52,1,52,5,
  	52,535,8,52,10,52,12,52,538,9,52,1,53,1,53,1,53,1,53,1,53,5,53,545,8,
  	53,10,53,12,53,548,9,53,1,54,1,54,1,54,1,54,1,54,1,54,1,54,1,54,1,54,
  	1,54,1,54,1,54,1,54,1,54,1,54,1,54,5,54,566,8,54,10,54,12,54,569,9,54,
  	1,54,3,54,572,8,54,3,54,574,8,54,1,54,1,54,3,54,578,8,54,1,54,1,54,1,
  	54,3,54,583,8,54,1,54,3,54,586,8,54,1,55,1,55,3,55,590,8,55,1,55,1,55,
  	3,55,594,8,55,1,55,1,55,1,55,1,55,1,55,1,55,3,55,602,8,55,1,56,1,56,1,
  	56,5,56,607,8,56,10,56,12,56,610,9,56,1,57,1,57,1,57,3,57,615,8,57,1,
  	57,5,57,618,8,57,10,57,12,57,621,9,57,1,58,1,58,1,58,1,58,1,58,1,58,1,
  	58,1,58,1,58,3,58,632,8,58,1,59,1,59,3,59,636,8,59,1,59,3,59,639,8,59,
  	1,59,1,59,3,59,643,8,59,1,59,1,59,1,59,1,59,1,60,1,60,1,61,1,61,1,61,
  	1,61,1,61,3,61,656,8,61,1,61,1,61,1,61,1,61,1,61,1,61,1,61,3,61,665,8,
  	61,1,61,1,61,1,61,1,61,1,61,1,61,1,61,3,61,674,8,61,1,61,1,61,3,61,678,
  	8,61,1,61,1,61,1,61,3,61,683,8,61,1,61,3,61,686,8,61,1,61,3,61,689,8,
  	61,1,61,1,61,1,61,3,61,694,8,61,1,61,3,61,697,8,61,1,61,3,61,700,8,61,
  	1,61,1,61,1,61,1,61,1,61,3,61,707,8,61,1,61,1,61,1,61,1,61,1,61,1,61,
  	1,61,1,61,5,61,717,8,61,10,61,12,61,720,9,61,1,61,3,61,723,8,61,1,61,
  	1,61,3,61,727,8,61,1,61,1,61,3,61,731,8,61,1,61,3,61,734,8,61,1,61,1,
  	61,1,61,1,61,1,61,3,61,741,8,61,1,62,1,62,1,63,1,63,1,63,0,0,64,0,2,4,
  	6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,
  	54,56,58,60,62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,96,98,
  	100,102,104,106,108,110,112,114,116,118,120,122,124,126,0,11,1,0,45,46,
  	1,0,54,57,2,0,2,2,16,23,1,0,27,28,2,0,14,15,29,30,1,0,31,32,3,0,4,4,33,
  	35,43,44,1,0,36,37,2,0,33,33,40,41,1,0,61,70,1,0,80,82,815,0,129,1,0,
  	0,0,2,133,1,0,0,0,4,151,1,0,0,0,6,170,1,0,0,0,8,172,1,0,0,0,10,183,1,
  	0,0,0,12,189,1,0,0,0,14,193,1,0,0,0,16,196,1,0,0,0,18,200,1,0,0,0,20,
  	217,1,0,0,0,22,222,1,0,0,0,24,237,1,0,0,0,26,239,1,0,0,0,28,243,1,0,0,
  	0,30,248,1,0,0,0,32,252,1,0,0,0,34,256,1,0,0,0,36,261,1,0,0,0,38,273,
  	1,0,0,0,40,279,1,0,0,0,42,285,1,0,0,0,44,293,1,0,0,0,46,301,1,0,0,0,48,
  	309,1,0,0,0,50,317,1,0,0,0,52,325,1,0,0,0,54,333,1,0,0,0,56,347,1,0,0,
  	0,58,349,1,0,0,0,60,358,1,0,0,0,62,367,1,0,0,0,64,373,1,0,0,0,66,380,
  	1,0,0,0,68,387,1,0,0,0,70,396,1,0,0,0,72,405,1,0,0,0,74,409,1,0,0,0,76,
  	418,1,0,0,0,78,427,1,0,0,0,80,436,1,0,0,0,82,445,1,0,0,0,84,454,1,0,0,
  	0,86,462,1,0,0,0,88,470,1,0,0,0,90,478,1,0,0,0,92,486,1,0,0,0,94,494,
  	1,0,0,0,96,502,1,0,0,0,98,510,1,0,0,0,100,521,1,0,0,0,102,523,1,0,0,0,
  	104,531,1,0,0,0,106,539,1,0,0,0,108,585,1,0,0,0,110,601,1,0,0,0,112,603,
  	1,0,0,0,114,611,1,0,0,0,116,631,1,0,0,0,118,638,1,0,0,0,120,648,1,0,0,
  	0,122,740,1,0,0,0,124,742,1,0,0,0,126,744,1,0,0,0,128,130,3,2,1,0,129,
  	128,1,0,0,0,129,130,1,0,0,0,130,131,1,0,0,0,131,132,5,0,0,1,132,1,1,0,
  	0,0,133,138,3,4,2,0,134,135,5,42,0,0,135,137,3,4,2,0,136,134,1,0,0,0,
  	137,140,1,0,0,0,138,136,1,0,0,0,138,139,1,0,0,0,139,142,1,0,0,0,140,138,
  	1,0,0,0,141,143,5,42,0,0,142,141,1,0,0,0,142,143,1,0,0,0,143,3,1,0,0,
  	0,144,152,3,6,3,0,145,152,3,8,4,0,146,152,3,10,5,0,147,152,3,12,6,0,148,
  	152,3,14,7,0,149,152,3,18,9,0,150,152,3,20,10,0,151,144,1,0,0,0,151,145,
  	1,0,0,0,151,146,1,0,0,0,151,147,1,0,0,0,151,148,1,0,0,0,151,149,1,0,0,
  	0,151,150,1,0,0,0,152,5,1,0,0,0,153,154,7,0,0,0,154,157,3,24,12,0,155,
  	156,5,1,0,0,156,158,3,112,56,0,157,155,1,0,0,0,157,158,1,0,0,0,158,159,
  	1,0,0,0,159,160,5,2,0,0,160,161,3,84,42,0,161,171,1,0,0,0,162,165,3,24,
  	12,0,163,164,5,1,0,0,164,166,3,112,56,0,165,163,1,0,0,0,165,166,1,0,0,
  	0,166,167,1,0,0,0,167,168,5,3,0,0,168,169,3,84,42,0,169,171,1,0,0,0,170,
  	153,1,0,0,0,170,162,1,0,0,0,171,7,1,0,0,0,172,175,5,47,0,0,173,176,3,
  	24,12,0,174,176,5,4,0,0,175,173,1,0,0,0,175,174,1,0,0,0,176,181,1,0,0,
  	0,177,178,5,2,0,0,178,182,3,84,42,0,179,180,5,48,0,0,180,182,5,90,0,0,
  	181,177,1,0,0,0,181,179,1,0,0,0,182,9,1,0,0,0,183,184,5,50,0,0,184,185,
  	3,126,63,0,185,186,5,2,0,0,186,187,3,112,56,0,187,11,1,0,0,0,188,190,
  	3,28,14,0,189,188,1,0,0,0,189,190,1,0,0,0,190,191,1,0,0,0,191,192,3,84,
  	42,0,192,13,1,0,0,0,193,194,5,52,0,0,194,195,3,84,42,0,195,15,1,0,0,0,
  	196,197,5,51,0,0,197,198,3,80,40,0,198,17,1,0,0,0,199,201,3,28,14,0,200,
  	199,1,0,0,0,200,201,1,0,0,0,201,203,1,0,0,0,202,204,3,16,8,0,203,202,
  	1,0,0,0,203,204,1,0,0,0,204,206,1,0,0,0,205,207,3,30,15,0,206,205,1,0,
  	0,0,206,207,1,0,0,0,207,208,1,0,0,0,208,209,5,49,0,0,209,210,3,126,63,
  	0,210,213,3,74,37,0,211,212,5,1,0,0,212,214,3,112,56,0,213,211,1,0,0,
  	0,213,214,1,0,0,0,214,215,1,0,0,0,215,216,3,62,31,0,216,19,1,0,0,0,217,
  	219,5,53,0,0,218,220,3,84,42,0,219,218,1,0,0,0,219,220,1,0,0,0,220,21,
  	1,0,0,0,221,223,3,30,15,0,222,221,1,0,0,0,222,223,1,0,0,0,223,224,1,0,
  	0,0,224,227,3,74,37,0,225,226,5,1,0,0,226,228,3,112,56,0,227,225,1,0,
  	0,0,227,228,1,0,0,0,228,229,1,0,0,0,229,232,5,5,0,0,230,233,3,62,31,0,
  	231,233,3,84,42,0,232,230,1,0,0,0,232,231,1,0,0,0,233,23,1,0,0,0,234,
  	238,3,126,63,0,235,238,3,60,30,0,236,238,3,68,34,0,237,234,1,0,0,0,237,
  	235,1,0,0,0,237,236,1,0,0,0,238,25,1,0,0,0,239,240,5,6,0,0,240,241,3,
  	108,54,0,241,27,1,0,0,0,242,244,3,26,13,0,243,242,1,0,0,0,244,245,1,0,
  	0,0,245,243,1,0,0,0,245,246,1,0,0,0,246,29,1,0,0,0,247,249,7,1,0,0,248,
  	247,1,0,0,0,249,250,1,0,0,0,250,248,1,0,0,0,250,251,1,0,0,0,251,31,1,
  	0,0,0,252,253,3,126,63,0,253,254,5,1,0,0,254,255,3,112,56,0,255,33,1,
  	0,0,0,256,257,3,126,63,0,257,258,5,1,0,0,258,259,3,84,42,0,259,35,1,0,
  	0,0,260,262,5,46,0,0,261,260,1,0,0,0,261,262,1,0,0,0,262,263,1,0,0,0,
  	263,265,3,126,63,0,264,266,3,26,13,0,265,264,1,0,0,0,265,266,1,0,0,0,
  	266,267,1,0,0,0,267,268,5,1,0,0,268,271,3,112,56,0,269,270,5,2,0,0,270,
  	272,3,84,42,0,271,269,1,0,0,0,271,272,1,0,0,0,272,37,1,0,0,0,273,274,
  	5,7,0,0,274,275,3,84,42,0,275,276,5,8,0,0,276,277,5,1,0,0,277,278,3,112,
  	56,0,278,39,1,0,0,0,279,280,5,7,0,0,280,281,3,84,42,0,281,282,5,8,0,0,
  	282,283,5,1,0,0,283,284,3,84,42,0,284,41,1,0,0,0,285,290,3,112,56,0,286,
  	287,5,9,0,0,287,289,3,112,56,0,288,286,1,0,0,0,289,292,1,0,0,0,290,288,
  	1,0,0,0,290,291,1,0,0,0,291,43,1,0,0,0,292,290,1,0,0,0,293,298,3,126,
  	63,0,294,295,5,9,0,0,295,297,3,126,63,0,296,294,1,0,0,0,297,300,1,0,0,
  	0,298,296,1,0,0,0,298,299,1,0,0,0,299,45,1,0,0,0,300,298,1,0,0,0,301,
  	306,3,84,42,0,302,303,5,9,0,0,303,305,3,84,42,0,304,302,1,0,0,0,305,308,
  	1,0,0,0,306,304,1,0,0,0,306,307,1,0,0,0,307,47,1,0,0,0,308,306,1,0,0,
  	0,309,314,3,32,16,0,310,311,5,9,0,0,311,313,3,32,16,0,312,310,1,0,0,0,
  	313,316,1,0,0,0,314,312,1,0,0,0,314,315,1,0,0,0,315,49,1,0,0,0,316,314,
  	1,0,0,0,317,322,3,34,17,0,318,319,5,9,0,0,319,321,3,34,17,0,320,318,1,
  	0,0,0,321,324,1,0,0,0,322,320,1,0,0,0,322,323,1,0,0,0,323,51,1,0,0,0,
  	324,322,1,0,0,0,325,330,3,36,18,0,326,327,5,9,0,0,327,329,3,36,18,0,328,
  	326,1,0,0,0,329,332,1,0,0,0,330,328,1,0,0,0,330,331,1,0,0,0,331,53,1,
  	0,0,0,332,330,1,0,0,0,333,338,3,40,20,0,334,335,5,9,0,0,335,337,3,40,
  	20,0,336,334,1,0,0,0,337,340,1,0,0,0,338,336,1,0,0,0,338,339,1,0,0,0,
  	339,55,1,0,0,0,340,338,1,0,0,0,341,344,3,46,23,0,342,343,5,9,0,0,343,
  	345,3,50,25,0,344,342,1,0,0,0,344,345,1,0,0,0,345,348,1,0,0,0,346,348,
  	3,50,25,0,347,341,1,0,0,0,347,346,1,0,0,0,348,57,1,0,0,0,349,351,5,10,
  	0,0,350,352,3,50,25,0,351,350,1,0,0,0,351,352,1,0,0,0,352,354,1,0,0,0,
  	353,355,5,9,0,0,354,353,1,0,0,0,354,355,1,0,0,0,355,356,1,0,0,0,356,357,
  	5,11,0,0,357,59,1,0,0,0,358,360,5,10,0,0,359,361,3,44,22,0,360,359,1,
  	0,0,0,360,361,1,0,0,0,361,363,1,0,0,0,362,364,5,9,0,0,363,362,1,0,0,0,
  	363,364,1,0,0,0,364,365,1,0,0,0,365,366,5,11,0,0,366,61,1,0,0,0,367,369,
  	5,10,0,0,368,370,3,2,1,0,369,368,1,0,0,0,369,370,1,0,0,0,370,371,1,0,
  	0,0,371,372,5,11,0,0,372,63,1,0,0,0,373,374,5,10,0,0,374,376,3,46,23,
  	0,375,377,5,9,0,0,376,375,1,0,0,0,376,377,1,0,0,0,377,378,1,0,0,0,378,
  	379,5,11,0,0,379,65,1,0,0,0,380,381,5,10,0,0,381,383,3,54,27,0,382,384,
  	5,9,0,0,383,382,1,0,0,0,383,384,1,0,0,0,384,385,1,0,0,0,385,386,5,11,
  	0,0,386,67,1,0,0,0,387,389,5,7,0,0,388,390,3,44,22,0,389,388,1,0,0,0,
  	389,390,1,0,0,0,390,392,1,0,0,0,391,393,5,9,0,0,392,391,1,0,0,0,392,393,
  	1,0,0,0,393,394,1,0,0,0,394,395,5,8,0,0,395,69,1,0,0,0,396,398,5,7,0,
  	0,397,399,3,46,23,0,398,397,1,0,0,0,398,399,1,0,0,0,399,401,1,0,0,0,400,
  	402,5,9,0,0,401,400,1,0,0,0,401,402,1,0,0,0,402,403,1,0,0,0,403,404,5,
  	8,0,0,404,71,1,0,0,0,405,406,5,7,0,0,406,407,3,84,42,0,407,408,5,8,0,
  	0,408,73,1,0,0,0,409,411,5,12,0,0,410,412,3,52,26,0,411,410,1,0,0,0,411,
  	412,1,0,0,0,412,414,1,0,0,0,413,415,5,9,0,0,414,413,1,0,0,0,414,415,1,
  	0,0,0,415,416,1,0,0,0,416,417,5,13,0,0,417,75,1,0,0,0,418,420,5,12,0,
  	0,419,421,3,56,28,0,420,419,1,0,0,0,420,421,1,0,0,0,421,423,1,0,0,0,422,
  	424,5,9,0,0,423,422,1,0,0,0,423,424,1,0,0,0,424,425,1,0,0,0,425,426,5,
  	13,0,0,426,77,1,0,0,0,427,429,5,12,0,0,428,430,3,46,23,0,429,428,1,0,
  	0,0,429,430,1,0,0,0,430,432,1,0,0,0,431,433,5,9,0,0,432,431,1,0,0,0,432,
  	433,1,0,0,0,433,434,1,0,0,0,434,435,5,13,0,0,435,79,1,0,0,0,436,438,5,
  	14,0,0,437,439,3,52,26,0,438,437,1,0,0,0,438,439,1,0,0,0,439,441,1,0,
  	0,0,440,442,5,9,0,0,441,440,1,0,0,0,441,442,1,0,0,0,442,443,1,0,0,0,443,
  	444,5,15,0,0,444,81,1,0,0,0,445,447,5,14,0,0,446,448,3,56,28,0,447,446,
  	1,0,0,0,447,448,1,0,0,0,448,450,1,0,0,0,449,451,5,9,0,0,450,449,1,0,0,
  	0,450,451,1,0,0,0,451,452,1,0,0,0,452,453,5,15,0,0,453,83,1,0,0,0,454,
  	459,3,86,43,0,455,456,7,2,0,0,456,458,3,86,43,0,457,455,1,0,0,0,458,461,
  	1,0,0,0,459,457,1,0,0,0,459,460,1,0,0,0,460,85,1,0,0,0,461,459,1,0,0,
  	0,462,468,3,94,47,0,463,464,5,24,0,0,464,465,3,86,43,0,465,466,5,1,0,
  	0,466,467,3,86,43,0,467,469,1,0,0,0,468,463,1,0,0,0,468,469,1,0,0,0,469,
  	87,1,0,0,0,470,475,3,90,45,0,471,472,5,25,0,0,472,474,3,90,45,0,473,471,
  	1,0,0,0,474,477,1,0,0,0,475,473,1,0,0,0,475,476,1,0,0,0,476,89,1,0,0,
  	0,477,475,1,0,0,0,478,483,3,92,46,0,479,480,5,26,0,0,480,482,3,92,46,
  	0,481,479,1,0,0,0,482,485,1,0,0,0,483,481,1,0,0,0,483,484,1,0,0,0,484,
  	91,1,0,0,0,485,483,1,0,0,0,486,491,3,94,47,0,487,488,7,3,0,0,488,490,
  	3,94,47,0,489,487,1,0,0,0,490,493,1,0,0,0,491,489,1,0,0,0,491,492,1,0,
  	0,0,492,93,1,0,0,0,493,491,1,0,0,0,494,499,3,96,48,0,495,496,7,4,0,0,
  	496,498,3,96,48,0,497,495,1,0,0,0,498,501,1,0,0,0,499,497,1,0,0,0,499,
  	500,1,0,0,0,500,95,1,0,0,0,501,499,1,0,0,0,502,507,3,98,49,0,503,504,
  	7,5,0,0,504,506,3,98,49,0,505,503,1,0,0,0,506,509,1,0,0,0,507,505,1,0,
  	0,0,507,508,1,0,0,0,508,97,1,0,0,0,509,507,1,0,0,0,510,515,3,100,50,0,
  	511,512,7,6,0,0,512,514,3,100,50,0,513,511,1,0,0,0,514,517,1,0,0,0,515,
  	513,1,0,0,0,515,516,1,0,0,0,516,99,1,0,0,0,517,515,1,0,0,0,518,522,3,
  	102,51,0,519,520,7,7,0,0,520,522,3,102,51,0,521,518,1,0,0,0,521,519,1,
  	0,0,0,522,101,1,0,0,0,523,528,3,104,52,0,524,525,5,38,0,0,525,527,3,104,
  	52,0,526,524,1,0,0,0,527,530,1,0,0,0,528,526,1,0,0,0,528,529,1,0,0,0,
  	529,103,1,0,0,0,530,528,1,0,0,0,531,536,3,106,53,0,532,533,5,39,0,0,533,
  	535,3,106,53,0,534,532,1,0,0,0,535,538,1,0,0,0,536,534,1,0,0,0,536,537,
  	1,0,0,0,537,105,1,0,0,0,538,536,1,0,0,0,539,546,3,108,54,0,540,545,3,
  	72,36,0,541,545,3,76,38,0,542,545,3,82,41,0,543,545,3,26,13,0,544,540,
  	1,0,0,0,544,541,1,0,0,0,544,542,1,0,0,0,544,543,1,0,0,0,545,548,1,0,0,
  	0,546,544,1,0,0,0,546,547,1,0,0,0,547,107,1,0,0,0,548,546,1,0,0,0,549,
  	586,3,126,63,0,550,586,3,110,55,0,551,586,3,66,33,0,552,586,3,58,29,0,
  	553,554,5,12,0,0,554,555,3,84,42,0,555,556,5,13,0,0,556,586,1,0,0,0,557,
  	558,5,14,0,0,558,573,3,112,56,0,559,571,5,9,0,0,560,572,5,92,0,0,561,
  	562,5,7,0,0,562,567,5,92,0,0,563,564,5,9,0,0,564,566,5,92,0,0,565,563,
  	1,0,0,0,566,569,1,0,0,0,567,565,1,0,0,0,567,568,1,0,0,0,568,570,1,0,0,
  	0,569,567,1,0,0,0,570,572,5,8,0,0,571,560,1,0,0,0,571,561,1,0,0,0,572,
  	574,1,0,0,0,573,559,1,0,0,0,573,574,1,0,0,0,574,575,1,0,0,0,575,576,5,
  	15,0,0,576,578,1,0,0,0,577,557,1,0,0,0,577,578,1,0,0,0,578,582,1,0,0,
  	0,579,583,3,70,35,0,580,583,3,78,39,0,581,583,3,64,32,0,582,579,1,0,0,
  	0,582,580,1,0,0,0,582,581,1,0,0,0,583,586,1,0,0,0,584,586,3,22,11,0,585,
  	549,1,0,0,0,585,550,1,0,0,0,585,551,1,0,0,0,585,552,1,0,0,0,585,553,1,
  	0,0,0,585,577,1,0,0,0,585,584,1,0,0,0,586,109,1,0,0,0,587,589,5,92,0,
  	0,588,590,5,89,0,0,589,588,1,0,0,0,589,590,1,0,0,0,590,602,1,0,0,0,591,
  	593,5,93,0,0,592,594,5,89,0,0,593,592,1,0,0,0,593,594,1,0,0,0,594,602,
  	1,0,0,0,595,602,5,90,0,0,596,602,5,87,0,0,597,602,5,91,0,0,598,602,5,
  	59,0,0,599,602,5,60,0,0,600,602,5,58,0,0,601,587,1,0,0,0,601,591,1,0,
  	0,0,601,595,1,0,0,0,601,596,1,0,0,0,601,597,1,0,0,0,601,598,1,0,0,0,601,
  	599,1,0,0,0,601,600,1,0,0,0,602,111,1,0,0,0,603,608,3,114,57,0,604,605,
  	7,8,0,0,605,607,3,114,57,0,606,604,1,0,0,0,607,610,1,0,0,0,608,606,1,
  	0,0,0,608,609,1,0,0,0,609,113,1,0,0,0,610,608,1,0,0,0,611,619,3,116,58,
  	0,612,614,5,7,0,0,613,615,5,92,0,0,614,613,1,0,0,0,614,615,1,0,0,0,615,
  	616,1,0,0,0,616,618,5,8,0,0,617,612,1,0,0,0,618,621,1,0,0,0,619,617,1,
  	0,0,0,619,620,1,0,0,0,620,115,1,0,0,0,621,619,1,0,0,0,622,632,3,120,60,
  	0,623,632,3,122,61,0,624,632,3,124,62,0,625,632,3,126,63,0,626,627,5,
  	12,0,0,627,628,3,112,56,0,628,629,5,13,0,0,629,632,1,0,0,0,630,632,3,
  	118,59,0,631,622,1,0,0,0,631,623,1,0,0,0,631,624,1,0,0,0,631,625,1,0,
  	0,0,631,626,1,0,0,0,631,630,1,0,0,0,632,117,1,0,0,0,633,635,5,14,0,0,
  	634,636,3,52,26,0,635,634,1,0,0,0,635,636,1,0,0,0,636,637,1,0,0,0,637,
  	639,5,15,0,0,638,633,1,0,0,0,638,639,1,0,0,0,639,640,1,0,0,0,640,642,
  	5,12,0,0,641,643,3,52,26,0,642,641,1,0,0,0,642,643,1,0,0,0,643,644,1,
  	0,0,0,644,645,5,13,0,0,645,646,5,5,0,0,646,647,3,112,56,0,647,119,1,0,
  	0,0,648,649,7,9,0,0,649,121,1,0,0,0,650,655,5,71,0,0,651,652,5,14,0,0,
  	652,653,3,112,56,0,653,654,5,15,0,0,654,656,1,0,0,0,655,651,1,0,0,0,655,
  	656,1,0,0,0,656,741,1,0,0,0,657,664,5,72,0,0,658,659,5,14,0,0,659,660,
  	3,112,56,0,660,661,5,9,0,0,661,662,3,112,56,0,662,663,5,15,0,0,663,665,
  	1,0,0,0,664,658,1,0,0,0,664,665,1,0,0,0,665,741,1,0,0,0,666,741,5,73,
  	0,0,667,741,5,74,0,0,668,677,5,75,0,0,669,670,5,14,0,0,670,673,3,112,
  	56,0,671,672,5,9,0,0,672,674,5,92,0,0,673,671,1,0,0,0,673,674,1,0,0,0,
  	674,675,1,0,0,0,675,676,5,15,0,0,676,678,1,0,0,0,677,669,1,0,0,0,677,
  	678,1,0,0,0,678,741,1,0,0,0,679,688,5,76,0,0,680,682,5,14,0,0,681,683,
  	3,42,21,0,682,681,1,0,0,0,682,683,1,0,0,0,683,685,1,0,0,0,684,686,5,9,
  	0,0,685,684,1,0,0,0,685,686,1,0,0,0,686,687,1,0,0,0,687,689,5,15,0,0,
  	688,680,1,0,0,0,688,689,1,0,0,0,689,741,1,0,0,0,690,699,5,77,0,0,691,
  	693,5,14,0,0,692,694,3,42,21,0,693,692,1,0,0,0,693,694,1,0,0,0,694,696,
  	1,0,0,0,695,697,5,9,0,0,696,695,1,0,0,0,696,697,1,0,0,0,697,698,1,0,0,
  	0,698,700,5,15,0,0,699,691,1,0,0,0,699,700,1,0,0,0,700,741,1,0,0,0,701,
  	706,5,78,0,0,702,703,5,14,0,0,703,704,3,112,56,0,704,705,5,15,0,0,705,
  	707,1,0,0,0,706,702,1,0,0,0,706,707,1,0,0,0,707,741,1,0,0,0,708,726,5,
  	79,0,0,709,710,5,14,0,0,710,722,3,112,56,0,711,712,5,9,0,0,712,713,5,
  	7,0,0,713,718,5,92,0,0,714,715,5,9,0,0,715,717,5,92,0,0,716,714,1,0,0,
  	0,717,720,1,0,0,0,718,716,1,0,0,0,718,719,1,0,0,0,719,721,1,0,0,0,720,
  	718,1,0,0,0,721,723,5,8,0,0,722,711,1,0,0,0,722,723,1,0,0,0,723,724,1,
  	0,0,0,724,725,5,15,0,0,725,727,1,0,0,0,726,709,1,0,0,0,726,727,1,0,0,
  	0,727,741,1,0,0,0,728,730,5,10,0,0,729,731,3,48,24,0,730,729,1,0,0,0,
  	730,731,1,0,0,0,731,733,1,0,0,0,732,734,5,9,0,0,733,732,1,0,0,0,733,734,
  	1,0,0,0,734,735,1,0,0,0,735,741,5,11,0,0,736,737,5,10,0,0,737,738,3,38,
  	19,0,738,739,5,11,0,0,739,741,1,0,0,0,740,650,1,0,0,0,740,657,1,0,0,0,
  	740,666,1,0,0,0,740,667,1,0,0,0,740,668,1,0,0,0,740,679,1,0,0,0,740,690,
  	1,0,0,0,740,701,1,0,0,0,740,708,1,0,0,0,740,728,1,0,0,0,740,736,1,0,0,
  	0,741,123,1,0,0,0,742,743,7,10,0,0,743,125,1,0,0,0,744,745,5,88,0,0,745,
  	127,1,0,0,0,100,129,138,142,151,157,165,170,175,181,189,200,203,206,213,
  	219,222,227,232,237,245,250,261,265,271,290,298,306,314,322,330,338,344,
  	347,351,354,360,363,369,376,383,389,392,398,401,411,414,420,423,429,432,
  	438,441,447,450,459,468,475,483,491,499,507,515,521,528,536,544,546,567,
  	571,573,577,582,585,589,593,601,608,614,619,631,635,638,642,655,664,673,
  	677,682,685,688,693,696,699,706,718,722,726,730,733,740
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
    setState(129);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2305526556023346368) != 0) || ((((_la - 87) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 87)) & 123) != 0)) {
      setState(128);
      stmtList();
    }
    setState(131);
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
    setState(133);
    stmt();
    setState(138);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 1, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(134);
        match(OpenCMLParser::SEP);
        setState(135);
        stmt(); 
      }
      setState(140);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 1, _ctx);
    }
    setState(142);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SEP) {
      setState(141);
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
    setState(151);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 3, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(144);
      letStmt();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(145);
      useStmt();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(146);
      typeStmt();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(147);
      exprStmt();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(148);
      waitStmt();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(149);
      funcDef();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(150);
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
    setState(170);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::LET:
      case OpenCMLParser::VAR: {
        enterOuterAlt(_localctx, 1);
        setState(153);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::LET

        || _la == OpenCMLParser::VAR)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(154);
        carrier();
        setState(157);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(155);
          match(OpenCMLParser::T__0);
          setState(156);
          typeExpr();
        }
        setState(159);
        match(OpenCMLParser::T__1);
        setState(160);
        entityExpr();
        break;
      }

      case OpenCMLParser::T__6:
      case OpenCMLParser::T__9:
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(162);
        carrier();
        setState(165);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(163);
          match(OpenCMLParser::T__0);
          setState(164);
          typeExpr();
        }
        setState(167);
        match(OpenCMLParser::T__2);
        setState(168);
        entityExpr();
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
    setState(172);
    match(OpenCMLParser::USE);
    setState(175);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__6:
      case OpenCMLParser::T__9:
      case OpenCMLParser::IDENTIFIER: {
        setState(173);
        carrier();
        break;
      }

      case OpenCMLParser::T__3: {
        setState(174);
        match(OpenCMLParser::T__3);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(181);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__1: {
        setState(177);
        match(OpenCMLParser::T__1);
        setState(178);
        entityExpr();
        break;
      }

      case OpenCMLParser::FROM: {
        setState(179);
        match(OpenCMLParser::FROM);
        setState(180);
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
    setState(183);
    match(OpenCMLParser::TYPE);
    setState(184);
    identRef();
    setState(185);
    match(OpenCMLParser::T__1);
    setState(186);
    typeExpr();
   
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
    setState(189);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(188);
      annotations();
    }
    setState(191);
    entityExpr();
   
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
    setState(193);
    match(OpenCMLParser::WAIT);
    setState(194);
    entityExpr();
   
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
    setState(196);
    match(OpenCMLParser::WITH);
    setState(197);
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
    setState(200);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(199);
      annotations();
    }
    setState(203);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WITH) {
      setState(202);
      withDef();
    }
    setState(206);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 270215977642229760) != 0)) {
      setState(205);
      modifiers();
    }
    setState(208);
    match(OpenCMLParser::FUNC);
    setState(209);
    identRef();
    setState(210);
    parentParams();
    setState(213);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(211);
      match(OpenCMLParser::T__0);
      setState(212);
      typeExpr();
    }
    setState(215);
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
    setState(217);
    match(OpenCMLParser::RETURN);
    setState(219);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2287828816862663808) != 0) || ((((_la - 87) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 87)) & 123) != 0)) {
      setState(218);
      entityExpr();
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
    setState(222);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 270215977642229760) != 0)) {
      setState(221);
      modifiers();
    }
    setState(224);
    parentParams();
    setState(227);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(225);
      match(OpenCMLParser::T__0);
      setState(226);
      typeExpr();
    }
    setState(229);
    match(OpenCMLParser::T__4);
    setState(232);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 17, _ctx)) {
    case 1: {
      setState(230);
      bracedStmts();
      break;
    }

    case 2: {
      setState(231);
      entityExpr();
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
    setState(237);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(234);
        identRef();
        break;
      }

      case OpenCMLParser::T__9: {
        enterOuterAlt(_localctx, 2);
        setState(235);
        bracedIdents();
        break;
      }

      case OpenCMLParser::T__6: {
        enterOuterAlt(_localctx, 3);
        setState(236);
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

OpenCMLParser::PrimaryExprContext* OpenCMLParser::AnnotationContext::primaryExpr() {
  return getRuleContext<OpenCMLParser::PrimaryExprContext>(0);
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
    setState(239);
    match(OpenCMLParser::T__5);
    setState(240);
    primaryExpr();
   
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
    setState(243); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(242);
      annotation();
      setState(245); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == OpenCMLParser::T__5);
   
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
    setState(248); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(247);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 270215977642229760) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(250); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 270215977642229760) != 0));
   
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
    setState(252);
    identRef();
    setState(253);
    match(OpenCMLParser::T__0);
    setState(254);
    typeExpr();
   
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
    setState(256);
    identRef();
    setState(257);
    match(OpenCMLParser::T__0);
    setState(258);
    entityExpr();
   
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
    setState(261);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR) {
      setState(260);
      match(OpenCMLParser::VAR);
    }
    setState(263);
    identRef();
    setState(265);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(264);
      annotation();
    }
    setState(267);
    match(OpenCMLParser::T__0);
    setState(268);
    typeExpr();
    setState(271);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(269);
      match(OpenCMLParser::T__1);
      setState(270);
      entityExpr();
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
    setState(273);
    match(OpenCMLParser::T__6);
    setState(274);
    entityExpr();
    setState(275);
    match(OpenCMLParser::T__7);
    setState(276);
    match(OpenCMLParser::T__0);
    setState(277);
    typeExpr();
   
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
    setState(279);
    match(OpenCMLParser::T__6);
    setState(280);
    entityExpr();
    setState(281);
    match(OpenCMLParser::T__7);
    setState(282);
    match(OpenCMLParser::T__0);
    setState(283);
    entityExpr();
   
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
    setState(285);
    typeExpr();
    setState(290);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 24, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(286);
        match(OpenCMLParser::T__8);
        setState(287);
        typeExpr(); 
      }
      setState(292);
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
    setState(293);
    identRef();
    setState(298);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 25, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(294);
        match(OpenCMLParser::T__8);
        setState(295);
        identRef(); 
      }
      setState(300);
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
    setState(301);
    entityExpr();
    setState(306);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(302);
        match(OpenCMLParser::T__8);
        setState(303);
        entityExpr(); 
      }
      setState(308);
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
    setState(309);
    keyTypePair();
    setState(314);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(310);
        match(OpenCMLParser::T__8);
        setState(311);
        keyTypePair(); 
      }
      setState(316);
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
    setState(317);
    keyValuePair();
    setState(322);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(318);
        match(OpenCMLParser::T__8);
        setState(319);
        keyValuePair(); 
      }
      setState(324);
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
    setState(325);
    keyParamPair();
    setState(330);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(326);
        match(OpenCMLParser::T__8);
        setState(327);
        keyParamPair(); 
      }
      setState(332);
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
    setState(333);
    indexKVPair();
    setState(338);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(334);
        match(OpenCMLParser::T__8);
        setState(335);
        indexKVPair(); 
      }
      setState(340);
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
    setState(347);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(341);
      valueList();
      setState(344);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx)) {
      case 1: {
        setState(342);
        match(OpenCMLParser::T__8);
        setState(343);
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
      setState(346);
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
    setState(349);
    match(OpenCMLParser::T__9);
    setState(351);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(350);
      pairedValues();
    }
    setState(354);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(353);
      match(OpenCMLParser::T__8);
    }
    setState(356);
    match(OpenCMLParser::T__10);
   
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
    setState(358);
    match(OpenCMLParser::T__9);
    setState(360);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(359);
      identList();
    }
    setState(363);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(362);
      match(OpenCMLParser::T__8);
    }
    setState(365);
    match(OpenCMLParser::T__10);
   
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
    setState(367);
    match(OpenCMLParser::T__9);
    setState(369);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2305526556023346368) != 0) || ((((_la - 87) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 87)) & 123) != 0)) {
      setState(368);
      stmtList();
    }
    setState(371);
    match(OpenCMLParser::T__10);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BracedValuesContext ------------------------------------------------------------------

OpenCMLParser::BracedValuesContext::BracedValuesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ValueListContext* OpenCMLParser::BracedValuesContext::valueList() {
  return getRuleContext<OpenCMLParser::ValueListContext>(0);
}


size_t OpenCMLParser::BracedValuesContext::getRuleIndex() const {
  return OpenCMLParser::RuleBracedValues;
}


std::any OpenCMLParser::BracedValuesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBracedValues(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BracedValuesContext* OpenCMLParser::bracedValues() {
  BracedValuesContext *_localctx = _tracker.createInstance<BracedValuesContext>(_ctx, getState());
  enterRule(_localctx, 64, OpenCMLParser::RuleBracedValues);
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
    setState(373);
    match(OpenCMLParser::T__9);
    setState(374);
    valueList();
    setState(376);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(375);
      match(OpenCMLParser::T__8);
    }
    setState(378);
    match(OpenCMLParser::T__10);
   
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
    setState(380);
    match(OpenCMLParser::T__9);
    setState(381);
    indexKVPairs();
    setState(383);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(382);
      match(OpenCMLParser::T__8);
    }
    setState(385);
    match(OpenCMLParser::T__10);
   
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
    setState(387);
    match(OpenCMLParser::T__6);
    setState(389);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(388);
      identList();
    }
    setState(392);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(391);
      match(OpenCMLParser::T__8);
    }
    setState(394);
    match(OpenCMLParser::T__7);
   
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
  enterRule(_localctx, 70, OpenCMLParser::RuleBracketValues);
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
    setState(396);
    match(OpenCMLParser::T__6);
    setState(398);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2287828816862663808) != 0) || ((((_la - 87) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 87)) & 123) != 0)) {
      setState(397);
      valueList();
    }
    setState(401);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(400);
      match(OpenCMLParser::T__8);
    }
    setState(403);
    match(OpenCMLParser::T__7);
   
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
  enterRule(_localctx, 72, OpenCMLParser::RuleMemberAccess);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(405);
    match(OpenCMLParser::T__6);
    setState(406);
    entityExpr();
    setState(407);
    match(OpenCMLParser::T__7);
   
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
    match(OpenCMLParser::T__11);
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
    if (_la == OpenCMLParser::T__8) {
      setState(413);
      match(OpenCMLParser::T__8);
    }
    setState(416);
    match(OpenCMLParser::T__12);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParentArguesContext ------------------------------------------------------------------

OpenCMLParser::ParentArguesContext::ParentArguesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ArgumentListContext* OpenCMLParser::ParentArguesContext::argumentList() {
  return getRuleContext<OpenCMLParser::ArgumentListContext>(0);
}


size_t OpenCMLParser::ParentArguesContext::getRuleIndex() const {
  return OpenCMLParser::RuleParentArgues;
}


std::any OpenCMLParser::ParentArguesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitParentArgues(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ParentArguesContext* OpenCMLParser::parentArgues() {
  ParentArguesContext *_localctx = _tracker.createInstance<ParentArguesContext>(_ctx, getState());
  enterRule(_localctx, 76, OpenCMLParser::RuleParentArgues);
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
    match(OpenCMLParser::T__11);
    setState(420);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2287828816862663808) != 0) || ((((_la - 87) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 87)) & 123) != 0)) {
      setState(419);
      argumentList();
    }
    setState(423);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(422);
      match(OpenCMLParser::T__8);
    }
    setState(425);
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

OpenCMLParser::ValueListContext* OpenCMLParser::ParentValuesContext::valueList() {
  return getRuleContext<OpenCMLParser::ValueListContext>(0);
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
  enterRule(_localctx, 78, OpenCMLParser::RuleParentValues);
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
    match(OpenCMLParser::T__11);
    setState(429);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2287828816862663808) != 0) || ((((_la - 87) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 87)) & 123) != 0)) {
      setState(428);
      valueList();
    }
    setState(432);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(431);
      match(OpenCMLParser::T__8);
    }
    setState(434);
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
  enterRule(_localctx, 80, OpenCMLParser::RuleAngledParams);
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
    match(OpenCMLParser::T__13);
    setState(438);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(437);
      pairedParams();
    }
    setState(441);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(440);
      match(OpenCMLParser::T__8);
    }
    setState(443);
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
  enterRule(_localctx, 82, OpenCMLParser::RuleAngledValues);
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
    match(OpenCMLParser::T__13);
    setState(447);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2287828816862663808) != 0) || ((((_la - 87) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 87)) & 123) != 0)) {
      setState(446);
      argumentList();
    }
    setState(450);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(449);
      match(OpenCMLParser::T__8);
    }
    setState(452);
    match(OpenCMLParser::T__14);
   
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

std::vector<OpenCMLParser::TernaryExprContext *> OpenCMLParser::EntityExprContext::ternaryExpr() {
  return getRuleContexts<OpenCMLParser::TernaryExprContext>();
}

OpenCMLParser::TernaryExprContext* OpenCMLParser::EntityExprContext::ternaryExpr(size_t i) {
  return getRuleContext<OpenCMLParser::TernaryExprContext>(i);
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
  EntityExprContext *_localctx = _tracker.createInstance<EntityExprContext>(_ctx, getState());
  enterRule(_localctx, 84, OpenCMLParser::RuleEntityExpr);
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
    setState(454);
    ternaryExpr();
    setState(459);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 54, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(455);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 16711684) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(456);
        ternaryExpr(); 
      }
      setState(461);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 54, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TernaryExprContext ------------------------------------------------------------------

OpenCMLParser::TernaryExprContext::TernaryExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::RelationalExprContext* OpenCMLParser::TernaryExprContext::relationalExpr() {
  return getRuleContext<OpenCMLParser::RelationalExprContext>(0);
}

std::vector<OpenCMLParser::TernaryExprContext *> OpenCMLParser::TernaryExprContext::ternaryExpr() {
  return getRuleContexts<OpenCMLParser::TernaryExprContext>();
}

OpenCMLParser::TernaryExprContext* OpenCMLParser::TernaryExprContext::ternaryExpr(size_t i) {
  return getRuleContext<OpenCMLParser::TernaryExprContext>(i);
}


size_t OpenCMLParser::TernaryExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleTernaryExpr;
}


std::any OpenCMLParser::TernaryExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitTernaryExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::TernaryExprContext* OpenCMLParser::ternaryExpr() {
  TernaryExprContext *_localctx = _tracker.createInstance<TernaryExprContext>(_ctx, getState());
  enterRule(_localctx, 86, OpenCMLParser::RuleTernaryExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(462);
    relationalExpr();
    setState(468);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 55, _ctx)) {
    case 1: {
      setState(463);
      match(OpenCMLParser::T__23);
      setState(464);
      ternaryExpr();
      setState(465);
      match(OpenCMLParser::T__0);
      setState(466);
      ternaryExpr();
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

//----------------- LogicalOrExprContext ------------------------------------------------------------------

OpenCMLParser::LogicalOrExprContext::LogicalOrExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::LogicalAndExprContext *> OpenCMLParser::LogicalOrExprContext::logicalAndExpr() {
  return getRuleContexts<OpenCMLParser::LogicalAndExprContext>();
}

OpenCMLParser::LogicalAndExprContext* OpenCMLParser::LogicalOrExprContext::logicalAndExpr(size_t i) {
  return getRuleContext<OpenCMLParser::LogicalAndExprContext>(i);
}


size_t OpenCMLParser::LogicalOrExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleLogicalOrExpr;
}


std::any OpenCMLParser::LogicalOrExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitLogicalOrExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::LogicalOrExprContext* OpenCMLParser::logicalOrExpr() {
  LogicalOrExprContext *_localctx = _tracker.createInstance<LogicalOrExprContext>(_ctx, getState());
  enterRule(_localctx, 88, OpenCMLParser::RuleLogicalOrExpr);
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
    setState(470);
    logicalAndExpr();
    setState(475);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__24) {
      setState(471);
      match(OpenCMLParser::T__24);
      setState(472);
      logicalAndExpr();
      setState(477);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LogicalAndExprContext ------------------------------------------------------------------

OpenCMLParser::LogicalAndExprContext::LogicalAndExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::EqualityExprContext *> OpenCMLParser::LogicalAndExprContext::equalityExpr() {
  return getRuleContexts<OpenCMLParser::EqualityExprContext>();
}

OpenCMLParser::EqualityExprContext* OpenCMLParser::LogicalAndExprContext::equalityExpr(size_t i) {
  return getRuleContext<OpenCMLParser::EqualityExprContext>(i);
}


size_t OpenCMLParser::LogicalAndExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleLogicalAndExpr;
}


std::any OpenCMLParser::LogicalAndExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitLogicalAndExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::LogicalAndExprContext* OpenCMLParser::logicalAndExpr() {
  LogicalAndExprContext *_localctx = _tracker.createInstance<LogicalAndExprContext>(_ctx, getState());
  enterRule(_localctx, 90, OpenCMLParser::RuleLogicalAndExpr);
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
    setState(478);
    equalityExpr();
    setState(483);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__25) {
      setState(479);
      match(OpenCMLParser::T__25);
      setState(480);
      equalityExpr();
      setState(485);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- EqualityExprContext ------------------------------------------------------------------

OpenCMLParser::EqualityExprContext::EqualityExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::RelationalExprContext *> OpenCMLParser::EqualityExprContext::relationalExpr() {
  return getRuleContexts<OpenCMLParser::RelationalExprContext>();
}

OpenCMLParser::RelationalExprContext* OpenCMLParser::EqualityExprContext::relationalExpr(size_t i) {
  return getRuleContext<OpenCMLParser::RelationalExprContext>(i);
}


size_t OpenCMLParser::EqualityExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleEqualityExpr;
}


std::any OpenCMLParser::EqualityExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEqualityExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::EqualityExprContext* OpenCMLParser::equalityExpr() {
  EqualityExprContext *_localctx = _tracker.createInstance<EqualityExprContext>(_ctx, getState());
  enterRule(_localctx, 92, OpenCMLParser::RuleEqualityExpr);
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
    setState(486);
    relationalExpr();
    setState(491);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__26

    || _la == OpenCMLParser::T__27) {
      setState(487);
      _la = _input->LA(1);
      if (!(_la == OpenCMLParser::T__26

      || _la == OpenCMLParser::T__27)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(488);
      relationalExpr();
      setState(493);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RelationalExprContext ------------------------------------------------------------------

OpenCMLParser::RelationalExprContext::RelationalExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::AdditiveExprContext *> OpenCMLParser::RelationalExprContext::additiveExpr() {
  return getRuleContexts<OpenCMLParser::AdditiveExprContext>();
}

OpenCMLParser::AdditiveExprContext* OpenCMLParser::RelationalExprContext::additiveExpr(size_t i) {
  return getRuleContext<OpenCMLParser::AdditiveExprContext>(i);
}


size_t OpenCMLParser::RelationalExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleRelationalExpr;
}


std::any OpenCMLParser::RelationalExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitRelationalExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::RelationalExprContext* OpenCMLParser::relationalExpr() {
  RelationalExprContext *_localctx = _tracker.createInstance<RelationalExprContext>(_ctx, getState());
  enterRule(_localctx, 94, OpenCMLParser::RuleRelationalExpr);
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
    setState(494);
    additiveExpr();
    setState(499);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 59, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(495);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 1610661888) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(496);
        additiveExpr(); 
      }
      setState(501);
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

//----------------- AdditiveExprContext ------------------------------------------------------------------

OpenCMLParser::AdditiveExprContext::AdditiveExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::MultiplicativeExprContext *> OpenCMLParser::AdditiveExprContext::multiplicativeExpr() {
  return getRuleContexts<OpenCMLParser::MultiplicativeExprContext>();
}

OpenCMLParser::MultiplicativeExprContext* OpenCMLParser::AdditiveExprContext::multiplicativeExpr(size_t i) {
  return getRuleContext<OpenCMLParser::MultiplicativeExprContext>(i);
}


size_t OpenCMLParser::AdditiveExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleAdditiveExpr;
}


std::any OpenCMLParser::AdditiveExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitAdditiveExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::AdditiveExprContext* OpenCMLParser::additiveExpr() {
  AdditiveExprContext *_localctx = _tracker.createInstance<AdditiveExprContext>(_ctx, getState());
  enterRule(_localctx, 96, OpenCMLParser::RuleAdditiveExpr);
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
    setState(502);
    multiplicativeExpr();
    setState(507);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 60, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(503);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__30

        || _la == OpenCMLParser::T__31)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(504);
        multiplicativeExpr(); 
      }
      setState(509);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 60, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MultiplicativeExprContext ------------------------------------------------------------------

OpenCMLParser::MultiplicativeExprContext::MultiplicativeExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::UnaryExprContext *> OpenCMLParser::MultiplicativeExprContext::unaryExpr() {
  return getRuleContexts<OpenCMLParser::UnaryExprContext>();
}

OpenCMLParser::UnaryExprContext* OpenCMLParser::MultiplicativeExprContext::unaryExpr(size_t i) {
  return getRuleContext<OpenCMLParser::UnaryExprContext>(i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::MultiplicativeExprContext::AS() {
  return getTokens(OpenCMLParser::AS);
}

tree::TerminalNode* OpenCMLParser::MultiplicativeExprContext::AS(size_t i) {
  return getToken(OpenCMLParser::AS, i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::MultiplicativeExprContext::IS() {
  return getTokens(OpenCMLParser::IS);
}

tree::TerminalNode* OpenCMLParser::MultiplicativeExprContext::IS(size_t i) {
  return getToken(OpenCMLParser::IS, i);
}


size_t OpenCMLParser::MultiplicativeExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleMultiplicativeExpr;
}


std::any OpenCMLParser::MultiplicativeExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitMultiplicativeExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::MultiplicativeExprContext* OpenCMLParser::multiplicativeExpr() {
  MultiplicativeExprContext *_localctx = _tracker.createInstance<MultiplicativeExprContext>(_ctx, getState());
  enterRule(_localctx, 98, OpenCMLParser::RuleMultiplicativeExpr);
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
    setState(510);
    unaryExpr();
    setState(515);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(511);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 26448408608784) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(512);
        unaryExpr(); 
      }
      setState(517);
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

//----------------- UnaryExprContext ------------------------------------------------------------------

OpenCMLParser::UnaryExprContext::UnaryExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::LinkExprContext* OpenCMLParser::UnaryExprContext::linkExpr() {
  return getRuleContext<OpenCMLParser::LinkExprContext>(0);
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
  enterRule(_localctx, 100, OpenCMLParser::RuleUnaryExpr);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(521);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__6:
      case OpenCMLParser::T__9:
      case OpenCMLParser::T__11:
      case OpenCMLParser::T__13:
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
        setState(518);
        linkExpr();
        break;
      }

      case OpenCMLParser::T__35:
      case OpenCMLParser::T__36: {
        enterOuterAlt(_localctx, 2);
        setState(519);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__35

        || _la == OpenCMLParser::T__36)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(520);
        linkExpr();
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

//----------------- LinkExprContext ------------------------------------------------------------------

OpenCMLParser::LinkExprContext::LinkExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::WithExprContext *> OpenCMLParser::LinkExprContext::withExpr() {
  return getRuleContexts<OpenCMLParser::WithExprContext>();
}

OpenCMLParser::WithExprContext* OpenCMLParser::LinkExprContext::withExpr(size_t i) {
  return getRuleContext<OpenCMLParser::WithExprContext>(i);
}


size_t OpenCMLParser::LinkExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleLinkExpr;
}


std::any OpenCMLParser::LinkExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitLinkExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::LinkExprContext* OpenCMLParser::linkExpr() {
  LinkExprContext *_localctx = _tracker.createInstance<LinkExprContext>(_ctx, getState());
  enterRule(_localctx, 102, OpenCMLParser::RuleLinkExpr);

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
    setState(523);
    withExpr();
    setState(528);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 63, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(524);
        match(OpenCMLParser::T__37);
        setState(525);
        withExpr(); 
      }
      setState(530);
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

//----------------- WithExprContext ------------------------------------------------------------------

OpenCMLParser::WithExprContext::WithExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::AnnotatedExprContext *> OpenCMLParser::WithExprContext::annotatedExpr() {
  return getRuleContexts<OpenCMLParser::AnnotatedExprContext>();
}

OpenCMLParser::AnnotatedExprContext* OpenCMLParser::WithExprContext::annotatedExpr(size_t i) {
  return getRuleContext<OpenCMLParser::AnnotatedExprContext>(i);
}


size_t OpenCMLParser::WithExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleWithExpr;
}


std::any OpenCMLParser::WithExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitWithExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::WithExprContext* OpenCMLParser::withExpr() {
  WithExprContext *_localctx = _tracker.createInstance<WithExprContext>(_ctx, getState());
  enterRule(_localctx, 104, OpenCMLParser::RuleWithExpr);

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
    setState(531);
    annotatedExpr();
    setState(536);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 64, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(532);
        match(OpenCMLParser::T__38);
        setState(533);
        annotatedExpr(); 
      }
      setState(538);
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

//----------------- AnnotatedExprContext ------------------------------------------------------------------

OpenCMLParser::AnnotatedExprContext::AnnotatedExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimaryExprContext* OpenCMLParser::AnnotatedExprContext::primaryExpr() {
  return getRuleContext<OpenCMLParser::PrimaryExprContext>(0);
}

std::vector<OpenCMLParser::MemberAccessContext *> OpenCMLParser::AnnotatedExprContext::memberAccess() {
  return getRuleContexts<OpenCMLParser::MemberAccessContext>();
}

OpenCMLParser::MemberAccessContext* OpenCMLParser::AnnotatedExprContext::memberAccess(size_t i) {
  return getRuleContext<OpenCMLParser::MemberAccessContext>(i);
}

std::vector<OpenCMLParser::ParentArguesContext *> OpenCMLParser::AnnotatedExprContext::parentArgues() {
  return getRuleContexts<OpenCMLParser::ParentArguesContext>();
}

OpenCMLParser::ParentArguesContext* OpenCMLParser::AnnotatedExprContext::parentArgues(size_t i) {
  return getRuleContext<OpenCMLParser::ParentArguesContext>(i);
}

std::vector<OpenCMLParser::AngledValuesContext *> OpenCMLParser::AnnotatedExprContext::angledValues() {
  return getRuleContexts<OpenCMLParser::AngledValuesContext>();
}

OpenCMLParser::AngledValuesContext* OpenCMLParser::AnnotatedExprContext::angledValues(size_t i) {
  return getRuleContext<OpenCMLParser::AngledValuesContext>(i);
}

std::vector<OpenCMLParser::AnnotationContext *> OpenCMLParser::AnnotatedExprContext::annotation() {
  return getRuleContexts<OpenCMLParser::AnnotationContext>();
}

OpenCMLParser::AnnotationContext* OpenCMLParser::AnnotatedExprContext::annotation(size_t i) {
  return getRuleContext<OpenCMLParser::AnnotationContext>(i);
}


size_t OpenCMLParser::AnnotatedExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleAnnotatedExpr;
}


std::any OpenCMLParser::AnnotatedExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitAnnotatedExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::AnnotatedExprContext* OpenCMLParser::annotatedExpr() {
  AnnotatedExprContext *_localctx = _tracker.createInstance<AnnotatedExprContext>(_ctx, getState());
  enterRule(_localctx, 106, OpenCMLParser::RuleAnnotatedExpr);

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
    setState(539);
    primaryExpr();
    setState(546);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 66, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(544);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case OpenCMLParser::T__6: {
            setState(540);
            memberAccess();
            break;
          }

          case OpenCMLParser::T__11: {
            setState(541);
            parentArgues();
            break;
          }

          case OpenCMLParser::T__13: {
            setState(542);
            angledValues();
            break;
          }

          case OpenCMLParser::T__5: {
            setState(543);
            annotation();
            break;
          }

        default:
          throw NoViableAltException(this);
        } 
      }
      setState(548);
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

//----------------- PrimaryExprContext ------------------------------------------------------------------

OpenCMLParser::PrimaryExprContext::PrimaryExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::PrimaryExprContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::LiteralContext* OpenCMLParser::PrimaryExprContext::literal() {
  return getRuleContext<OpenCMLParser::LiteralContext>(0);
}

OpenCMLParser::BracedIndexKVPairsContext* OpenCMLParser::PrimaryExprContext::bracedIndexKVPairs() {
  return getRuleContext<OpenCMLParser::BracedIndexKVPairsContext>(0);
}

OpenCMLParser::BracedPairedValuesContext* OpenCMLParser::PrimaryExprContext::bracedPairedValues() {
  return getRuleContext<OpenCMLParser::BracedPairedValuesContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::PrimaryExprContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}

OpenCMLParser::BracketValuesContext* OpenCMLParser::PrimaryExprContext::bracketValues() {
  return getRuleContext<OpenCMLParser::BracketValuesContext>(0);
}

OpenCMLParser::ParentValuesContext* OpenCMLParser::PrimaryExprContext::parentValues() {
  return getRuleContext<OpenCMLParser::ParentValuesContext>(0);
}

OpenCMLParser::BracedValuesContext* OpenCMLParser::PrimaryExprContext::bracedValues() {
  return getRuleContext<OpenCMLParser::BracedValuesContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::PrimaryExprContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

std::vector<tree::TerminalNode *> OpenCMLParser::PrimaryExprContext::INTEGER() {
  return getTokens(OpenCMLParser::INTEGER);
}

tree::TerminalNode* OpenCMLParser::PrimaryExprContext::INTEGER(size_t i) {
  return getToken(OpenCMLParser::INTEGER, i);
}

OpenCMLParser::LambdaExprContext* OpenCMLParser::PrimaryExprContext::lambdaExpr() {
  return getRuleContext<OpenCMLParser::LambdaExprContext>(0);
}


size_t OpenCMLParser::PrimaryExprContext::getRuleIndex() const {
  return OpenCMLParser::RulePrimaryExpr;
}


std::any OpenCMLParser::PrimaryExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitPrimaryExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::PrimaryExprContext* OpenCMLParser::primaryExpr() {
  PrimaryExprContext *_localctx = _tracker.createInstance<PrimaryExprContext>(_ctx, getState());
  enterRule(_localctx, 108, OpenCMLParser::RulePrimaryExpr);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(585);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 72, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(549);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(550);
      literal();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(551);
      bracedIndexKVPairs();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(552);
      bracedPairedValues();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(553);
      match(OpenCMLParser::T__11);
      setState(554);
      entityExpr();
      setState(555);
      match(OpenCMLParser::T__12);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(577);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__13) {
        setState(557);
        match(OpenCMLParser::T__13);
        setState(558);
        typeExpr();
        setState(573);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__8) {
          setState(559);
          match(OpenCMLParser::T__8);
          setState(571);
          _errHandler->sync(this);
          switch (_input->LA(1)) {
            case OpenCMLParser::INTEGER: {
              setState(560);
              match(OpenCMLParser::INTEGER);
              break;
            }

            case OpenCMLParser::T__6: {
              setState(561);
              match(OpenCMLParser::T__6);
              setState(562);
              match(OpenCMLParser::INTEGER);
              setState(567);
              _errHandler->sync(this);
              _la = _input->LA(1);
              while (_la == OpenCMLParser::T__8) {
                setState(563);
                match(OpenCMLParser::T__8);
                setState(564);
                match(OpenCMLParser::INTEGER);
                setState(569);
                _errHandler->sync(this);
                _la = _input->LA(1);
              }
              setState(570);
              match(OpenCMLParser::T__7);
              break;
            }

          default:
            throw NoViableAltException(this);
          }
        }
        setState(575);
        match(OpenCMLParser::T__14);
      }
      setState(582);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case OpenCMLParser::T__6: {
          setState(579);
          bracketValues();
          break;
        }

        case OpenCMLParser::T__11: {
          setState(580);
          parentValues();
          break;
        }

        case OpenCMLParser::T__9: {
          setState(581);
          bracedValues();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(584);
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
  enterRule(_localctx, 110, OpenCMLParser::RuleLiteral);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(601);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER: {
        enterOuterAlt(_localctx, 1);
        setState(587);
        match(OpenCMLParser::INTEGER);
        setState(589);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::UNIT) {
          setState(588);
          match(OpenCMLParser::UNIT);
        }
        break;
      }

      case OpenCMLParser::REAL: {
        enterOuterAlt(_localctx, 2);
        setState(591);
        match(OpenCMLParser::REAL);
        setState(593);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::UNIT) {
          setState(592);
          match(OpenCMLParser::UNIT);
        }
        break;
      }

      case OpenCMLParser::STRING: {
        enterOuterAlt(_localctx, 3);
        setState(595);
        match(OpenCMLParser::STRING);
        break;
      }

      case OpenCMLParser::MULTI_STR: {
        enterOuterAlt(_localctx, 4);
        setState(596);
        match(OpenCMLParser::MULTI_STR);
        break;
      }

      case OpenCMLParser::FSTRING: {
        enterOuterAlt(_localctx, 5);
        setState(597);
        match(OpenCMLParser::FSTRING);
        break;
      }

      case OpenCMLParser::TRUE: {
        enterOuterAlt(_localctx, 6);
        setState(598);
        match(OpenCMLParser::TRUE);
        break;
      }

      case OpenCMLParser::FALSE: {
        enterOuterAlt(_localctx, 7);
        setState(599);
        match(OpenCMLParser::FALSE);
        break;
      }

      case OpenCMLParser::NULL_: {
        enterOuterAlt(_localctx, 8);
        setState(600);
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

std::vector<OpenCMLParser::ArrayTypeContext *> OpenCMLParser::TypeExprContext::arrayType() {
  return getRuleContexts<OpenCMLParser::ArrayTypeContext>();
}

OpenCMLParser::ArrayTypeContext* OpenCMLParser::TypeExprContext::arrayType(size_t i) {
  return getRuleContext<OpenCMLParser::ArrayTypeContext>(i);
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
  TypeExprContext *_localctx = _tracker.createInstance<TypeExprContext>(_ctx, getState());
  enterRule(_localctx, 112, OpenCMLParser::RuleTypeExpr);
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
    setState(603);
    arrayType();
    setState(608);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 76, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(604);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 3307124817920) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(605);
        arrayType(); 
      }
      setState(610);
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

//----------------- ArrayTypeContext ------------------------------------------------------------------

OpenCMLParser::ArrayTypeContext::ArrayTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::AtomTypeContext* OpenCMLParser::ArrayTypeContext::atomType() {
  return getRuleContext<OpenCMLParser::AtomTypeContext>(0);
}

std::vector<tree::TerminalNode *> OpenCMLParser::ArrayTypeContext::INTEGER() {
  return getTokens(OpenCMLParser::INTEGER);
}

tree::TerminalNode* OpenCMLParser::ArrayTypeContext::INTEGER(size_t i) {
  return getToken(OpenCMLParser::INTEGER, i);
}


size_t OpenCMLParser::ArrayTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleArrayType;
}


std::any OpenCMLParser::ArrayTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitArrayType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ArrayTypeContext* OpenCMLParser::arrayType() {
  ArrayTypeContext *_localctx = _tracker.createInstance<ArrayTypeContext>(_ctx, getState());
  enterRule(_localctx, 114, OpenCMLParser::RuleArrayType);
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
    setState(611);
    atomType();
    setState(619);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 78, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(612);
        match(OpenCMLParser::T__6);
        setState(614);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::INTEGER) {
          setState(613);
          match(OpenCMLParser::INTEGER);
        }
        setState(616);
        match(OpenCMLParser::T__7); 
      }
      setState(621);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 78, _ctx);
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

OpenCMLParser::PrimaryTypeContext* OpenCMLParser::AtomTypeContext::primaryType() {
  return getRuleContext<OpenCMLParser::PrimaryTypeContext>(0);
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
  enterRule(_localctx, 116, OpenCMLParser::RuleAtomType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(631);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 79, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(622);
      primaryType();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(623);
      structType();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(624);
      specialType();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(625);
      identRef();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(626);
      match(OpenCMLParser::T__11);
      setState(627);
      typeExpr();
      setState(628);
      match(OpenCMLParser::T__12);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(630);
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
  enterRule(_localctx, 118, OpenCMLParser::RuleLambdaType);
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
    setState(638);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__13) {
      setState(633);
      match(OpenCMLParser::T__13);
      setState(635);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::VAR

      || _la == OpenCMLParser::IDENTIFIER) {
        setState(634);
        pairedParams();
      }
      setState(637);
      match(OpenCMLParser::T__14);
    }
    setState(640);
    match(OpenCMLParser::T__11);
    setState(642);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(641);
      pairedParams();
    }
    setState(644);
    match(OpenCMLParser::T__12);
    setState(645);
    match(OpenCMLParser::T__4);
    setState(646);
    typeExpr();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrimaryTypeContext ------------------------------------------------------------------

OpenCMLParser::PrimaryTypeContext::PrimaryTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::PrimaryTypeContext::INTEGER_TYPE() {
  return getToken(OpenCMLParser::INTEGER_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimaryTypeContext::INTEGER32_TYPE() {
  return getToken(OpenCMLParser::INTEGER32_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimaryTypeContext::INTEGER64_TYPE() {
  return getToken(OpenCMLParser::INTEGER64_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimaryTypeContext::REAL_TYPE() {
  return getToken(OpenCMLParser::REAL_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimaryTypeContext::FLOAT_TYPE() {
  return getToken(OpenCMLParser::FLOAT_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimaryTypeContext::DOUBLE_TYPE() {
  return getToken(OpenCMLParser::DOUBLE_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimaryTypeContext::NUMBER_TYPE() {
  return getToken(OpenCMLParser::NUMBER_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimaryTypeContext::STRING_TYPE() {
  return getToken(OpenCMLParser::STRING_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimaryTypeContext::BOOL_TYPE() {
  return getToken(OpenCMLParser::BOOL_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimaryTypeContext::CHAR_TYPE() {
  return getToken(OpenCMLParser::CHAR_TYPE, 0);
}


size_t OpenCMLParser::PrimaryTypeContext::getRuleIndex() const {
  return OpenCMLParser::RulePrimaryType;
}


std::any OpenCMLParser::PrimaryTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitPrimaryType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::PrimaryTypeContext* OpenCMLParser::primaryType() {
  PrimaryTypeContext *_localctx = _tracker.createInstance<PrimaryTypeContext>(_ctx, getState());
  enterRule(_localctx, 120, OpenCMLParser::RulePrimaryType);
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
    setState(648);
    _la = _input->LA(1);
    if (!(((((_la - 61) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 61)) & 1023) != 0))) {
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

std::vector<tree::TerminalNode *> OpenCMLParser::StructTypeContext::INTEGER() {
  return getTokens(OpenCMLParser::INTEGER);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::INTEGER(size_t i) {
  return getToken(OpenCMLParser::INTEGER, i);
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
  enterRule(_localctx, 122, OpenCMLParser::RuleStructType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(740);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 99, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(650);
      match(OpenCMLParser::SET_TYPE);
      setState(655);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__13) {
        setState(651);
        match(OpenCMLParser::T__13);
        setState(652);
        typeExpr();
        setState(653);
        match(OpenCMLParser::T__14);
      }
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(657);
      match(OpenCMLParser::MAP_TYPE);
      setState(664);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__13) {
        setState(658);
        match(OpenCMLParser::T__13);
        setState(659);
        typeExpr();
        setState(660);
        match(OpenCMLParser::T__8);
        setState(661);
        typeExpr();
        setState(662);
        match(OpenCMLParser::T__14);
      }
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(666);
      match(OpenCMLParser::LIST_TYPE);
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(667);
      match(OpenCMLParser::DICT_TYPE);
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(668);
      match(OpenCMLParser::ARRAY_TYPE);
      setState(677);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__13) {
        setState(669);
        match(OpenCMLParser::T__13);
        setState(670);
        typeExpr();
        setState(673);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__8) {
          setState(671);
          match(OpenCMLParser::T__8);
          setState(672);
          match(OpenCMLParser::INTEGER);
        }
        setState(675);
        match(OpenCMLParser::T__14);
      }
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(679);
      match(OpenCMLParser::TUPLE_TYPE);
      setState(688);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__13) {
        setState(680);
        match(OpenCMLParser::T__13);
        setState(682);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & -2305843009213672448) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
          ((1ULL << (_la - 64)) & 17301503) != 0)) {
          setState(681);
          typeList();
        }
        setState(685);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__8) {
          setState(684);
          match(OpenCMLParser::T__8);
        }
        setState(687);
        match(OpenCMLParser::T__14);
      }
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(690);
      match(OpenCMLParser::UNION_TYPE);
      setState(699);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__13) {
        setState(691);
        match(OpenCMLParser::T__13);
        setState(693);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & -2305843009213672448) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
          ((1ULL << (_la - 64)) & 17301503) != 0)) {
          setState(692);
          typeList();
        }
        setState(696);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__8) {
          setState(695);
          match(OpenCMLParser::T__8);
        }
        setState(698);
        match(OpenCMLParser::T__14);
      }
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(701);
      match(OpenCMLParser::VECTOR_TYPE);
      setState(706);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__13) {
        setState(702);
        match(OpenCMLParser::T__13);
        setState(703);
        typeExpr();
        setState(704);
        match(OpenCMLParser::T__14);
      }
      break;
    }

    case 9: {
      enterOuterAlt(_localctx, 9);
      setState(708);
      match(OpenCMLParser::TENSOR_TYPE);
      setState(726);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__13) {
        setState(709);
        match(OpenCMLParser::T__13);
        setState(710);
        typeExpr();
        setState(722);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__8) {
          setState(711);
          match(OpenCMLParser::T__8);
          setState(712);
          match(OpenCMLParser::T__6);
          setState(713);
          match(OpenCMLParser::INTEGER);
          setState(718);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == OpenCMLParser::T__8) {
            setState(714);
            match(OpenCMLParser::T__8);
            setState(715);
            match(OpenCMLParser::INTEGER);
            setState(720);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
          setState(721);
          match(OpenCMLParser::T__7);
        }
        setState(724);
        match(OpenCMLParser::T__14);
      }
      break;
    }

    case 10: {
      enterOuterAlt(_localctx, 10);
      setState(728);
      match(OpenCMLParser::T__9);
      setState(730);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::IDENTIFIER) {
        setState(729);
        pairedTypes();
      }
      setState(733);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__8) {
        setState(732);
        match(OpenCMLParser::T__8);
      }
      setState(735);
      match(OpenCMLParser::T__10);
      break;
    }

    case 11: {
      enterOuterAlt(_localctx, 11);
      setState(736);
      match(OpenCMLParser::T__9);
      setState(737);
      indexKTPair();
      setState(738);
      match(OpenCMLParser::T__10);
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
  enterRule(_localctx, 124, OpenCMLParser::RuleSpecialType);
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
    setState(742);
    _la = _input->LA(1);
    if (!(((((_la - 80) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 80)) & 7) != 0))) {
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
  enterRule(_localctx, 126, OpenCMLParser::RuleIdentRef);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(744);
    match(OpenCMLParser::IDENTIFIER);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

void OpenCMLParser::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  opencmlParserInitialize();
#else
  ::antlr4::internal::call_once(opencmlParserOnceFlag, opencmlParserInitialize);
#endif
}
