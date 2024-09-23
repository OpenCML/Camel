
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
      "", "':'", "'='", "'*'", "'=>'", "'@'", "'['", "']'", "','", "'{'", 
      "'}'", "'('", "')'", "'<'", "'>'", "'+='", "'-='", "'*='", "'/='", 
      "'%='", "'^='", "'&='", "'|='", "'\\u003F'", "'||'", "'&&'", "'=='", 
      "'!='", "'<='", "'>='", "'+'", "'-'", "'^'", "'/'", "'%'", "'!'", 
      "'~'", "'->'", "'.'", "'&'", "'|'", "';'", "'as'", "'is'", "'let'", 
      "'var'", "'use'", "'from'", "'func'", "'type'", "'with'", "'wait'", 
      "'return'", "'inner'", "'outer'", "'atomic'", "'static'", "'null'", 
      "'true'", "'false'", "'Int'", "'Int32'", "'Int64'", "'Real'", "'Float'", 
      "'Double'", "'Number'", "'String'", "'Bool'", "'Char'", "'Set'", "'Map'", 
      "'List'", "'Dict'", "'Array'", "'Tuple'", "'Union'", "'Vector'", "'Tensor'", 
      "'Any'", "'Void'", "'Functor'"
    },
    std::vector<std::string>{
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "SEP", "AS", "IS", "LET", "VAR", "USE", 
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
  	4,1,92,736,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
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
  	3,3,3,158,8,3,1,3,1,3,1,3,1,4,1,4,1,4,3,4,166,8,4,1,4,1,4,1,4,1,5,1,5,
  	1,5,1,5,1,5,1,6,3,6,177,8,6,1,6,1,6,1,7,1,7,1,7,1,8,1,8,1,8,1,9,3,9,188,
  	8,9,1,9,3,9,191,8,9,1,9,3,9,194,8,9,1,9,1,9,1,9,1,9,1,9,3,9,201,8,9,1,
  	9,1,9,1,10,1,10,3,10,207,8,10,1,11,3,11,210,8,11,1,11,1,11,1,11,3,11,
  	215,8,11,1,11,1,11,1,11,3,11,220,8,11,1,12,1,12,1,12,3,12,225,8,12,1,
  	13,1,13,1,13,1,14,4,14,231,8,14,11,14,12,14,232,1,15,4,15,236,8,15,11,
  	15,12,15,237,1,16,1,16,1,16,1,16,1,17,1,17,1,17,1,17,1,18,3,18,249,8,
  	18,1,18,1,18,3,18,253,8,18,1,18,1,18,1,18,1,18,3,18,259,8,18,1,19,1,19,
  	1,19,1,19,1,19,1,19,1,20,1,20,1,20,1,20,1,20,1,20,1,21,1,21,1,21,5,21,
  	276,8,21,10,21,12,21,279,9,21,1,22,1,22,1,22,5,22,284,8,22,10,22,12,22,
  	287,9,22,1,23,1,23,1,23,5,23,292,8,23,10,23,12,23,295,9,23,1,24,1,24,
  	1,24,5,24,300,8,24,10,24,12,24,303,9,24,1,25,1,25,1,25,5,25,308,8,25,
  	10,25,12,25,311,9,25,1,26,1,26,1,26,5,26,316,8,26,10,26,12,26,319,9,26,
  	1,27,1,27,1,27,5,27,324,8,27,10,27,12,27,327,9,27,1,28,1,28,1,28,3,28,
  	332,8,28,1,28,3,28,335,8,28,1,29,1,29,3,29,339,8,29,1,29,3,29,342,8,29,
  	1,29,1,29,1,30,1,30,3,30,348,8,30,1,30,3,30,351,8,30,1,30,1,30,1,31,1,
  	31,3,31,357,8,31,1,31,1,31,1,32,1,32,3,32,363,8,32,1,32,3,32,366,8,32,
  	1,32,1,32,1,33,1,33,3,33,372,8,33,1,33,3,33,375,8,33,1,33,1,33,1,34,1,
  	34,3,34,381,8,34,1,34,3,34,384,8,34,1,34,1,34,1,35,1,35,3,35,390,8,35,
  	1,35,3,35,393,8,35,1,35,1,35,1,36,1,36,1,36,1,36,1,37,1,37,3,37,403,8,
  	37,1,37,3,37,406,8,37,1,37,1,37,1,38,1,38,3,38,412,8,38,1,38,3,38,415,
  	8,38,1,38,1,38,1,39,1,39,3,39,421,8,39,1,39,3,39,424,8,39,1,39,1,39,1,
  	40,1,40,3,40,430,8,40,1,40,3,40,433,8,40,1,40,1,40,1,41,1,41,3,41,439,
  	8,41,1,41,3,41,442,8,41,1,41,1,41,1,42,1,42,1,42,3,42,449,8,42,1,43,1,
  	43,1,43,1,43,1,43,1,43,3,43,457,8,43,1,44,1,44,1,44,5,44,462,8,44,10,
  	44,12,44,465,9,44,1,45,1,45,1,45,5,45,470,8,45,10,45,12,45,473,9,45,1,
  	46,1,46,1,46,5,46,478,8,46,10,46,12,46,481,9,46,1,47,1,47,1,47,5,47,486,
  	8,47,10,47,12,47,489,9,47,1,48,1,48,1,48,5,48,494,8,48,10,48,12,48,497,
  	9,48,1,49,1,49,1,49,5,49,502,8,49,10,49,12,49,505,9,49,1,50,1,50,1,50,
  	3,50,510,8,50,1,51,1,51,1,51,5,51,515,8,51,10,51,12,51,518,9,51,1,52,
  	1,52,1,52,5,52,523,8,52,10,52,12,52,526,9,52,1,53,1,53,1,53,1,53,1,53,
  	5,53,533,8,53,10,53,12,53,536,9,53,1,54,1,54,1,54,1,54,1,54,1,54,1,54,
  	1,54,1,54,1,54,1,54,1,54,1,54,1,54,1,54,1,54,5,54,554,8,54,10,54,12,54,
  	557,9,54,1,54,3,54,560,8,54,3,54,562,8,54,1,54,1,54,3,54,566,8,54,1,54,
  	1,54,1,54,1,54,3,54,572,8,54,1,54,3,54,575,8,54,1,55,1,55,3,55,579,8,
  	55,1,55,1,55,3,55,583,8,55,1,55,1,55,1,55,1,55,1,55,1,55,3,55,591,8,55,
  	1,56,1,56,1,56,5,56,596,8,56,10,56,12,56,599,9,56,1,57,1,57,1,57,3,57,
  	604,8,57,1,57,5,57,607,8,57,10,57,12,57,610,9,57,1,58,1,58,1,58,1,58,
  	1,58,1,58,1,58,1,58,1,58,3,58,621,8,58,1,59,1,59,3,59,625,8,59,1,59,3,
  	59,628,8,59,1,59,1,59,3,59,632,8,59,1,59,1,59,1,59,1,59,1,60,1,60,1,61,
  	1,61,1,61,1,61,1,61,3,61,645,8,61,1,61,1,61,1,61,1,61,1,61,1,61,1,61,
  	3,61,654,8,61,1,61,1,61,1,61,1,61,1,61,1,61,1,61,3,61,663,8,61,1,61,1,
  	61,3,61,667,8,61,1,61,1,61,1,61,3,61,672,8,61,1,61,3,61,675,8,61,1,61,
  	3,61,678,8,61,1,61,1,61,1,61,3,61,683,8,61,1,61,3,61,686,8,61,1,61,3,
  	61,689,8,61,1,61,1,61,1,61,1,61,1,61,3,61,696,8,61,1,61,1,61,1,61,1,61,
  	1,61,1,61,1,61,1,61,5,61,706,8,61,10,61,12,61,709,9,61,1,61,3,61,712,
  	8,61,1,61,1,61,3,61,716,8,61,1,61,1,61,3,61,720,8,61,1,61,3,61,723,8,
  	61,1,61,1,61,1,61,1,61,1,61,3,61,730,8,61,1,62,1,62,1,63,1,63,1,63,0,
  	0,64,0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,
  	46,48,50,52,54,56,58,60,62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,
  	92,94,96,98,100,102,104,106,108,110,112,114,116,118,120,122,124,126,0,
  	11,1,0,44,45,1,0,53,56,2,0,2,2,15,22,1,0,26,27,2,0,13,14,28,29,1,0,30,
  	31,3,0,3,3,32,34,42,43,1,0,35,36,2,0,32,32,39,40,1,0,60,69,1,0,79,81,
  	804,0,129,1,0,0,0,2,133,1,0,0,0,4,151,1,0,0,0,6,153,1,0,0,0,8,162,1,0,
  	0,0,10,170,1,0,0,0,12,176,1,0,0,0,14,180,1,0,0,0,16,183,1,0,0,0,18,187,
  	1,0,0,0,20,204,1,0,0,0,22,209,1,0,0,0,24,224,1,0,0,0,26,226,1,0,0,0,28,
  	230,1,0,0,0,30,235,1,0,0,0,32,239,1,0,0,0,34,243,1,0,0,0,36,248,1,0,0,
  	0,38,260,1,0,0,0,40,266,1,0,0,0,42,272,1,0,0,0,44,280,1,0,0,0,46,288,
  	1,0,0,0,48,296,1,0,0,0,50,304,1,0,0,0,52,312,1,0,0,0,54,320,1,0,0,0,56,
  	334,1,0,0,0,58,336,1,0,0,0,60,345,1,0,0,0,62,354,1,0,0,0,64,360,1,0,0,
  	0,66,369,1,0,0,0,68,378,1,0,0,0,70,387,1,0,0,0,72,396,1,0,0,0,74,400,
  	1,0,0,0,76,409,1,0,0,0,78,418,1,0,0,0,80,427,1,0,0,0,82,436,1,0,0,0,84,
  	445,1,0,0,0,86,450,1,0,0,0,88,458,1,0,0,0,90,466,1,0,0,0,92,474,1,0,0,
  	0,94,482,1,0,0,0,96,490,1,0,0,0,98,498,1,0,0,0,100,509,1,0,0,0,102,511,
  	1,0,0,0,104,519,1,0,0,0,106,527,1,0,0,0,108,574,1,0,0,0,110,590,1,0,0,
  	0,112,592,1,0,0,0,114,600,1,0,0,0,116,620,1,0,0,0,118,627,1,0,0,0,120,
  	637,1,0,0,0,122,729,1,0,0,0,124,731,1,0,0,0,126,733,1,0,0,0,128,130,3,
  	2,1,0,129,128,1,0,0,0,129,130,1,0,0,0,130,131,1,0,0,0,131,132,5,0,0,1,
  	132,1,1,0,0,0,133,138,3,4,2,0,134,135,5,41,0,0,135,137,3,4,2,0,136,134,
  	1,0,0,0,137,140,1,0,0,0,138,136,1,0,0,0,138,139,1,0,0,0,139,142,1,0,0,
  	0,140,138,1,0,0,0,141,143,5,41,0,0,142,141,1,0,0,0,142,143,1,0,0,0,143,
  	3,1,0,0,0,144,152,3,6,3,0,145,152,3,8,4,0,146,152,3,10,5,0,147,152,3,
  	12,6,0,148,152,3,14,7,0,149,152,3,18,9,0,150,152,3,20,10,0,151,144,1,
  	0,0,0,151,145,1,0,0,0,151,146,1,0,0,0,151,147,1,0,0,0,151,148,1,0,0,0,
  	151,149,1,0,0,0,151,150,1,0,0,0,152,5,1,0,0,0,153,154,7,0,0,0,154,157,
  	3,24,12,0,155,156,5,1,0,0,156,158,3,112,56,0,157,155,1,0,0,0,157,158,
  	1,0,0,0,158,159,1,0,0,0,159,160,5,2,0,0,160,161,3,84,42,0,161,7,1,0,0,
  	0,162,165,5,46,0,0,163,166,3,24,12,0,164,166,5,3,0,0,165,163,1,0,0,0,
  	165,164,1,0,0,0,166,167,1,0,0,0,167,168,5,47,0,0,168,169,5,89,0,0,169,
  	9,1,0,0,0,170,171,5,49,0,0,171,172,3,126,63,0,172,173,5,2,0,0,173,174,
  	3,112,56,0,174,11,1,0,0,0,175,177,3,28,14,0,176,175,1,0,0,0,176,177,1,
  	0,0,0,177,178,1,0,0,0,178,179,3,84,42,0,179,13,1,0,0,0,180,181,5,51,0,
  	0,181,182,3,84,42,0,182,15,1,0,0,0,183,184,5,50,0,0,184,185,3,80,40,0,
  	185,17,1,0,0,0,186,188,3,28,14,0,187,186,1,0,0,0,187,188,1,0,0,0,188,
  	190,1,0,0,0,189,191,3,16,8,0,190,189,1,0,0,0,190,191,1,0,0,0,191,193,
  	1,0,0,0,192,194,3,30,15,0,193,192,1,0,0,0,193,194,1,0,0,0,194,195,1,0,
  	0,0,195,196,5,48,0,0,196,197,3,126,63,0,197,200,3,74,37,0,198,199,5,1,
  	0,0,199,201,3,112,56,0,200,198,1,0,0,0,200,201,1,0,0,0,201,202,1,0,0,
  	0,202,203,3,62,31,0,203,19,1,0,0,0,204,206,5,52,0,0,205,207,3,84,42,0,
  	206,205,1,0,0,0,206,207,1,0,0,0,207,21,1,0,0,0,208,210,3,30,15,0,209,
  	208,1,0,0,0,209,210,1,0,0,0,210,211,1,0,0,0,211,214,3,74,37,0,212,213,
  	5,1,0,0,213,215,3,112,56,0,214,212,1,0,0,0,214,215,1,0,0,0,215,216,1,
  	0,0,0,216,219,5,4,0,0,217,220,3,62,31,0,218,220,3,84,42,0,219,217,1,0,
  	0,0,219,218,1,0,0,0,220,23,1,0,0,0,221,225,3,126,63,0,222,225,3,60,30,
  	0,223,225,3,68,34,0,224,221,1,0,0,0,224,222,1,0,0,0,224,223,1,0,0,0,225,
  	25,1,0,0,0,226,227,5,5,0,0,227,228,3,108,54,0,228,27,1,0,0,0,229,231,
  	3,26,13,0,230,229,1,0,0,0,231,232,1,0,0,0,232,230,1,0,0,0,232,233,1,0,
  	0,0,233,29,1,0,0,0,234,236,7,1,0,0,235,234,1,0,0,0,236,237,1,0,0,0,237,
  	235,1,0,0,0,237,238,1,0,0,0,238,31,1,0,0,0,239,240,3,126,63,0,240,241,
  	5,1,0,0,241,242,3,112,56,0,242,33,1,0,0,0,243,244,3,126,63,0,244,245,
  	5,1,0,0,245,246,3,84,42,0,246,35,1,0,0,0,247,249,5,45,0,0,248,247,1,0,
  	0,0,248,249,1,0,0,0,249,250,1,0,0,0,250,252,3,126,63,0,251,253,3,26,13,
  	0,252,251,1,0,0,0,252,253,1,0,0,0,253,254,1,0,0,0,254,255,5,1,0,0,255,
  	258,3,112,56,0,256,257,5,2,0,0,257,259,3,84,42,0,258,256,1,0,0,0,258,
  	259,1,0,0,0,259,37,1,0,0,0,260,261,5,6,0,0,261,262,3,84,42,0,262,263,
  	5,7,0,0,263,264,5,1,0,0,264,265,3,112,56,0,265,39,1,0,0,0,266,267,5,6,
  	0,0,267,268,3,84,42,0,268,269,5,7,0,0,269,270,5,1,0,0,270,271,3,84,42,
  	0,271,41,1,0,0,0,272,277,3,112,56,0,273,274,5,8,0,0,274,276,3,112,56,
  	0,275,273,1,0,0,0,276,279,1,0,0,0,277,275,1,0,0,0,277,278,1,0,0,0,278,
  	43,1,0,0,0,279,277,1,0,0,0,280,285,3,126,63,0,281,282,5,8,0,0,282,284,
  	3,126,63,0,283,281,1,0,0,0,284,287,1,0,0,0,285,283,1,0,0,0,285,286,1,
  	0,0,0,286,45,1,0,0,0,287,285,1,0,0,0,288,293,3,84,42,0,289,290,5,8,0,
  	0,290,292,3,84,42,0,291,289,1,0,0,0,292,295,1,0,0,0,293,291,1,0,0,0,293,
  	294,1,0,0,0,294,47,1,0,0,0,295,293,1,0,0,0,296,301,3,32,16,0,297,298,
  	5,8,0,0,298,300,3,32,16,0,299,297,1,0,0,0,300,303,1,0,0,0,301,299,1,0,
  	0,0,301,302,1,0,0,0,302,49,1,0,0,0,303,301,1,0,0,0,304,309,3,34,17,0,
  	305,306,5,8,0,0,306,308,3,34,17,0,307,305,1,0,0,0,308,311,1,0,0,0,309,
  	307,1,0,0,0,309,310,1,0,0,0,310,51,1,0,0,0,311,309,1,0,0,0,312,317,3,
  	36,18,0,313,314,5,8,0,0,314,316,3,36,18,0,315,313,1,0,0,0,316,319,1,0,
  	0,0,317,315,1,0,0,0,317,318,1,0,0,0,318,53,1,0,0,0,319,317,1,0,0,0,320,
  	325,3,40,20,0,321,322,5,8,0,0,322,324,3,40,20,0,323,321,1,0,0,0,324,327,
  	1,0,0,0,325,323,1,0,0,0,325,326,1,0,0,0,326,55,1,0,0,0,327,325,1,0,0,
  	0,328,331,3,46,23,0,329,330,5,8,0,0,330,332,3,50,25,0,331,329,1,0,0,0,
  	331,332,1,0,0,0,332,335,1,0,0,0,333,335,3,50,25,0,334,328,1,0,0,0,334,
  	333,1,0,0,0,335,57,1,0,0,0,336,338,5,9,0,0,337,339,3,50,25,0,338,337,
  	1,0,0,0,338,339,1,0,0,0,339,341,1,0,0,0,340,342,5,8,0,0,341,340,1,0,0,
  	0,341,342,1,0,0,0,342,343,1,0,0,0,343,344,5,10,0,0,344,59,1,0,0,0,345,
  	347,5,9,0,0,346,348,3,44,22,0,347,346,1,0,0,0,347,348,1,0,0,0,348,350,
  	1,0,0,0,349,351,5,8,0,0,350,349,1,0,0,0,350,351,1,0,0,0,351,352,1,0,0,
  	0,352,353,5,10,0,0,353,61,1,0,0,0,354,356,5,9,0,0,355,357,3,2,1,0,356,
  	355,1,0,0,0,356,357,1,0,0,0,357,358,1,0,0,0,358,359,5,10,0,0,359,63,1,
  	0,0,0,360,362,5,9,0,0,361,363,3,46,23,0,362,361,1,0,0,0,362,363,1,0,0,
  	0,363,365,1,0,0,0,364,366,5,8,0,0,365,364,1,0,0,0,365,366,1,0,0,0,366,
  	367,1,0,0,0,367,368,5,10,0,0,368,65,1,0,0,0,369,371,5,9,0,0,370,372,3,
  	54,27,0,371,370,1,0,0,0,371,372,1,0,0,0,372,374,1,0,0,0,373,375,5,8,0,
  	0,374,373,1,0,0,0,374,375,1,0,0,0,375,376,1,0,0,0,376,377,5,10,0,0,377,
  	67,1,0,0,0,378,380,5,6,0,0,379,381,3,44,22,0,380,379,1,0,0,0,380,381,
  	1,0,0,0,381,383,1,0,0,0,382,384,5,8,0,0,383,382,1,0,0,0,383,384,1,0,0,
  	0,384,385,1,0,0,0,385,386,5,7,0,0,386,69,1,0,0,0,387,389,5,6,0,0,388,
  	390,3,46,23,0,389,388,1,0,0,0,389,390,1,0,0,0,390,392,1,0,0,0,391,393,
  	5,8,0,0,392,391,1,0,0,0,392,393,1,0,0,0,393,394,1,0,0,0,394,395,5,7,0,
  	0,395,71,1,0,0,0,396,397,5,6,0,0,397,398,3,84,42,0,398,399,5,7,0,0,399,
  	73,1,0,0,0,400,402,5,11,0,0,401,403,3,52,26,0,402,401,1,0,0,0,402,403,
  	1,0,0,0,403,405,1,0,0,0,404,406,5,8,0,0,405,404,1,0,0,0,405,406,1,0,0,
  	0,406,407,1,0,0,0,407,408,5,12,0,0,408,75,1,0,0,0,409,411,5,11,0,0,410,
  	412,3,56,28,0,411,410,1,0,0,0,411,412,1,0,0,0,412,414,1,0,0,0,413,415,
  	5,8,0,0,414,413,1,0,0,0,414,415,1,0,0,0,415,416,1,0,0,0,416,417,5,12,
  	0,0,417,77,1,0,0,0,418,420,5,11,0,0,419,421,3,46,23,0,420,419,1,0,0,0,
  	420,421,1,0,0,0,421,423,1,0,0,0,422,424,5,8,0,0,423,422,1,0,0,0,423,424,
  	1,0,0,0,424,425,1,0,0,0,425,426,5,12,0,0,426,79,1,0,0,0,427,429,5,13,
  	0,0,428,430,3,52,26,0,429,428,1,0,0,0,429,430,1,0,0,0,430,432,1,0,0,0,
  	431,433,5,8,0,0,432,431,1,0,0,0,432,433,1,0,0,0,433,434,1,0,0,0,434,435,
  	5,14,0,0,435,81,1,0,0,0,436,438,5,13,0,0,437,439,3,56,28,0,438,437,1,
  	0,0,0,438,439,1,0,0,0,439,441,1,0,0,0,440,442,5,8,0,0,441,440,1,0,0,0,
  	441,442,1,0,0,0,442,443,1,0,0,0,443,444,5,14,0,0,444,83,1,0,0,0,445,448,
  	3,86,43,0,446,447,7,2,0,0,447,449,3,86,43,0,448,446,1,0,0,0,448,449,1,
  	0,0,0,449,85,1,0,0,0,450,456,3,94,47,0,451,452,5,23,0,0,452,453,3,86,
  	43,0,453,454,5,1,0,0,454,455,3,86,43,0,455,457,1,0,0,0,456,451,1,0,0,
  	0,456,457,1,0,0,0,457,87,1,0,0,0,458,463,3,90,45,0,459,460,5,24,0,0,460,
  	462,3,90,45,0,461,459,1,0,0,0,462,465,1,0,0,0,463,461,1,0,0,0,463,464,
  	1,0,0,0,464,89,1,0,0,0,465,463,1,0,0,0,466,471,3,92,46,0,467,468,5,25,
  	0,0,468,470,3,92,46,0,469,467,1,0,0,0,470,473,1,0,0,0,471,469,1,0,0,0,
  	471,472,1,0,0,0,472,91,1,0,0,0,473,471,1,0,0,0,474,479,3,94,47,0,475,
  	476,7,3,0,0,476,478,3,94,47,0,477,475,1,0,0,0,478,481,1,0,0,0,479,477,
  	1,0,0,0,479,480,1,0,0,0,480,93,1,0,0,0,481,479,1,0,0,0,482,487,3,96,48,
  	0,483,484,7,4,0,0,484,486,3,96,48,0,485,483,1,0,0,0,486,489,1,0,0,0,487,
  	485,1,0,0,0,487,488,1,0,0,0,488,95,1,0,0,0,489,487,1,0,0,0,490,495,3,
  	98,49,0,491,492,7,5,0,0,492,494,3,98,49,0,493,491,1,0,0,0,494,497,1,0,
  	0,0,495,493,1,0,0,0,495,496,1,0,0,0,496,97,1,0,0,0,497,495,1,0,0,0,498,
  	503,3,100,50,0,499,500,7,6,0,0,500,502,3,100,50,0,501,499,1,0,0,0,502,
  	505,1,0,0,0,503,501,1,0,0,0,503,504,1,0,0,0,504,99,1,0,0,0,505,503,1,
  	0,0,0,506,510,3,102,51,0,507,508,7,7,0,0,508,510,3,102,51,0,509,506,1,
  	0,0,0,509,507,1,0,0,0,510,101,1,0,0,0,511,516,3,104,52,0,512,513,5,37,
  	0,0,513,515,3,104,52,0,514,512,1,0,0,0,515,518,1,0,0,0,516,514,1,0,0,
  	0,516,517,1,0,0,0,517,103,1,0,0,0,518,516,1,0,0,0,519,524,3,106,53,0,
  	520,521,5,38,0,0,521,523,3,106,53,0,522,520,1,0,0,0,523,526,1,0,0,0,524,
  	522,1,0,0,0,524,525,1,0,0,0,525,105,1,0,0,0,526,524,1,0,0,0,527,534,3,
  	108,54,0,528,533,3,72,36,0,529,533,3,76,38,0,530,533,3,82,41,0,531,533,
  	3,26,13,0,532,528,1,0,0,0,532,529,1,0,0,0,532,530,1,0,0,0,532,531,1,0,
  	0,0,533,536,1,0,0,0,534,532,1,0,0,0,534,535,1,0,0,0,535,107,1,0,0,0,536,
  	534,1,0,0,0,537,575,3,126,63,0,538,575,3,110,55,0,539,575,3,58,29,0,540,
  	541,5,11,0,0,541,542,3,84,42,0,542,543,5,12,0,0,543,575,1,0,0,0,544,545,
  	5,13,0,0,545,561,3,112,56,0,546,559,5,8,0,0,547,560,3,112,56,0,548,560,
  	5,91,0,0,549,550,5,6,0,0,550,555,5,91,0,0,551,552,5,8,0,0,552,554,5,91,
  	0,0,553,551,1,0,0,0,554,557,1,0,0,0,555,553,1,0,0,0,555,556,1,0,0,0,556,
  	558,1,0,0,0,557,555,1,0,0,0,558,560,5,7,0,0,559,547,1,0,0,0,559,548,1,
  	0,0,0,559,549,1,0,0,0,560,562,1,0,0,0,561,546,1,0,0,0,561,562,1,0,0,0,
  	562,563,1,0,0,0,563,564,5,14,0,0,564,566,1,0,0,0,565,544,1,0,0,0,565,
  	566,1,0,0,0,566,571,1,0,0,0,567,572,3,70,35,0,568,572,3,78,39,0,569,572,
  	3,64,32,0,570,572,3,66,33,0,571,567,1,0,0,0,571,568,1,0,0,0,571,569,1,
  	0,0,0,571,570,1,0,0,0,572,575,1,0,0,0,573,575,3,22,11,0,574,537,1,0,0,
  	0,574,538,1,0,0,0,574,539,1,0,0,0,574,540,1,0,0,0,574,565,1,0,0,0,574,
  	573,1,0,0,0,575,109,1,0,0,0,576,578,5,91,0,0,577,579,5,88,0,0,578,577,
  	1,0,0,0,578,579,1,0,0,0,579,591,1,0,0,0,580,582,5,92,0,0,581,583,5,88,
  	0,0,582,581,1,0,0,0,582,583,1,0,0,0,583,591,1,0,0,0,584,591,5,89,0,0,
  	585,591,5,86,0,0,586,591,5,90,0,0,587,591,5,58,0,0,588,591,5,59,0,0,589,
  	591,5,57,0,0,590,576,1,0,0,0,590,580,1,0,0,0,590,584,1,0,0,0,590,585,
  	1,0,0,0,590,586,1,0,0,0,590,587,1,0,0,0,590,588,1,0,0,0,590,589,1,0,0,
  	0,591,111,1,0,0,0,592,597,3,114,57,0,593,594,7,8,0,0,594,596,3,114,57,
  	0,595,593,1,0,0,0,596,599,1,0,0,0,597,595,1,0,0,0,597,598,1,0,0,0,598,
  	113,1,0,0,0,599,597,1,0,0,0,600,608,3,116,58,0,601,603,5,6,0,0,602,604,
  	5,91,0,0,603,602,1,0,0,0,603,604,1,0,0,0,604,605,1,0,0,0,605,607,5,7,
  	0,0,606,601,1,0,0,0,607,610,1,0,0,0,608,606,1,0,0,0,608,609,1,0,0,0,609,
  	115,1,0,0,0,610,608,1,0,0,0,611,621,3,120,60,0,612,621,3,122,61,0,613,
  	621,3,124,62,0,614,621,3,126,63,0,615,616,5,11,0,0,616,617,3,112,56,0,
  	617,618,5,12,0,0,618,621,1,0,0,0,619,621,3,118,59,0,620,611,1,0,0,0,620,
  	612,1,0,0,0,620,613,1,0,0,0,620,614,1,0,0,0,620,615,1,0,0,0,620,619,1,
  	0,0,0,621,117,1,0,0,0,622,624,5,13,0,0,623,625,3,52,26,0,624,623,1,0,
  	0,0,624,625,1,0,0,0,625,626,1,0,0,0,626,628,5,14,0,0,627,622,1,0,0,0,
  	627,628,1,0,0,0,628,629,1,0,0,0,629,631,5,11,0,0,630,632,3,52,26,0,631,
  	630,1,0,0,0,631,632,1,0,0,0,632,633,1,0,0,0,633,634,5,12,0,0,634,635,
  	5,4,0,0,635,636,3,112,56,0,636,119,1,0,0,0,637,638,7,9,0,0,638,121,1,
  	0,0,0,639,644,5,70,0,0,640,641,5,13,0,0,641,642,3,112,56,0,642,643,5,
  	14,0,0,643,645,1,0,0,0,644,640,1,0,0,0,644,645,1,0,0,0,645,730,1,0,0,
  	0,646,653,5,71,0,0,647,648,5,13,0,0,648,649,3,112,56,0,649,650,5,8,0,
  	0,650,651,3,112,56,0,651,652,5,14,0,0,652,654,1,0,0,0,653,647,1,0,0,0,
  	653,654,1,0,0,0,654,730,1,0,0,0,655,730,5,72,0,0,656,730,5,73,0,0,657,
  	666,5,74,0,0,658,659,5,13,0,0,659,662,3,112,56,0,660,661,5,8,0,0,661,
  	663,5,91,0,0,662,660,1,0,0,0,662,663,1,0,0,0,663,664,1,0,0,0,664,665,
  	5,14,0,0,665,667,1,0,0,0,666,658,1,0,0,0,666,667,1,0,0,0,667,730,1,0,
  	0,0,668,677,5,75,0,0,669,671,5,13,0,0,670,672,3,42,21,0,671,670,1,0,0,
  	0,671,672,1,0,0,0,672,674,1,0,0,0,673,675,5,8,0,0,674,673,1,0,0,0,674,
  	675,1,0,0,0,675,676,1,0,0,0,676,678,5,14,0,0,677,669,1,0,0,0,677,678,
  	1,0,0,0,678,730,1,0,0,0,679,688,5,76,0,0,680,682,5,13,0,0,681,683,3,42,
  	21,0,682,681,1,0,0,0,682,683,1,0,0,0,683,685,1,0,0,0,684,686,5,8,0,0,
  	685,684,1,0,0,0,685,686,1,0,0,0,686,687,1,0,0,0,687,689,5,14,0,0,688,
  	680,1,0,0,0,688,689,1,0,0,0,689,730,1,0,0,0,690,695,5,77,0,0,691,692,
  	5,13,0,0,692,693,3,112,56,0,693,694,5,14,0,0,694,696,1,0,0,0,695,691,
  	1,0,0,0,695,696,1,0,0,0,696,730,1,0,0,0,697,715,5,78,0,0,698,699,5,13,
  	0,0,699,711,3,112,56,0,700,701,5,8,0,0,701,702,5,6,0,0,702,707,5,91,0,
  	0,703,704,5,8,0,0,704,706,5,91,0,0,705,703,1,0,0,0,706,709,1,0,0,0,707,
  	705,1,0,0,0,707,708,1,0,0,0,708,710,1,0,0,0,709,707,1,0,0,0,710,712,5,
  	7,0,0,711,700,1,0,0,0,711,712,1,0,0,0,712,713,1,0,0,0,713,714,5,14,0,
  	0,714,716,1,0,0,0,715,698,1,0,0,0,715,716,1,0,0,0,716,730,1,0,0,0,717,
  	719,5,9,0,0,718,720,3,48,24,0,719,718,1,0,0,0,719,720,1,0,0,0,720,722,
  	1,0,0,0,721,723,5,8,0,0,722,721,1,0,0,0,722,723,1,0,0,0,723,724,1,0,0,
  	0,724,730,5,10,0,0,725,726,5,9,0,0,726,727,3,38,19,0,727,728,5,10,0,0,
  	728,730,1,0,0,0,729,639,1,0,0,0,729,646,1,0,0,0,729,655,1,0,0,0,729,656,
  	1,0,0,0,729,657,1,0,0,0,729,668,1,0,0,0,729,679,1,0,0,0,729,690,1,0,0,
  	0,729,697,1,0,0,0,729,717,1,0,0,0,729,725,1,0,0,0,730,123,1,0,0,0,731,
  	732,7,10,0,0,732,125,1,0,0,0,733,734,5,87,0,0,734,127,1,0,0,0,99,129,
  	138,142,151,157,165,176,187,190,193,200,206,209,214,219,224,232,237,248,
  	252,258,277,285,293,301,309,317,325,331,334,338,341,347,350,356,362,365,
  	371,374,380,383,389,392,402,405,411,414,420,423,429,432,438,441,448,456,
  	463,471,479,487,495,503,509,516,524,532,534,555,559,561,565,571,574,578,
  	582,590,597,603,608,620,624,627,631,644,653,662,666,671,674,677,682,685,
  	688,695,707,711,715,719,722,729
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
      ((1ULL << _la) & 1152763278011673184) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
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

tree::TerminalNode* OpenCMLParser::UseStmtContext::FROM() {
  return getToken(OpenCMLParser::FROM, 0);
}

tree::TerminalNode* OpenCMLParser::UseStmtContext::STRING() {
  return getToken(OpenCMLParser::STRING, 0);
}

OpenCMLParser::CarrierContext* OpenCMLParser::UseStmtContext::carrier() {
  return getRuleContext<OpenCMLParser::CarrierContext>(0);
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
    setState(162);
    match(OpenCMLParser::USE);
    setState(165);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__5:
      case OpenCMLParser::T__8:
      case OpenCMLParser::IDENTIFIER: {
        setState(163);
        carrier();
        break;
      }

      case OpenCMLParser::T__2: {
        setState(164);
        match(OpenCMLParser::T__2);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(167);
    match(OpenCMLParser::FROM);
    setState(168);
    match(OpenCMLParser::STRING);
   
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
    setState(170);
    match(OpenCMLParser::TYPE);
    setState(171);
    identRef();
    setState(172);
    match(OpenCMLParser::T__1);
    setState(173);
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
    setState(176);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__4) {
      setState(175);
      annotations();
    }
    setState(178);
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
    setState(180);
    match(OpenCMLParser::WAIT);
    setState(181);
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
    setState(183);
    match(OpenCMLParser::WITH);
    setState(184);
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
    setState(187);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__4) {
      setState(186);
      annotations();
    }
    setState(190);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WITH) {
      setState(189);
      withDef();
    }
    setState(193);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 135107988821114880) != 0)) {
      setState(192);
      modifiers();
    }
    setState(195);
    match(OpenCMLParser::FUNC);
    setState(196);
    identRef();
    setState(197);
    parentParams();
    setState(200);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(198);
      match(OpenCMLParser::T__0);
      setState(199);
      typeExpr();
    }
    setState(202);
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
    setState(204);
    match(OpenCMLParser::RETURN);
    setState(206);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1143914408431331904) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
      setState(205);
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
    setState(209);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 135107988821114880) != 0)) {
      setState(208);
      modifiers();
    }
    setState(211);
    parentParams();
    setState(214);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(212);
      match(OpenCMLParser::T__0);
      setState(213);
      typeExpr();
    }
    setState(216);
    match(OpenCMLParser::T__3);
    setState(219);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 14, _ctx)) {
    case 1: {
      setState(217);
      bracedStmts();
      break;
    }

    case 2: {
      setState(218);
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
    setState(224);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(221);
        identRef();
        break;
      }

      case OpenCMLParser::T__8: {
        enterOuterAlt(_localctx, 2);
        setState(222);
        bracedIdents();
        break;
      }

      case OpenCMLParser::T__5: {
        enterOuterAlt(_localctx, 3);
        setState(223);
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
    setState(226);
    match(OpenCMLParser::T__4);
    setState(227);
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
    setState(230); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(229);
      annotation();
      setState(232); 
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
    setState(235); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(234);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 135107988821114880) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(237); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 135107988821114880) != 0));
   
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
    setState(239);
    identRef();
    setState(240);
    match(OpenCMLParser::T__0);
    setState(241);
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
    setState(243);
    identRef();
    setState(244);
    match(OpenCMLParser::T__0);
    setState(245);
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
    setState(248);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR) {
      setState(247);
      match(OpenCMLParser::VAR);
    }
    setState(250);
    identRef();
    setState(252);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__4) {
      setState(251);
      annotation();
    }
    setState(254);
    match(OpenCMLParser::T__0);
    setState(255);
    typeExpr();
    setState(258);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(256);
      match(OpenCMLParser::T__1);
      setState(257);
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
    setState(260);
    match(OpenCMLParser::T__5);
    setState(261);
    entityExpr();
    setState(262);
    match(OpenCMLParser::T__6);
    setState(263);
    match(OpenCMLParser::T__0);
    setState(264);
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
    setState(266);
    match(OpenCMLParser::T__5);
    setState(267);
    entityExpr();
    setState(268);
    match(OpenCMLParser::T__6);
    setState(269);
    match(OpenCMLParser::T__0);
    setState(270);
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
    setState(272);
    typeExpr();
    setState(277);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 21, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(273);
        match(OpenCMLParser::T__7);
        setState(274);
        typeExpr(); 
      }
      setState(279);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 21, _ctx);
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
    setState(280);
    identRef();
    setState(285);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 22, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(281);
        match(OpenCMLParser::T__7);
        setState(282);
        identRef(); 
      }
      setState(287);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 22, _ctx);
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
    setState(288);
    entityExpr();
    setState(293);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(289);
        match(OpenCMLParser::T__7);
        setState(290);
        entityExpr(); 
      }
      setState(295);
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
    setState(296);
    keyTypePair();
    setState(301);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 24, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(297);
        match(OpenCMLParser::T__7);
        setState(298);
        keyTypePair(); 
      }
      setState(303);
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
    setState(304);
    keyValuePair();
    setState(309);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 25, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(305);
        match(OpenCMLParser::T__7);
        setState(306);
        keyValuePair(); 
      }
      setState(311);
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
    setState(312);
    keyParamPair();
    setState(317);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(313);
        match(OpenCMLParser::T__7);
        setState(314);
        keyParamPair(); 
      }
      setState(319);
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
    setState(320);
    indexKVPair();
    setState(325);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(321);
        match(OpenCMLParser::T__7);
        setState(322);
        indexKVPair(); 
      }
      setState(327);
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
    setState(334);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(328);
      valueList();
      setState(331);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28, _ctx)) {
      case 1: {
        setState(329);
        match(OpenCMLParser::T__7);
        setState(330);
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
      setState(333);
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
    setState(336);
    match(OpenCMLParser::T__8);
    setState(338);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(337);
      pairedValues();
    }
    setState(341);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(340);
      match(OpenCMLParser::T__7);
    }
    setState(343);
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
    setState(345);
    match(OpenCMLParser::T__8);
    setState(347);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(346);
      identList();
    }
    setState(350);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(349);
      match(OpenCMLParser::T__7);
    }
    setState(352);
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
    setState(354);
    match(OpenCMLParser::T__8);
    setState(356);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1152763278011673184) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
      setState(355);
      stmtList();
    }
    setState(358);
    match(OpenCMLParser::T__9);
   
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
    setState(360);
    match(OpenCMLParser::T__8);
    setState(362);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1143914408431331904) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
      setState(361);
      valueList();
    }
    setState(365);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(364);
      match(OpenCMLParser::T__7);
    }
    setState(367);
    match(OpenCMLParser::T__9);
   
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
    setState(369);
    match(OpenCMLParser::T__8);
    setState(371);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(370);
      indexKVPairs();
    }
    setState(374);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(373);
      match(OpenCMLParser::T__7);
    }
    setState(376);
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
    setState(378);
    match(OpenCMLParser::T__5);
    setState(380);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(379);
      identList();
    }
    setState(383);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(382);
      match(OpenCMLParser::T__7);
    }
    setState(385);
    match(OpenCMLParser::T__6);
   
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
    setState(387);
    match(OpenCMLParser::T__5);
    setState(389);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1143914408431331904) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
      setState(388);
      valueList();
    }
    setState(392);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(391);
      match(OpenCMLParser::T__7);
    }
    setState(394);
    match(OpenCMLParser::T__6);
   
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
    setState(396);
    match(OpenCMLParser::T__5);
    setState(397);
    entityExpr();
    setState(398);
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
    setState(400);
    match(OpenCMLParser::T__10);
    setState(402);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(401);
      pairedParams();
    }
    setState(405);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(404);
      match(OpenCMLParser::T__7);
    }
    setState(407);
    match(OpenCMLParser::T__11);
   
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
    setState(409);
    match(OpenCMLParser::T__10);
    setState(411);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1143914408431331904) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
      setState(410);
      argumentList();
    }
    setState(414);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(413);
      match(OpenCMLParser::T__7);
    }
    setState(416);
    match(OpenCMLParser::T__11);
   
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
    setState(418);
    match(OpenCMLParser::T__10);
    setState(420);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1143914408431331904) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
      setState(419);
      valueList();
    }
    setState(423);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(422);
      match(OpenCMLParser::T__7);
    }
    setState(425);
    match(OpenCMLParser::T__11);
   
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
    setState(427);
    match(OpenCMLParser::T__12);
    setState(429);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(428);
      pairedParams();
    }
    setState(432);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(431);
      match(OpenCMLParser::T__7);
    }
    setState(434);
    match(OpenCMLParser::T__13);
   
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
    setState(436);
    match(OpenCMLParser::T__12);
    setState(438);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1143914408431331904) != 0) || ((((_la - 86) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 86)) & 123) != 0)) {
      setState(437);
      argumentList();
    }
    setState(441);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(440);
      match(OpenCMLParser::T__7);
    }
    setState(443);
    match(OpenCMLParser::T__13);
   
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
    enterOuterAlt(_localctx, 1);
    setState(445);
    ternaryExpr();
    setState(448);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 53, _ctx)) {
    case 1: {
      setState(446);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8355844) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(447);
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
    setState(450);
    relationalExpr();
    setState(456);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 54, _ctx)) {
    case 1: {
      setState(451);
      match(OpenCMLParser::T__22);
      setState(452);
      ternaryExpr();
      setState(453);
      match(OpenCMLParser::T__0);
      setState(454);
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
    setState(458);
    logicalAndExpr();
    setState(463);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__23) {
      setState(459);
      match(OpenCMLParser::T__23);
      setState(460);
      logicalAndExpr();
      setState(465);
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
    setState(466);
    equalityExpr();
    setState(471);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__24) {
      setState(467);
      match(OpenCMLParser::T__24);
      setState(468);
      equalityExpr();
      setState(473);
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
    setState(474);
    relationalExpr();
    setState(479);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__25

    || _la == OpenCMLParser::T__26) {
      setState(475);
      _la = _input->LA(1);
      if (!(_la == OpenCMLParser::T__25

      || _la == OpenCMLParser::T__26)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(476);
      relationalExpr();
      setState(481);
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
    setState(482);
    additiveExpr();
    setState(487);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 58, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(483);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 805330944) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(484);
        additiveExpr(); 
      }
      setState(489);
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
    setState(490);
    multiplicativeExpr();
    setState(495);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 59, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(491);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__29

        || _la == OpenCMLParser::T__30)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(492);
        multiplicativeExpr(); 
      }
      setState(497);
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
    setState(498);
    unaryExpr();
    setState(503);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 60, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(499);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 13224204304392) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(500);
        unaryExpr(); 
      }
      setState(505);
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
    setState(509);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__5:
      case OpenCMLParser::T__8:
      case OpenCMLParser::T__10:
      case OpenCMLParser::T__12:
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
        setState(506);
        linkExpr();
        break;
      }

      case OpenCMLParser::T__34:
      case OpenCMLParser::T__35: {
        enterOuterAlt(_localctx, 2);
        setState(507);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__34

        || _la == OpenCMLParser::T__35)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(508);
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
    setState(511);
    withExpr();
    setState(516);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 62, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(512);
        match(OpenCMLParser::T__36);
        setState(513);
        withExpr(); 
      }
      setState(518);
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
    setState(519);
    annotatedExpr();
    setState(524);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 63, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(520);
        match(OpenCMLParser::T__37);
        setState(521);
        annotatedExpr(); 
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
    setState(527);
    primaryExpr();
    setState(534);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 65, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(532);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case OpenCMLParser::T__5: {
            setState(528);
            memberAccess();
            break;
          }

          case OpenCMLParser::T__10: {
            setState(529);
            parentArgues();
            break;
          }

          case OpenCMLParser::T__12: {
            setState(530);
            angledValues();
            break;
          }

          case OpenCMLParser::T__4: {
            setState(531);
            annotation();
            break;
          }

        default:
          throw NoViableAltException(this);
        } 
      }
      setState(536);
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

OpenCMLParser::BracedIndexKVPairsContext* OpenCMLParser::PrimaryExprContext::bracedIndexKVPairs() {
  return getRuleContext<OpenCMLParser::BracedIndexKVPairsContext>(0);
}

std::vector<OpenCMLParser::TypeExprContext *> OpenCMLParser::PrimaryExprContext::typeExpr() {
  return getRuleContexts<OpenCMLParser::TypeExprContext>();
}

OpenCMLParser::TypeExprContext* OpenCMLParser::PrimaryExprContext::typeExpr(size_t i) {
  return getRuleContext<OpenCMLParser::TypeExprContext>(i);
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
    setState(574);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 71, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(537);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(538);
      literal();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(539);
      bracedPairedValues();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(540);
      match(OpenCMLParser::T__10);
      setState(541);
      entityExpr();
      setState(542);
      match(OpenCMLParser::T__11);
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(565);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__12) {
        setState(544);
        match(OpenCMLParser::T__12);
        setState(545);
        typeExpr();
        setState(561);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__7) {
          setState(546);
          match(OpenCMLParser::T__7);
          setState(559);
          _errHandler->sync(this);
          switch (_input->LA(1)) {
            case OpenCMLParser::T__8:
            case OpenCMLParser::T__10:
            case OpenCMLParser::T__12:
            case OpenCMLParser::INTEGER_TYPE:
            case OpenCMLParser::INTEGER32_TYPE:
            case OpenCMLParser::INTEGER64_TYPE:
            case OpenCMLParser::REAL_TYPE:
            case OpenCMLParser::FLOAT_TYPE:
            case OpenCMLParser::DOUBLE_TYPE:
            case OpenCMLParser::NUMBER_TYPE:
            case OpenCMLParser::STRING_TYPE:
            case OpenCMLParser::BOOL_TYPE:
            case OpenCMLParser::CHAR_TYPE:
            case OpenCMLParser::SET_TYPE:
            case OpenCMLParser::MAP_TYPE:
            case OpenCMLParser::LIST_TYPE:
            case OpenCMLParser::DICT_TYPE:
            case OpenCMLParser::ARRAY_TYPE:
            case OpenCMLParser::TUPLE_TYPE:
            case OpenCMLParser::UNION_TYPE:
            case OpenCMLParser::VECTOR_TYPE:
            case OpenCMLParser::TENSOR_TYPE:
            case OpenCMLParser::ANY_TYPE:
            case OpenCMLParser::VOID_TYPE:
            case OpenCMLParser::FUNCTOR_TYPE:
            case OpenCMLParser::IDENTIFIER: {
              setState(547);
              typeExpr();
              break;
            }

            case OpenCMLParser::INTEGER: {
              setState(548);
              match(OpenCMLParser::INTEGER);
              break;
            }

            case OpenCMLParser::T__5: {
              setState(549);
              match(OpenCMLParser::T__5);
              setState(550);
              match(OpenCMLParser::INTEGER);
              setState(555);
              _errHandler->sync(this);
              _la = _input->LA(1);
              while (_la == OpenCMLParser::T__7) {
                setState(551);
                match(OpenCMLParser::T__7);
                setState(552);
                match(OpenCMLParser::INTEGER);
                setState(557);
                _errHandler->sync(this);
                _la = _input->LA(1);
              }
              setState(558);
              match(OpenCMLParser::T__6);
              break;
            }

          default:
            throw NoViableAltException(this);
          }
        }
        setState(563);
        match(OpenCMLParser::T__13);
      }
      setState(571);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx)) {
      case 1: {
        setState(567);
        bracketValues();
        break;
      }

      case 2: {
        setState(568);
        parentValues();
        break;
      }

      case 3: {
        setState(569);
        bracedValues();
        break;
      }

      case 4: {
        setState(570);
        bracedIndexKVPairs();
        break;
      }

      default:
        break;
      }
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(573);
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
    setState(590);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER: {
        enterOuterAlt(_localctx, 1);
        setState(576);
        match(OpenCMLParser::INTEGER);
        setState(578);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::UNIT) {
          setState(577);
          match(OpenCMLParser::UNIT);
        }
        break;
      }

      case OpenCMLParser::REAL: {
        enterOuterAlt(_localctx, 2);
        setState(580);
        match(OpenCMLParser::REAL);
        setState(582);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::UNIT) {
          setState(581);
          match(OpenCMLParser::UNIT);
        }
        break;
      }

      case OpenCMLParser::STRING: {
        enterOuterAlt(_localctx, 3);
        setState(584);
        match(OpenCMLParser::STRING);
        break;
      }

      case OpenCMLParser::MULTI_STR: {
        enterOuterAlt(_localctx, 4);
        setState(585);
        match(OpenCMLParser::MULTI_STR);
        break;
      }

      case OpenCMLParser::FSTRING: {
        enterOuterAlt(_localctx, 5);
        setState(586);
        match(OpenCMLParser::FSTRING);
        break;
      }

      case OpenCMLParser::TRUE: {
        enterOuterAlt(_localctx, 6);
        setState(587);
        match(OpenCMLParser::TRUE);
        break;
      }

      case OpenCMLParser::FALSE: {
        enterOuterAlt(_localctx, 7);
        setState(588);
        match(OpenCMLParser::FALSE);
        break;
      }

      case OpenCMLParser::NULL_: {
        enterOuterAlt(_localctx, 8);
        setState(589);
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
    setState(592);
    arrayType();
    setState(597);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 75, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(593);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 1653562408960) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(594);
        arrayType(); 
      }
      setState(599);
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
    setState(600);
    atomType();
    setState(608);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 77, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(601);
        match(OpenCMLParser::T__5);
        setState(603);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::INTEGER) {
          setState(602);
          match(OpenCMLParser::INTEGER);
        }
        setState(605);
        match(OpenCMLParser::T__6); 
      }
      setState(610);
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
    setState(620);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 78, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(611);
      primaryType();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(612);
      structType();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(613);
      specialType();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(614);
      identRef();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(615);
      match(OpenCMLParser::T__10);
      setState(616);
      typeExpr();
      setState(617);
      match(OpenCMLParser::T__11);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(619);
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
    setState(627);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__12) {
      setState(622);
      match(OpenCMLParser::T__12);
      setState(624);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::VAR

      || _la == OpenCMLParser::IDENTIFIER) {
        setState(623);
        pairedParams();
      }
      setState(626);
      match(OpenCMLParser::T__13);
    }
    setState(629);
    match(OpenCMLParser::T__10);
    setState(631);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(630);
      pairedParams();
    }
    setState(633);
    match(OpenCMLParser::T__11);
    setState(634);
    match(OpenCMLParser::T__3);
    setState(635);
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
    setState(637);
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
    setState(729);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 98, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(639);
      match(OpenCMLParser::SET_TYPE);
      setState(644);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__12) {
        setState(640);
        match(OpenCMLParser::T__12);
        setState(641);
        typeExpr();
        setState(642);
        match(OpenCMLParser::T__13);
      }
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(646);
      match(OpenCMLParser::MAP_TYPE);
      setState(653);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__12) {
        setState(647);
        match(OpenCMLParser::T__12);
        setState(648);
        typeExpr();
        setState(649);
        match(OpenCMLParser::T__7);
        setState(650);
        typeExpr();
        setState(651);
        match(OpenCMLParser::T__13);
      }
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(655);
      match(OpenCMLParser::LIST_TYPE);
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(656);
      match(OpenCMLParser::DICT_TYPE);
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(657);
      match(OpenCMLParser::ARRAY_TYPE);
      setState(666);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__12) {
        setState(658);
        match(OpenCMLParser::T__12);
        setState(659);
        typeExpr();
        setState(662);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__7) {
          setState(660);
          match(OpenCMLParser::T__7);
          setState(661);
          match(OpenCMLParser::INTEGER);
        }
        setState(664);
        match(OpenCMLParser::T__13);
      }
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(668);
      match(OpenCMLParser::TUPLE_TYPE);
      setState(677);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__12) {
        setState(669);
        match(OpenCMLParser::T__12);
        setState(671);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & -1152921504606836224) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
          ((1ULL << (_la - 64)) & 8650751) != 0)) {
          setState(670);
          typeList();
        }
        setState(674);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__7) {
          setState(673);
          match(OpenCMLParser::T__7);
        }
        setState(676);
        match(OpenCMLParser::T__13);
      }
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(679);
      match(OpenCMLParser::UNION_TYPE);
      setState(688);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__12) {
        setState(680);
        match(OpenCMLParser::T__12);
        setState(682);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & -1152921504606836224) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
          ((1ULL << (_la - 64)) & 8650751) != 0)) {
          setState(681);
          typeList();
        }
        setState(685);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__7) {
          setState(684);
          match(OpenCMLParser::T__7);
        }
        setState(687);
        match(OpenCMLParser::T__13);
      }
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(690);
      match(OpenCMLParser::VECTOR_TYPE);
      setState(695);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__12) {
        setState(691);
        match(OpenCMLParser::T__12);
        setState(692);
        typeExpr();
        setState(693);
        match(OpenCMLParser::T__13);
      }
      break;
    }

    case 9: {
      enterOuterAlt(_localctx, 9);
      setState(697);
      match(OpenCMLParser::TENSOR_TYPE);
      setState(715);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__12) {
        setState(698);
        match(OpenCMLParser::T__12);
        setState(699);
        typeExpr();
        setState(711);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__7) {
          setState(700);
          match(OpenCMLParser::T__7);
          setState(701);
          match(OpenCMLParser::T__5);
          setState(702);
          match(OpenCMLParser::INTEGER);
          setState(707);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == OpenCMLParser::T__7) {
            setState(703);
            match(OpenCMLParser::T__7);
            setState(704);
            match(OpenCMLParser::INTEGER);
            setState(709);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
          setState(710);
          match(OpenCMLParser::T__6);
        }
        setState(713);
        match(OpenCMLParser::T__13);
      }
      break;
    }

    case 10: {
      enterOuterAlt(_localctx, 10);
      setState(717);
      match(OpenCMLParser::T__8);
      setState(719);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::IDENTIFIER) {
        setState(718);
        pairedTypes();
      }
      setState(722);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__7) {
        setState(721);
        match(OpenCMLParser::T__7);
      }
      setState(724);
      match(OpenCMLParser::T__9);
      break;
    }

    case 11: {
      enterOuterAlt(_localctx, 11);
      setState(725);
      match(OpenCMLParser::T__8);
      setState(726);
      indexKTPair();
      setState(727);
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
    setState(731);
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
    setState(733);
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
