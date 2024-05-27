
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
      "parentValues", "angledParams", "angledValues", "primEntity", "memberAccess", 
      "entity", "entityChain", "entityLink", "entityCall", "entitySpread", 
      "entityExpr", "relaExpr", "addExpr", "multiExpr", "unaryExpr", "primExpr", 
      "literal", "typeExpr", "type", "lambdaType", "primType", "structType", 
      "specialType", "identRef"
    },
    std::vector<std::string>{
      "", "':'", "'='", "':='", "'::'", "'=>'", "'{'", "'}'", "'@'", "','", 
      "'['", "']'", "'('", "')'", "'<'", "'>'", "'->'", "'.'", "'...'", 
      "'+='", "'-='", "'*='", "'/='", "'%='", "'^='", "'&='", "'|='", "'<='", 
      "'>='", "'=='", "'!='", "'&&'", "'||'", "'+'", "'-'", "'&'", "'|'", 
      "'^'", "'*'", "'/'", "'%'", "'!'", "'~'", "';'", "'as'", "'is'", "'let'", 
      "'use'", "'func'", "'type'", "'with'", "'return'", "'inner'", "'outer'", 
      "'atomic'", "'static'", "'sync'", "'null'", "'true'", "'false'", "'Int'", 
      "'Int32'", "'Int64'", "'Real'", "'Float'", "'Double'", "'Number'", 
      "'String'", "'Bool'", "'Char'", "'Set'", "'Map'", "'List'", "'Dict'", 
      "'Array'", "'Tuple'", "'Union'", "'Vector'", "'Tensor'", "'Any'", 
      "'Void'", "'Functor'"
    },
    std::vector<std::string>{
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "SEP", "AS", "IS", "LET", "USE", 
      "FUNC", "TYPE", "WITH", "RETURN", "INNER", "OUTER", "ATOMIC", "STATIC", 
      "SYNC", "NULL", "TRUE", "FALSE", "INTEGER_TYPE", "INTEGER32_TYPE", 
      "INTEGER64_TYPE", "REAL_TYPE", "FLOAT_TYPE", "DOUBLE_TYPE", "NUMBER_TYPE", 
      "STRING_TYPE", "BOOL_TYPE", "CHAR_TYPE", "SET_TYPE", "MAP_TYPE", "LIST_TYPE", 
      "DICT_TYPE", "ARRAY_TYPE", "TUPLE_TYPE", "UNION_TYPE", "VECTOR_TYPE", 
      "TENSOR_TYPE", "ANY_TYPE", "VOID_TYPE", "FUNCTOR_TYPE", "BLANK", "LIN_CMT_HASH", 
      "LIN_CMT_SLASH", "BLK_CMT", "MULTI_STR", "IDENTIFIER", "UNIT", "STRING", 
      "FSTRING", "INTEGER", "REAL"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,92,741,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,2,55,7,55,1,0,3,
  	0,114,8,0,1,0,1,0,1,1,1,1,1,1,5,1,121,8,1,10,1,12,1,124,9,1,1,1,3,1,127,
  	8,1,1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,2,136,8,2,1,3,1,3,1,3,1,3,3,3,142,8,
  	3,1,3,3,3,145,8,3,1,3,3,3,148,8,3,1,3,1,3,1,3,3,3,153,8,3,1,3,1,3,1,3,
  	3,3,158,8,3,1,4,1,4,1,4,3,4,163,8,4,1,4,1,4,1,4,1,4,1,4,1,4,3,4,171,8,
  	4,1,5,1,5,1,5,3,5,176,8,5,1,5,1,5,1,6,3,6,181,8,6,1,6,1,6,1,7,1,7,3,7,
  	187,8,7,1,7,1,7,1,7,1,8,1,8,1,8,1,9,3,9,196,8,9,1,9,3,9,199,8,9,1,9,3,
  	9,202,8,9,1,9,1,9,1,9,1,9,1,9,3,9,209,8,9,1,9,1,9,1,10,1,10,3,10,215,
  	8,10,1,11,3,11,218,8,11,1,11,1,11,1,11,3,11,223,8,11,1,11,1,11,1,11,3,
  	11,228,8,11,1,11,1,11,1,11,1,11,3,11,234,8,11,1,12,1,12,1,12,3,12,239,
  	8,12,1,13,1,13,1,13,1,14,4,14,245,8,14,11,14,12,14,246,1,15,4,15,250,
  	8,15,11,15,12,15,251,1,16,1,16,1,16,1,16,1,17,1,17,1,17,1,17,1,18,1,18,
  	3,18,264,8,18,1,18,1,18,1,18,1,18,3,18,270,8,18,1,19,1,19,1,19,5,19,275,
  	8,19,10,19,12,19,278,9,19,1,20,1,20,1,20,5,20,283,8,20,10,20,12,20,286,
  	9,20,1,21,1,21,1,21,5,21,291,8,21,10,21,12,21,294,9,21,1,22,1,22,1,22,
  	5,22,299,8,22,10,22,12,22,302,9,22,1,23,1,23,1,23,5,23,307,8,23,10,23,
  	12,23,310,9,23,1,24,1,24,1,24,5,24,315,8,24,10,24,12,24,318,9,24,1,25,
  	1,25,1,25,3,25,323,8,25,1,25,3,25,326,8,25,1,26,1,26,3,26,330,8,26,1,
  	26,3,26,333,8,26,1,26,1,26,1,27,1,27,3,27,339,8,27,1,27,3,27,342,8,27,
  	1,27,1,27,1,28,1,28,3,28,348,8,28,1,28,1,28,1,29,1,29,3,29,354,8,29,1,
  	29,3,29,357,8,29,1,29,1,29,1,30,1,30,3,30,363,8,30,1,30,3,30,366,8,30,
  	1,30,1,30,1,31,1,31,3,31,372,8,31,1,31,3,31,375,8,31,1,31,1,31,1,32,1,
  	32,3,32,381,8,32,1,32,3,32,384,8,32,1,32,1,32,1,33,1,33,3,33,390,8,33,
  	1,33,3,33,393,8,33,1,33,1,33,1,34,1,34,3,34,399,8,34,1,34,3,34,402,8,
  	34,1,34,1,34,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,3,35,415,8,
  	35,1,36,1,36,1,36,1,36,1,37,1,37,1,37,1,37,1,37,3,37,426,8,37,1,37,5,
  	37,429,8,37,10,37,12,37,432,9,37,1,38,4,38,435,8,38,11,38,12,38,436,1,
  	39,1,39,1,39,1,39,1,39,1,39,5,39,445,8,39,10,39,12,39,448,9,39,1,40,1,
  	40,1,40,1,40,1,40,1,40,5,40,456,8,40,10,40,12,40,459,9,40,1,41,1,41,1,
  	41,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,
  	42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,5,
  	42,491,8,42,10,42,12,42,494,9,42,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,
  	43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,
  	43,1,43,1,43,1,43,1,43,1,43,5,43,523,8,43,10,43,12,43,526,9,43,1,44,1,
  	44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,5,
  	44,543,8,44,10,44,12,44,546,9,44,1,45,1,45,1,45,1,45,1,45,1,45,1,45,1,
  	45,1,45,1,45,1,45,1,45,1,45,1,45,1,45,1,45,1,45,1,45,1,45,1,45,1,45,5,
  	45,569,8,45,10,45,12,45,572,9,45,1,46,1,46,1,46,1,46,1,46,3,46,579,8,
  	46,1,47,1,47,1,47,1,47,1,47,3,47,586,8,47,1,48,1,48,3,48,590,8,48,1,48,
  	1,48,3,48,594,8,48,1,48,1,48,1,48,1,48,1,48,1,48,3,48,602,8,48,1,49,1,
  	49,1,49,1,49,3,49,608,8,49,1,49,1,49,1,49,1,49,1,49,1,49,5,49,616,8,49,
  	10,49,12,49,619,9,49,1,50,1,50,1,50,1,50,1,50,1,50,1,50,1,50,1,50,3,50,
  	630,8,50,1,51,1,51,3,51,634,8,51,1,51,3,51,637,8,51,1,51,1,51,3,51,641,
  	8,51,1,51,1,51,1,51,1,51,1,52,1,52,1,53,1,53,1,53,1,53,1,53,3,53,654,
  	8,53,1,53,1,53,1,53,1,53,1,53,1,53,1,53,3,53,663,8,53,1,53,1,53,1,53,
  	1,53,3,53,669,8,53,1,53,3,53,672,8,53,1,53,1,53,1,53,1,53,1,53,1,53,3,
  	53,680,8,53,1,53,1,53,1,53,3,53,685,8,53,1,53,3,53,688,8,53,1,53,3,53,
  	691,8,53,1,53,1,53,1,53,3,53,696,8,53,1,53,3,53,699,8,53,1,53,3,53,702,
  	8,53,1,53,1,53,1,53,1,53,1,53,3,53,709,8,53,1,53,1,53,3,53,713,8,53,1,
  	53,1,53,1,53,1,53,1,53,1,53,1,53,1,53,5,53,723,8,53,10,53,12,53,726,9,
  	53,1,53,3,53,729,8,53,1,53,1,53,3,53,733,8,53,3,53,735,8,53,1,54,1,54,
  	1,55,1,55,1,55,0,7,78,80,84,86,88,90,98,56,0,2,4,6,8,10,12,14,16,18,20,
  	22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,
  	68,70,72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104,106,108,110,
  	0,3,1,0,52,56,1,0,60,69,1,0,79,81,829,0,113,1,0,0,0,2,117,1,0,0,0,4,135,
  	1,0,0,0,6,157,1,0,0,0,8,170,1,0,0,0,10,172,1,0,0,0,12,180,1,0,0,0,14,
  	184,1,0,0,0,16,191,1,0,0,0,18,195,1,0,0,0,20,212,1,0,0,0,22,217,1,0,0,
  	0,24,238,1,0,0,0,26,240,1,0,0,0,28,244,1,0,0,0,30,249,1,0,0,0,32,253,
  	1,0,0,0,34,257,1,0,0,0,36,261,1,0,0,0,38,271,1,0,0,0,40,279,1,0,0,0,42,
  	287,1,0,0,0,44,295,1,0,0,0,46,303,1,0,0,0,48,311,1,0,0,0,50,325,1,0,0,
  	0,52,327,1,0,0,0,54,336,1,0,0,0,56,345,1,0,0,0,58,351,1,0,0,0,60,360,
  	1,0,0,0,62,369,1,0,0,0,64,378,1,0,0,0,66,387,1,0,0,0,68,396,1,0,0,0,70,
  	414,1,0,0,0,72,416,1,0,0,0,74,420,1,0,0,0,76,434,1,0,0,0,78,438,1,0,0,
  	0,80,449,1,0,0,0,82,460,1,0,0,0,84,463,1,0,0,0,86,495,1,0,0,0,88,527,
  	1,0,0,0,90,547,1,0,0,0,92,578,1,0,0,0,94,585,1,0,0,0,96,601,1,0,0,0,98,
  	603,1,0,0,0,100,629,1,0,0,0,102,636,1,0,0,0,104,646,1,0,0,0,106,734,1,
  	0,0,0,108,736,1,0,0,0,110,738,1,0,0,0,112,114,3,2,1,0,113,112,1,0,0,0,
  	113,114,1,0,0,0,114,115,1,0,0,0,115,116,5,0,0,1,116,1,1,0,0,0,117,122,
  	3,4,2,0,118,119,5,43,0,0,119,121,3,4,2,0,120,118,1,0,0,0,121,124,1,0,
  	0,0,122,120,1,0,0,0,122,123,1,0,0,0,123,126,1,0,0,0,124,122,1,0,0,0,125,
  	127,5,43,0,0,126,125,1,0,0,0,126,127,1,0,0,0,127,3,1,0,0,0,128,136,3,
  	6,3,0,129,136,3,8,4,0,130,136,3,10,5,0,131,136,3,12,6,0,132,136,3,14,
  	7,0,133,136,3,18,9,0,134,136,3,20,10,0,135,128,1,0,0,0,135,129,1,0,0,
  	0,135,130,1,0,0,0,135,131,1,0,0,0,135,132,1,0,0,0,135,133,1,0,0,0,135,
  	134,1,0,0,0,136,5,1,0,0,0,137,138,5,46,0,0,138,141,3,24,12,0,139,140,
  	5,1,0,0,140,142,3,98,49,0,141,139,1,0,0,0,141,142,1,0,0,0,142,147,1,0,
  	0,0,143,145,5,2,0,0,144,143,1,0,0,0,144,145,1,0,0,0,145,146,1,0,0,0,146,
  	148,3,84,42,0,147,144,1,0,0,0,147,148,1,0,0,0,148,158,1,0,0,0,149,152,
  	3,24,12,0,150,151,5,1,0,0,151,153,3,98,49,0,152,150,1,0,0,0,152,153,1,
  	0,0,0,153,154,1,0,0,0,154,155,5,3,0,0,155,156,3,84,42,0,156,158,1,0,0,
  	0,157,137,1,0,0,0,157,149,1,0,0,0,158,7,1,0,0,0,159,160,5,47,0,0,160,
  	162,3,24,12,0,161,163,5,2,0,0,162,161,1,0,0,0,162,163,1,0,0,0,163,164,
  	1,0,0,0,164,165,3,84,42,0,165,171,1,0,0,0,166,167,3,24,12,0,167,168,5,
  	4,0,0,168,169,3,84,42,0,169,171,1,0,0,0,170,159,1,0,0,0,170,166,1,0,0,
  	0,171,9,1,0,0,0,172,173,5,49,0,0,173,175,3,110,55,0,174,176,5,2,0,0,175,
  	174,1,0,0,0,175,176,1,0,0,0,176,177,1,0,0,0,177,178,3,98,49,0,178,11,
  	1,0,0,0,179,181,3,28,14,0,180,179,1,0,0,0,180,181,1,0,0,0,181,182,1,0,
  	0,0,182,183,3,84,42,0,183,13,1,0,0,0,184,186,3,110,55,0,185,187,3,72,
  	36,0,186,185,1,0,0,0,186,187,1,0,0,0,187,188,1,0,0,0,188,189,5,2,0,0,
  	189,190,3,84,42,0,190,15,1,0,0,0,191,192,5,50,0,0,192,193,3,66,33,0,193,
  	17,1,0,0,0,194,196,3,28,14,0,195,194,1,0,0,0,195,196,1,0,0,0,196,198,
  	1,0,0,0,197,199,3,16,8,0,198,197,1,0,0,0,198,199,1,0,0,0,199,201,1,0,
  	0,0,200,202,3,30,15,0,201,200,1,0,0,0,201,202,1,0,0,0,202,203,1,0,0,0,
  	203,204,5,48,0,0,204,205,3,110,55,0,205,208,3,62,31,0,206,207,5,1,0,0,
  	207,209,3,98,49,0,208,206,1,0,0,0,208,209,1,0,0,0,209,210,1,0,0,0,210,
  	211,3,56,28,0,211,19,1,0,0,0,212,214,5,51,0,0,213,215,3,84,42,0,214,213,
  	1,0,0,0,214,215,1,0,0,0,215,21,1,0,0,0,216,218,3,30,15,0,217,216,1,0,
  	0,0,217,218,1,0,0,0,218,233,1,0,0,0,219,222,3,62,31,0,220,221,5,1,0,0,
  	221,223,3,98,49,0,222,220,1,0,0,0,222,223,1,0,0,0,223,224,1,0,0,0,224,
  	227,5,5,0,0,225,228,3,56,28,0,226,228,3,84,42,0,227,225,1,0,0,0,227,226,
  	1,0,0,0,228,234,1,0,0,0,229,230,5,6,0,0,230,231,3,2,1,0,231,232,5,7,0,
  	0,232,234,1,0,0,0,233,219,1,0,0,0,233,229,1,0,0,0,234,23,1,0,0,0,235,
  	239,3,110,55,0,236,239,3,54,27,0,237,239,3,58,29,0,238,235,1,0,0,0,238,
  	236,1,0,0,0,238,237,1,0,0,0,239,25,1,0,0,0,240,241,5,8,0,0,241,242,3,
  	70,35,0,242,27,1,0,0,0,243,245,3,26,13,0,244,243,1,0,0,0,245,246,1,0,
  	0,0,246,244,1,0,0,0,246,247,1,0,0,0,247,29,1,0,0,0,248,250,7,0,0,0,249,
  	248,1,0,0,0,250,251,1,0,0,0,251,249,1,0,0,0,251,252,1,0,0,0,252,31,1,
  	0,0,0,253,254,3,110,55,0,254,255,5,1,0,0,255,256,3,98,49,0,256,33,1,0,
  	0,0,257,258,3,110,55,0,258,259,5,1,0,0,259,260,3,84,42,0,260,35,1,0,0,
  	0,261,263,3,110,55,0,262,264,3,26,13,0,263,262,1,0,0,0,263,264,1,0,0,
  	0,264,265,1,0,0,0,265,266,5,1,0,0,266,269,3,98,49,0,267,268,5,2,0,0,268,
  	270,3,84,42,0,269,267,1,0,0,0,269,270,1,0,0,0,270,37,1,0,0,0,271,276,
  	3,98,49,0,272,273,5,9,0,0,273,275,3,98,49,0,274,272,1,0,0,0,275,278,1,
  	0,0,0,276,274,1,0,0,0,276,277,1,0,0,0,277,39,1,0,0,0,278,276,1,0,0,0,
  	279,284,3,110,55,0,280,281,5,9,0,0,281,283,3,110,55,0,282,280,1,0,0,0,
  	283,286,1,0,0,0,284,282,1,0,0,0,284,285,1,0,0,0,285,41,1,0,0,0,286,284,
  	1,0,0,0,287,292,3,84,42,0,288,289,5,9,0,0,289,291,3,84,42,0,290,288,1,
  	0,0,0,291,294,1,0,0,0,292,290,1,0,0,0,292,293,1,0,0,0,293,43,1,0,0,0,
  	294,292,1,0,0,0,295,300,3,32,16,0,296,297,5,9,0,0,297,299,3,32,16,0,298,
  	296,1,0,0,0,299,302,1,0,0,0,300,298,1,0,0,0,300,301,1,0,0,0,301,45,1,
  	0,0,0,302,300,1,0,0,0,303,308,3,34,17,0,304,305,5,9,0,0,305,307,3,34,
  	17,0,306,304,1,0,0,0,307,310,1,0,0,0,308,306,1,0,0,0,308,309,1,0,0,0,
  	309,47,1,0,0,0,310,308,1,0,0,0,311,316,3,36,18,0,312,313,5,9,0,0,313,
  	315,3,36,18,0,314,312,1,0,0,0,315,318,1,0,0,0,316,314,1,0,0,0,316,317,
  	1,0,0,0,317,49,1,0,0,0,318,316,1,0,0,0,319,322,3,42,21,0,320,321,5,9,
  	0,0,321,323,3,46,23,0,322,320,1,0,0,0,322,323,1,0,0,0,323,326,1,0,0,0,
  	324,326,3,46,23,0,325,319,1,0,0,0,325,324,1,0,0,0,326,51,1,0,0,0,327,
  	329,5,6,0,0,328,330,3,46,23,0,329,328,1,0,0,0,329,330,1,0,0,0,330,332,
  	1,0,0,0,331,333,5,9,0,0,332,331,1,0,0,0,332,333,1,0,0,0,333,334,1,0,0,
  	0,334,335,5,7,0,0,335,53,1,0,0,0,336,338,5,6,0,0,337,339,3,40,20,0,338,
  	337,1,0,0,0,338,339,1,0,0,0,339,341,1,0,0,0,340,342,5,9,0,0,341,340,1,
  	0,0,0,341,342,1,0,0,0,342,343,1,0,0,0,343,344,5,7,0,0,344,55,1,0,0,0,
  	345,347,5,6,0,0,346,348,3,2,1,0,347,346,1,0,0,0,347,348,1,0,0,0,348,349,
  	1,0,0,0,349,350,5,7,0,0,350,57,1,0,0,0,351,353,5,10,0,0,352,354,3,40,
  	20,0,353,352,1,0,0,0,353,354,1,0,0,0,354,356,1,0,0,0,355,357,5,9,0,0,
  	356,355,1,0,0,0,356,357,1,0,0,0,357,358,1,0,0,0,358,359,5,11,0,0,359,
  	59,1,0,0,0,360,362,5,10,0,0,361,363,3,42,21,0,362,361,1,0,0,0,362,363,
  	1,0,0,0,363,365,1,0,0,0,364,366,5,9,0,0,365,364,1,0,0,0,365,366,1,0,0,
  	0,366,367,1,0,0,0,367,368,5,11,0,0,368,61,1,0,0,0,369,371,5,12,0,0,370,
  	372,3,48,24,0,371,370,1,0,0,0,371,372,1,0,0,0,372,374,1,0,0,0,373,375,
  	5,9,0,0,374,373,1,0,0,0,374,375,1,0,0,0,375,376,1,0,0,0,376,377,5,13,
  	0,0,377,63,1,0,0,0,378,380,5,12,0,0,379,381,3,50,25,0,380,379,1,0,0,0,
  	380,381,1,0,0,0,381,383,1,0,0,0,382,384,5,9,0,0,383,382,1,0,0,0,383,384,
  	1,0,0,0,384,385,1,0,0,0,385,386,5,13,0,0,386,65,1,0,0,0,387,389,5,14,
  	0,0,388,390,3,48,24,0,389,388,1,0,0,0,389,390,1,0,0,0,390,392,1,0,0,0,
  	391,393,5,9,0,0,392,391,1,0,0,0,392,393,1,0,0,0,393,394,1,0,0,0,394,395,
  	5,15,0,0,395,67,1,0,0,0,396,398,5,14,0,0,397,399,3,50,25,0,398,397,1,
  	0,0,0,398,399,1,0,0,0,399,401,1,0,0,0,400,402,5,9,0,0,401,400,1,0,0,0,
  	401,402,1,0,0,0,402,403,1,0,0,0,403,404,5,15,0,0,404,69,1,0,0,0,405,415,
  	3,110,55,0,406,415,3,96,48,0,407,415,3,60,30,0,408,415,3,52,26,0,409,
  	415,3,22,11,0,410,411,5,12,0,0,411,412,3,84,42,0,412,413,5,13,0,0,413,
  	415,1,0,0,0,414,405,1,0,0,0,414,406,1,0,0,0,414,407,1,0,0,0,414,408,1,
  	0,0,0,414,409,1,0,0,0,414,410,1,0,0,0,415,71,1,0,0,0,416,417,5,10,0,0,
  	417,418,3,84,42,0,418,419,5,11,0,0,419,73,1,0,0,0,420,430,3,70,35,0,421,
  	425,4,37,0,0,422,426,3,72,36,0,423,426,3,68,34,0,424,426,3,64,32,0,425,
  	422,1,0,0,0,425,423,1,0,0,0,425,424,1,0,0,0,426,429,1,0,0,0,427,429,3,
  	26,13,0,428,421,1,0,0,0,428,427,1,0,0,0,429,432,1,0,0,0,430,428,1,0,0,
  	0,430,431,1,0,0,0,431,75,1,0,0,0,432,430,1,0,0,0,433,435,3,78,39,0,434,
  	433,1,0,0,0,435,436,1,0,0,0,436,434,1,0,0,0,436,437,1,0,0,0,437,77,1,
  	0,0,0,438,439,6,39,-1,0,439,440,3,80,40,0,440,446,1,0,0,0,441,442,10,
  	1,0,0,442,443,5,16,0,0,443,445,3,80,40,0,444,441,1,0,0,0,445,448,1,0,
  	0,0,446,444,1,0,0,0,446,447,1,0,0,0,447,79,1,0,0,0,448,446,1,0,0,0,449,
  	450,6,40,-1,0,450,451,3,74,37,0,451,457,1,0,0,0,452,453,10,1,0,0,453,
  	454,5,17,0,0,454,456,3,74,37,0,455,452,1,0,0,0,456,459,1,0,0,0,457,455,
  	1,0,0,0,457,458,1,0,0,0,458,81,1,0,0,0,459,457,1,0,0,0,460,461,5,18,0,
  	0,461,462,3,74,37,0,462,83,1,0,0,0,463,464,6,42,-1,0,464,465,3,86,43,
  	0,465,492,1,0,0,0,466,467,10,8,0,0,467,468,5,19,0,0,468,491,3,86,43,0,
  	469,470,10,7,0,0,470,471,5,20,0,0,471,491,3,86,43,0,472,473,10,6,0,0,
  	473,474,5,21,0,0,474,491,3,86,43,0,475,476,10,5,0,0,476,477,5,22,0,0,
  	477,491,3,86,43,0,478,479,10,4,0,0,479,480,5,23,0,0,480,491,3,86,43,0,
  	481,482,10,3,0,0,482,483,5,24,0,0,483,491,3,86,43,0,484,485,10,2,0,0,
  	485,486,5,25,0,0,486,491,3,86,43,0,487,488,10,1,0,0,488,489,5,26,0,0,
  	489,491,3,86,43,0,490,466,1,0,0,0,490,469,1,0,0,0,490,472,1,0,0,0,490,
  	475,1,0,0,0,490,478,1,0,0,0,490,481,1,0,0,0,490,484,1,0,0,0,490,487,1,
  	0,0,0,491,494,1,0,0,0,492,490,1,0,0,0,492,493,1,0,0,0,493,85,1,0,0,0,
  	494,492,1,0,0,0,495,496,6,43,-1,0,496,497,3,88,44,0,497,524,1,0,0,0,498,
  	499,10,8,0,0,499,500,5,14,0,0,500,523,3,88,44,0,501,502,10,7,0,0,502,
  	503,5,15,0,0,503,523,3,88,44,0,504,505,10,6,0,0,505,506,5,27,0,0,506,
  	523,3,88,44,0,507,508,10,5,0,0,508,509,5,28,0,0,509,523,3,88,44,0,510,
  	511,10,4,0,0,511,512,5,29,0,0,512,523,3,88,44,0,513,514,10,3,0,0,514,
  	515,5,30,0,0,515,523,3,88,44,0,516,517,10,2,0,0,517,518,5,31,0,0,518,
  	523,3,88,44,0,519,520,10,1,0,0,520,521,5,32,0,0,521,523,3,88,44,0,522,
  	498,1,0,0,0,522,501,1,0,0,0,522,504,1,0,0,0,522,507,1,0,0,0,522,510,1,
  	0,0,0,522,513,1,0,0,0,522,516,1,0,0,0,522,519,1,0,0,0,523,526,1,0,0,0,
  	524,522,1,0,0,0,524,525,1,0,0,0,525,87,1,0,0,0,526,524,1,0,0,0,527,528,
  	6,44,-1,0,528,529,3,90,45,0,529,544,1,0,0,0,530,531,10,4,0,0,531,532,
  	5,33,0,0,532,543,3,90,45,0,533,534,10,3,0,0,534,535,5,34,0,0,535,543,
  	3,90,45,0,536,537,10,2,0,0,537,538,5,35,0,0,538,543,3,90,45,0,539,540,
  	10,1,0,0,540,541,5,36,0,0,541,543,3,90,45,0,542,530,1,0,0,0,542,533,1,
  	0,0,0,542,536,1,0,0,0,542,539,1,0,0,0,543,546,1,0,0,0,544,542,1,0,0,0,
  	544,545,1,0,0,0,545,89,1,0,0,0,546,544,1,0,0,0,547,548,6,45,-1,0,548,
  	549,3,92,46,0,549,570,1,0,0,0,550,551,10,6,0,0,551,552,5,37,0,0,552,569,
  	3,92,46,0,553,554,10,5,0,0,554,555,5,38,0,0,555,569,3,92,46,0,556,557,
  	10,4,0,0,557,558,5,39,0,0,558,569,3,92,46,0,559,560,10,3,0,0,560,561,
  	5,40,0,0,561,569,3,92,46,0,562,563,10,2,0,0,563,564,5,44,0,0,564,569,
  	3,98,49,0,565,566,10,1,0,0,566,567,5,45,0,0,567,569,3,98,49,0,568,550,
  	1,0,0,0,568,553,1,0,0,0,568,556,1,0,0,0,568,559,1,0,0,0,568,562,1,0,0,
  	0,568,565,1,0,0,0,569,572,1,0,0,0,570,568,1,0,0,0,570,571,1,0,0,0,571,
  	91,1,0,0,0,572,570,1,0,0,0,573,579,3,94,47,0,574,575,5,41,0,0,575,579,
  	3,94,47,0,576,577,5,42,0,0,577,579,3,94,47,0,578,573,1,0,0,0,578,574,
  	1,0,0,0,578,576,1,0,0,0,579,93,1,0,0,0,580,586,3,76,38,0,581,582,5,12,
  	0,0,582,583,3,84,42,0,583,584,5,13,0,0,584,586,1,0,0,0,585,580,1,0,0,
  	0,585,581,1,0,0,0,586,95,1,0,0,0,587,589,5,91,0,0,588,590,5,88,0,0,589,
  	588,1,0,0,0,589,590,1,0,0,0,590,602,1,0,0,0,591,593,5,92,0,0,592,594,
  	5,88,0,0,593,592,1,0,0,0,593,594,1,0,0,0,594,602,1,0,0,0,595,602,5,89,
  	0,0,596,602,5,86,0,0,597,602,5,90,0,0,598,602,5,58,0,0,599,602,5,59,0,
  	0,600,602,5,57,0,0,601,587,1,0,0,0,601,591,1,0,0,0,601,595,1,0,0,0,601,
  	596,1,0,0,0,601,597,1,0,0,0,601,598,1,0,0,0,601,599,1,0,0,0,601,600,1,
  	0,0,0,602,97,1,0,0,0,603,604,6,49,-1,0,604,607,3,100,50,0,605,606,5,10,
  	0,0,606,608,5,11,0,0,607,605,1,0,0,0,607,608,1,0,0,0,608,617,1,0,0,0,
  	609,610,10,2,0,0,610,611,5,36,0,0,611,616,3,98,49,3,612,613,10,1,0,0,
  	613,614,5,35,0,0,614,616,3,98,49,2,615,609,1,0,0,0,615,612,1,0,0,0,616,
  	619,1,0,0,0,617,615,1,0,0,0,617,618,1,0,0,0,618,99,1,0,0,0,619,617,1,
  	0,0,0,620,630,3,104,52,0,621,630,3,106,53,0,622,630,3,108,54,0,623,630,
  	3,110,55,0,624,625,5,12,0,0,625,626,3,98,49,0,626,627,5,13,0,0,627,630,
  	1,0,0,0,628,630,3,102,51,0,629,620,1,0,0,0,629,621,1,0,0,0,629,622,1,
  	0,0,0,629,623,1,0,0,0,629,624,1,0,0,0,629,628,1,0,0,0,630,101,1,0,0,0,
  	631,633,5,14,0,0,632,634,3,48,24,0,633,632,1,0,0,0,633,634,1,0,0,0,634,
  	635,1,0,0,0,635,637,5,15,0,0,636,631,1,0,0,0,636,637,1,0,0,0,637,638,
  	1,0,0,0,638,640,5,12,0,0,639,641,3,48,24,0,640,639,1,0,0,0,640,641,1,
  	0,0,0,641,642,1,0,0,0,642,643,5,13,0,0,643,644,5,5,0,0,644,645,3,98,49,
  	0,645,103,1,0,0,0,646,647,7,1,0,0,647,105,1,0,0,0,648,653,5,70,0,0,649,
  	650,5,14,0,0,650,651,3,98,49,0,651,652,5,15,0,0,652,654,1,0,0,0,653,649,
  	1,0,0,0,653,654,1,0,0,0,654,735,1,0,0,0,655,662,5,71,0,0,656,657,5,14,
  	0,0,657,658,3,98,49,0,658,659,5,9,0,0,659,660,3,98,49,0,660,661,5,15,
  	0,0,661,663,1,0,0,0,662,656,1,0,0,0,662,663,1,0,0,0,663,735,1,0,0,0,664,
  	735,5,72,0,0,665,735,5,73,0,0,666,668,5,6,0,0,667,669,3,44,22,0,668,667,
  	1,0,0,0,668,669,1,0,0,0,669,671,1,0,0,0,670,672,5,9,0,0,671,670,1,0,0,
  	0,671,672,1,0,0,0,672,673,1,0,0,0,673,735,5,7,0,0,674,679,5,74,0,0,675,
  	676,5,14,0,0,676,677,3,98,49,0,677,678,5,15,0,0,678,680,1,0,0,0,679,675,
  	1,0,0,0,679,680,1,0,0,0,680,735,1,0,0,0,681,690,5,75,0,0,682,684,5,14,
  	0,0,683,685,3,38,19,0,684,683,1,0,0,0,684,685,1,0,0,0,685,687,1,0,0,0,
  	686,688,5,9,0,0,687,686,1,0,0,0,687,688,1,0,0,0,688,689,1,0,0,0,689,691,
  	5,15,0,0,690,682,1,0,0,0,690,691,1,0,0,0,691,735,1,0,0,0,692,701,5,76,
  	0,0,693,695,5,14,0,0,694,696,3,38,19,0,695,694,1,0,0,0,695,696,1,0,0,
  	0,696,698,1,0,0,0,697,699,5,9,0,0,698,697,1,0,0,0,698,699,1,0,0,0,699,
  	700,1,0,0,0,700,702,5,15,0,0,701,693,1,0,0,0,701,702,1,0,0,0,702,735,
  	1,0,0,0,703,712,5,77,0,0,704,705,5,14,0,0,705,708,3,98,49,0,706,707,5,
  	9,0,0,707,709,5,91,0,0,708,706,1,0,0,0,708,709,1,0,0,0,709,710,1,0,0,
  	0,710,711,5,15,0,0,711,713,1,0,0,0,712,704,1,0,0,0,712,713,1,0,0,0,713,
  	735,1,0,0,0,714,732,5,78,0,0,715,716,5,14,0,0,716,728,3,98,49,0,717,718,
  	5,9,0,0,718,719,5,10,0,0,719,724,5,91,0,0,720,721,5,9,0,0,721,723,5,91,
  	0,0,722,720,1,0,0,0,723,726,1,0,0,0,724,722,1,0,0,0,724,725,1,0,0,0,725,
  	727,1,0,0,0,726,724,1,0,0,0,727,729,5,11,0,0,728,717,1,0,0,0,728,729,
  	1,0,0,0,729,730,1,0,0,0,730,731,5,15,0,0,731,733,1,0,0,0,732,715,1,0,
  	0,0,732,733,1,0,0,0,733,735,1,0,0,0,734,648,1,0,0,0,734,655,1,0,0,0,734,
  	664,1,0,0,0,734,665,1,0,0,0,734,666,1,0,0,0,734,674,1,0,0,0,734,681,1,
  	0,0,0,734,692,1,0,0,0,734,703,1,0,0,0,734,714,1,0,0,0,735,107,1,0,0,0,
  	736,737,7,2,0,0,737,109,1,0,0,0,738,739,5,87,0,0,739,111,1,0,0,0,97,113,
  	122,126,135,141,144,147,152,157,162,170,175,180,186,195,198,201,208,214,
  	217,222,227,233,238,246,251,263,269,276,284,292,300,308,316,322,325,329,
  	332,338,341,347,353,356,362,365,371,374,380,383,389,392,398,401,414,425,
  	428,430,436,446,457,490,492,522,524,542,544,568,570,578,585,589,593,601,
  	607,615,617,629,633,636,640,653,662,668,671,679,684,687,690,695,698,701,
  	708,712,724,728,732,734
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
    setState(113);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1152857732932441408) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
      setState(112);
      stmtList();
    }
    setState(115);
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
    setState(117);
    stmt();
    setState(122);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 1, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(118);
        match(OpenCMLParser::SEP);
        setState(119);
        stmt(); 
      }
      setState(124);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 1, _ctx);
    }
    setState(126);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SEP) {
      setState(125);
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
    setState(135);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 3, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(128);
      letStmt();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(129);
      useStmt();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(130);
      typeStmt();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(131);
      exprStmt();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(132);
      assignStmt();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(133);
      funcDef();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(134);
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
    setState(157);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::LET: {
        enterOuterAlt(_localctx, 1);
        setState(137);
        match(OpenCMLParser::LET);
        setState(138);
        carrier();
        setState(141);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(139);
          match(OpenCMLParser::T__0);
          setState(140);
          typeExpr(0);
        }
        setState(147);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 1148424502049248324) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
          ((1ULL << (_la - 86)) & 123) != 0)) {
          setState(144);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == OpenCMLParser::T__1) {
            setState(143);
            match(OpenCMLParser::T__1);
          }
          setState(146);
          entityExpr(0);
        }
        break;
      }

      case OpenCMLParser::T__5:
      case OpenCMLParser::T__9:
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(149);
        carrier();
        setState(152);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(150);
          match(OpenCMLParser::T__0);
          setState(151);
          typeExpr(0);
        }
        setState(154);
        match(OpenCMLParser::T__2);
        setState(155);
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
    setState(170);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::USE: {
        enterOuterAlt(_localctx, 1);
        setState(159);
        match(OpenCMLParser::USE);
        setState(160);
        carrier();
        setState(162);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__1) {
          setState(161);
          match(OpenCMLParser::T__1);
        }
        setState(164);
        entityExpr(0);
        break;
      }

      case OpenCMLParser::T__5:
      case OpenCMLParser::T__9:
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(166);
        carrier();
        setState(167);
        match(OpenCMLParser::T__3);
        setState(168);
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
    setState(172);
    match(OpenCMLParser::TYPE);
    setState(173);
    identRef();
    setState(175);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(174);
      match(OpenCMLParser::T__1);
    }
    setState(177);
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
    setState(180);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(179);
      annotations();
    }
    setState(182);
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

OpenCMLParser::MemberAccessContext* OpenCMLParser::AssignStmtContext::memberAccess() {
  return getRuleContext<OpenCMLParser::MemberAccessContext>(0);
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
    setState(184);
    identRef();
    setState(186);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__9) {
      setState(185);
      memberAccess();
    }
    setState(188);
    match(OpenCMLParser::T__1);
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
    if (_la == OpenCMLParser::T__7) {
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
      ((1ULL << _la) & 139611588448485376) != 0)) {
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
      ((1ULL << _la) & 1148424502049248320) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
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
    setState(217);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 139611588448485376) != 0)) {
      setState(216);
      modifiers();
    }
    setState(233);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__11: {
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
        match(OpenCMLParser::T__4);
        setState(227);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 21, _ctx)) {
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
        break;
      }

      case OpenCMLParser::T__5: {
        setState(229);
        match(OpenCMLParser::T__5);
        setState(230);
        stmtList();
        setState(231);
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
    setState(238);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(235);
        identRef();
        break;
      }

      case OpenCMLParser::T__5: {
        enterOuterAlt(_localctx, 2);
        setState(236);
        bracedIdents();
        break;
      }

      case OpenCMLParser::T__9: {
        enterOuterAlt(_localctx, 3);
        setState(237);
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
    setState(240);
    match(OpenCMLParser::T__7);
    setState(241);
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
    setState(244); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(243);
      annotation();
      setState(246); 
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
    setState(249); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(248);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 139611588448485376) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(251); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 139611588448485376) != 0));
   
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
    setState(253);
    identRef();
    setState(254);
    match(OpenCMLParser::T__0);
    setState(255);
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
    setState(257);
    identRef();
    setState(258);
    match(OpenCMLParser::T__0);
    setState(259);
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
    setState(261);
    identRef();
    setState(263);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(262);
      annotation();
    }
    setState(265);
    match(OpenCMLParser::T__0);
    setState(266);
    typeExpr(0);
    setState(269);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(267);
      match(OpenCMLParser::T__1);
      setState(268);
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
    setState(271);
    typeExpr(0);
    setState(276);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(272);
        match(OpenCMLParser::T__8);
        setState(273);
        typeExpr(0); 
      }
      setState(278);
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
    setState(279);
    identRef();
    setState(284);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(280);
        match(OpenCMLParser::T__8);
        setState(281);
        identRef(); 
      }
      setState(286);
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
    setState(287);
    entityExpr(0);
    setState(292);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(288);
        match(OpenCMLParser::T__8);
        setState(289);
        entityExpr(0); 
      }
      setState(294);
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
    setState(295);
    keyTypePair();
    setState(300);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(296);
        match(OpenCMLParser::T__8);
        setState(297);
        keyTypePair(); 
      }
      setState(302);
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
    setState(303);
    keyValuePair();
    setState(308);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(304);
        match(OpenCMLParser::T__8);
        setState(305);
        keyValuePair(); 
      }
      setState(310);
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
    setState(311);
    keyParamPair();
    setState(316);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(312);
        match(OpenCMLParser::T__8);
        setState(313);
        keyParamPair(); 
      }
      setState(318);
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
    setState(325);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(319);
      valueList();
      setState(322);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 34, _ctx)) {
      case 1: {
        setState(320);
        match(OpenCMLParser::T__8);
        setState(321);
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
      setState(324);
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
    setState(327);
    match(OpenCMLParser::T__5);
    setState(329);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(328);
      pairedValues();
    }
    setState(332);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(331);
      match(OpenCMLParser::T__8);
    }
    setState(334);
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
    setState(336);
    match(OpenCMLParser::T__5);
    setState(338);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(337);
      identList();
    }
    setState(341);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(340);
      match(OpenCMLParser::T__8);
    }
    setState(343);
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
    setState(345);
    match(OpenCMLParser::T__5);
    setState(347);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1152857732932441408) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
      setState(346);
      stmtList();
    }
    setState(349);
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
    setState(351);
    match(OpenCMLParser::T__9);
    setState(353);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(352);
      identList();
    }
    setState(356);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(355);
      match(OpenCMLParser::T__8);
    }
    setState(358);
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
    setState(360);
    match(OpenCMLParser::T__9);
    setState(362);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1148424502049248320) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
      setState(361);
      valueList();
    }
    setState(365);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(364);
      match(OpenCMLParser::T__8);
    }
    setState(367);
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
    setState(369);
    match(OpenCMLParser::T__11);
    setState(371);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(370);
      pairedParams();
    }
    setState(374);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(373);
      match(OpenCMLParser::T__8);
    }
    setState(376);
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
    setState(378);
    match(OpenCMLParser::T__11);
    setState(380);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1148424502049248320) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
      setState(379);
      argumentList();
    }
    setState(383);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(382);
      match(OpenCMLParser::T__8);
    }
    setState(385);
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
    setState(387);
    match(OpenCMLParser::T__13);
    setState(389);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(388);
      pairedParams();
    }
    setState(392);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(391);
      match(OpenCMLParser::T__8);
    }
    setState(394);
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
    setState(396);
    match(OpenCMLParser::T__13);
    setState(398);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1148424502049248320) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
      setState(397);
      argumentList();
    }
    setState(401);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(400);
      match(OpenCMLParser::T__8);
    }
    setState(403);
    match(OpenCMLParser::T__14);
   
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
  enterRule(_localctx, 70, OpenCMLParser::RulePrimEntity);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(414);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 53, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(405);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(406);
      literal();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(407);
      bracketValues();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(408);
      bracedPairedValues();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(409);
      lambdaExpr();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(410);
      match(OpenCMLParser::T__11);
      setState(411);
      entityExpr(0);
      setState(412);
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
    setState(416);
    match(OpenCMLParser::T__9);
    setState(417);
    entityExpr(0);
    setState(418);
    match(OpenCMLParser::T__10);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- EntityContext ------------------------------------------------------------------

OpenCMLParser::EntityContext::EntityContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimEntityContext* OpenCMLParser::EntityContext::primEntity() {
  return getRuleContext<OpenCMLParser::PrimEntityContext>(0);
}

std::vector<OpenCMLParser::AnnotationContext *> OpenCMLParser::EntityContext::annotation() {
  return getRuleContexts<OpenCMLParser::AnnotationContext>();
}

OpenCMLParser::AnnotationContext* OpenCMLParser::EntityContext::annotation(size_t i) {
  return getRuleContext<OpenCMLParser::AnnotationContext>(i);
}

std::vector<OpenCMLParser::MemberAccessContext *> OpenCMLParser::EntityContext::memberAccess() {
  return getRuleContexts<OpenCMLParser::MemberAccessContext>();
}

OpenCMLParser::MemberAccessContext* OpenCMLParser::EntityContext::memberAccess(size_t i) {
  return getRuleContext<OpenCMLParser::MemberAccessContext>(i);
}

std::vector<OpenCMLParser::AngledValuesContext *> OpenCMLParser::EntityContext::angledValues() {
  return getRuleContexts<OpenCMLParser::AngledValuesContext>();
}

OpenCMLParser::AngledValuesContext* OpenCMLParser::EntityContext::angledValues(size_t i) {
  return getRuleContext<OpenCMLParser::AngledValuesContext>(i);
}

std::vector<OpenCMLParser::ParentValuesContext *> OpenCMLParser::EntityContext::parentValues() {
  return getRuleContexts<OpenCMLParser::ParentValuesContext>();
}

OpenCMLParser::ParentValuesContext* OpenCMLParser::EntityContext::parentValues(size_t i) {
  return getRuleContext<OpenCMLParser::ParentValuesContext>(i);
}


size_t OpenCMLParser::EntityContext::getRuleIndex() const {
  return OpenCMLParser::RuleEntity;
}


std::any OpenCMLParser::EntityContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEntity(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::EntityContext* OpenCMLParser::entity() {
  EntityContext *_localctx = _tracker.createInstance<EntityContext>(_ctx, getState());
  enterRule(_localctx, 74, OpenCMLParser::RuleEntity);

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
    setState(420);
    primEntity();
    setState(430);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 56, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(428);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 55, _ctx)) {
        case 1: {
          setState(421);

          if (!(isAdjacent())) throw FailedPredicateException(this, "isAdjacent()");
          setState(425);
          _errHandler->sync(this);
          switch (_input->LA(1)) {
            case OpenCMLParser::T__9: {
              setState(422);
              memberAccess();
              break;
            }

            case OpenCMLParser::T__13: {
              setState(423);
              angledValues();
              break;
            }

            case OpenCMLParser::T__11: {
              setState(424);
              parentValues();
              break;
            }

          default:
            throw NoViableAltException(this);
          }
          break;
        }

        case 2: {
          setState(427);
          annotation();
          break;
        }

        default:
          break;
        } 
      }
      setState(432);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 56, _ctx);
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
  enterRule(_localctx, 76, OpenCMLParser::RuleEntityChain);

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
    setState(434); 
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(433);
              entityLink(0);
              break;
            }

      default:
        throw NoViableAltException(this);
      }
      setState(436); 
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 57, _ctx);
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

OpenCMLParser::EntityCallContext* OpenCMLParser::EntityLinkContext::entityCall() {
  return getRuleContext<OpenCMLParser::EntityCallContext>(0);
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
  size_t startState = 78;
  enterRecursionRule(_localctx, 78, OpenCMLParser::RuleEntityLink, precedence);

    

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
    setState(439);
    entityCall(0);
    _ctx->stop = _input->LT(-1);
    setState(446);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 58, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EntityLinkContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEntityLink);
        setState(441);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(442);
        match(OpenCMLParser::T__15);
        setState(443);
        entityCall(0); 
      }
      setState(448);
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

//----------------- EntityCallContext ------------------------------------------------------------------

OpenCMLParser::EntityCallContext::EntityCallContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::EntityContext* OpenCMLParser::EntityCallContext::entity() {
  return getRuleContext<OpenCMLParser::EntityContext>(0);
}

OpenCMLParser::EntityCallContext* OpenCMLParser::EntityCallContext::entityCall() {
  return getRuleContext<OpenCMLParser::EntityCallContext>(0);
}


size_t OpenCMLParser::EntityCallContext::getRuleIndex() const {
  return OpenCMLParser::RuleEntityCall;
}


std::any OpenCMLParser::EntityCallContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEntityCall(this);
  else
    return visitor->visitChildren(this);
}


OpenCMLParser::EntityCallContext* OpenCMLParser::entityCall() {
   return entityCall(0);
}

OpenCMLParser::EntityCallContext* OpenCMLParser::entityCall(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::EntityCallContext *_localctx = _tracker.createInstance<EntityCallContext>(_ctx, parentState);
  OpenCMLParser::EntityCallContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 80;
  enterRecursionRule(_localctx, 80, OpenCMLParser::RuleEntityCall, precedence);

    

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
    setState(450);
    entity();
    _ctx->stop = _input->LT(-1);
    setState(457);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 59, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EntityCallContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEntityCall);
        setState(452);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(453);
        match(OpenCMLParser::T__16);
        setState(454);
        entity(); 
      }
      setState(459);
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

//----------------- EntitySpreadContext ------------------------------------------------------------------

OpenCMLParser::EntitySpreadContext::EntitySpreadContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::EntityContext* OpenCMLParser::EntitySpreadContext::entity() {
  return getRuleContext<OpenCMLParser::EntityContext>(0);
}


size_t OpenCMLParser::EntitySpreadContext::getRuleIndex() const {
  return OpenCMLParser::RuleEntitySpread;
}


std::any OpenCMLParser::EntitySpreadContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEntitySpread(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::EntitySpreadContext* OpenCMLParser::entitySpread() {
  EntitySpreadContext *_localctx = _tracker.createInstance<EntitySpreadContext>(_ctx, getState());
  enterRule(_localctx, 82, OpenCMLParser::RuleEntitySpread);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(460);
    match(OpenCMLParser::T__17);
    setState(461);
    entity();
   
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
  size_t startState = 84;
  enterRecursionRule(_localctx, 84, OpenCMLParser::RuleEntityExpr, precedence);

    

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
    setState(464);
    relaExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(492);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(490);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 60, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(466);

          if (!(precpred(_ctx, 8))) throw FailedPredicateException(this, "precpred(_ctx, 8)");
          setState(467);
          match(OpenCMLParser::T__18);
          setState(468);
          relaExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(469);

          if (!(precpred(_ctx, 7))) throw FailedPredicateException(this, "precpred(_ctx, 7)");
          setState(470);
          match(OpenCMLParser::T__19);
          setState(471);
          relaExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(472);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(473);
          match(OpenCMLParser::T__20);
          setState(474);
          relaExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(475);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(476);
          match(OpenCMLParser::T__21);
          setState(477);
          relaExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(478);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(479);
          match(OpenCMLParser::T__22);
          setState(480);
          relaExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(481);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(482);
          match(OpenCMLParser::T__23);
          setState(483);
          relaExpr(0);
          break;
        }

        case 7: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(484);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(485);
          match(OpenCMLParser::T__24);
          setState(486);
          relaExpr(0);
          break;
        }

        case 8: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(487);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(488);
          match(OpenCMLParser::T__25);
          setState(489);
          relaExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(494);
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
  size_t startState = 86;
  enterRecursionRule(_localctx, 86, OpenCMLParser::RuleRelaExpr, precedence);

    

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
    setState(496);
    addExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(524);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 63, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(522);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 62, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(498);

          if (!(precpred(_ctx, 8))) throw FailedPredicateException(this, "precpred(_ctx, 8)");
          setState(499);
          match(OpenCMLParser::T__13);
          setState(500);
          addExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(501);

          if (!(precpred(_ctx, 7))) throw FailedPredicateException(this, "precpred(_ctx, 7)");
          setState(502);
          match(OpenCMLParser::T__14);
          setState(503);
          addExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(504);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(505);
          match(OpenCMLParser::T__26);
          setState(506);
          addExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(507);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(508);
          match(OpenCMLParser::T__27);
          setState(509);
          addExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(510);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(511);
          match(OpenCMLParser::T__28);
          setState(512);
          addExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(513);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(514);
          match(OpenCMLParser::T__29);
          setState(515);
          addExpr(0);
          break;
        }

        case 7: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(516);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(517);
          match(OpenCMLParser::T__30);
          setState(518);
          addExpr(0);
          break;
        }

        case 8: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(519);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(520);
          match(OpenCMLParser::T__31);
          setState(521);
          addExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(526);
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
  size_t startState = 88;
  enterRecursionRule(_localctx, 88, OpenCMLParser::RuleAddExpr, precedence);

    

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
    setState(528);
    multiExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(544);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 65, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(542);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 64, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(530);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(531);
          match(OpenCMLParser::T__32);
          setState(532);
          multiExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(533);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(534);
          match(OpenCMLParser::T__33);
          setState(535);
          multiExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(536);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(537);
          match(OpenCMLParser::T__34);
          setState(538);
          multiExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(539);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(540);
          match(OpenCMLParser::T__35);
          setState(541);
          multiExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(546);
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
  size_t startState = 90;
  enterRecursionRule(_localctx, 90, OpenCMLParser::RuleMultiExpr, precedence);

    

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
    setState(548);
    unaryExpr();
    _ctx->stop = _input->LT(-1);
    setState(570);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 67, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(568);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 66, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(550);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(551);
          match(OpenCMLParser::T__36);
          setState(552);
          unaryExpr();
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(553);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(554);
          match(OpenCMLParser::T__37);
          setState(555);
          unaryExpr();
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(556);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(557);
          match(OpenCMLParser::T__38);
          setState(558);
          unaryExpr();
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(559);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(560);
          match(OpenCMLParser::T__39);
          setState(561);
          unaryExpr();
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(562);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(563);
          match(OpenCMLParser::AS);
          setState(564);
          typeExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(565);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(566);
          match(OpenCMLParser::IS);
          setState(567);
          typeExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(572);
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
  enterRule(_localctx, 92, OpenCMLParser::RuleUnaryExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(578);
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
        setState(573);
        primExpr();
        break;
      }

      case OpenCMLParser::T__40: {
        enterOuterAlt(_localctx, 2);
        setState(574);
        match(OpenCMLParser::T__40);
        setState(575);
        primExpr();
        break;
      }

      case OpenCMLParser::T__41: {
        enterOuterAlt(_localctx, 3);
        setState(576);
        match(OpenCMLParser::T__41);
        setState(577);
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
  enterRule(_localctx, 94, OpenCMLParser::RulePrimExpr);

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
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(580);
      entityChain();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(581);
      match(OpenCMLParser::T__11);
      setState(582);
      entityExpr(0);
      setState(583);
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
  enterRule(_localctx, 96, OpenCMLParser::RuleLiteral);

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

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx)) {
        case 1: {
          setState(588);
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
        setState(591);
        match(OpenCMLParser::REAL);
        setState(593);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 71, _ctx)) {
        case 1: {
          setState(592);
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

OpenCMLParser::TypeContext* OpenCMLParser::TypeExprContext::type() {
  return getRuleContext<OpenCMLParser::TypeContext>(0);
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
  size_t startState = 98;
  enterRecursionRule(_localctx, 98, OpenCMLParser::RuleTypeExpr, precedence);

    

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
    setState(604);
    type();
    setState(607);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 73, _ctx)) {
    case 1: {
      setState(605);
      match(OpenCMLParser::T__9);
      setState(606);
      match(OpenCMLParser::T__10);
      break;
    }

    default:
      break;
    }
    _ctx->stop = _input->LT(-1);
    setState(617);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 75, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(615);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<TypeExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleTypeExpr);
          setState(609);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(610);
          match(OpenCMLParser::T__35);
          setState(611);
          typeExpr(3);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<TypeExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleTypeExpr);
          setState(612);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(613);
          match(OpenCMLParser::T__34);
          setState(614);
          typeExpr(2);
          break;
        }

        default:
          break;
        } 
      }
      setState(619);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 75, _ctx);
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
  enterRule(_localctx, 100, OpenCMLParser::RuleType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(629);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 76, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(620);
      primType();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(621);
      structType();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(622);
      specialType();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(623);
      identRef();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(624);
      match(OpenCMLParser::T__11);
      setState(625);
      typeExpr(0);
      setState(626);
      match(OpenCMLParser::T__12);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(628);
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
  enterRule(_localctx, 102, OpenCMLParser::RuleLambdaType);
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
    setState(636);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__13) {
      setState(631);
      match(OpenCMLParser::T__13);
      setState(633);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::IDENTIFIER) {
        setState(632);
        pairedParams();
      }
      setState(635);
      match(OpenCMLParser::T__14);
    }
    setState(638);
    match(OpenCMLParser::T__11);
    setState(640);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(639);
      pairedParams();
    }
    setState(642);
    match(OpenCMLParser::T__12);
    setState(643);
    match(OpenCMLParser::T__4);
    setState(644);
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
  enterRule(_localctx, 104, OpenCMLParser::RulePrimType);
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
    setState(646);
    _la = _input->LA(1);
    if (!(((((_la - 60) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 60)) & 1023) != 0))) {
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
  enterRule(_localctx, 106, OpenCMLParser::RuleStructType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(734);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::SET_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(648);
        match(OpenCMLParser::SET_TYPE);
        setState(653);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 80, _ctx)) {
        case 1: {
          setState(649);
          match(OpenCMLParser::T__13);
          setState(650);
          typeExpr(0);
          setState(651);
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
        setState(655);
        match(OpenCMLParser::MAP_TYPE);
        setState(662);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 81, _ctx)) {
        case 1: {
          setState(656);
          match(OpenCMLParser::T__13);
          setState(657);
          typeExpr(0);
          setState(658);
          match(OpenCMLParser::T__8);
          setState(659);
          typeExpr(0);
          setState(660);
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
        setState(664);
        match(OpenCMLParser::LIST_TYPE);
        break;
      }

      case OpenCMLParser::DICT_TYPE: {
        enterOuterAlt(_localctx, 4);
        setState(665);
        match(OpenCMLParser::DICT_TYPE);
        break;
      }

      case OpenCMLParser::T__5: {
        enterOuterAlt(_localctx, 5);
        setState(666);
        match(OpenCMLParser::T__5);
        setState(668);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::IDENTIFIER) {
          setState(667);
          pairedTypes();
        }
        setState(671);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__8) {
          setState(670);
          match(OpenCMLParser::T__8);
        }
        setState(673);
        match(OpenCMLParser::T__6);
        break;
      }

      case OpenCMLParser::ARRAY_TYPE: {
        enterOuterAlt(_localctx, 6);
        setState(674);
        match(OpenCMLParser::ARRAY_TYPE);
        setState(679);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 84, _ctx)) {
        case 1: {
          setState(675);
          match(OpenCMLParser::T__13);
          setState(676);
          typeExpr(0);
          setState(677);
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
        setState(681);
        match(OpenCMLParser::TUPLE_TYPE);
        setState(690);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 87, _ctx)) {
        case 1: {
          setState(682);
          match(OpenCMLParser::T__13);
          setState(684);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if ((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & -1152921504606826432) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
            ((1ULL << (_la - 64)) & 8650751) != 0)) {
            setState(683);
            typeList();
          }
          setState(687);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == OpenCMLParser::T__8) {
            setState(686);
            match(OpenCMLParser::T__8);
          }
          setState(689);
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
        setState(692);
        match(OpenCMLParser::UNION_TYPE);
        setState(701);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 90, _ctx)) {
        case 1: {
          setState(693);
          match(OpenCMLParser::T__13);
          setState(695);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if ((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & -1152921504606826432) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
            ((1ULL << (_la - 64)) & 8650751) != 0)) {
            setState(694);
            typeList();
          }
          setState(698);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == OpenCMLParser::T__8) {
            setState(697);
            match(OpenCMLParser::T__8);
          }
          setState(700);
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
        setState(703);
        match(OpenCMLParser::VECTOR_TYPE);
        setState(712);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 92, _ctx)) {
        case 1: {
          setState(704);
          match(OpenCMLParser::T__13);
          setState(705);
          typeExpr(0);
          setState(708);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == OpenCMLParser::T__8) {
            setState(706);
            match(OpenCMLParser::T__8);
            setState(707);
            match(OpenCMLParser::INTEGER);
          }
          setState(710);
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
        setState(714);
        match(OpenCMLParser::TENSOR_TYPE);
        setState(732);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 95, _ctx)) {
        case 1: {
          setState(715);
          match(OpenCMLParser::T__13);
          setState(716);
          typeExpr(0);
          setState(728);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == OpenCMLParser::T__8) {
            setState(717);
            match(OpenCMLParser::T__8);
            setState(718);
            match(OpenCMLParser::T__9);
            setState(719);
            match(OpenCMLParser::INTEGER);
            setState(724);
            _errHandler->sync(this);
            _la = _input->LA(1);
            while (_la == OpenCMLParser::T__8) {
              setState(720);
              match(OpenCMLParser::T__8);
              setState(721);
              match(OpenCMLParser::INTEGER);
              setState(726);
              _errHandler->sync(this);
              _la = _input->LA(1);
            }
            setState(727);
            match(OpenCMLParser::T__10);
          }
          setState(730);
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
  enterRule(_localctx, 108, OpenCMLParser::RuleSpecialType);
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
    setState(736);
    _la = _input->LA(1);
    if (!(((((_la - 79) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 79)) & 7) != 0))) {
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
  enterRule(_localctx, 110, OpenCMLParser::RuleIdentRef);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(738);
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
    case 37: return entitySempred(antlrcpp::downCast<EntityContext *>(context), predicateIndex);
    case 39: return entityLinkSempred(antlrcpp::downCast<EntityLinkContext *>(context), predicateIndex);
    case 40: return entityCallSempred(antlrcpp::downCast<EntityCallContext *>(context), predicateIndex);
    case 42: return entityExprSempred(antlrcpp::downCast<EntityExprContext *>(context), predicateIndex);
    case 43: return relaExprSempred(antlrcpp::downCast<RelaExprContext *>(context), predicateIndex);
    case 44: return addExprSempred(antlrcpp::downCast<AddExprContext *>(context), predicateIndex);
    case 45: return multiExprSempred(antlrcpp::downCast<MultiExprContext *>(context), predicateIndex);
    case 49: return typeExprSempred(antlrcpp::downCast<TypeExprContext *>(context), predicateIndex);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::entitySempred(EntityContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 0: return isAdjacent();

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::entityLinkSempred(EntityLinkContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 1: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::entityCallSempred(EntityCallContext *_localctx, size_t predicateIndex) {
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
