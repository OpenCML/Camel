
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
      "program", "decl", "stmt", "stmtList", "moduleDecl", "importDecl", 
      "exportDecl", "funcDecl", "letStmt", "useStmt", "typeStmt", "enumStmt", 
      "exprStmt", "retStmt", "stmtBlock", "lambdaExpr", "carrier", "annotation", 
      "annotations", "modifiers", "indexValue", "keyTypePair", "keyValuePair", 
      "keyParamPair", "typeList", "identList", "valueList", "indexValues", 
      "pairedTypes", "pairedValues", "pairedParams", "argumentList", "bracedIdents", 
      "bracketIdents", "memberAccess", "parentParams", "parentArgues", "parentValues", 
      "angledParams", "angledValues", "blockExpr", "dataExpr", "structExpr", 
      "logicalOrExpr", "logicalAndExpr", "equalityExpr", "relationalExpr", 
      "additiveExpr", "multiplicativeExpr", "nullableExpr", "unaryExpr", 
      "linkExpr", "bindExpr", "withExpr", "annotatedExpr", "dictExpr", "listExpr", 
      "primaryData", "literal", "typeName", "typeExpr", "unionType", "listType", 
      "argsType", "primaryType", "dictExprType", "dictType", "lambdaType", 
      "identDef", "identRef"
    },
    std::vector<std::string>{
      "", "':'", "'='", "'{'", "'}'", "'=>'", "'@'", "'...'", "','", "'['", 
      "']'", "'('", "')'", "'<'", "'>'", "'+='", "'-='", "'*='", "'/='", 
      "'%='", "'^='", "'&='", "'|='", "'||'", "'&&'", "'==='", "'!=='", 
      "'=='", "'!='", "'<='", "'>='", "'+'", "'-'", "'^'", "'*'", "'/'", 
      "'%'", "'\\u003F\\u003F'", "'!'", "'~'", "'->'", "'\\u003F->'", "'..'", 
      "'\\u003F..'", "'.'", "'\\u003F.'", "'\\u003F'", "'|'", "'&'", "'::'", 
      "';'", "'as'", "'is'", "'let'", "'var'", "'use'", "'from'", "'func'", 
      "'type'", "'enum'", "'with'", "'wait'", "'return'", "'module'", "'import'", 
      "'export'", "'typeas'", "'typeof'", "'if'", "'then'", "'else'", "'match'", 
      "'case'", "'for'", "'in'", "'sync'", "'macro'", "'inner'", "'outer'", 
      "'atomic'", "'shared'", "'null'", "'true'", "'false'"
    },
    std::vector<std::string>{
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "SEP", 
      "AS", "IS", "LET", "VAR", "USE", "FROM", "FUNC", "TYPE", "ENUM", "WITH", 
      "WAIT", "RETURN", "MODULE", "IMPORT", "EXPORT", "TYPEAS", "TYPEOF", 
      "IF", "THEN", "ELSE", "MATCH", "CASE", "FOR", "IN", "SYNC", "MACRO", 
      "INNER", "OUTER", "ATOMIC", "SHARED", "NULL", "TRUE", "FALSE", "INNER_ATOM_TYPE", 
      "BLANK", "COMMAND", "LIN_CMT", "BLK_CMT", "MULTI_STR", "IDENTIFIER", 
      "STRING", "FSTRING", "INTEGER", "REAL"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,94,733,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,2,55,7,55,2,56,7,
  	56,2,57,7,57,2,58,7,58,2,59,7,59,2,60,7,60,2,61,7,61,2,62,7,62,2,63,7,
  	63,2,64,7,64,2,65,7,65,2,66,7,66,2,67,7,67,2,68,7,68,2,69,7,69,1,0,3,
  	0,142,8,0,1,0,1,0,3,0,146,8,0,1,0,3,0,149,8,0,5,0,151,8,0,10,0,12,0,154,
  	9,0,1,0,1,0,1,1,1,1,1,1,1,1,3,1,162,8,1,1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,
  	2,171,8,2,1,3,1,3,3,3,175,8,3,1,3,5,3,178,8,3,10,3,12,3,181,9,3,1,3,3,
  	3,184,8,3,1,4,1,4,1,4,1,5,1,5,1,5,1,5,3,5,193,8,5,1,5,1,5,1,5,3,5,198,
  	8,5,1,6,1,6,1,6,1,6,3,6,204,8,6,1,7,3,7,207,8,7,1,7,1,7,3,7,211,8,7,1,
  	7,3,7,214,8,7,1,7,3,7,217,8,7,1,7,1,7,1,7,1,7,1,7,3,7,224,8,7,1,7,1,7,
  	1,8,1,8,1,8,1,8,3,8,232,8,8,1,8,1,8,1,8,1,9,1,9,1,9,1,9,3,9,241,8,9,1,
  	9,1,9,1,10,1,10,1,10,1,10,1,10,1,11,1,11,1,11,1,11,1,11,1,12,3,12,256,
  	8,12,1,12,1,12,1,13,1,13,1,13,1,14,3,14,264,8,14,1,14,1,14,3,14,268,8,
  	14,1,14,1,14,1,15,3,15,273,8,15,1,15,3,15,276,8,15,1,15,1,15,1,15,3,15,
  	281,8,15,1,15,1,15,1,15,1,16,1,16,1,16,3,16,289,8,16,1,17,1,17,1,17,1,
  	18,4,18,295,8,18,11,18,12,18,296,1,19,4,19,300,8,19,11,19,12,19,301,1,
  	20,1,20,1,20,3,20,307,8,20,1,21,1,21,1,21,1,21,1,22,1,22,1,22,1,22,1,
  	22,1,22,3,22,319,8,22,1,23,3,23,322,8,23,1,23,1,23,3,23,326,8,23,1,23,
  	1,23,1,23,3,23,331,8,23,1,23,1,23,3,23,335,8,23,1,24,1,24,1,24,5,24,340,
  	8,24,10,24,12,24,343,9,24,1,25,1,25,1,25,5,25,348,8,25,10,25,12,25,351,
  	9,25,1,26,1,26,1,26,5,26,356,8,26,10,26,12,26,359,9,26,1,27,1,27,1,27,
  	5,27,364,8,27,10,27,12,27,367,9,27,1,28,1,28,1,28,5,28,372,8,28,10,28,
  	12,28,375,9,28,1,29,1,29,1,29,5,29,380,8,29,10,29,12,29,383,9,29,1,30,
  	1,30,1,30,5,30,388,8,30,10,30,12,30,391,9,30,1,31,1,31,1,31,3,31,396,
  	8,31,1,31,3,31,399,8,31,1,32,1,32,3,32,403,8,32,1,32,3,32,406,8,32,1,
  	32,1,32,1,33,1,33,3,33,412,8,33,1,33,3,33,415,8,33,1,33,1,33,1,34,1,34,
  	1,34,1,34,1,34,1,34,3,34,425,8,34,3,34,427,8,34,1,34,1,34,1,35,1,35,3,
  	35,433,8,35,1,35,3,35,436,8,35,1,35,1,35,1,36,1,36,3,36,442,8,36,1,36,
  	3,36,445,8,36,1,36,1,36,1,37,1,37,3,37,451,8,37,1,37,3,37,454,8,37,1,
  	37,1,37,1,38,1,38,3,38,460,8,38,1,38,3,38,463,8,38,1,38,1,38,1,39,1,39,
  	3,39,469,8,39,1,39,3,39,472,8,39,1,39,1,39,1,40,1,40,3,40,478,8,40,1,
  	41,3,41,481,8,41,1,41,1,41,1,41,3,41,486,8,41,1,42,1,42,1,42,1,42,1,42,
  	1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,4,42,504,8,42,
  	11,42,12,42,505,1,42,1,42,3,42,510,8,42,1,43,1,43,1,43,5,43,515,8,43,
  	10,43,12,43,518,9,43,1,44,1,44,1,44,5,44,523,8,44,10,44,12,44,526,9,44,
  	1,45,1,45,1,45,5,45,531,8,45,10,45,12,45,534,9,45,1,46,1,46,1,46,5,46,
  	539,8,46,10,46,12,46,542,9,46,1,47,1,47,1,47,5,47,547,8,47,10,47,12,47,
  	550,9,47,1,48,1,48,1,48,5,48,555,8,48,10,48,12,48,558,9,48,1,49,1,49,
  	1,49,3,49,563,8,49,1,50,1,50,1,50,3,50,568,8,50,1,50,1,50,3,50,572,8,
  	50,1,51,1,51,1,51,5,51,577,8,51,10,51,12,51,580,9,51,1,52,1,52,1,52,5,
  	52,585,8,52,10,52,12,52,588,9,52,1,53,1,53,1,53,5,53,593,8,53,10,53,12,
  	53,596,9,53,1,54,1,54,1,54,1,54,1,54,3,54,603,8,54,1,54,5,54,606,8,54,
  	10,54,12,54,609,9,54,1,55,1,55,1,55,3,55,614,8,55,3,55,616,8,55,1,55,
  	1,55,1,56,1,56,1,56,3,56,623,8,56,1,56,1,56,1,56,1,56,1,56,1,56,1,56,
  	3,56,632,8,56,3,56,634,8,56,1,56,1,56,1,57,1,57,1,57,1,57,1,57,1,57,1,
  	57,1,57,1,57,1,57,3,57,648,8,57,1,58,1,58,1,59,1,59,1,59,1,60,1,60,3,
  	60,657,8,60,1,61,1,61,1,61,5,61,662,8,61,10,61,12,61,665,9,61,1,62,1,
  	62,1,62,5,62,670,8,62,10,62,12,62,673,9,62,1,63,1,63,1,63,3,63,678,8,
  	63,1,63,3,63,681,8,63,1,64,1,64,1,64,1,64,1,64,1,64,1,64,1,64,1,64,1,
  	64,3,64,693,8,64,1,65,1,65,1,65,5,65,698,8,65,10,65,12,65,701,9,65,1,
  	66,1,66,3,66,705,8,66,1,66,3,66,708,8,66,1,66,1,66,1,67,3,67,713,8,67,
  	1,67,3,67,716,8,67,1,67,1,67,1,67,1,67,1,68,1,68,1,69,1,69,5,69,726,8,
  	69,10,69,12,69,729,9,69,1,69,1,69,1,69,0,0,70,0,2,4,6,8,10,12,14,16,18,
  	20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,
  	66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104,106,108,
  	110,112,114,116,118,120,122,124,126,128,130,132,134,136,138,0,14,1,0,
  	53,54,1,0,75,80,2,0,2,2,15,22,1,0,25,28,2,0,13,14,29,30,1,0,31,32,1,0,
  	33,36,1,0,51,52,2,0,32,32,38,39,1,0,40,41,1,0,42,43,1,0,44,45,3,0,81,
  	83,89,89,91,94,2,0,33,33,48,48,780,0,141,1,0,0,0,2,161,1,0,0,0,4,170,
  	1,0,0,0,6,172,1,0,0,0,8,185,1,0,0,0,10,188,1,0,0,0,12,199,1,0,0,0,14,
  	206,1,0,0,0,16,227,1,0,0,0,18,236,1,0,0,0,20,244,1,0,0,0,22,249,1,0,0,
  	0,24,255,1,0,0,0,26,259,1,0,0,0,28,263,1,0,0,0,30,272,1,0,0,0,32,288,
  	1,0,0,0,34,290,1,0,0,0,36,294,1,0,0,0,38,299,1,0,0,0,40,306,1,0,0,0,42,
  	308,1,0,0,0,44,318,1,0,0,0,46,321,1,0,0,0,48,336,1,0,0,0,50,344,1,0,0,
  	0,52,352,1,0,0,0,54,360,1,0,0,0,56,368,1,0,0,0,58,376,1,0,0,0,60,384,
  	1,0,0,0,62,398,1,0,0,0,64,400,1,0,0,0,66,409,1,0,0,0,68,418,1,0,0,0,70,
  	430,1,0,0,0,72,439,1,0,0,0,74,448,1,0,0,0,76,457,1,0,0,0,78,466,1,0,0,
  	0,80,477,1,0,0,0,82,480,1,0,0,0,84,509,1,0,0,0,86,511,1,0,0,0,88,519,
  	1,0,0,0,90,527,1,0,0,0,92,535,1,0,0,0,94,543,1,0,0,0,96,551,1,0,0,0,98,
  	559,1,0,0,0,100,571,1,0,0,0,102,573,1,0,0,0,104,581,1,0,0,0,106,589,1,
  	0,0,0,108,597,1,0,0,0,110,610,1,0,0,0,112,619,1,0,0,0,114,647,1,0,0,0,
  	116,649,1,0,0,0,118,651,1,0,0,0,120,654,1,0,0,0,122,658,1,0,0,0,124,666,
  	1,0,0,0,126,674,1,0,0,0,128,692,1,0,0,0,130,694,1,0,0,0,132,702,1,0,0,
  	0,134,712,1,0,0,0,136,721,1,0,0,0,138,727,1,0,0,0,140,142,5,50,0,0,141,
  	140,1,0,0,0,141,142,1,0,0,0,142,152,1,0,0,0,143,146,3,2,1,0,144,146,3,
  	4,2,0,145,143,1,0,0,0,145,144,1,0,0,0,146,148,1,0,0,0,147,149,5,50,0,
  	0,148,147,1,0,0,0,148,149,1,0,0,0,149,151,1,0,0,0,150,145,1,0,0,0,151,
  	154,1,0,0,0,152,150,1,0,0,0,152,153,1,0,0,0,153,155,1,0,0,0,154,152,1,
  	0,0,0,155,156,5,0,0,1,156,1,1,0,0,0,157,162,3,8,4,0,158,162,3,10,5,0,
  	159,162,3,12,6,0,160,162,3,14,7,0,161,157,1,0,0,0,161,158,1,0,0,0,161,
  	159,1,0,0,0,161,160,1,0,0,0,162,3,1,0,0,0,163,171,3,16,8,0,164,171,3,
  	18,9,0,165,171,3,20,10,0,166,171,3,22,11,0,167,171,3,24,12,0,168,171,
  	3,26,13,0,169,171,3,28,14,0,170,163,1,0,0,0,170,164,1,0,0,0,170,165,1,
  	0,0,0,170,166,1,0,0,0,170,167,1,0,0,0,170,168,1,0,0,0,170,169,1,0,0,0,
  	171,5,1,0,0,0,172,179,3,4,2,0,173,175,5,50,0,0,174,173,1,0,0,0,174,175,
  	1,0,0,0,175,176,1,0,0,0,176,178,3,4,2,0,177,174,1,0,0,0,178,181,1,0,0,
  	0,179,177,1,0,0,0,179,180,1,0,0,0,180,183,1,0,0,0,181,179,1,0,0,0,182,
  	184,5,50,0,0,183,182,1,0,0,0,183,184,1,0,0,0,184,7,1,0,0,0,185,186,5,
  	63,0,0,186,187,3,136,68,0,187,9,1,0,0,0,188,197,5,64,0,0,189,198,5,91,
  	0,0,190,193,3,136,68,0,191,193,3,64,32,0,192,190,1,0,0,0,192,191,1,0,
  	0,0,193,194,1,0,0,0,194,195,5,56,0,0,195,196,5,91,0,0,196,198,1,0,0,0,
  	197,189,1,0,0,0,197,192,1,0,0,0,198,11,1,0,0,0,199,203,5,65,0,0,200,204,
  	3,16,8,0,201,204,3,20,10,0,202,204,3,64,32,0,203,200,1,0,0,0,203,201,
  	1,0,0,0,203,202,1,0,0,0,204,13,1,0,0,0,205,207,3,36,18,0,206,205,1,0,
  	0,0,206,207,1,0,0,0,207,210,1,0,0,0,208,209,5,60,0,0,209,211,3,76,38,
  	0,210,208,1,0,0,0,210,211,1,0,0,0,211,213,1,0,0,0,212,214,5,65,0,0,213,
  	212,1,0,0,0,213,214,1,0,0,0,214,216,1,0,0,0,215,217,3,38,19,0,216,215,
  	1,0,0,0,216,217,1,0,0,0,217,218,1,0,0,0,218,219,5,57,0,0,219,220,3,136,
  	68,0,220,223,3,70,35,0,221,222,5,1,0,0,222,224,3,120,60,0,223,221,1,0,
  	0,0,223,224,1,0,0,0,224,225,1,0,0,0,225,226,3,28,14,0,226,15,1,0,0,0,
  	227,228,7,0,0,0,228,231,3,32,16,0,229,230,5,1,0,0,230,232,3,120,60,0,
  	231,229,1,0,0,0,231,232,1,0,0,0,232,233,1,0,0,0,233,234,5,2,0,0,234,235,
  	3,82,41,0,235,17,1,0,0,0,236,240,5,55,0,0,237,238,3,136,68,0,238,239,
  	5,2,0,0,239,241,1,0,0,0,240,237,1,0,0,0,240,241,1,0,0,0,241,242,1,0,0,
  	0,242,243,3,138,69,0,243,19,1,0,0,0,244,245,5,58,0,0,245,246,3,136,68,
  	0,246,247,5,2,0,0,247,248,3,120,60,0,248,21,1,0,0,0,249,250,5,59,0,0,
  	250,251,3,136,68,0,251,252,5,2,0,0,252,253,3,64,32,0,253,23,1,0,0,0,254,
  	256,3,36,18,0,255,254,1,0,0,0,255,256,1,0,0,0,256,257,1,0,0,0,257,258,
  	3,82,41,0,258,25,1,0,0,0,259,260,5,62,0,0,260,261,3,82,41,0,261,27,1,
  	0,0,0,262,264,5,75,0,0,263,262,1,0,0,0,263,264,1,0,0,0,264,265,1,0,0,
  	0,265,267,5,3,0,0,266,268,3,6,3,0,267,266,1,0,0,0,267,268,1,0,0,0,268,
  	269,1,0,0,0,269,270,5,4,0,0,270,29,1,0,0,0,271,273,3,38,19,0,272,271,
  	1,0,0,0,272,273,1,0,0,0,273,275,1,0,0,0,274,276,3,76,38,0,275,274,1,0,
  	0,0,275,276,1,0,0,0,276,277,1,0,0,0,277,280,3,70,35,0,278,279,5,1,0,0,
  	279,281,3,120,60,0,280,278,1,0,0,0,280,281,1,0,0,0,281,282,1,0,0,0,282,
  	283,5,5,0,0,283,284,3,80,40,0,284,31,1,0,0,0,285,289,3,136,68,0,286,289,
  	3,64,32,0,287,289,3,66,33,0,288,285,1,0,0,0,288,286,1,0,0,0,288,287,1,
  	0,0,0,289,33,1,0,0,0,290,291,5,6,0,0,291,292,3,114,57,0,292,35,1,0,0,
  	0,293,295,3,34,17,0,294,293,1,0,0,0,295,296,1,0,0,0,296,294,1,0,0,0,296,
  	297,1,0,0,0,297,37,1,0,0,0,298,300,7,1,0,0,299,298,1,0,0,0,300,301,1,
  	0,0,0,301,299,1,0,0,0,301,302,1,0,0,0,302,39,1,0,0,0,303,307,3,82,41,
  	0,304,305,5,7,0,0,305,307,3,82,41,0,306,303,1,0,0,0,306,304,1,0,0,0,307,
  	41,1,0,0,0,308,309,3,136,68,0,309,310,5,1,0,0,310,311,3,120,60,0,311,
  	43,1,0,0,0,312,313,3,136,68,0,313,314,5,1,0,0,314,315,3,82,41,0,315,319,
  	1,0,0,0,316,317,5,7,0,0,317,319,3,82,41,0,318,312,1,0,0,0,318,316,1,0,
  	0,0,319,45,1,0,0,0,320,322,5,54,0,0,321,320,1,0,0,0,321,322,1,0,0,0,322,
  	323,1,0,0,0,323,325,3,136,68,0,324,326,3,34,17,0,325,324,1,0,0,0,325,
  	326,1,0,0,0,326,327,1,0,0,0,327,330,5,1,0,0,328,331,3,120,60,0,329,331,
  	3,118,59,0,330,328,1,0,0,0,330,329,1,0,0,0,331,334,1,0,0,0,332,333,5,
  	2,0,0,333,335,3,82,41,0,334,332,1,0,0,0,334,335,1,0,0,0,335,47,1,0,0,
  	0,336,341,3,120,60,0,337,338,5,8,0,0,338,340,3,120,60,0,339,337,1,0,0,
  	0,340,343,1,0,0,0,341,339,1,0,0,0,341,342,1,0,0,0,342,49,1,0,0,0,343,
  	341,1,0,0,0,344,349,3,136,68,0,345,346,5,8,0,0,346,348,3,136,68,0,347,
  	345,1,0,0,0,348,351,1,0,0,0,349,347,1,0,0,0,349,350,1,0,0,0,350,51,1,
  	0,0,0,351,349,1,0,0,0,352,357,3,82,41,0,353,354,5,8,0,0,354,356,3,82,
  	41,0,355,353,1,0,0,0,356,359,1,0,0,0,357,355,1,0,0,0,357,358,1,0,0,0,
  	358,53,1,0,0,0,359,357,1,0,0,0,360,365,3,40,20,0,361,362,5,8,0,0,362,
  	364,3,40,20,0,363,361,1,0,0,0,364,367,1,0,0,0,365,363,1,0,0,0,365,366,
  	1,0,0,0,366,55,1,0,0,0,367,365,1,0,0,0,368,373,3,42,21,0,369,370,5,8,
  	0,0,370,372,3,42,21,0,371,369,1,0,0,0,372,375,1,0,0,0,373,371,1,0,0,0,
  	373,374,1,0,0,0,374,57,1,0,0,0,375,373,1,0,0,0,376,381,3,44,22,0,377,
  	378,5,8,0,0,378,380,3,44,22,0,379,377,1,0,0,0,380,383,1,0,0,0,381,379,
  	1,0,0,0,381,382,1,0,0,0,382,59,1,0,0,0,383,381,1,0,0,0,384,389,3,46,23,
  	0,385,386,5,8,0,0,386,388,3,46,23,0,387,385,1,0,0,0,388,391,1,0,0,0,389,
  	387,1,0,0,0,389,390,1,0,0,0,390,61,1,0,0,0,391,389,1,0,0,0,392,395,3,
  	54,27,0,393,394,5,8,0,0,394,396,3,58,29,0,395,393,1,0,0,0,395,396,1,0,
  	0,0,396,399,1,0,0,0,397,399,3,58,29,0,398,392,1,0,0,0,398,397,1,0,0,0,
  	399,63,1,0,0,0,400,402,5,3,0,0,401,403,3,50,25,0,402,401,1,0,0,0,402,
  	403,1,0,0,0,403,405,1,0,0,0,404,406,5,8,0,0,405,404,1,0,0,0,405,406,1,
  	0,0,0,406,407,1,0,0,0,407,408,5,4,0,0,408,65,1,0,0,0,409,411,5,9,0,0,
  	410,412,3,50,25,0,411,410,1,0,0,0,411,412,1,0,0,0,412,414,1,0,0,0,413,
  	415,5,8,0,0,414,413,1,0,0,0,414,415,1,0,0,0,415,416,1,0,0,0,416,417,5,
  	10,0,0,417,67,1,0,0,0,418,419,5,9,0,0,419,426,3,82,41,0,420,421,5,1,0,
  	0,421,424,3,82,41,0,422,423,5,1,0,0,423,425,3,82,41,0,424,422,1,0,0,0,
  	424,425,1,0,0,0,425,427,1,0,0,0,426,420,1,0,0,0,426,427,1,0,0,0,427,428,
  	1,0,0,0,428,429,5,10,0,0,429,69,1,0,0,0,430,432,5,11,0,0,431,433,3,60,
  	30,0,432,431,1,0,0,0,432,433,1,0,0,0,433,435,1,0,0,0,434,436,5,8,0,0,
  	435,434,1,0,0,0,435,436,1,0,0,0,436,437,1,0,0,0,437,438,5,12,0,0,438,
  	71,1,0,0,0,439,441,5,11,0,0,440,442,3,62,31,0,441,440,1,0,0,0,441,442,
  	1,0,0,0,442,444,1,0,0,0,443,445,5,8,0,0,444,443,1,0,0,0,444,445,1,0,0,
  	0,445,446,1,0,0,0,446,447,5,12,0,0,447,73,1,0,0,0,448,450,5,11,0,0,449,
  	451,3,52,26,0,450,449,1,0,0,0,450,451,1,0,0,0,451,453,1,0,0,0,452,454,
  	5,8,0,0,453,452,1,0,0,0,453,454,1,0,0,0,454,455,1,0,0,0,455,456,5,12,
  	0,0,456,75,1,0,0,0,457,459,5,13,0,0,458,460,3,60,30,0,459,458,1,0,0,0,
  	459,460,1,0,0,0,460,462,1,0,0,0,461,463,5,8,0,0,462,461,1,0,0,0,462,463,
  	1,0,0,0,463,464,1,0,0,0,464,465,5,14,0,0,465,77,1,0,0,0,466,468,5,13,
  	0,0,467,469,3,62,31,0,468,467,1,0,0,0,468,469,1,0,0,0,469,471,1,0,0,0,
  	470,472,5,8,0,0,471,470,1,0,0,0,471,472,1,0,0,0,472,473,1,0,0,0,473,474,
  	5,14,0,0,474,79,1,0,0,0,475,478,3,28,14,0,476,478,3,82,41,0,477,475,1,
  	0,0,0,477,476,1,0,0,0,478,81,1,0,0,0,479,481,5,61,0,0,480,479,1,0,0,0,
  	480,481,1,0,0,0,481,482,1,0,0,0,482,485,3,84,42,0,483,484,7,2,0,0,484,
  	486,3,84,42,0,485,483,1,0,0,0,485,486,1,0,0,0,486,83,1,0,0,0,487,510,
  	3,86,43,0,488,489,5,68,0,0,489,490,3,86,43,0,490,491,5,69,0,0,491,492,
  	3,80,40,0,492,493,5,70,0,0,493,494,3,80,40,0,494,510,1,0,0,0,495,496,
  	5,71,0,0,496,497,3,138,69,0,497,503,5,3,0,0,498,499,5,72,0,0,499,500,
  	3,82,41,0,500,501,5,5,0,0,501,502,3,80,40,0,502,504,1,0,0,0,503,498,1,
  	0,0,0,504,505,1,0,0,0,505,503,1,0,0,0,505,506,1,0,0,0,506,507,1,0,0,0,
  	507,508,5,4,0,0,508,510,1,0,0,0,509,487,1,0,0,0,509,488,1,0,0,0,509,495,
  	1,0,0,0,510,85,1,0,0,0,511,516,3,88,44,0,512,513,5,23,0,0,513,515,3,88,
  	44,0,514,512,1,0,0,0,515,518,1,0,0,0,516,514,1,0,0,0,516,517,1,0,0,0,
  	517,87,1,0,0,0,518,516,1,0,0,0,519,524,3,90,45,0,520,521,5,24,0,0,521,
  	523,3,90,45,0,522,520,1,0,0,0,523,526,1,0,0,0,524,522,1,0,0,0,524,525,
  	1,0,0,0,525,89,1,0,0,0,526,524,1,0,0,0,527,532,3,92,46,0,528,529,7,3,
  	0,0,529,531,3,92,46,0,530,528,1,0,0,0,531,534,1,0,0,0,532,530,1,0,0,0,
  	532,533,1,0,0,0,533,91,1,0,0,0,534,532,1,0,0,0,535,540,3,94,47,0,536,
  	537,7,4,0,0,537,539,3,94,47,0,538,536,1,0,0,0,539,542,1,0,0,0,540,538,
  	1,0,0,0,540,541,1,0,0,0,541,93,1,0,0,0,542,540,1,0,0,0,543,548,3,96,48,
  	0,544,545,7,5,0,0,545,547,3,96,48,0,546,544,1,0,0,0,547,550,1,0,0,0,548,
  	546,1,0,0,0,548,549,1,0,0,0,549,95,1,0,0,0,550,548,1,0,0,0,551,556,3,
  	98,49,0,552,553,7,6,0,0,553,555,3,98,49,0,554,552,1,0,0,0,555,558,1,0,
  	0,0,556,554,1,0,0,0,556,557,1,0,0,0,557,97,1,0,0,0,558,556,1,0,0,0,559,
  	562,3,100,50,0,560,561,5,37,0,0,561,563,3,82,41,0,562,560,1,0,0,0,562,
  	563,1,0,0,0,563,99,1,0,0,0,564,567,3,102,51,0,565,566,7,7,0,0,566,568,
  	3,120,60,0,567,565,1,0,0,0,567,568,1,0,0,0,568,572,1,0,0,0,569,570,7,
  	8,0,0,570,572,3,102,51,0,571,564,1,0,0,0,571,569,1,0,0,0,572,101,1,0,
  	0,0,573,578,3,104,52,0,574,575,7,9,0,0,575,577,3,104,52,0,576,574,1,0,
  	0,0,577,580,1,0,0,0,578,576,1,0,0,0,578,579,1,0,0,0,579,103,1,0,0,0,580,
  	578,1,0,0,0,581,586,3,106,53,0,582,583,7,10,0,0,583,585,3,106,53,0,584,
  	582,1,0,0,0,585,588,1,0,0,0,586,584,1,0,0,0,586,587,1,0,0,0,587,105,1,
  	0,0,0,588,586,1,0,0,0,589,594,3,108,54,0,590,591,7,11,0,0,591,593,3,108,
  	54,0,592,590,1,0,0,0,593,596,1,0,0,0,594,592,1,0,0,0,594,595,1,0,0,0,
  	595,107,1,0,0,0,596,594,1,0,0,0,597,607,3,114,57,0,598,602,4,54,0,0,599,
  	603,3,68,34,0,600,603,3,72,36,0,601,603,3,78,39,0,602,599,1,0,0,0,602,
  	600,1,0,0,0,602,601,1,0,0,0,603,606,1,0,0,0,604,606,3,34,17,0,605,598,
  	1,0,0,0,605,604,1,0,0,0,606,609,1,0,0,0,607,605,1,0,0,0,607,608,1,0,0,
  	0,608,109,1,0,0,0,609,607,1,0,0,0,610,615,5,3,0,0,611,613,3,58,29,0,612,
  	614,5,8,0,0,613,612,1,0,0,0,613,614,1,0,0,0,614,616,1,0,0,0,615,611,1,
  	0,0,0,615,616,1,0,0,0,616,617,1,0,0,0,617,618,5,4,0,0,618,111,1,0,0,0,
  	619,633,5,9,0,0,620,622,3,54,27,0,621,623,5,8,0,0,622,621,1,0,0,0,622,
  	623,1,0,0,0,623,634,1,0,0,0,624,625,3,82,41,0,625,626,5,73,0,0,626,627,
  	3,138,69,0,627,628,5,74,0,0,628,631,3,82,41,0,629,630,5,68,0,0,630,632,
  	3,82,41,0,631,629,1,0,0,0,631,632,1,0,0,0,632,634,1,0,0,0,633,620,1,0,
  	0,0,633,624,1,0,0,0,633,634,1,0,0,0,634,635,1,0,0,0,635,636,5,10,0,0,
  	636,113,1,0,0,0,637,648,3,138,69,0,638,648,3,116,58,0,639,648,3,112,56,
  	0,640,648,3,110,55,0,641,642,5,11,0,0,642,643,3,82,41,0,643,644,5,12,
  	0,0,644,648,1,0,0,0,645,648,3,74,37,0,646,648,3,30,15,0,647,637,1,0,0,
  	0,647,638,1,0,0,0,647,639,1,0,0,0,647,640,1,0,0,0,647,641,1,0,0,0,647,
  	645,1,0,0,0,647,646,1,0,0,0,648,115,1,0,0,0,649,650,7,12,0,0,650,117,
  	1,0,0,0,651,652,5,66,0,0,652,653,3,136,68,0,653,119,1,0,0,0,654,656,3,
  	122,61,0,655,657,5,46,0,0,656,655,1,0,0,0,656,657,1,0,0,0,657,121,1,0,
  	0,0,658,663,3,124,62,0,659,660,5,47,0,0,660,662,3,124,62,0,661,659,1,
  	0,0,0,662,665,1,0,0,0,663,661,1,0,0,0,663,664,1,0,0,0,664,123,1,0,0,0,
  	665,663,1,0,0,0,666,671,3,126,63,0,667,668,5,9,0,0,668,670,5,10,0,0,669,
  	667,1,0,0,0,670,673,1,0,0,0,671,669,1,0,0,0,671,672,1,0,0,0,672,125,1,
  	0,0,0,673,671,1,0,0,0,674,680,3,128,64,0,675,677,5,13,0,0,676,678,3,48,
  	24,0,677,676,1,0,0,0,677,678,1,0,0,0,678,679,1,0,0,0,679,681,5,14,0,0,
  	680,675,1,0,0,0,680,681,1,0,0,0,681,127,1,0,0,0,682,693,5,84,0,0,683,
  	693,3,130,65,0,684,693,3,138,69,0,685,686,5,11,0,0,686,687,3,120,60,0,
  	687,688,5,12,0,0,688,693,1,0,0,0,689,693,3,134,67,0,690,691,5,67,0,0,
  	691,693,3,82,41,0,692,682,1,0,0,0,692,683,1,0,0,0,692,684,1,0,0,0,692,
  	685,1,0,0,0,692,689,1,0,0,0,692,690,1,0,0,0,693,129,1,0,0,0,694,699,3,
  	132,66,0,695,696,7,13,0,0,696,698,3,132,66,0,697,695,1,0,0,0,698,701,
  	1,0,0,0,699,697,1,0,0,0,699,700,1,0,0,0,700,131,1,0,0,0,701,699,1,0,0,
  	0,702,704,5,3,0,0,703,705,3,56,28,0,704,703,1,0,0,0,704,705,1,0,0,0,705,
  	707,1,0,0,0,706,708,5,8,0,0,707,706,1,0,0,0,707,708,1,0,0,0,708,709,1,
  	0,0,0,709,710,5,4,0,0,710,133,1,0,0,0,711,713,3,38,19,0,712,711,1,0,0,
  	0,712,713,1,0,0,0,713,715,1,0,0,0,714,716,3,76,38,0,715,714,1,0,0,0,715,
  	716,1,0,0,0,716,717,1,0,0,0,717,718,3,70,35,0,718,719,5,5,0,0,719,720,
  	3,120,60,0,720,135,1,0,0,0,721,722,5,90,0,0,722,137,1,0,0,0,723,724,5,
  	90,0,0,724,726,5,49,0,0,725,723,1,0,0,0,726,729,1,0,0,0,727,725,1,0,0,
  	0,727,728,1,0,0,0,728,730,1,0,0,0,729,727,1,0,0,0,730,731,5,90,0,0,731,
  	139,1,0,0,0,97,141,145,148,152,161,170,174,179,183,192,197,203,206,210,
  	213,216,223,231,240,255,263,267,272,275,280,288,296,301,306,318,321,325,
  	330,334,341,349,357,365,373,381,389,395,398,402,405,411,414,424,426,432,
  	435,441,444,450,453,459,462,468,471,477,480,485,505,509,516,524,532,540,
  	548,556,562,567,571,578,586,594,602,605,607,613,615,622,631,633,647,656,
  	663,671,677,680,692,699,704,707,712,715,727
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

std::vector<tree::TerminalNode *> OpenCMLParser::ProgramContext::SEP() {
  return getTokens(OpenCMLParser::SEP);
}

tree::TerminalNode* OpenCMLParser::ProgramContext::SEP(size_t i) {
  return getToken(OpenCMLParser::SEP, i);
}

std::vector<OpenCMLParser::DeclContext *> OpenCMLParser::ProgramContext::decl() {
  return getRuleContexts<OpenCMLParser::DeclContext>();
}

OpenCMLParser::DeclContext* OpenCMLParser::ProgramContext::decl(size_t i) {
  return getRuleContext<OpenCMLParser::DeclContext>(i);
}

std::vector<OpenCMLParser::StmtContext *> OpenCMLParser::ProgramContext::stmt() {
  return getRuleContexts<OpenCMLParser::StmtContext>();
}

OpenCMLParser::StmtContext* OpenCMLParser::ProgramContext::stmt(size_t i) {
  return getRuleContext<OpenCMLParser::StmtContext>(i);
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
    setState(141);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SEP) {
      setState(140);
      match(OpenCMLParser::SEP);
    }
    setState(152);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -81063964363969976) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 2114975891) != 0)) {
      setState(145);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 1, _ctx)) {
      case 1: {
        setState(143);
        decl();
        break;
      }

      case 2: {
        setState(144);
        stmt();
        break;
      }

      default:
        break;
      }
      setState(148);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::SEP) {
        setState(147);
        match(OpenCMLParser::SEP);
      }
      setState(154);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(155);
    match(OpenCMLParser::EOF);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- DeclContext ------------------------------------------------------------------

OpenCMLParser::DeclContext::DeclContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ModuleDeclContext* OpenCMLParser::DeclContext::moduleDecl() {
  return getRuleContext<OpenCMLParser::ModuleDeclContext>(0);
}

OpenCMLParser::ImportDeclContext* OpenCMLParser::DeclContext::importDecl() {
  return getRuleContext<OpenCMLParser::ImportDeclContext>(0);
}

OpenCMLParser::ExportDeclContext* OpenCMLParser::DeclContext::exportDecl() {
  return getRuleContext<OpenCMLParser::ExportDeclContext>(0);
}

OpenCMLParser::FuncDeclContext* OpenCMLParser::DeclContext::funcDecl() {
  return getRuleContext<OpenCMLParser::FuncDeclContext>(0);
}


size_t OpenCMLParser::DeclContext::getRuleIndex() const {
  return OpenCMLParser::RuleDecl;
}


std::any OpenCMLParser::DeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitDecl(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::DeclContext* OpenCMLParser::decl() {
  DeclContext *_localctx = _tracker.createInstance<DeclContext>(_ctx, getState());
  enterRule(_localctx, 2, OpenCMLParser::RuleDecl);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(161);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 4, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(157);
      moduleDecl();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(158);
      importDecl();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(159);
      exportDecl();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(160);
      funcDecl();
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

OpenCMLParser::EnumStmtContext* OpenCMLParser::StmtContext::enumStmt() {
  return getRuleContext<OpenCMLParser::EnumStmtContext>(0);
}

OpenCMLParser::ExprStmtContext* OpenCMLParser::StmtContext::exprStmt() {
  return getRuleContext<OpenCMLParser::ExprStmtContext>(0);
}

OpenCMLParser::RetStmtContext* OpenCMLParser::StmtContext::retStmt() {
  return getRuleContext<OpenCMLParser::RetStmtContext>(0);
}

OpenCMLParser::StmtBlockContext* OpenCMLParser::StmtContext::stmtBlock() {
  return getRuleContext<OpenCMLParser::StmtBlockContext>(0);
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
    setState(170);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 5, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(163);
      letStmt();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(164);
      useStmt();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(165);
      typeStmt();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(166);
      enumStmt();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(167);
      exprStmt();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(168);
      retStmt();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(169);
      stmtBlock();
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
  enterRule(_localctx, 6, OpenCMLParser::RuleStmtList);
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
    setState(172);
    stmt();
    setState(179);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 7, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(174);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::SEP) {
          setState(173);
          match(OpenCMLParser::SEP);
        }
        setState(176);
        stmt(); 
      }
      setState(181);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 7, _ctx);
    }
    setState(183);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SEP) {
      setState(182);
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

//----------------- ModuleDeclContext ------------------------------------------------------------------

OpenCMLParser::ModuleDeclContext::ModuleDeclContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::ModuleDeclContext::MODULE() {
  return getToken(OpenCMLParser::MODULE, 0);
}

OpenCMLParser::IdentDefContext* OpenCMLParser::ModuleDeclContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
}


size_t OpenCMLParser::ModuleDeclContext::getRuleIndex() const {
  return OpenCMLParser::RuleModuleDecl;
}


std::any OpenCMLParser::ModuleDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitModuleDecl(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ModuleDeclContext* OpenCMLParser::moduleDecl() {
  ModuleDeclContext *_localctx = _tracker.createInstance<ModuleDeclContext>(_ctx, getState());
  enterRule(_localctx, 8, OpenCMLParser::RuleModuleDecl);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(185);
    match(OpenCMLParser::MODULE);
    setState(186);
    identDef();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ImportDeclContext ------------------------------------------------------------------

OpenCMLParser::ImportDeclContext::ImportDeclContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::ImportDeclContext::IMPORT() {
  return getToken(OpenCMLParser::IMPORT, 0);
}

tree::TerminalNode* OpenCMLParser::ImportDeclContext::STRING() {
  return getToken(OpenCMLParser::STRING, 0);
}

tree::TerminalNode* OpenCMLParser::ImportDeclContext::FROM() {
  return getToken(OpenCMLParser::FROM, 0);
}

OpenCMLParser::IdentDefContext* OpenCMLParser::ImportDeclContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
}

OpenCMLParser::BracedIdentsContext* OpenCMLParser::ImportDeclContext::bracedIdents() {
  return getRuleContext<OpenCMLParser::BracedIdentsContext>(0);
}


size_t OpenCMLParser::ImportDeclContext::getRuleIndex() const {
  return OpenCMLParser::RuleImportDecl;
}


std::any OpenCMLParser::ImportDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitImportDecl(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ImportDeclContext* OpenCMLParser::importDecl() {
  ImportDeclContext *_localctx = _tracker.createInstance<ImportDeclContext>(_ctx, getState());
  enterRule(_localctx, 10, OpenCMLParser::RuleImportDecl);

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
    match(OpenCMLParser::IMPORT);
    setState(197);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::STRING: {
        setState(189);
        match(OpenCMLParser::STRING);
        break;
      }

      case OpenCMLParser::T__2:
      case OpenCMLParser::IDENTIFIER: {
        setState(192);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case OpenCMLParser::IDENTIFIER: {
            setState(190);
            identDef();
            break;
          }

          case OpenCMLParser::T__2: {
            setState(191);
            bracedIdents();
            break;
          }

        default:
          throw NoViableAltException(this);
        }
        setState(194);
        match(OpenCMLParser::FROM);
        setState(195);
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

//----------------- ExportDeclContext ------------------------------------------------------------------

OpenCMLParser::ExportDeclContext::ExportDeclContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::ExportDeclContext::EXPORT() {
  return getToken(OpenCMLParser::EXPORT, 0);
}

OpenCMLParser::LetStmtContext* OpenCMLParser::ExportDeclContext::letStmt() {
  return getRuleContext<OpenCMLParser::LetStmtContext>(0);
}

OpenCMLParser::TypeStmtContext* OpenCMLParser::ExportDeclContext::typeStmt() {
  return getRuleContext<OpenCMLParser::TypeStmtContext>(0);
}

OpenCMLParser::BracedIdentsContext* OpenCMLParser::ExportDeclContext::bracedIdents() {
  return getRuleContext<OpenCMLParser::BracedIdentsContext>(0);
}


size_t OpenCMLParser::ExportDeclContext::getRuleIndex() const {
  return OpenCMLParser::RuleExportDecl;
}


std::any OpenCMLParser::ExportDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitExportDecl(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ExportDeclContext* OpenCMLParser::exportDecl() {
  ExportDeclContext *_localctx = _tracker.createInstance<ExportDeclContext>(_ctx, getState());
  enterRule(_localctx, 12, OpenCMLParser::RuleExportDecl);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(199);
    match(OpenCMLParser::EXPORT);
    setState(203);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::LET:
      case OpenCMLParser::VAR: {
        setState(200);
        letStmt();
        break;
      }

      case OpenCMLParser::TYPE: {
        setState(201);
        typeStmt();
        break;
      }

      case OpenCMLParser::T__2: {
        setState(202);
        bracedIdents();
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

//----------------- FuncDeclContext ------------------------------------------------------------------

OpenCMLParser::FuncDeclContext::FuncDeclContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::FuncDeclContext::FUNC() {
  return getToken(OpenCMLParser::FUNC, 0);
}

OpenCMLParser::IdentDefContext* OpenCMLParser::FuncDeclContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
}

OpenCMLParser::ParentParamsContext* OpenCMLParser::FuncDeclContext::parentParams() {
  return getRuleContext<OpenCMLParser::ParentParamsContext>(0);
}

OpenCMLParser::StmtBlockContext* OpenCMLParser::FuncDeclContext::stmtBlock() {
  return getRuleContext<OpenCMLParser::StmtBlockContext>(0);
}

OpenCMLParser::AnnotationsContext* OpenCMLParser::FuncDeclContext::annotations() {
  return getRuleContext<OpenCMLParser::AnnotationsContext>(0);
}

tree::TerminalNode* OpenCMLParser::FuncDeclContext::WITH() {
  return getToken(OpenCMLParser::WITH, 0);
}

OpenCMLParser::AngledParamsContext* OpenCMLParser::FuncDeclContext::angledParams() {
  return getRuleContext<OpenCMLParser::AngledParamsContext>(0);
}

tree::TerminalNode* OpenCMLParser::FuncDeclContext::EXPORT() {
  return getToken(OpenCMLParser::EXPORT, 0);
}

OpenCMLParser::ModifiersContext* OpenCMLParser::FuncDeclContext::modifiers() {
  return getRuleContext<OpenCMLParser::ModifiersContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::FuncDeclContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}


size_t OpenCMLParser::FuncDeclContext::getRuleIndex() const {
  return OpenCMLParser::RuleFuncDecl;
}


std::any OpenCMLParser::FuncDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitFuncDecl(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::FuncDeclContext* OpenCMLParser::funcDecl() {
  FuncDeclContext *_localctx = _tracker.createInstance<FuncDeclContext>(_ctx, getState());
  enterRule(_localctx, 14, OpenCMLParser::RuleFuncDecl);
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
    setState(206);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(205);
      annotations();
    }
    setState(210);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WITH) {
      setState(208);
      match(OpenCMLParser::WITH);
      setState(209);
      angledParams();
    }
    setState(213);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::EXPORT) {
      setState(212);
      match(OpenCMLParser::EXPORT);
    }
    setState(216);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 75) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 75)) & 63) != 0)) {
      setState(215);
      modifiers();
    }
    setState(218);
    match(OpenCMLParser::FUNC);
    setState(219);
    identDef();
    setState(220);
    parentParams();
    setState(223);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(221);
      match(OpenCMLParser::T__0);
      setState(222);
      typeExpr();
    }
    setState(225);
    stmtBlock();
   
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

OpenCMLParser::DataExprContext* OpenCMLParser::LetStmtContext::dataExpr() {
  return getRuleContext<OpenCMLParser::DataExprContext>(0);
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
  enterRule(_localctx, 16, OpenCMLParser::RuleLetStmt);
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
    setState(227);
    _la = _input->LA(1);
    if (!(_la == OpenCMLParser::LET

    || _la == OpenCMLParser::VAR)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(228);
    carrier();
    setState(231);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(229);
      match(OpenCMLParser::T__0);
      setState(230);
      typeExpr();
    }
    setState(233);
    match(OpenCMLParser::T__1);
    setState(234);
    dataExpr();
   
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

OpenCMLParser::IdentRefContext* OpenCMLParser::UseStmtContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::IdentDefContext* OpenCMLParser::UseStmtContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
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
  enterRule(_localctx, 18, OpenCMLParser::RuleUseStmt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(236);
    match(OpenCMLParser::USE);
    setState(240);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 18, _ctx)) {
    case 1: {
      setState(237);
      identDef();
      setState(238);
      match(OpenCMLParser::T__1);
      break;
    }

    default:
      break;
    }
    setState(242);
    identRef();
   
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

OpenCMLParser::IdentDefContext* OpenCMLParser::TypeStmtContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
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
  enterRule(_localctx, 20, OpenCMLParser::RuleTypeStmt);

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
    match(OpenCMLParser::TYPE);
    setState(245);
    identDef();
    setState(246);
    match(OpenCMLParser::T__1);
    setState(247);
    typeExpr();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- EnumStmtContext ------------------------------------------------------------------

OpenCMLParser::EnumStmtContext::EnumStmtContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::EnumStmtContext::ENUM() {
  return getToken(OpenCMLParser::ENUM, 0);
}

OpenCMLParser::IdentDefContext* OpenCMLParser::EnumStmtContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
}

OpenCMLParser::BracedIdentsContext* OpenCMLParser::EnumStmtContext::bracedIdents() {
  return getRuleContext<OpenCMLParser::BracedIdentsContext>(0);
}


size_t OpenCMLParser::EnumStmtContext::getRuleIndex() const {
  return OpenCMLParser::RuleEnumStmt;
}


std::any OpenCMLParser::EnumStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEnumStmt(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::EnumStmtContext* OpenCMLParser::enumStmt() {
  EnumStmtContext *_localctx = _tracker.createInstance<EnumStmtContext>(_ctx, getState());
  enterRule(_localctx, 22, OpenCMLParser::RuleEnumStmt);

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
    match(OpenCMLParser::ENUM);
    setState(250);
    identDef();
    setState(251);
    match(OpenCMLParser::T__1);
    setState(252);
    bracedIdents();
   
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

OpenCMLParser::DataExprContext* OpenCMLParser::ExprStmtContext::dataExpr() {
  return getRuleContext<OpenCMLParser::DataExprContext>(0);
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
  enterRule(_localctx, 24, OpenCMLParser::RuleExprStmt);
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
    setState(255);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(254);
      annotations();
    }
    setState(257);
    dataExpr();
   
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

OpenCMLParser::DataExprContext* OpenCMLParser::RetStmtContext::dataExpr() {
  return getRuleContext<OpenCMLParser::DataExprContext>(0);
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
  enterRule(_localctx, 26, OpenCMLParser::RuleRetStmt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(259);
    match(OpenCMLParser::RETURN);
    setState(260);
    dataExpr();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StmtBlockContext ------------------------------------------------------------------

OpenCMLParser::StmtBlockContext::StmtBlockContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::StmtBlockContext::SYNC() {
  return getToken(OpenCMLParser::SYNC, 0);
}

OpenCMLParser::StmtListContext* OpenCMLParser::StmtBlockContext::stmtList() {
  return getRuleContext<OpenCMLParser::StmtListContext>(0);
}


size_t OpenCMLParser::StmtBlockContext::getRuleIndex() const {
  return OpenCMLParser::RuleStmtBlock;
}


std::any OpenCMLParser::StmtBlockContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitStmtBlock(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::StmtBlockContext* OpenCMLParser::stmtBlock() {
  StmtBlockContext *_localctx = _tracker.createInstance<StmtBlockContext>(_ctx, getState());
  enterRule(_localctx, 28, OpenCMLParser::RuleStmtBlock);
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
    setState(263);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SYNC) {
      setState(262);
      match(OpenCMLParser::SYNC);
    }
    setState(265);
    match(OpenCMLParser::T__2);
    setState(267);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 7845271379808102984) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 132185993) != 0)) {
      setState(266);
      stmtList();
    }
    setState(269);
    match(OpenCMLParser::T__3);
   
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

OpenCMLParser::BlockExprContext* OpenCMLParser::LambdaExprContext::blockExpr() {
  return getRuleContext<OpenCMLParser::BlockExprContext>(0);
}

OpenCMLParser::ModifiersContext* OpenCMLParser::LambdaExprContext::modifiers() {
  return getRuleContext<OpenCMLParser::ModifiersContext>(0);
}

OpenCMLParser::AngledParamsContext* OpenCMLParser::LambdaExprContext::angledParams() {
  return getRuleContext<OpenCMLParser::AngledParamsContext>(0);
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
  enterRule(_localctx, 30, OpenCMLParser::RuleLambdaExpr);
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
    setState(272);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 75) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 75)) & 63) != 0)) {
      setState(271);
      modifiers();
    }
    setState(275);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__12) {
      setState(274);
      angledParams();
    }
    setState(277);
    parentParams();
    setState(280);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(278);
      match(OpenCMLParser::T__0);
      setState(279);
      typeExpr();
    }
    setState(282);
    match(OpenCMLParser::T__4);
    setState(283);
    blockExpr();
   
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

OpenCMLParser::IdentDefContext* OpenCMLParser::CarrierContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
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
  enterRule(_localctx, 32, OpenCMLParser::RuleCarrier);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(288);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(285);
        identDef();
        break;
      }

      case OpenCMLParser::T__2: {
        enterOuterAlt(_localctx, 2);
        setState(286);
        bracedIdents();
        break;
      }

      case OpenCMLParser::T__8: {
        enterOuterAlt(_localctx, 3);
        setState(287);
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

OpenCMLParser::PrimaryDataContext* OpenCMLParser::AnnotationContext::primaryData() {
  return getRuleContext<OpenCMLParser::PrimaryDataContext>(0);
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
  enterRule(_localctx, 34, OpenCMLParser::RuleAnnotation);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(290);
    match(OpenCMLParser::T__5);
    setState(291);
    primaryData();
   
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
  enterRule(_localctx, 36, OpenCMLParser::RuleAnnotations);
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
    setState(294); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(293);
      annotation();
      setState(296); 
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

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::SHARED() {
  return getTokens(OpenCMLParser::SHARED);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::SHARED(size_t i) {
  return getToken(OpenCMLParser::SHARED, i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::SYNC() {
  return getTokens(OpenCMLParser::SYNC);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::SYNC(size_t i) {
  return getToken(OpenCMLParser::SYNC, i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::MACRO() {
  return getTokens(OpenCMLParser::MACRO);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::MACRO(size_t i) {
  return getToken(OpenCMLParser::MACRO, i);
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
  enterRule(_localctx, 38, OpenCMLParser::RuleModifiers);
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
    setState(299); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(298);
      _la = _input->LA(1);
      if (!(((((_la - 75) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 75)) & 63) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(301); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (((((_la - 75) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 75)) & 63) != 0));
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- IndexValueContext ------------------------------------------------------------------

OpenCMLParser::IndexValueContext::IndexValueContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::DataExprContext* OpenCMLParser::IndexValueContext::dataExpr() {
  return getRuleContext<OpenCMLParser::DataExprContext>(0);
}


size_t OpenCMLParser::IndexValueContext::getRuleIndex() const {
  return OpenCMLParser::RuleIndexValue;
}


std::any OpenCMLParser::IndexValueContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitIndexValue(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::IndexValueContext* OpenCMLParser::indexValue() {
  IndexValueContext *_localctx = _tracker.createInstance<IndexValueContext>(_ctx, getState());
  enterRule(_localctx, 40, OpenCMLParser::RuleIndexValue);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(306);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__2:
      case OpenCMLParser::T__8:
      case OpenCMLParser::T__10:
      case OpenCMLParser::T__12:
      case OpenCMLParser::T__31:
      case OpenCMLParser::T__37:
      case OpenCMLParser::T__38:
      case OpenCMLParser::WAIT:
      case OpenCMLParser::IF:
      case OpenCMLParser::MATCH:
      case OpenCMLParser::SYNC:
      case OpenCMLParser::MACRO:
      case OpenCMLParser::INNER:
      case OpenCMLParser::OUTER:
      case OpenCMLParser::ATOMIC:
      case OpenCMLParser::SHARED:
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
        setState(303);
        dataExpr();
        break;
      }

      case OpenCMLParser::T__6: {
        enterOuterAlt(_localctx, 2);
        setState(304);
        match(OpenCMLParser::T__6);
        setState(305);
        dataExpr();
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

//----------------- KeyTypePairContext ------------------------------------------------------------------

OpenCMLParser::KeyTypePairContext::KeyTypePairContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentDefContext* OpenCMLParser::KeyTypePairContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
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
  enterRule(_localctx, 42, OpenCMLParser::RuleKeyTypePair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(308);
    identDef();
    setState(309);
    match(OpenCMLParser::T__0);
    setState(310);
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

OpenCMLParser::IdentDefContext* OpenCMLParser::KeyValuePairContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
}

OpenCMLParser::DataExprContext* OpenCMLParser::KeyValuePairContext::dataExpr() {
  return getRuleContext<OpenCMLParser::DataExprContext>(0);
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
  enterRule(_localctx, 44, OpenCMLParser::RuleKeyValuePair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(318);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(312);
        identDef();
        setState(313);
        match(OpenCMLParser::T__0);
        setState(314);
        dataExpr();
        break;
      }

      case OpenCMLParser::T__6: {
        enterOuterAlt(_localctx, 2);
        setState(316);
        match(OpenCMLParser::T__6);
        setState(317);
        dataExpr();
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

//----------------- KeyParamPairContext ------------------------------------------------------------------

OpenCMLParser::KeyParamPairContext::KeyParamPairContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentDefContext* OpenCMLParser::KeyParamPairContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::KeyParamPairContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

OpenCMLParser::TypeNameContext* OpenCMLParser::KeyParamPairContext::typeName() {
  return getRuleContext<OpenCMLParser::TypeNameContext>(0);
}

tree::TerminalNode* OpenCMLParser::KeyParamPairContext::VAR() {
  return getToken(OpenCMLParser::VAR, 0);
}

OpenCMLParser::AnnotationContext* OpenCMLParser::KeyParamPairContext::annotation() {
  return getRuleContext<OpenCMLParser::AnnotationContext>(0);
}

OpenCMLParser::DataExprContext* OpenCMLParser::KeyParamPairContext::dataExpr() {
  return getRuleContext<OpenCMLParser::DataExprContext>(0);
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
  enterRule(_localctx, 46, OpenCMLParser::RuleKeyParamPair);
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
    setState(321);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR) {
      setState(320);
      match(OpenCMLParser::VAR);
    }
    setState(323);
    identDef();
    setState(325);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(324);
      annotation();
    }
    setState(327);
    match(OpenCMLParser::T__0);
    setState(330);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__2:
      case OpenCMLParser::T__10:
      case OpenCMLParser::T__12:
      case OpenCMLParser::TYPEOF:
      case OpenCMLParser::SYNC:
      case OpenCMLParser::MACRO:
      case OpenCMLParser::INNER:
      case OpenCMLParser::OUTER:
      case OpenCMLParser::ATOMIC:
      case OpenCMLParser::SHARED:
      case OpenCMLParser::INNER_ATOM_TYPE:
      case OpenCMLParser::IDENTIFIER: {
        setState(328);
        typeExpr();
        break;
      }

      case OpenCMLParser::TYPEAS: {
        setState(329);
        typeName();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(334);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(332);
      match(OpenCMLParser::T__1);
      setState(333);
      dataExpr();
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
  enterRule(_localctx, 48, OpenCMLParser::RuleTypeList);
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
    typeExpr();
    setState(341);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__7) {
      setState(337);
      match(OpenCMLParser::T__7);
      setState(338);
      typeExpr();
      setState(343);
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

//----------------- IdentListContext ------------------------------------------------------------------

OpenCMLParser::IdentListContext::IdentListContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::IdentDefContext *> OpenCMLParser::IdentListContext::identDef() {
  return getRuleContexts<OpenCMLParser::IdentDefContext>();
}

OpenCMLParser::IdentDefContext* OpenCMLParser::IdentListContext::identDef(size_t i) {
  return getRuleContext<OpenCMLParser::IdentDefContext>(i);
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
  enterRule(_localctx, 50, OpenCMLParser::RuleIdentList);

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
    setState(344);
    identDef();
    setState(349);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(345);
        match(OpenCMLParser::T__7);
        setState(346);
        identDef(); 
      }
      setState(351);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx);
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

std::vector<OpenCMLParser::DataExprContext *> OpenCMLParser::ValueListContext::dataExpr() {
  return getRuleContexts<OpenCMLParser::DataExprContext>();
}

OpenCMLParser::DataExprContext* OpenCMLParser::ValueListContext::dataExpr(size_t i) {
  return getRuleContext<OpenCMLParser::DataExprContext>(i);
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
  enterRule(_localctx, 52, OpenCMLParser::RuleValueList);

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
    setState(352);
    dataExpr();
    setState(357);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(353);
        match(OpenCMLParser::T__7);
        setState(354);
        dataExpr(); 
      }
      setState(359);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- IndexValuesContext ------------------------------------------------------------------

OpenCMLParser::IndexValuesContext::IndexValuesContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::IndexValueContext *> OpenCMLParser::IndexValuesContext::indexValue() {
  return getRuleContexts<OpenCMLParser::IndexValueContext>();
}

OpenCMLParser::IndexValueContext* OpenCMLParser::IndexValuesContext::indexValue(size_t i) {
  return getRuleContext<OpenCMLParser::IndexValueContext>(i);
}


size_t OpenCMLParser::IndexValuesContext::getRuleIndex() const {
  return OpenCMLParser::RuleIndexValues;
}


std::any OpenCMLParser::IndexValuesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitIndexValues(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::IndexValuesContext* OpenCMLParser::indexValues() {
  IndexValuesContext *_localctx = _tracker.createInstance<IndexValuesContext>(_ctx, getState());
  enterRule(_localctx, 54, OpenCMLParser::RuleIndexValues);

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
    setState(360);
    indexValue();
    setState(365);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 37, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(361);
        match(OpenCMLParser::T__7);
        setState(362);
        indexValue(); 
      }
      setState(367);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 37, _ctx);
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
  enterRule(_localctx, 56, OpenCMLParser::RulePairedTypes);

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
    setState(368);
    keyTypePair();
    setState(373);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 38, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(369);
        match(OpenCMLParser::T__7);
        setState(370);
        keyTypePair(); 
      }
      setState(375);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 38, _ctx);
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
  enterRule(_localctx, 58, OpenCMLParser::RulePairedValues);

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
    setState(376);
    keyValuePair();
    setState(381);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 39, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(377);
        match(OpenCMLParser::T__7);
        setState(378);
        keyValuePair(); 
      }
      setState(383);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 39, _ctx);
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
  enterRule(_localctx, 60, OpenCMLParser::RulePairedParams);

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
    setState(384);
    keyParamPair();
    setState(389);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 40, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(385);
        match(OpenCMLParser::T__7);
        setState(386);
        keyParamPair(); 
      }
      setState(391);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 40, _ctx);
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

OpenCMLParser::IndexValuesContext* OpenCMLParser::ArgumentListContext::indexValues() {
  return getRuleContext<OpenCMLParser::IndexValuesContext>(0);
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
  enterRule(_localctx, 62, OpenCMLParser::RuleArgumentList);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(398);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 42, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(392);
      indexValues();
      setState(395);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 41, _ctx)) {
      case 1: {
        setState(393);
        match(OpenCMLParser::T__7);
        setState(394);
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
      setState(397);
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
  enterRule(_localctx, 64, OpenCMLParser::RuleBracedIdents);
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
    match(OpenCMLParser::T__2);
    setState(402);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(401);
      identList();
    }
    setState(405);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(404);
      match(OpenCMLParser::T__7);
    }
    setState(407);
    match(OpenCMLParser::T__3);
   
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
  enterRule(_localctx, 66, OpenCMLParser::RuleBracketIdents);
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
    match(OpenCMLParser::T__8);
    setState(411);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(410);
      identList();
    }
    setState(414);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(413);
      match(OpenCMLParser::T__7);
    }
    setState(416);
    match(OpenCMLParser::T__9);
   
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

std::vector<OpenCMLParser::DataExprContext *> OpenCMLParser::MemberAccessContext::dataExpr() {
  return getRuleContexts<OpenCMLParser::DataExprContext>();
}

OpenCMLParser::DataExprContext* OpenCMLParser::MemberAccessContext::dataExpr(size_t i) {
  return getRuleContext<OpenCMLParser::DataExprContext>(i);
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
  enterRule(_localctx, 68, OpenCMLParser::RuleMemberAccess);
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
    match(OpenCMLParser::T__8);
    setState(419);
    dataExpr();
    setState(426);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(420);
      match(OpenCMLParser::T__0);
      setState(421);
      dataExpr();
      setState(424);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__0) {
        setState(422);
        match(OpenCMLParser::T__0);
        setState(423);
        dataExpr();
      }
    }
    setState(428);
    match(OpenCMLParser::T__9);
   
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
  enterRule(_localctx, 70, OpenCMLParser::RuleParentParams);
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
    setState(430);
    match(OpenCMLParser::T__10);
    setState(432);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(431);
      pairedParams();
    }
    setState(435);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(434);
      match(OpenCMLParser::T__7);
    }
    setState(437);
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
  enterRule(_localctx, 72, OpenCMLParser::RuleParentArgues);
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
    setState(439);
    match(OpenCMLParser::T__10);
    setState(441);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2305843838142392968) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 132185993) != 0)) {
      setState(440);
      argumentList();
    }
    setState(444);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(443);
      match(OpenCMLParser::T__7);
    }
    setState(446);
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
  enterRule(_localctx, 74, OpenCMLParser::RuleParentValues);
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
    setState(448);
    match(OpenCMLParser::T__10);
    setState(450);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2305843838142392840) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 132185993) != 0)) {
      setState(449);
      valueList();
    }
    setState(453);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(452);
      match(OpenCMLParser::T__7);
    }
    setState(455);
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
  enterRule(_localctx, 76, OpenCMLParser::RuleAngledParams);
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
    setState(457);
    match(OpenCMLParser::T__12);
    setState(459);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(458);
      pairedParams();
    }
    setState(462);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(461);
      match(OpenCMLParser::T__7);
    }
    setState(464);
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
  enterRule(_localctx, 78, OpenCMLParser::RuleAngledValues);
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
    match(OpenCMLParser::T__12);
    setState(468);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2305843838142392968) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 132185993) != 0)) {
      setState(467);
      argumentList();
    }
    setState(471);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(470);
      match(OpenCMLParser::T__7);
    }
    setState(473);
    match(OpenCMLParser::T__13);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BlockExprContext ------------------------------------------------------------------

OpenCMLParser::BlockExprContext::BlockExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::StmtBlockContext* OpenCMLParser::BlockExprContext::stmtBlock() {
  return getRuleContext<OpenCMLParser::StmtBlockContext>(0);
}

OpenCMLParser::DataExprContext* OpenCMLParser::BlockExprContext::dataExpr() {
  return getRuleContext<OpenCMLParser::DataExprContext>(0);
}


size_t OpenCMLParser::BlockExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleBlockExpr;
}


std::any OpenCMLParser::BlockExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBlockExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BlockExprContext* OpenCMLParser::blockExpr() {
  BlockExprContext *_localctx = _tracker.createInstance<BlockExprContext>(_ctx, getState());
  enterRule(_localctx, 80, OpenCMLParser::RuleBlockExpr);

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
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 59, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(475);
      stmtBlock();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(476);
      dataExpr();
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

//----------------- DataExprContext ------------------------------------------------------------------

OpenCMLParser::DataExprContext::DataExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::StructExprContext *> OpenCMLParser::DataExprContext::structExpr() {
  return getRuleContexts<OpenCMLParser::StructExprContext>();
}

OpenCMLParser::StructExprContext* OpenCMLParser::DataExprContext::structExpr(size_t i) {
  return getRuleContext<OpenCMLParser::StructExprContext>(i);
}

tree::TerminalNode* OpenCMLParser::DataExprContext::WAIT() {
  return getToken(OpenCMLParser::WAIT, 0);
}


size_t OpenCMLParser::DataExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleDataExpr;
}


std::any OpenCMLParser::DataExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitDataExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::DataExprContext* OpenCMLParser::dataExpr() {
  DataExprContext *_localctx = _tracker.createInstance<DataExprContext>(_ctx, getState());
  enterRule(_localctx, 82, OpenCMLParser::RuleDataExpr);
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
    setState(480);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WAIT) {
      setState(479);
      match(OpenCMLParser::WAIT);
    }
    setState(482);
    structExpr();
    setState(485);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx)) {
    case 1: {
      setState(483);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8355844) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(484);
      structExpr();
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

//----------------- StructExprContext ------------------------------------------------------------------

OpenCMLParser::StructExprContext::StructExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::LogicalOrExprContext* OpenCMLParser::StructExprContext::logicalOrExpr() {
  return getRuleContext<OpenCMLParser::LogicalOrExprContext>(0);
}

tree::TerminalNode* OpenCMLParser::StructExprContext::IF() {
  return getToken(OpenCMLParser::IF, 0);
}

tree::TerminalNode* OpenCMLParser::StructExprContext::THEN() {
  return getToken(OpenCMLParser::THEN, 0);
}

std::vector<OpenCMLParser::BlockExprContext *> OpenCMLParser::StructExprContext::blockExpr() {
  return getRuleContexts<OpenCMLParser::BlockExprContext>();
}

OpenCMLParser::BlockExprContext* OpenCMLParser::StructExprContext::blockExpr(size_t i) {
  return getRuleContext<OpenCMLParser::BlockExprContext>(i);
}

tree::TerminalNode* OpenCMLParser::StructExprContext::ELSE() {
  return getToken(OpenCMLParser::ELSE, 0);
}

tree::TerminalNode* OpenCMLParser::StructExprContext::MATCH() {
  return getToken(OpenCMLParser::MATCH, 0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::StructExprContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

std::vector<tree::TerminalNode *> OpenCMLParser::StructExprContext::CASE() {
  return getTokens(OpenCMLParser::CASE);
}

tree::TerminalNode* OpenCMLParser::StructExprContext::CASE(size_t i) {
  return getToken(OpenCMLParser::CASE, i);
}

std::vector<OpenCMLParser::DataExprContext *> OpenCMLParser::StructExprContext::dataExpr() {
  return getRuleContexts<OpenCMLParser::DataExprContext>();
}

OpenCMLParser::DataExprContext* OpenCMLParser::StructExprContext::dataExpr(size_t i) {
  return getRuleContext<OpenCMLParser::DataExprContext>(i);
}


size_t OpenCMLParser::StructExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleStructExpr;
}


std::any OpenCMLParser::StructExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitStructExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::StructExprContext* OpenCMLParser::structExpr() {
  StructExprContext *_localctx = _tracker.createInstance<StructExprContext>(_ctx, getState());
  enterRule(_localctx, 84, OpenCMLParser::RuleStructExpr);
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
      case OpenCMLParser::T__2:
      case OpenCMLParser::T__8:
      case OpenCMLParser::T__10:
      case OpenCMLParser::T__12:
      case OpenCMLParser::T__31:
      case OpenCMLParser::T__37:
      case OpenCMLParser::T__38:
      case OpenCMLParser::SYNC:
      case OpenCMLParser::MACRO:
      case OpenCMLParser::INNER:
      case OpenCMLParser::OUTER:
      case OpenCMLParser::ATOMIC:
      case OpenCMLParser::SHARED:
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
        setState(487);
        logicalOrExpr();
        break;
      }

      case OpenCMLParser::IF: {
        enterOuterAlt(_localctx, 2);
        setState(488);
        match(OpenCMLParser::IF);
        setState(489);
        logicalOrExpr();
        setState(490);
        match(OpenCMLParser::THEN);
        setState(491);
        blockExpr();
        setState(492);
        match(OpenCMLParser::ELSE);
        setState(493);
        blockExpr();
        break;
      }

      case OpenCMLParser::MATCH: {
        enterOuterAlt(_localctx, 3);
        setState(495);
        match(OpenCMLParser::MATCH);
        setState(496);
        identRef();
        setState(497);
        match(OpenCMLParser::T__2);
        setState(503); 
        _errHandler->sync(this);
        _la = _input->LA(1);
        do {
          setState(498);
          match(OpenCMLParser::CASE);
          setState(499);
          dataExpr();
          setState(500);
          match(OpenCMLParser::T__4);
          setState(501);
          blockExpr();
          setState(505); 
          _errHandler->sync(this);
          _la = _input->LA(1);
        } while (_la == OpenCMLParser::CASE);
        setState(507);
        match(OpenCMLParser::T__3);
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
  enterRule(_localctx, 86, OpenCMLParser::RuleLogicalOrExpr);

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
    logicalAndExpr();
    setState(516);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 64, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(512);
        match(OpenCMLParser::T__22);
        setState(513);
        logicalAndExpr(); 
      }
      setState(518);
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
  enterRule(_localctx, 88, OpenCMLParser::RuleLogicalAndExpr);

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
    equalityExpr();
    setState(524);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 65, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(520);
        match(OpenCMLParser::T__23);
        setState(521);
        equalityExpr(); 
      }
      setState(526);
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
  enterRule(_localctx, 90, OpenCMLParser::RuleEqualityExpr);
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
    setState(527);
    relationalExpr();
    setState(532);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 66, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(528);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 503316480) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(529);
        relationalExpr(); 
      }
      setState(534);
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
  enterRule(_localctx, 92, OpenCMLParser::RuleRelationalExpr);
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
    setState(535);
    additiveExpr();
    setState(540);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 67, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(536);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 1610637312) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(537);
        additiveExpr(); 
      }
      setState(542);
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
  enterRule(_localctx, 94, OpenCMLParser::RuleAdditiveExpr);
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
    setState(543);
    multiplicativeExpr();
    setState(548);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 68, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(544);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__30

        || _la == OpenCMLParser::T__31)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(545);
        multiplicativeExpr(); 
      }
      setState(550);
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

//----------------- MultiplicativeExprContext ------------------------------------------------------------------

OpenCMLParser::MultiplicativeExprContext::MultiplicativeExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::NullableExprContext *> OpenCMLParser::MultiplicativeExprContext::nullableExpr() {
  return getRuleContexts<OpenCMLParser::NullableExprContext>();
}

OpenCMLParser::NullableExprContext* OpenCMLParser::MultiplicativeExprContext::nullableExpr(size_t i) {
  return getRuleContext<OpenCMLParser::NullableExprContext>(i);
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
  enterRule(_localctx, 96, OpenCMLParser::RuleMultiplicativeExpr);
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
    setState(551);
    nullableExpr();
    setState(556);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(552);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 128849018880) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(553);
        nullableExpr(); 
      }
      setState(558);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- NullableExprContext ------------------------------------------------------------------

OpenCMLParser::NullableExprContext::NullableExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::UnaryExprContext* OpenCMLParser::NullableExprContext::unaryExpr() {
  return getRuleContext<OpenCMLParser::UnaryExprContext>(0);
}

OpenCMLParser::DataExprContext* OpenCMLParser::NullableExprContext::dataExpr() {
  return getRuleContext<OpenCMLParser::DataExprContext>(0);
}


size_t OpenCMLParser::NullableExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleNullableExpr;
}


std::any OpenCMLParser::NullableExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitNullableExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::NullableExprContext* OpenCMLParser::nullableExpr() {
  NullableExprContext *_localctx = _tracker.createInstance<NullableExprContext>(_ctx, getState());
  enterRule(_localctx, 98, OpenCMLParser::RuleNullableExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(559);
    unaryExpr();
    setState(562);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx)) {
    case 1: {
      setState(560);
      match(OpenCMLParser::T__36);
      setState(561);
      dataExpr();
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

//----------------- UnaryExprContext ------------------------------------------------------------------

OpenCMLParser::UnaryExprContext::UnaryExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::LinkExprContext* OpenCMLParser::UnaryExprContext::linkExpr() {
  return getRuleContext<OpenCMLParser::LinkExprContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::UnaryExprContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

tree::TerminalNode* OpenCMLParser::UnaryExprContext::AS() {
  return getToken(OpenCMLParser::AS, 0);
}

tree::TerminalNode* OpenCMLParser::UnaryExprContext::IS() {
  return getToken(OpenCMLParser::IS, 0);
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
    setState(571);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__2:
      case OpenCMLParser::T__8:
      case OpenCMLParser::T__10:
      case OpenCMLParser::T__12:
      case OpenCMLParser::SYNC:
      case OpenCMLParser::MACRO:
      case OpenCMLParser::INNER:
      case OpenCMLParser::OUTER:
      case OpenCMLParser::ATOMIC:
      case OpenCMLParser::SHARED:
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
        setState(564);
        linkExpr();
        setState(567);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 71, _ctx)) {
        case 1: {
          setState(565);
          _la = _input->LA(1);
          if (!(_la == OpenCMLParser::AS

          || _la == OpenCMLParser::IS)) {
          _errHandler->recoverInline(this);
          }
          else {
            _errHandler->reportMatch(this);
            consume();
          }
          setState(566);
          typeExpr();
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::T__31:
      case OpenCMLParser::T__37:
      case OpenCMLParser::T__38: {
        enterOuterAlt(_localctx, 2);
        setState(569);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 828928688128) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(570);
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

std::vector<OpenCMLParser::BindExprContext *> OpenCMLParser::LinkExprContext::bindExpr() {
  return getRuleContexts<OpenCMLParser::BindExprContext>();
}

OpenCMLParser::BindExprContext* OpenCMLParser::LinkExprContext::bindExpr(size_t i) {
  return getRuleContext<OpenCMLParser::BindExprContext>(i);
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
    setState(573);
    bindExpr();
    setState(578);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 73, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(574);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__39

        || _la == OpenCMLParser::T__40)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(575);
        bindExpr(); 
      }
      setState(580);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 73, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BindExprContext ------------------------------------------------------------------

OpenCMLParser::BindExprContext::BindExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::WithExprContext *> OpenCMLParser::BindExprContext::withExpr() {
  return getRuleContexts<OpenCMLParser::WithExprContext>();
}

OpenCMLParser::WithExprContext* OpenCMLParser::BindExprContext::withExpr(size_t i) {
  return getRuleContext<OpenCMLParser::WithExprContext>(i);
}


size_t OpenCMLParser::BindExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleBindExpr;
}


std::any OpenCMLParser::BindExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBindExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BindExprContext* OpenCMLParser::bindExpr() {
  BindExprContext *_localctx = _tracker.createInstance<BindExprContext>(_ctx, getState());
  enterRule(_localctx, 104, OpenCMLParser::RuleBindExpr);
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
    setState(581);
    withExpr();
    setState(586);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(582);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__41

        || _la == OpenCMLParser::T__42)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(583);
        withExpr(); 
      }
      setState(588);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx);
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
  enterRule(_localctx, 106, OpenCMLParser::RuleWithExpr);
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
    setState(589);
    annotatedExpr();
    setState(594);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 75, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(590);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__43

        || _la == OpenCMLParser::T__44)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(591);
        annotatedExpr(); 
      }
      setState(596);
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

//----------------- AnnotatedExprContext ------------------------------------------------------------------

OpenCMLParser::AnnotatedExprContext::AnnotatedExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimaryDataContext* OpenCMLParser::AnnotatedExprContext::primaryData() {
  return getRuleContext<OpenCMLParser::PrimaryDataContext>(0);
}

std::vector<OpenCMLParser::AnnotationContext *> OpenCMLParser::AnnotatedExprContext::annotation() {
  return getRuleContexts<OpenCMLParser::AnnotationContext>();
}

OpenCMLParser::AnnotationContext* OpenCMLParser::AnnotatedExprContext::annotation(size_t i) {
  return getRuleContext<OpenCMLParser::AnnotationContext>(i);
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
  enterRule(_localctx, 108, OpenCMLParser::RuleAnnotatedExpr);

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
    setState(597);
    primaryData();
    setState(607);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 78, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(605);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 77, _ctx)) {
        case 1: {
          setState(598);

          if (!(isAdjacent())) throw FailedPredicateException(this, "isAdjacent()");
          setState(602);
          _errHandler->sync(this);
          switch (_input->LA(1)) {
            case OpenCMLParser::T__8: {
              setState(599);
              memberAccess();
              break;
            }

            case OpenCMLParser::T__10: {
              setState(600);
              parentArgues();
              break;
            }

            case OpenCMLParser::T__12: {
              setState(601);
              angledValues();
              break;
            }

          default:
            throw NoViableAltException(this);
          }
          break;
        }

        case 2: {
          setState(604);
          annotation();
          break;
        }

        default:
          break;
        } 
      }
      setState(609);
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

//----------------- DictExprContext ------------------------------------------------------------------

OpenCMLParser::DictExprContext::DictExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PairedValuesContext* OpenCMLParser::DictExprContext::pairedValues() {
  return getRuleContext<OpenCMLParser::PairedValuesContext>(0);
}


size_t OpenCMLParser::DictExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleDictExpr;
}


std::any OpenCMLParser::DictExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitDictExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::DictExprContext* OpenCMLParser::dictExpr() {
  DictExprContext *_localctx = _tracker.createInstance<DictExprContext>(_ctx, getState());
  enterRule(_localctx, 110, OpenCMLParser::RuleDictExpr);
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
    setState(610);
    match(OpenCMLParser::T__2);
    setState(615);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6 || _la == OpenCMLParser::IDENTIFIER) {
      setState(611);
      pairedValues();
      setState(613);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__7) {
        setState(612);
        match(OpenCMLParser::T__7);
      }
    }
    setState(617);
    match(OpenCMLParser::T__3);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ListExprContext ------------------------------------------------------------------

OpenCMLParser::ListExprContext::ListExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::DataExprContext *> OpenCMLParser::ListExprContext::dataExpr() {
  return getRuleContexts<OpenCMLParser::DataExprContext>();
}

OpenCMLParser::DataExprContext* OpenCMLParser::ListExprContext::dataExpr(size_t i) {
  return getRuleContext<OpenCMLParser::DataExprContext>(i);
}

tree::TerminalNode* OpenCMLParser::ListExprContext::FOR() {
  return getToken(OpenCMLParser::FOR, 0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::ListExprContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

tree::TerminalNode* OpenCMLParser::ListExprContext::IN() {
  return getToken(OpenCMLParser::IN, 0);
}

OpenCMLParser::IndexValuesContext* OpenCMLParser::ListExprContext::indexValues() {
  return getRuleContext<OpenCMLParser::IndexValuesContext>(0);
}

tree::TerminalNode* OpenCMLParser::ListExprContext::IF() {
  return getToken(OpenCMLParser::IF, 0);
}


size_t OpenCMLParser::ListExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleListExpr;
}


std::any OpenCMLParser::ListExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitListExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ListExprContext* OpenCMLParser::listExpr() {
  ListExprContext *_localctx = _tracker.createInstance<ListExprContext>(_ctx, getState());
  enterRule(_localctx, 112, OpenCMLParser::RuleListExpr);
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
    setState(619);
    match(OpenCMLParser::T__8);
    setState(633);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 83, _ctx)) {
    case 1: {
      setState(620);
      indexValues();
      setState(622);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__7) {
        setState(621);
        match(OpenCMLParser::T__7);
      }
      break;
    }

    case 2: {
      setState(624);
      dataExpr();
      setState(625);
      match(OpenCMLParser::FOR);
      setState(626);
      identRef();
      setState(627);
      match(OpenCMLParser::IN);
      setState(628);
      dataExpr();
      setState(631);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::IF) {
        setState(629);
        match(OpenCMLParser::IF);
        setState(630);
        dataExpr();
      }
      break;
    }

    default:
      break;
    }
    setState(635);
    match(OpenCMLParser::T__9);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrimaryDataContext ------------------------------------------------------------------

OpenCMLParser::PrimaryDataContext::PrimaryDataContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::PrimaryDataContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::LiteralContext* OpenCMLParser::PrimaryDataContext::literal() {
  return getRuleContext<OpenCMLParser::LiteralContext>(0);
}

OpenCMLParser::ListExprContext* OpenCMLParser::PrimaryDataContext::listExpr() {
  return getRuleContext<OpenCMLParser::ListExprContext>(0);
}

OpenCMLParser::DictExprContext* OpenCMLParser::PrimaryDataContext::dictExpr() {
  return getRuleContext<OpenCMLParser::DictExprContext>(0);
}

OpenCMLParser::DataExprContext* OpenCMLParser::PrimaryDataContext::dataExpr() {
  return getRuleContext<OpenCMLParser::DataExprContext>(0);
}

OpenCMLParser::ParentValuesContext* OpenCMLParser::PrimaryDataContext::parentValues() {
  return getRuleContext<OpenCMLParser::ParentValuesContext>(0);
}

OpenCMLParser::LambdaExprContext* OpenCMLParser::PrimaryDataContext::lambdaExpr() {
  return getRuleContext<OpenCMLParser::LambdaExprContext>(0);
}


size_t OpenCMLParser::PrimaryDataContext::getRuleIndex() const {
  return OpenCMLParser::RulePrimaryData;
}


std::any OpenCMLParser::PrimaryDataContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitPrimaryData(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::PrimaryDataContext* OpenCMLParser::primaryData() {
  PrimaryDataContext *_localctx = _tracker.createInstance<PrimaryDataContext>(_ctx, getState());
  enterRule(_localctx, 114, OpenCMLParser::RulePrimaryData);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(647);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 84, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(637);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(638);
      literal();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(639);
      listExpr();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(640);
      dictExpr();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(641);
      match(OpenCMLParser::T__10);
      setState(642);
      dataExpr();
      setState(643);
      match(OpenCMLParser::T__11);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(645);
      parentValues();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(646);
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
  enterRule(_localctx, 116, OpenCMLParser::RuleLiteral);
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
    setState(649);
    _la = _input->LA(1);
    if (!(((((_la - 81) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 81)) & 15623) != 0))) {
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

//----------------- TypeNameContext ------------------------------------------------------------------

OpenCMLParser::TypeNameContext::TypeNameContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::TypeNameContext::TYPEAS() {
  return getToken(OpenCMLParser::TYPEAS, 0);
}

OpenCMLParser::IdentDefContext* OpenCMLParser::TypeNameContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
}


size_t OpenCMLParser::TypeNameContext::getRuleIndex() const {
  return OpenCMLParser::RuleTypeName;
}


std::any OpenCMLParser::TypeNameContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitTypeName(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::TypeNameContext* OpenCMLParser::typeName() {
  TypeNameContext *_localctx = _tracker.createInstance<TypeNameContext>(_ctx, getState());
  enterRule(_localctx, 118, OpenCMLParser::RuleTypeName);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(651);
    match(OpenCMLParser::TYPEAS);
    setState(652);
    identDef();
   
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

OpenCMLParser::UnionTypeContext* OpenCMLParser::TypeExprContext::unionType() {
  return getRuleContext<OpenCMLParser::UnionTypeContext>(0);
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
  enterRule(_localctx, 120, OpenCMLParser::RuleTypeExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(654);
    unionType();
    setState(656);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 85, _ctx)) {
    case 1: {
      setState(655);
      match(OpenCMLParser::T__45);
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

//----------------- UnionTypeContext ------------------------------------------------------------------

OpenCMLParser::UnionTypeContext::UnionTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::ListTypeContext *> OpenCMLParser::UnionTypeContext::listType() {
  return getRuleContexts<OpenCMLParser::ListTypeContext>();
}

OpenCMLParser::ListTypeContext* OpenCMLParser::UnionTypeContext::listType(size_t i) {
  return getRuleContext<OpenCMLParser::ListTypeContext>(i);
}


size_t OpenCMLParser::UnionTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleUnionType;
}


std::any OpenCMLParser::UnionTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitUnionType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::UnionTypeContext* OpenCMLParser::unionType() {
  UnionTypeContext *_localctx = _tracker.createInstance<UnionTypeContext>(_ctx, getState());
  enterRule(_localctx, 122, OpenCMLParser::RuleUnionType);

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
    setState(658);
    listType();
    setState(663);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 86, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(659);
        match(OpenCMLParser::T__46);
        setState(660);
        listType(); 
      }
      setState(665);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 86, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ListTypeContext ------------------------------------------------------------------

OpenCMLParser::ListTypeContext::ListTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ArgsTypeContext* OpenCMLParser::ListTypeContext::argsType() {
  return getRuleContext<OpenCMLParser::ArgsTypeContext>(0);
}


size_t OpenCMLParser::ListTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleListType;
}


std::any OpenCMLParser::ListTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitListType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ListTypeContext* OpenCMLParser::listType() {
  ListTypeContext *_localctx = _tracker.createInstance<ListTypeContext>(_ctx, getState());
  enterRule(_localctx, 124, OpenCMLParser::RuleListType);

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
    setState(666);
    argsType();
    setState(671);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 87, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(667);
        match(OpenCMLParser::T__8);
        setState(668);
        match(OpenCMLParser::T__9); 
      }
      setState(673);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 87, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ArgsTypeContext ------------------------------------------------------------------

OpenCMLParser::ArgsTypeContext::ArgsTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimaryTypeContext* OpenCMLParser::ArgsTypeContext::primaryType() {
  return getRuleContext<OpenCMLParser::PrimaryTypeContext>(0);
}

OpenCMLParser::TypeListContext* OpenCMLParser::ArgsTypeContext::typeList() {
  return getRuleContext<OpenCMLParser::TypeListContext>(0);
}


size_t OpenCMLParser::ArgsTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleArgsType;
}


std::any OpenCMLParser::ArgsTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitArgsType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ArgsTypeContext* OpenCMLParser::argsType() {
  ArgsTypeContext *_localctx = _tracker.createInstance<ArgsTypeContext>(_ctx, getState());
  enterRule(_localctx, 126, OpenCMLParser::RuleArgsType);
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
    setState(674);
    primaryType();
    setState(680);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 89, _ctx)) {
    case 1: {
      setState(675);
      match(OpenCMLParser::T__12);
      setState(677);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 10248) != 0) || ((((_la - 67) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 67)) & 8535809) != 0)) {
        setState(676);
        typeList();
      }
      setState(679);
      match(OpenCMLParser::T__13);
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

//----------------- PrimaryTypeContext ------------------------------------------------------------------

OpenCMLParser::PrimaryTypeContext::PrimaryTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::PrimaryTypeContext::INNER_ATOM_TYPE() {
  return getToken(OpenCMLParser::INNER_ATOM_TYPE, 0);
}

OpenCMLParser::DictExprTypeContext* OpenCMLParser::PrimaryTypeContext::dictExprType() {
  return getRuleContext<OpenCMLParser::DictExprTypeContext>(0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::PrimaryTypeContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::PrimaryTypeContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

OpenCMLParser::LambdaTypeContext* OpenCMLParser::PrimaryTypeContext::lambdaType() {
  return getRuleContext<OpenCMLParser::LambdaTypeContext>(0);
}

tree::TerminalNode* OpenCMLParser::PrimaryTypeContext::TYPEOF() {
  return getToken(OpenCMLParser::TYPEOF, 0);
}

OpenCMLParser::DataExprContext* OpenCMLParser::PrimaryTypeContext::dataExpr() {
  return getRuleContext<OpenCMLParser::DataExprContext>(0);
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
  enterRule(_localctx, 128, OpenCMLParser::RulePrimaryType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(692);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 90, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(682);
      match(OpenCMLParser::INNER_ATOM_TYPE);
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(683);
      dictExprType();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(684);
      identRef();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(685);
      match(OpenCMLParser::T__10);
      setState(686);
      typeExpr();
      setState(687);
      match(OpenCMLParser::T__11);
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(689);
      lambdaType();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(690);
      match(OpenCMLParser::TYPEOF);
      setState(691);
      dataExpr();
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

//----------------- DictExprTypeContext ------------------------------------------------------------------

OpenCMLParser::DictExprTypeContext::DictExprTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::DictTypeContext *> OpenCMLParser::DictExprTypeContext::dictType() {
  return getRuleContexts<OpenCMLParser::DictTypeContext>();
}

OpenCMLParser::DictTypeContext* OpenCMLParser::DictExprTypeContext::dictType(size_t i) {
  return getRuleContext<OpenCMLParser::DictTypeContext>(i);
}


size_t OpenCMLParser::DictExprTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleDictExprType;
}


std::any OpenCMLParser::DictExprTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitDictExprType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::DictExprTypeContext* OpenCMLParser::dictExprType() {
  DictExprTypeContext *_localctx = _tracker.createInstance<DictExprTypeContext>(_ctx, getState());
  enterRule(_localctx, 130, OpenCMLParser::RuleDictExprType);
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
    setState(694);
    dictType();
    setState(699);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 91, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(695);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__32

        || _la == OpenCMLParser::T__47)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(696);
        dictType(); 
      }
      setState(701);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 91, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- DictTypeContext ------------------------------------------------------------------

OpenCMLParser::DictTypeContext::DictTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PairedTypesContext* OpenCMLParser::DictTypeContext::pairedTypes() {
  return getRuleContext<OpenCMLParser::PairedTypesContext>(0);
}


size_t OpenCMLParser::DictTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleDictType;
}


std::any OpenCMLParser::DictTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitDictType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::DictTypeContext* OpenCMLParser::dictType() {
  DictTypeContext *_localctx = _tracker.createInstance<DictTypeContext>(_ctx, getState());
  enterRule(_localctx, 132, OpenCMLParser::RuleDictType);
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
    setState(702);
    match(OpenCMLParser::T__2);
    setState(704);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(703);
      pairedTypes();
    }
    setState(707);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(706);
      match(OpenCMLParser::T__7);
    }
    setState(709);
    match(OpenCMLParser::T__3);
   
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

OpenCMLParser::ParentParamsContext* OpenCMLParser::LambdaTypeContext::parentParams() {
  return getRuleContext<OpenCMLParser::ParentParamsContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::LambdaTypeContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

OpenCMLParser::ModifiersContext* OpenCMLParser::LambdaTypeContext::modifiers() {
  return getRuleContext<OpenCMLParser::ModifiersContext>(0);
}

OpenCMLParser::AngledParamsContext* OpenCMLParser::LambdaTypeContext::angledParams() {
  return getRuleContext<OpenCMLParser::AngledParamsContext>(0);
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
  enterRule(_localctx, 134, OpenCMLParser::RuleLambdaType);
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
    setState(712);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 75) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 75)) & 63) != 0)) {
      setState(711);
      modifiers();
    }
    setState(715);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__12) {
      setState(714);
      angledParams();
    }
    setState(717);
    parentParams();
    setState(718);
    match(OpenCMLParser::T__4);
    setState(719);
    typeExpr();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- IdentDefContext ------------------------------------------------------------------

OpenCMLParser::IdentDefContext::IdentDefContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::IdentDefContext::IDENTIFIER() {
  return getToken(OpenCMLParser::IDENTIFIER, 0);
}


size_t OpenCMLParser::IdentDefContext::getRuleIndex() const {
  return OpenCMLParser::RuleIdentDef;
}


std::any OpenCMLParser::IdentDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitIdentDef(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::IdentDefContext* OpenCMLParser::identDef() {
  IdentDefContext *_localctx = _tracker.createInstance<IdentDefContext>(_ctx, getState());
  enterRule(_localctx, 136, OpenCMLParser::RuleIdentDef);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(721);
    match(OpenCMLParser::IDENTIFIER);
   
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

std::vector<tree::TerminalNode *> OpenCMLParser::IdentRefContext::IDENTIFIER() {
  return getTokens(OpenCMLParser::IDENTIFIER);
}

tree::TerminalNode* OpenCMLParser::IdentRefContext::IDENTIFIER(size_t i) {
  return getToken(OpenCMLParser::IDENTIFIER, i);
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
  enterRule(_localctx, 138, OpenCMLParser::RuleIdentRef);

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
    setState(727);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 96, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(723);
        match(OpenCMLParser::IDENTIFIER);
        setState(724);
        match(OpenCMLParser::T__48); 
      }
      setState(729);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 96, _ctx);
    }
    setState(730);
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
    case 54: return annotatedExprSempred(antlrcpp::downCast<AnnotatedExprContext *>(context), predicateIndex);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::annotatedExprSempred(AnnotatedExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 0: return isAdjacent();

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
