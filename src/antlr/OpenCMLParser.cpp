
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
      "exportDecl", "stmtBlock", "blockExpr", "blockStmt", "lambdaExpr", 
      "funcDecl", "parentIdents", "bracedIdents", "bracketIdents", "carrier", 
      "letDecl", "useDecl", "retStmt", "typeDecl", "enumDecl", "exprStmt", 
      "annotation", "annotations", "modifiers", "indexValue", "keyTypePair", 
      "keyValuePair", "keyParamPair", "identList", "valueList", "indexValues", 
      "pairedValues", "pairedParams", "argumentList", "memberAccess", "parentParams", 
      "parentArgues", "angledParams", "angledValues", "dataExpr", "pattern", 
      "matchCase", "catchClause", "structExpr", "logicalOrExpr", "logicalAndExpr", 
      "equalityExpr", "relationalExpr", "additiveExpr", "multiplicativeExpr", 
      "nullableExpr", "unaryExpr", "linkExpr", "bindExpr", "withExpr", "annoExpr", 
      "dictExpr", "listExpr", "primaryData", "literal", "typeExpr", "unionType", 
      "unionUnit", "listType", "typeOrData", "argsType", "primaryType", 
      "dictExprType", "dictType", "tupleType", "lambdaType", "identDef", 
      "identRef"
    },
    std::vector<std::string>{
      "", "'{'", "'}'", "':'", "'=>'", "'('", "','", "')'", "'['", "']'", 
      "'='", "'@'", "'...'", "'<'", "'>'", "'+='", "'-='", "'*='", "'/='", 
      "'%='", "'^='", "'&='", "'|='", "'_'", "'|'", "'||'", "'&&'", "'==='", 
      "'!=='", "'=='", "'!='", "'<='", "'>='", "'+'", "'-'", "'^'", "'*'", 
      "'/'", "'%'", "'\\u003F\\u003F'", "'!!'", "'!'", "'~'", "'->'", "'\\u003F->'", 
      "'..'", "'\\u003F..'", "'.'", "'\\u003F.'", "'\\u003F'", "'&'", "'::'", 
      "';'", "'as'", "'is'", "'let'", "'var'", "'use'", "'from'", "'func'", 
      "'type'", "'enum'", "'with'", "'wait'", "'return'", "'module'", "'import'", 
      "'export'", "'typeas'", "'typeof'", "'if'", "'then'", "'else'", "'match'", 
      "'case'", "'for'", "'in'", "'of'", "'try'", "'catch'", "'finally'", 
      "'throw'", "'raise'", "'sync'", "'macro'", "'inner'", "'outer'", "'atomic'", 
      "'shared'", "'null'", "'true'", "'false'"
    },
    std::vector<std::string>{
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "SEP", "AS", "IS", "LET", "VAR", "USE", "FROM", "FUNC", "TYPE", 
      "ENUM", "WITH", "WAIT", "RETURN", "MODULE", "IMPORT", "EXPORT", "TYPEAS", 
      "TYPEOF", "IF", "THEN", "ELSE", "MATCH", "CASE", "FOR", "IN", "OF", 
      "TRY", "CATCH", "FINALLY", "THROW", "RAISE", "SYNC", "MACRO", "INNER", 
      "OUTER", "ATOMIC", "SHARED", "NULL", "TRUE", "FALSE", "INNER_ATOM_TYPE", 
      "BLANK", "COMMAND", "LIN_CMT", "BLK_CMT", "MULTI_STR", "IDENTIFIER", 
      "STRING", "FSTRING", "INTEGER", "REAL"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,102,840,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,2,55,7,55,2,56,7,
  	56,2,57,7,57,2,58,7,58,2,59,7,59,2,60,7,60,2,61,7,61,2,62,7,62,2,63,7,
  	63,2,64,7,64,2,65,7,65,2,66,7,66,2,67,7,67,2,68,7,68,2,69,7,69,2,70,7,
  	70,2,71,7,71,2,72,7,72,2,73,7,73,1,0,3,0,150,8,0,1,0,1,0,3,0,154,8,0,
  	5,0,156,8,0,10,0,12,0,159,9,0,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  	3,1,171,8,1,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,2,181,8,2,1,3,1,3,3,3,185,
  	8,3,1,3,5,3,188,8,3,10,3,12,3,191,9,3,1,3,3,3,194,8,3,1,4,1,4,1,4,1,5,
  	1,5,1,5,1,5,3,5,203,8,5,1,5,1,5,1,5,3,5,208,8,5,1,6,1,6,1,6,1,6,3,6,214,
  	8,6,1,7,3,7,217,8,7,1,7,1,7,3,7,221,8,7,1,7,1,7,1,8,1,8,3,8,227,8,8,1,
  	9,3,9,230,8,9,1,9,1,9,1,10,3,10,235,8,10,1,10,3,10,238,8,10,1,10,1,10,
  	1,10,3,10,243,8,10,1,10,1,10,1,10,1,11,3,11,249,8,11,1,11,1,11,3,11,253,
  	8,11,1,11,3,11,256,8,11,1,11,3,11,259,8,11,1,11,1,11,1,11,1,11,1,11,3,
  	11,266,8,11,1,11,1,11,1,12,1,12,3,12,272,8,12,1,12,3,12,275,8,12,1,12,
  	1,12,1,13,1,13,3,13,281,8,13,1,13,3,13,284,8,13,1,13,1,13,1,14,1,14,3,
  	14,290,8,14,1,14,3,14,293,8,14,1,14,1,14,1,15,1,15,1,15,1,15,3,15,301,
  	8,15,1,16,1,16,1,16,1,16,3,16,307,8,16,1,16,1,16,1,16,1,17,1,17,1,17,
  	1,17,3,17,316,8,17,1,17,1,17,1,18,1,18,1,18,1,19,1,19,1,19,1,19,1,19,
  	1,20,1,20,1,20,1,20,3,20,332,8,20,1,20,1,20,1,20,1,20,3,20,338,8,20,1,
  	20,1,20,1,21,3,21,343,8,21,1,21,1,21,1,22,1,22,1,22,1,23,4,23,351,8,23,
  	11,23,12,23,352,1,24,4,24,356,8,24,11,24,12,24,357,1,25,1,25,1,25,3,25,
  	363,8,25,1,26,1,26,1,26,1,26,1,27,1,27,1,27,1,27,1,27,1,27,3,27,375,8,
  	27,1,28,3,28,378,8,28,1,28,1,28,3,28,382,8,28,1,28,1,28,1,28,1,28,3,28,
  	388,8,28,1,28,1,28,3,28,392,8,28,1,29,1,29,1,29,5,29,397,8,29,10,29,12,
  	29,400,9,29,1,30,1,30,1,30,5,30,405,8,30,10,30,12,30,408,9,30,1,31,1,
  	31,1,31,5,31,413,8,31,10,31,12,31,416,9,31,1,32,1,32,1,32,5,32,421,8,
  	32,10,32,12,32,424,9,32,1,33,1,33,1,33,5,33,429,8,33,10,33,12,33,432,
  	9,33,1,34,1,34,1,34,3,34,437,8,34,1,34,3,34,440,8,34,1,35,1,35,1,35,1,
  	35,1,35,1,35,3,35,448,8,35,3,35,450,8,35,1,35,1,35,1,36,1,36,3,36,456,
  	8,36,1,36,3,36,459,8,36,1,36,1,36,1,37,1,37,3,37,465,8,37,1,37,3,37,468,
  	8,37,1,37,1,37,1,38,1,38,3,38,474,8,38,1,38,3,38,477,8,38,1,38,1,38,1,
  	39,1,39,3,39,483,8,39,1,39,3,39,486,8,39,1,39,1,39,1,40,3,40,491,8,40,
  	1,40,1,40,1,40,3,40,496,8,40,1,41,1,41,1,41,1,41,1,41,3,41,503,8,41,1,
  	41,3,41,506,8,41,1,41,1,41,1,41,1,41,3,41,512,8,41,1,41,3,41,515,8,41,
  	1,41,1,41,3,41,519,8,41,1,42,1,42,1,42,1,42,5,42,525,8,42,10,42,12,42,
  	528,9,42,1,42,1,42,1,42,1,43,1,43,1,43,1,43,1,43,1,43,1,44,1,44,1,44,
  	1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,4,44,551,8,44,11,44,12,44,
  	552,1,44,1,44,1,44,1,44,1,44,4,44,560,8,44,11,44,12,44,561,1,44,1,44,
  	3,44,566,8,44,3,44,568,8,44,1,45,1,45,1,45,5,45,573,8,45,10,45,12,45,
  	576,9,45,1,46,1,46,1,46,5,46,581,8,46,10,46,12,46,584,9,46,1,47,1,47,
  	1,47,5,47,589,8,47,10,47,12,47,592,9,47,1,48,1,48,1,48,5,48,597,8,48,
  	10,48,12,48,600,9,48,1,49,1,49,1,49,5,49,605,8,49,10,49,12,49,608,9,49,
  	1,50,1,50,1,50,5,50,613,8,50,10,50,12,50,616,9,50,1,51,1,51,1,51,3,51,
  	621,8,51,1,52,1,52,1,52,3,52,626,8,52,1,52,1,52,3,52,630,8,52,1,53,1,
  	53,1,53,5,53,635,8,53,10,53,12,53,638,9,53,1,54,1,54,1,54,5,54,643,8,
  	54,10,54,12,54,646,9,54,1,55,1,55,1,55,5,55,651,8,55,10,55,12,55,654,
  	9,55,1,56,1,56,1,56,1,56,1,56,1,56,3,56,662,8,56,1,56,5,56,665,8,56,10,
  	56,12,56,668,9,56,1,57,1,57,1,57,3,57,673,8,57,3,57,675,8,57,1,57,1,57,
  	1,58,1,58,1,58,3,58,682,8,58,1,58,1,58,1,58,1,58,1,58,1,58,1,58,3,58,
  	691,8,58,3,58,693,8,58,1,58,1,58,1,59,1,59,1,59,1,59,1,59,1,59,1,59,1,
  	59,1,59,1,59,3,59,707,8,59,1,59,3,59,710,8,59,1,59,1,59,3,59,714,8,59,
  	1,60,1,60,1,61,1,61,1,61,3,61,721,8,61,3,61,723,8,61,1,62,1,62,1,62,5,
  	62,728,8,62,10,62,12,62,731,9,62,1,63,1,63,1,63,3,63,736,8,63,1,63,1,
  	63,1,64,1,64,1,64,5,64,743,8,64,10,64,12,64,746,9,64,1,65,1,65,3,65,750,
  	8,65,1,66,1,66,1,66,1,66,1,66,5,66,757,8,66,10,66,12,66,760,9,66,1,66,
  	1,66,3,66,764,8,66,1,67,1,67,1,67,1,67,1,67,1,67,1,67,1,67,1,67,1,67,
  	1,67,3,67,777,8,67,1,68,1,68,1,68,5,68,782,8,68,10,68,12,68,785,9,68,
  	1,69,1,69,1,69,1,69,5,69,791,8,69,10,69,12,69,794,9,69,3,69,796,8,69,
  	1,69,3,69,799,8,69,1,69,1,69,1,70,1,70,1,70,1,70,5,70,807,8,70,10,70,
  	12,70,810,9,70,3,70,812,8,70,1,70,3,70,815,8,70,1,70,1,70,1,71,3,71,820,
  	8,71,1,71,3,71,823,8,71,1,71,1,71,1,71,1,71,1,72,1,72,1,73,1,73,5,73,
  	833,8,73,10,73,12,73,836,9,73,1,73,1,73,1,73,0,0,74,0,2,4,6,8,10,12,14,
  	16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,
  	62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104,
  	106,108,110,112,114,116,118,120,122,124,126,128,130,132,134,136,138,140,
  	142,144,146,0,16,1,0,55,56,2,0,64,64,81,82,1,0,83,88,2,0,10,10,15,22,
  	1,0,27,30,2,0,13,14,31,32,1,0,33,34,1,0,35,38,1,0,39,40,1,0,53,54,2,0,
  	34,34,41,42,1,0,43,44,1,0,45,46,1,0,47,48,3,0,89,91,97,97,99,102,2,0,
  	35,35,50,50,914,0,149,1,0,0,0,2,170,1,0,0,0,4,180,1,0,0,0,6,182,1,0,0,
  	0,8,195,1,0,0,0,10,198,1,0,0,0,12,209,1,0,0,0,14,216,1,0,0,0,16,226,1,
  	0,0,0,18,229,1,0,0,0,20,234,1,0,0,0,22,248,1,0,0,0,24,269,1,0,0,0,26,
  	278,1,0,0,0,28,287,1,0,0,0,30,300,1,0,0,0,32,302,1,0,0,0,34,311,1,0,0,
  	0,36,319,1,0,0,0,38,322,1,0,0,0,40,327,1,0,0,0,42,342,1,0,0,0,44,346,
  	1,0,0,0,46,350,1,0,0,0,48,355,1,0,0,0,50,362,1,0,0,0,52,364,1,0,0,0,54,
  	374,1,0,0,0,56,377,1,0,0,0,58,393,1,0,0,0,60,401,1,0,0,0,62,409,1,0,0,
  	0,64,417,1,0,0,0,66,425,1,0,0,0,68,439,1,0,0,0,70,441,1,0,0,0,72,453,
  	1,0,0,0,74,462,1,0,0,0,76,471,1,0,0,0,78,480,1,0,0,0,80,490,1,0,0,0,82,
  	518,1,0,0,0,84,520,1,0,0,0,86,532,1,0,0,0,88,567,1,0,0,0,90,569,1,0,0,
  	0,92,577,1,0,0,0,94,585,1,0,0,0,96,593,1,0,0,0,98,601,1,0,0,0,100,609,
  	1,0,0,0,102,617,1,0,0,0,104,629,1,0,0,0,106,631,1,0,0,0,108,639,1,0,0,
  	0,110,647,1,0,0,0,112,655,1,0,0,0,114,669,1,0,0,0,116,678,1,0,0,0,118,
  	713,1,0,0,0,120,715,1,0,0,0,122,717,1,0,0,0,124,724,1,0,0,0,126,735,1,
  	0,0,0,128,739,1,0,0,0,130,749,1,0,0,0,132,751,1,0,0,0,134,776,1,0,0,0,
  	136,778,1,0,0,0,138,786,1,0,0,0,140,802,1,0,0,0,142,819,1,0,0,0,144,828,
  	1,0,0,0,146,834,1,0,0,0,148,150,5,52,0,0,149,148,1,0,0,0,149,150,1,0,
  	0,0,150,157,1,0,0,0,151,153,3,2,1,0,152,154,5,52,0,0,153,152,1,0,0,0,
  	153,154,1,0,0,0,154,156,1,0,0,0,155,151,1,0,0,0,156,159,1,0,0,0,157,155,
  	1,0,0,0,157,158,1,0,0,0,158,160,1,0,0,0,159,157,1,0,0,0,160,161,5,0,0,
  	1,161,1,1,0,0,0,162,171,3,8,4,0,163,171,3,10,5,0,164,171,3,12,6,0,165,
  	171,3,32,16,0,166,171,3,34,17,0,167,171,3,22,11,0,168,171,3,38,19,0,169,
  	171,3,40,20,0,170,162,1,0,0,0,170,163,1,0,0,0,170,164,1,0,0,0,170,165,
  	1,0,0,0,170,166,1,0,0,0,170,167,1,0,0,0,170,168,1,0,0,0,170,169,1,0,0,
  	0,171,3,1,0,0,0,172,181,3,32,16,0,173,181,3,34,17,0,174,181,3,22,11,0,
  	175,181,3,38,19,0,176,181,3,40,20,0,177,181,3,36,18,0,178,181,3,42,21,
  	0,179,181,3,18,9,0,180,172,1,0,0,0,180,173,1,0,0,0,180,174,1,0,0,0,180,
  	175,1,0,0,0,180,176,1,0,0,0,180,177,1,0,0,0,180,178,1,0,0,0,180,179,1,
  	0,0,0,181,5,1,0,0,0,182,189,3,4,2,0,183,185,5,52,0,0,184,183,1,0,0,0,
  	184,185,1,0,0,0,185,186,1,0,0,0,186,188,3,4,2,0,187,184,1,0,0,0,188,191,
  	1,0,0,0,189,187,1,0,0,0,189,190,1,0,0,0,190,193,1,0,0,0,191,189,1,0,0,
  	0,192,194,5,52,0,0,193,192,1,0,0,0,193,194,1,0,0,0,194,7,1,0,0,0,195,
  	196,5,65,0,0,196,197,3,144,72,0,197,9,1,0,0,0,198,207,5,66,0,0,199,208,
  	5,99,0,0,200,203,3,144,72,0,201,203,3,26,13,0,202,200,1,0,0,0,202,201,
  	1,0,0,0,203,204,1,0,0,0,204,205,5,58,0,0,205,206,5,99,0,0,206,208,1,0,
  	0,0,207,199,1,0,0,0,207,202,1,0,0,0,208,11,1,0,0,0,209,213,5,67,0,0,210,
  	214,3,32,16,0,211,214,3,38,19,0,212,214,3,26,13,0,213,210,1,0,0,0,213,
  	211,1,0,0,0,213,212,1,0,0,0,214,13,1,0,0,0,215,217,5,83,0,0,216,215,1,
  	0,0,0,216,217,1,0,0,0,217,218,1,0,0,0,218,220,5,1,0,0,219,221,3,6,3,0,
  	220,219,1,0,0,0,220,221,1,0,0,0,221,222,1,0,0,0,222,223,5,2,0,0,223,15,
  	1,0,0,0,224,227,3,14,7,0,225,227,3,80,40,0,226,224,1,0,0,0,226,225,1,
  	0,0,0,227,17,1,0,0,0,228,230,5,63,0,0,229,228,1,0,0,0,229,230,1,0,0,0,
  	230,231,1,0,0,0,231,232,3,14,7,0,232,19,1,0,0,0,233,235,3,48,24,0,234,
  	233,1,0,0,0,234,235,1,0,0,0,235,237,1,0,0,0,236,238,3,76,38,0,237,236,
  	1,0,0,0,237,238,1,0,0,0,238,239,1,0,0,0,239,242,3,72,36,0,240,241,5,3,
  	0,0,241,243,3,122,61,0,242,240,1,0,0,0,242,243,1,0,0,0,243,244,1,0,0,
  	0,244,245,5,4,0,0,245,246,3,16,8,0,246,21,1,0,0,0,247,249,3,46,23,0,248,
  	247,1,0,0,0,248,249,1,0,0,0,249,252,1,0,0,0,250,251,5,62,0,0,251,253,
  	3,76,38,0,252,250,1,0,0,0,252,253,1,0,0,0,253,255,1,0,0,0,254,256,5,67,
  	0,0,255,254,1,0,0,0,255,256,1,0,0,0,256,258,1,0,0,0,257,259,3,48,24,0,
  	258,257,1,0,0,0,258,259,1,0,0,0,259,260,1,0,0,0,260,261,5,59,0,0,261,
  	262,3,144,72,0,262,265,3,72,36,0,263,264,5,3,0,0,264,266,3,122,61,0,265,
  	263,1,0,0,0,265,266,1,0,0,0,266,267,1,0,0,0,267,268,3,14,7,0,268,23,1,
  	0,0,0,269,271,5,5,0,0,270,272,3,58,29,0,271,270,1,0,0,0,271,272,1,0,0,
  	0,272,274,1,0,0,0,273,275,5,6,0,0,274,273,1,0,0,0,274,275,1,0,0,0,275,
  	276,1,0,0,0,276,277,5,7,0,0,277,25,1,0,0,0,278,280,5,1,0,0,279,281,3,
  	58,29,0,280,279,1,0,0,0,280,281,1,0,0,0,281,283,1,0,0,0,282,284,5,6,0,
  	0,283,282,1,0,0,0,283,284,1,0,0,0,284,285,1,0,0,0,285,286,5,2,0,0,286,
  	27,1,0,0,0,287,289,5,8,0,0,288,290,3,58,29,0,289,288,1,0,0,0,289,290,
  	1,0,0,0,290,292,1,0,0,0,291,293,5,6,0,0,292,291,1,0,0,0,292,293,1,0,0,
  	0,293,294,1,0,0,0,294,295,5,9,0,0,295,29,1,0,0,0,296,301,3,144,72,0,297,
  	301,3,24,12,0,298,301,3,26,13,0,299,301,3,28,14,0,300,296,1,0,0,0,300,
  	297,1,0,0,0,300,298,1,0,0,0,300,299,1,0,0,0,301,31,1,0,0,0,302,303,7,
  	0,0,0,303,306,3,30,15,0,304,305,5,3,0,0,305,307,3,122,61,0,306,304,1,
  	0,0,0,306,307,1,0,0,0,307,308,1,0,0,0,308,309,5,10,0,0,309,310,3,80,40,
  	0,310,33,1,0,0,0,311,315,5,57,0,0,312,313,3,144,72,0,313,314,5,10,0,0,
  	314,316,1,0,0,0,315,312,1,0,0,0,315,316,1,0,0,0,316,317,1,0,0,0,317,318,
  	3,146,73,0,318,35,1,0,0,0,319,320,7,1,0,0,320,321,3,80,40,0,321,37,1,
  	0,0,0,322,323,5,60,0,0,323,324,3,144,72,0,324,325,5,10,0,0,325,326,3,
  	122,61,0,326,39,1,0,0,0,327,328,5,61,0,0,328,331,3,144,72,0,329,330,5,
  	77,0,0,330,332,3,122,61,0,331,329,1,0,0,0,331,332,1,0,0,0,332,333,1,0,
  	0,0,333,334,5,10,0,0,334,335,5,1,0,0,335,337,3,64,32,0,336,338,5,6,0,
  	0,337,336,1,0,0,0,337,338,1,0,0,0,338,339,1,0,0,0,339,340,5,2,0,0,340,
  	41,1,0,0,0,341,343,3,46,23,0,342,341,1,0,0,0,342,343,1,0,0,0,343,344,
  	1,0,0,0,344,345,3,80,40,0,345,43,1,0,0,0,346,347,5,11,0,0,347,348,3,118,
  	59,0,348,45,1,0,0,0,349,351,3,44,22,0,350,349,1,0,0,0,351,352,1,0,0,0,
  	352,350,1,0,0,0,352,353,1,0,0,0,353,47,1,0,0,0,354,356,7,2,0,0,355,354,
  	1,0,0,0,356,357,1,0,0,0,357,355,1,0,0,0,357,358,1,0,0,0,358,49,1,0,0,
  	0,359,363,3,80,40,0,360,361,5,12,0,0,361,363,3,80,40,0,362,359,1,0,0,
  	0,362,360,1,0,0,0,363,51,1,0,0,0,364,365,3,144,72,0,365,366,5,3,0,0,366,
  	367,3,122,61,0,367,53,1,0,0,0,368,369,3,144,72,0,369,370,5,3,0,0,370,
  	371,3,80,40,0,371,375,1,0,0,0,372,373,5,12,0,0,373,375,3,80,40,0,374,
  	368,1,0,0,0,374,372,1,0,0,0,375,55,1,0,0,0,376,378,5,56,0,0,377,376,1,
  	0,0,0,377,378,1,0,0,0,378,379,1,0,0,0,379,381,3,144,72,0,380,382,3,44,
  	22,0,381,380,1,0,0,0,381,382,1,0,0,0,382,383,1,0,0,0,383,387,5,3,0,0,
  	384,388,3,122,61,0,385,386,5,68,0,0,386,388,3,144,72,0,387,384,1,0,0,
  	0,387,385,1,0,0,0,388,391,1,0,0,0,389,390,5,10,0,0,390,392,3,80,40,0,
  	391,389,1,0,0,0,391,392,1,0,0,0,392,57,1,0,0,0,393,398,3,144,72,0,394,
  	395,5,6,0,0,395,397,3,144,72,0,396,394,1,0,0,0,397,400,1,0,0,0,398,396,
  	1,0,0,0,398,399,1,0,0,0,399,59,1,0,0,0,400,398,1,0,0,0,401,406,3,80,40,
  	0,402,403,5,6,0,0,403,405,3,80,40,0,404,402,1,0,0,0,405,408,1,0,0,0,406,
  	404,1,0,0,0,406,407,1,0,0,0,407,61,1,0,0,0,408,406,1,0,0,0,409,414,3,
  	50,25,0,410,411,5,6,0,0,411,413,3,50,25,0,412,410,1,0,0,0,413,416,1,0,
  	0,0,414,412,1,0,0,0,414,415,1,0,0,0,415,63,1,0,0,0,416,414,1,0,0,0,417,
  	422,3,54,27,0,418,419,5,6,0,0,419,421,3,54,27,0,420,418,1,0,0,0,421,424,
  	1,0,0,0,422,420,1,0,0,0,422,423,1,0,0,0,423,65,1,0,0,0,424,422,1,0,0,
  	0,425,430,3,56,28,0,426,427,5,6,0,0,427,429,3,56,28,0,428,426,1,0,0,0,
  	429,432,1,0,0,0,430,428,1,0,0,0,430,431,1,0,0,0,431,67,1,0,0,0,432,430,
  	1,0,0,0,433,436,3,62,31,0,434,435,5,6,0,0,435,437,3,64,32,0,436,434,1,
  	0,0,0,436,437,1,0,0,0,437,440,1,0,0,0,438,440,3,64,32,0,439,433,1,0,0,
  	0,439,438,1,0,0,0,440,69,1,0,0,0,441,442,5,8,0,0,442,449,3,80,40,0,443,
  	444,5,3,0,0,444,447,3,80,40,0,445,446,5,3,0,0,446,448,3,80,40,0,447,445,
  	1,0,0,0,447,448,1,0,0,0,448,450,1,0,0,0,449,443,1,0,0,0,449,450,1,0,0,
  	0,450,451,1,0,0,0,451,452,5,9,0,0,452,71,1,0,0,0,453,455,5,5,0,0,454,
  	456,3,66,33,0,455,454,1,0,0,0,455,456,1,0,0,0,456,458,1,0,0,0,457,459,
  	5,6,0,0,458,457,1,0,0,0,458,459,1,0,0,0,459,460,1,0,0,0,460,461,5,7,0,
  	0,461,73,1,0,0,0,462,464,5,5,0,0,463,465,3,68,34,0,464,463,1,0,0,0,464,
  	465,1,0,0,0,465,467,1,0,0,0,466,468,5,6,0,0,467,466,1,0,0,0,467,468,1,
  	0,0,0,468,469,1,0,0,0,469,470,5,7,0,0,470,75,1,0,0,0,471,473,5,13,0,0,
  	472,474,3,66,33,0,473,472,1,0,0,0,473,474,1,0,0,0,474,476,1,0,0,0,475,
  	477,5,6,0,0,476,475,1,0,0,0,476,477,1,0,0,0,477,478,1,0,0,0,478,479,5,
  	14,0,0,479,77,1,0,0,0,480,482,5,13,0,0,481,483,3,68,34,0,482,481,1,0,
  	0,0,482,483,1,0,0,0,483,485,1,0,0,0,484,486,5,6,0,0,485,484,1,0,0,0,485,
  	486,1,0,0,0,486,487,1,0,0,0,487,488,5,14,0,0,488,79,1,0,0,0,489,491,5,
  	63,0,0,490,489,1,0,0,0,490,491,1,0,0,0,491,492,1,0,0,0,492,495,3,88,44,
  	0,493,494,7,3,0,0,494,496,3,88,44,0,495,493,1,0,0,0,495,496,1,0,0,0,496,
  	81,1,0,0,0,497,519,3,146,73,0,498,519,3,120,60,0,499,502,5,5,0,0,500,
  	503,3,60,30,0,501,503,3,58,29,0,502,500,1,0,0,0,502,501,1,0,0,0,502,503,
  	1,0,0,0,503,505,1,0,0,0,504,506,5,6,0,0,505,504,1,0,0,0,505,506,1,0,0,
  	0,506,507,1,0,0,0,507,519,5,7,0,0,508,511,5,1,0,0,509,512,3,64,32,0,510,
  	512,3,58,29,0,511,509,1,0,0,0,511,510,1,0,0,0,511,512,1,0,0,0,512,514,
  	1,0,0,0,513,515,5,6,0,0,514,513,1,0,0,0,514,515,1,0,0,0,515,516,1,0,0,
  	0,516,519,5,2,0,0,517,519,5,23,0,0,518,497,1,0,0,0,518,498,1,0,0,0,518,
  	499,1,0,0,0,518,508,1,0,0,0,518,517,1,0,0,0,519,83,1,0,0,0,520,521,5,
  	74,0,0,521,526,3,82,41,0,522,523,5,24,0,0,523,525,3,82,41,0,524,522,1,
  	0,0,0,525,528,1,0,0,0,526,524,1,0,0,0,526,527,1,0,0,0,527,529,1,0,0,0,
  	528,526,1,0,0,0,529,530,5,4,0,0,530,531,3,16,8,0,531,85,1,0,0,0,532,533,
  	5,79,0,0,533,534,3,144,72,0,534,535,5,3,0,0,535,536,3,122,61,0,536,537,
  	3,14,7,0,537,87,1,0,0,0,538,568,3,90,45,0,539,540,5,70,0,0,540,541,3,
  	90,45,0,541,542,5,71,0,0,542,543,3,16,8,0,543,544,5,72,0,0,544,545,3,
  	16,8,0,545,568,1,0,0,0,546,547,5,73,0,0,547,548,3,146,73,0,548,550,5,
  	1,0,0,549,551,3,84,42,0,550,549,1,0,0,0,551,552,1,0,0,0,552,550,1,0,0,
  	0,552,553,1,0,0,0,553,554,1,0,0,0,554,555,5,2,0,0,555,568,1,0,0,0,556,
  	557,5,78,0,0,557,559,3,14,7,0,558,560,3,86,43,0,559,558,1,0,0,0,560,561,
  	1,0,0,0,561,559,1,0,0,0,561,562,1,0,0,0,562,565,1,0,0,0,563,564,5,80,
  	0,0,564,566,3,14,7,0,565,563,1,0,0,0,565,566,1,0,0,0,566,568,1,0,0,0,
  	567,538,1,0,0,0,567,539,1,0,0,0,567,546,1,0,0,0,567,556,1,0,0,0,568,89,
  	1,0,0,0,569,574,3,92,46,0,570,571,5,25,0,0,571,573,3,92,46,0,572,570,
  	1,0,0,0,573,576,1,0,0,0,574,572,1,0,0,0,574,575,1,0,0,0,575,91,1,0,0,
  	0,576,574,1,0,0,0,577,582,3,94,47,0,578,579,5,26,0,0,579,581,3,94,47,
  	0,580,578,1,0,0,0,581,584,1,0,0,0,582,580,1,0,0,0,582,583,1,0,0,0,583,
  	93,1,0,0,0,584,582,1,0,0,0,585,590,3,96,48,0,586,587,7,4,0,0,587,589,
  	3,96,48,0,588,586,1,0,0,0,589,592,1,0,0,0,590,588,1,0,0,0,590,591,1,0,
  	0,0,591,95,1,0,0,0,592,590,1,0,0,0,593,598,3,98,49,0,594,595,7,5,0,0,
  	595,597,3,98,49,0,596,594,1,0,0,0,597,600,1,0,0,0,598,596,1,0,0,0,598,
  	599,1,0,0,0,599,97,1,0,0,0,600,598,1,0,0,0,601,606,3,100,50,0,602,603,
  	7,6,0,0,603,605,3,100,50,0,604,602,1,0,0,0,605,608,1,0,0,0,606,604,1,
  	0,0,0,606,607,1,0,0,0,607,99,1,0,0,0,608,606,1,0,0,0,609,614,3,102,51,
  	0,610,611,7,7,0,0,611,613,3,102,51,0,612,610,1,0,0,0,613,616,1,0,0,0,
  	614,612,1,0,0,0,614,615,1,0,0,0,615,101,1,0,0,0,616,614,1,0,0,0,617,620,
  	3,104,52,0,618,619,7,8,0,0,619,621,3,80,40,0,620,618,1,0,0,0,620,621,
  	1,0,0,0,621,103,1,0,0,0,622,625,3,106,53,0,623,624,7,9,0,0,624,626,3,
  	122,61,0,625,623,1,0,0,0,625,626,1,0,0,0,626,630,1,0,0,0,627,628,7,10,
  	0,0,628,630,3,106,53,0,629,622,1,0,0,0,629,627,1,0,0,0,630,105,1,0,0,
  	0,631,636,3,108,54,0,632,633,7,11,0,0,633,635,3,108,54,0,634,632,1,0,
  	0,0,635,638,1,0,0,0,636,634,1,0,0,0,636,637,1,0,0,0,637,107,1,0,0,0,638,
  	636,1,0,0,0,639,644,3,110,55,0,640,641,7,12,0,0,641,643,3,110,55,0,642,
  	640,1,0,0,0,643,646,1,0,0,0,644,642,1,0,0,0,644,645,1,0,0,0,645,109,1,
  	0,0,0,646,644,1,0,0,0,647,652,3,112,56,0,648,649,7,13,0,0,649,651,3,112,
  	56,0,650,648,1,0,0,0,651,654,1,0,0,0,652,650,1,0,0,0,652,653,1,0,0,0,
  	653,111,1,0,0,0,654,652,1,0,0,0,655,666,3,118,59,0,656,661,4,56,0,0,657,
  	662,3,70,35,0,658,662,3,74,37,0,659,662,3,78,39,0,660,662,5,41,0,0,661,
  	657,1,0,0,0,661,658,1,0,0,0,661,659,1,0,0,0,661,660,1,0,0,0,662,665,1,
  	0,0,0,663,665,3,44,22,0,664,656,1,0,0,0,664,663,1,0,0,0,665,668,1,0,0,
  	0,666,664,1,0,0,0,666,667,1,0,0,0,667,113,1,0,0,0,668,666,1,0,0,0,669,
  	674,5,1,0,0,670,672,3,64,32,0,671,673,5,6,0,0,672,671,1,0,0,0,672,673,
  	1,0,0,0,673,675,1,0,0,0,674,670,1,0,0,0,674,675,1,0,0,0,675,676,1,0,0,
  	0,676,677,5,2,0,0,677,115,1,0,0,0,678,692,5,8,0,0,679,681,3,62,31,0,680,
  	682,5,6,0,0,681,680,1,0,0,0,681,682,1,0,0,0,682,693,1,0,0,0,683,684,3,
  	80,40,0,684,685,5,75,0,0,685,686,3,146,73,0,686,687,5,76,0,0,687,690,
  	3,80,40,0,688,689,5,70,0,0,689,691,3,80,40,0,690,688,1,0,0,0,690,691,
  	1,0,0,0,691,693,1,0,0,0,692,679,1,0,0,0,692,683,1,0,0,0,692,693,1,0,0,
  	0,693,694,1,0,0,0,694,695,5,9,0,0,695,117,1,0,0,0,696,714,3,146,73,0,
  	697,714,3,120,60,0,698,714,3,116,58,0,699,714,3,114,57,0,700,701,5,5,
  	0,0,701,702,3,80,40,0,702,703,5,7,0,0,703,714,1,0,0,0,704,706,5,5,0,0,
  	705,707,3,60,30,0,706,705,1,0,0,0,706,707,1,0,0,0,707,709,1,0,0,0,708,
  	710,5,6,0,0,709,708,1,0,0,0,709,710,1,0,0,0,710,711,1,0,0,0,711,714,5,
  	7,0,0,712,714,3,20,10,0,713,696,1,0,0,0,713,697,1,0,0,0,713,698,1,0,0,
  	0,713,699,1,0,0,0,713,700,1,0,0,0,713,704,1,0,0,0,713,712,1,0,0,0,714,
  	119,1,0,0,0,715,716,7,14,0,0,716,121,1,0,0,0,717,722,3,124,62,0,718,720,
  	5,49,0,0,719,721,3,124,62,0,720,719,1,0,0,0,720,721,1,0,0,0,721,723,1,
  	0,0,0,722,718,1,0,0,0,722,723,1,0,0,0,723,123,1,0,0,0,724,729,3,126,63,
  	0,725,726,5,24,0,0,726,728,3,126,63,0,727,725,1,0,0,0,728,731,1,0,0,0,
  	729,727,1,0,0,0,729,730,1,0,0,0,730,125,1,0,0,0,731,729,1,0,0,0,732,733,
  	3,144,72,0,733,734,5,77,0,0,734,736,1,0,0,0,735,732,1,0,0,0,735,736,1,
  	0,0,0,736,737,1,0,0,0,737,738,3,128,64,0,738,127,1,0,0,0,739,744,3,132,
  	66,0,740,741,5,8,0,0,741,743,5,9,0,0,742,740,1,0,0,0,743,746,1,0,0,0,
  	744,742,1,0,0,0,744,745,1,0,0,0,745,129,1,0,0,0,746,744,1,0,0,0,747,750,
  	3,122,61,0,748,750,3,118,59,0,749,747,1,0,0,0,749,748,1,0,0,0,750,131,
  	1,0,0,0,751,763,3,134,67,0,752,753,5,13,0,0,753,758,3,130,65,0,754,755,
  	5,6,0,0,755,757,3,130,65,0,756,754,1,0,0,0,757,760,1,0,0,0,758,756,1,
  	0,0,0,758,759,1,0,0,0,759,761,1,0,0,0,760,758,1,0,0,0,761,762,5,14,0,
  	0,762,764,1,0,0,0,763,752,1,0,0,0,763,764,1,0,0,0,764,133,1,0,0,0,765,
  	777,5,92,0,0,766,777,3,136,68,0,767,777,3,146,73,0,768,769,5,5,0,0,769,
  	770,3,122,61,0,770,771,5,7,0,0,771,777,1,0,0,0,772,777,3,140,70,0,773,
  	777,3,142,71,0,774,775,5,69,0,0,775,777,3,80,40,0,776,765,1,0,0,0,776,
  	766,1,0,0,0,776,767,1,0,0,0,776,768,1,0,0,0,776,772,1,0,0,0,776,773,1,
  	0,0,0,776,774,1,0,0,0,777,135,1,0,0,0,778,783,3,138,69,0,779,780,7,15,
  	0,0,780,782,3,138,69,0,781,779,1,0,0,0,782,785,1,0,0,0,783,781,1,0,0,
  	0,783,784,1,0,0,0,784,137,1,0,0,0,785,783,1,0,0,0,786,795,5,1,0,0,787,
  	792,3,52,26,0,788,789,5,6,0,0,789,791,3,52,26,0,790,788,1,0,0,0,791,794,
  	1,0,0,0,792,790,1,0,0,0,792,793,1,0,0,0,793,796,1,0,0,0,794,792,1,0,0,
  	0,795,787,1,0,0,0,795,796,1,0,0,0,796,798,1,0,0,0,797,799,5,6,0,0,798,
  	797,1,0,0,0,798,799,1,0,0,0,799,800,1,0,0,0,800,801,5,2,0,0,801,139,1,
  	0,0,0,802,811,5,5,0,0,803,808,3,122,61,0,804,805,5,6,0,0,805,807,3,122,
  	61,0,806,804,1,0,0,0,807,810,1,0,0,0,808,806,1,0,0,0,808,809,1,0,0,0,
  	809,812,1,0,0,0,810,808,1,0,0,0,811,803,1,0,0,0,811,812,1,0,0,0,812,814,
  	1,0,0,0,813,815,5,6,0,0,814,813,1,0,0,0,814,815,1,0,0,0,815,816,1,0,0,
  	0,816,817,5,7,0,0,817,141,1,0,0,0,818,820,3,48,24,0,819,818,1,0,0,0,819,
  	820,1,0,0,0,820,822,1,0,0,0,821,823,3,76,38,0,822,821,1,0,0,0,822,823,
  	1,0,0,0,823,824,1,0,0,0,824,825,3,72,36,0,825,826,5,4,0,0,826,827,3,122,
  	61,0,827,143,1,0,0,0,828,829,5,98,0,0,829,145,1,0,0,0,830,831,5,98,0,
  	0,831,833,5,51,0,0,832,830,1,0,0,0,833,836,1,0,0,0,834,832,1,0,0,0,834,
  	835,1,0,0,0,835,837,1,0,0,0,836,834,1,0,0,0,837,838,5,98,0,0,838,147,
  	1,0,0,0,114,149,153,157,170,180,184,189,193,202,207,213,216,220,226,229,
  	234,237,242,248,252,255,258,265,271,274,280,283,289,292,300,306,315,331,
  	337,342,352,357,362,374,377,381,387,391,398,406,414,422,430,436,439,447,
  	449,455,458,464,467,473,476,482,485,490,495,502,505,511,514,518,526,552,
  	561,565,567,574,582,590,598,606,614,620,625,629,636,644,652,661,664,666,
  	672,674,681,690,692,706,709,713,720,722,729,735,744,749,758,763,776,783,
  	792,795,798,808,811,814,819,822,834
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
    setState(149);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SEP) {
      setState(148);
      match(OpenCMLParser::SEP);
    }
    setState(157);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8899112863684102144) != 0) || ((((_la - 65) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 65)) & 16515079) != 0)) {
      setState(151);
      decl();
      setState(153);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::SEP) {
        setState(152);
        match(OpenCMLParser::SEP);
      }
      setState(159);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(160);
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

OpenCMLParser::LetDeclContext* OpenCMLParser::DeclContext::letDecl() {
  return getRuleContext<OpenCMLParser::LetDeclContext>(0);
}

OpenCMLParser::UseDeclContext* OpenCMLParser::DeclContext::useDecl() {
  return getRuleContext<OpenCMLParser::UseDeclContext>(0);
}

OpenCMLParser::FuncDeclContext* OpenCMLParser::DeclContext::funcDecl() {
  return getRuleContext<OpenCMLParser::FuncDeclContext>(0);
}

OpenCMLParser::TypeDeclContext* OpenCMLParser::DeclContext::typeDecl() {
  return getRuleContext<OpenCMLParser::TypeDeclContext>(0);
}

OpenCMLParser::EnumDeclContext* OpenCMLParser::DeclContext::enumDecl() {
  return getRuleContext<OpenCMLParser::EnumDeclContext>(0);
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
    setState(170);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 3, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(162);
      moduleDecl();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(163);
      importDecl();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(164);
      exportDecl();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(165);
      letDecl();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(166);
      useDecl();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(167);
      funcDecl();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(168);
      typeDecl();
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(169);
      enumDecl();
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

OpenCMLParser::LetDeclContext* OpenCMLParser::StmtContext::letDecl() {
  return getRuleContext<OpenCMLParser::LetDeclContext>(0);
}

OpenCMLParser::UseDeclContext* OpenCMLParser::StmtContext::useDecl() {
  return getRuleContext<OpenCMLParser::UseDeclContext>(0);
}

OpenCMLParser::FuncDeclContext* OpenCMLParser::StmtContext::funcDecl() {
  return getRuleContext<OpenCMLParser::FuncDeclContext>(0);
}

OpenCMLParser::TypeDeclContext* OpenCMLParser::StmtContext::typeDecl() {
  return getRuleContext<OpenCMLParser::TypeDeclContext>(0);
}

OpenCMLParser::EnumDeclContext* OpenCMLParser::StmtContext::enumDecl() {
  return getRuleContext<OpenCMLParser::EnumDeclContext>(0);
}

OpenCMLParser::RetStmtContext* OpenCMLParser::StmtContext::retStmt() {
  return getRuleContext<OpenCMLParser::RetStmtContext>(0);
}

OpenCMLParser::ExprStmtContext* OpenCMLParser::StmtContext::exprStmt() {
  return getRuleContext<OpenCMLParser::ExprStmtContext>(0);
}

OpenCMLParser::BlockStmtContext* OpenCMLParser::StmtContext::blockStmt() {
  return getRuleContext<OpenCMLParser::BlockStmtContext>(0);
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
    setState(180);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 4, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(172);
      letDecl();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(173);
      useDecl();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(174);
      funcDecl();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(175);
      typeDecl();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(176);
      enumDecl();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(177);
      retStmt();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(178);
      exprStmt();
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(179);
      blockStmt();
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
    setState(182);
    stmt();
    setState(189);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 6, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(184);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::SEP) {
          setState(183);
          match(OpenCMLParser::SEP);
        }
        setState(186);
        stmt(); 
      }
      setState(191);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 6, _ctx);
    }
    setState(193);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SEP) {
      setState(192);
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
    setState(195);
    match(OpenCMLParser::MODULE);
    setState(196);
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
    setState(198);
    match(OpenCMLParser::IMPORT);
    setState(207);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::STRING: {
        setState(199);
        match(OpenCMLParser::STRING);
        break;
      }

      case OpenCMLParser::T__0:
      case OpenCMLParser::IDENTIFIER: {
        setState(202);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case OpenCMLParser::IDENTIFIER: {
            setState(200);
            identDef();
            break;
          }

          case OpenCMLParser::T__0: {
            setState(201);
            bracedIdents();
            break;
          }

        default:
          throw NoViableAltException(this);
        }
        setState(204);
        match(OpenCMLParser::FROM);
        setState(205);
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

OpenCMLParser::LetDeclContext* OpenCMLParser::ExportDeclContext::letDecl() {
  return getRuleContext<OpenCMLParser::LetDeclContext>(0);
}

OpenCMLParser::TypeDeclContext* OpenCMLParser::ExportDeclContext::typeDecl() {
  return getRuleContext<OpenCMLParser::TypeDeclContext>(0);
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
    setState(209);
    match(OpenCMLParser::EXPORT);
    setState(213);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::LET:
      case OpenCMLParser::VAR: {
        setState(210);
        letDecl();
        break;
      }

      case OpenCMLParser::TYPE: {
        setState(211);
        typeDecl();
        break;
      }

      case OpenCMLParser::T__0: {
        setState(212);
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
  enterRule(_localctx, 14, OpenCMLParser::RuleStmtBlock);
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
    setState(216);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SYNC) {
      setState(215);
      match(OpenCMLParser::SYNC);
    }
    setState(218);
    match(OpenCMLParser::T__0);
    setState(220);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -324252558921029342) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 541434200649) != 0)) {
      setState(219);
      stmtList();
    }
    setState(222);
    match(OpenCMLParser::T__1);
   
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
  enterRule(_localctx, 16, OpenCMLParser::RuleBlockExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(226);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 13, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(224);
      stmtBlock();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(225);
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

//----------------- BlockStmtContext ------------------------------------------------------------------

OpenCMLParser::BlockStmtContext::BlockStmtContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::StmtBlockContext* OpenCMLParser::BlockStmtContext::stmtBlock() {
  return getRuleContext<OpenCMLParser::StmtBlockContext>(0);
}

tree::TerminalNode* OpenCMLParser::BlockStmtContext::WAIT() {
  return getToken(OpenCMLParser::WAIT, 0);
}


size_t OpenCMLParser::BlockStmtContext::getRuleIndex() const {
  return OpenCMLParser::RuleBlockStmt;
}


std::any OpenCMLParser::BlockStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBlockStmt(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BlockStmtContext* OpenCMLParser::blockStmt() {
  BlockStmtContext *_localctx = _tracker.createInstance<BlockStmtContext>(_ctx, getState());
  enterRule(_localctx, 18, OpenCMLParser::RuleBlockStmt);
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
    setState(229);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WAIT) {
      setState(228);
      match(OpenCMLParser::WAIT);
    }
    setState(231);
    stmtBlock();
   
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
  enterRule(_localctx, 20, OpenCMLParser::RuleLambdaExpr);
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
    setState(234);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 83) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 83)) & 63) != 0)) {
      setState(233);
      modifiers();
    }
    setState(237);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__12) {
      setState(236);
      angledParams();
    }
    setState(239);
    parentParams();
    setState(242);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__2) {
      setState(240);
      match(OpenCMLParser::T__2);
      setState(241);
      typeExpr();
    }
    setState(244);
    match(OpenCMLParser::T__3);
    setState(245);
    blockExpr();
   
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
  enterRule(_localctx, 22, OpenCMLParser::RuleFuncDecl);
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
    if (_la == OpenCMLParser::T__10) {
      setState(247);
      annotations();
    }
    setState(252);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WITH) {
      setState(250);
      match(OpenCMLParser::WITH);
      setState(251);
      angledParams();
    }
    setState(255);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::EXPORT) {
      setState(254);
      match(OpenCMLParser::EXPORT);
    }
    setState(258);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 83) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 83)) & 63) != 0)) {
      setState(257);
      modifiers();
    }
    setState(260);
    match(OpenCMLParser::FUNC);
    setState(261);
    identDef();
    setState(262);
    parentParams();
    setState(265);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__2) {
      setState(263);
      match(OpenCMLParser::T__2);
      setState(264);
      typeExpr();
    }
    setState(267);
    stmtBlock();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParentIdentsContext ------------------------------------------------------------------

OpenCMLParser::ParentIdentsContext::ParentIdentsContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentListContext* OpenCMLParser::ParentIdentsContext::identList() {
  return getRuleContext<OpenCMLParser::IdentListContext>(0);
}


size_t OpenCMLParser::ParentIdentsContext::getRuleIndex() const {
  return OpenCMLParser::RuleParentIdents;
}


std::any OpenCMLParser::ParentIdentsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitParentIdents(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ParentIdentsContext* OpenCMLParser::parentIdents() {
  ParentIdentsContext *_localctx = _tracker.createInstance<ParentIdentsContext>(_ctx, getState());
  enterRule(_localctx, 24, OpenCMLParser::RuleParentIdents);
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
    setState(269);
    match(OpenCMLParser::T__4);
    setState(271);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(270);
      identList();
    }
    setState(274);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(273);
      match(OpenCMLParser::T__5);
    }
    setState(276);
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
  enterRule(_localctx, 26, OpenCMLParser::RuleBracedIdents);
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
    setState(278);
    match(OpenCMLParser::T__0);
    setState(280);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(279);
      identList();
    }
    setState(283);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(282);
      match(OpenCMLParser::T__5);
    }
    setState(285);
    match(OpenCMLParser::T__1);
   
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
  enterRule(_localctx, 28, OpenCMLParser::RuleBracketIdents);
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
    setState(287);
    match(OpenCMLParser::T__7);
    setState(289);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(288);
      identList();
    }
    setState(292);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(291);
      match(OpenCMLParser::T__5);
    }
    setState(294);
    match(OpenCMLParser::T__8);
   
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

OpenCMLParser::ParentIdentsContext* OpenCMLParser::CarrierContext::parentIdents() {
  return getRuleContext<OpenCMLParser::ParentIdentsContext>(0);
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
  enterRule(_localctx, 30, OpenCMLParser::RuleCarrier);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(300);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(296);
        identDef();
        break;
      }

      case OpenCMLParser::T__4: {
        enterOuterAlt(_localctx, 2);
        setState(297);
        parentIdents();
        break;
      }

      case OpenCMLParser::T__0: {
        enterOuterAlt(_localctx, 3);
        setState(298);
        bracedIdents();
        break;
      }

      case OpenCMLParser::T__7: {
        enterOuterAlt(_localctx, 4);
        setState(299);
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

//----------------- LetDeclContext ------------------------------------------------------------------

OpenCMLParser::LetDeclContext::LetDeclContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::CarrierContext* OpenCMLParser::LetDeclContext::carrier() {
  return getRuleContext<OpenCMLParser::CarrierContext>(0);
}

OpenCMLParser::DataExprContext* OpenCMLParser::LetDeclContext::dataExpr() {
  return getRuleContext<OpenCMLParser::DataExprContext>(0);
}

tree::TerminalNode* OpenCMLParser::LetDeclContext::LET() {
  return getToken(OpenCMLParser::LET, 0);
}

tree::TerminalNode* OpenCMLParser::LetDeclContext::VAR() {
  return getToken(OpenCMLParser::VAR, 0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::LetDeclContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}


size_t OpenCMLParser::LetDeclContext::getRuleIndex() const {
  return OpenCMLParser::RuleLetDecl;
}


std::any OpenCMLParser::LetDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitLetDecl(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::LetDeclContext* OpenCMLParser::letDecl() {
  LetDeclContext *_localctx = _tracker.createInstance<LetDeclContext>(_ctx, getState());
  enterRule(_localctx, 32, OpenCMLParser::RuleLetDecl);
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
    setState(302);
    _la = _input->LA(1);
    if (!(_la == OpenCMLParser::LET

    || _la == OpenCMLParser::VAR)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(303);
    carrier();
    setState(306);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__2) {
      setState(304);
      match(OpenCMLParser::T__2);
      setState(305);
      typeExpr();
    }
    setState(308);
    match(OpenCMLParser::T__9);
    setState(309);
    dataExpr();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UseDeclContext ------------------------------------------------------------------

OpenCMLParser::UseDeclContext::UseDeclContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::UseDeclContext::USE() {
  return getToken(OpenCMLParser::USE, 0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::UseDeclContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::IdentDefContext* OpenCMLParser::UseDeclContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
}


size_t OpenCMLParser::UseDeclContext::getRuleIndex() const {
  return OpenCMLParser::RuleUseDecl;
}


std::any OpenCMLParser::UseDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitUseDecl(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::UseDeclContext* OpenCMLParser::useDecl() {
  UseDeclContext *_localctx = _tracker.createInstance<UseDeclContext>(_ctx, getState());
  enterRule(_localctx, 34, OpenCMLParser::RuleUseDecl);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(311);
    match(OpenCMLParser::USE);
    setState(315);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx)) {
    case 1: {
      setState(312);
      identDef();
      setState(313);
      match(OpenCMLParser::T__9);
      break;
    }

    default:
      break;
    }
    setState(317);
    identRef();
   
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

OpenCMLParser::DataExprContext* OpenCMLParser::RetStmtContext::dataExpr() {
  return getRuleContext<OpenCMLParser::DataExprContext>(0);
}

tree::TerminalNode* OpenCMLParser::RetStmtContext::RETURN() {
  return getToken(OpenCMLParser::RETURN, 0);
}

tree::TerminalNode* OpenCMLParser::RetStmtContext::RAISE() {
  return getToken(OpenCMLParser::RAISE, 0);
}

tree::TerminalNode* OpenCMLParser::RetStmtContext::THROW() {
  return getToken(OpenCMLParser::THROW, 0);
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
  enterRule(_localctx, 36, OpenCMLParser::RuleRetStmt);
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
    setState(319);
    _la = _input->LA(1);
    if (!(((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 393217) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(320);
    dataExpr();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TypeDeclContext ------------------------------------------------------------------

OpenCMLParser::TypeDeclContext::TypeDeclContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::TypeDeclContext::TYPE() {
  return getToken(OpenCMLParser::TYPE, 0);
}

OpenCMLParser::IdentDefContext* OpenCMLParser::TypeDeclContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::TypeDeclContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}


size_t OpenCMLParser::TypeDeclContext::getRuleIndex() const {
  return OpenCMLParser::RuleTypeDecl;
}


std::any OpenCMLParser::TypeDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitTypeDecl(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::TypeDeclContext* OpenCMLParser::typeDecl() {
  TypeDeclContext *_localctx = _tracker.createInstance<TypeDeclContext>(_ctx, getState());
  enterRule(_localctx, 38, OpenCMLParser::RuleTypeDecl);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(322);
    match(OpenCMLParser::TYPE);
    setState(323);
    identDef();
    setState(324);
    match(OpenCMLParser::T__9);
    setState(325);
    typeExpr();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- EnumDeclContext ------------------------------------------------------------------

OpenCMLParser::EnumDeclContext::EnumDeclContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::EnumDeclContext::ENUM() {
  return getToken(OpenCMLParser::ENUM, 0);
}

OpenCMLParser::IdentDefContext* OpenCMLParser::EnumDeclContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
}

OpenCMLParser::PairedValuesContext* OpenCMLParser::EnumDeclContext::pairedValues() {
  return getRuleContext<OpenCMLParser::PairedValuesContext>(0);
}

tree::TerminalNode* OpenCMLParser::EnumDeclContext::OF() {
  return getToken(OpenCMLParser::OF, 0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::EnumDeclContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}


size_t OpenCMLParser::EnumDeclContext::getRuleIndex() const {
  return OpenCMLParser::RuleEnumDecl;
}


std::any OpenCMLParser::EnumDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEnumDecl(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::EnumDeclContext* OpenCMLParser::enumDecl() {
  EnumDeclContext *_localctx = _tracker.createInstance<EnumDeclContext>(_ctx, getState());
  enterRule(_localctx, 40, OpenCMLParser::RuleEnumDecl);
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
    match(OpenCMLParser::ENUM);
    setState(328);
    identDef();
    setState(331);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::OF) {
      setState(329);
      match(OpenCMLParser::OF);
      setState(330);
      typeExpr();
    }
    setState(333);
    match(OpenCMLParser::T__9);
    setState(334);
    match(OpenCMLParser::T__0);
    setState(335);
    pairedValues();
    setState(337);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(336);
      match(OpenCMLParser::T__5);
    }
    setState(339);
    match(OpenCMLParser::T__1);
   
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
  enterRule(_localctx, 42, OpenCMLParser::RuleExprStmt);
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
    setState(342);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__10) {
      setState(341);
      annotations();
    }
    setState(344);
    dataExpr();
   
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
  enterRule(_localctx, 44, OpenCMLParser::RuleAnnotation);

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
    match(OpenCMLParser::T__10);
    setState(347);
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
  enterRule(_localctx, 46, OpenCMLParser::RuleAnnotations);
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
    setState(350); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(349);
      annotation();
      setState(352); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == OpenCMLParser::T__10);
   
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
  enterRule(_localctx, 48, OpenCMLParser::RuleModifiers);
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
    setState(355); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(354);
      _la = _input->LA(1);
      if (!(((((_la - 83) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 83)) & 63) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(357); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (((((_la - 83) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 83)) & 63) != 0));
   
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
  enterRule(_localctx, 50, OpenCMLParser::RuleIndexValue);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(362);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__0:
      case OpenCMLParser::T__4:
      case OpenCMLParser::T__7:
      case OpenCMLParser::T__12:
      case OpenCMLParser::T__33:
      case OpenCMLParser::T__40:
      case OpenCMLParser::T__41:
      case OpenCMLParser::WAIT:
      case OpenCMLParser::IF:
      case OpenCMLParser::MATCH:
      case OpenCMLParser::TRY:
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
        setState(359);
        dataExpr();
        break;
      }

      case OpenCMLParser::T__11: {
        enterOuterAlt(_localctx, 2);
        setState(360);
        match(OpenCMLParser::T__11);
        setState(361);
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
  enterRule(_localctx, 52, OpenCMLParser::RuleKeyTypePair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(364);
    identDef();
    setState(365);
    match(OpenCMLParser::T__2);
    setState(366);
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
  enterRule(_localctx, 54, OpenCMLParser::RuleKeyValuePair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(374);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(368);
        identDef();
        setState(369);
        match(OpenCMLParser::T__2);
        setState(370);
        dataExpr();
        break;
      }

      case OpenCMLParser::T__11: {
        enterOuterAlt(_localctx, 2);
        setState(372);
        match(OpenCMLParser::T__11);
        setState(373);
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

std::vector<OpenCMLParser::IdentDefContext *> OpenCMLParser::KeyParamPairContext::identDef() {
  return getRuleContexts<OpenCMLParser::IdentDefContext>();
}

OpenCMLParser::IdentDefContext* OpenCMLParser::KeyParamPairContext::identDef(size_t i) {
  return getRuleContext<OpenCMLParser::IdentDefContext>(i);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::KeyParamPairContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

tree::TerminalNode* OpenCMLParser::KeyParamPairContext::TYPEAS() {
  return getToken(OpenCMLParser::TYPEAS, 0);
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
  enterRule(_localctx, 56, OpenCMLParser::RuleKeyParamPair);
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
    setState(377);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR) {
      setState(376);
      match(OpenCMLParser::VAR);
    }
    setState(379);
    identDef();
    setState(381);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__10) {
      setState(380);
      annotation();
    }
    setState(383);
    match(OpenCMLParser::T__2);
    setState(387);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__0:
      case OpenCMLParser::T__4:
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
        setState(384);
        typeExpr();
        break;
      }

      case OpenCMLParser::TYPEAS: {
        setState(385);
        match(OpenCMLParser::TYPEAS);
        setState(386);
        identDef();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(391);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__9) {
      setState(389);
      match(OpenCMLParser::T__9);
      setState(390);
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
  enterRule(_localctx, 58, OpenCMLParser::RuleIdentList);

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
    setState(393);
    identDef();
    setState(398);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 43, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(394);
        match(OpenCMLParser::T__5);
        setState(395);
        identDef(); 
      }
      setState(400);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 43, _ctx);
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
  enterRule(_localctx, 60, OpenCMLParser::RuleValueList);

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
    setState(401);
    dataExpr();
    setState(406);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 44, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(402);
        match(OpenCMLParser::T__5);
        setState(403);
        dataExpr(); 
      }
      setState(408);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 44, _ctx);
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
  enterRule(_localctx, 62, OpenCMLParser::RuleIndexValues);

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
    setState(409);
    indexValue();
    setState(414);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 45, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(410);
        match(OpenCMLParser::T__5);
        setState(411);
        indexValue(); 
      }
      setState(416);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 45, _ctx);
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
  enterRule(_localctx, 64, OpenCMLParser::RulePairedValues);

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
    setState(417);
    keyValuePair();
    setState(422);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 46, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(418);
        match(OpenCMLParser::T__5);
        setState(419);
        keyValuePair(); 
      }
      setState(424);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 46, _ctx);
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
  enterRule(_localctx, 66, OpenCMLParser::RulePairedParams);

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
    setState(425);
    keyParamPair();
    setState(430);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 47, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(426);
        match(OpenCMLParser::T__5);
        setState(427);
        keyParamPair(); 
      }
      setState(432);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 47, _ctx);
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
  enterRule(_localctx, 68, OpenCMLParser::RuleArgumentList);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(439);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 49, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(433);
      indexValues();
      setState(436);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 48, _ctx)) {
      case 1: {
        setState(434);
        match(OpenCMLParser::T__5);
        setState(435);
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
      setState(438);
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
  enterRule(_localctx, 70, OpenCMLParser::RuleMemberAccess);
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
    setState(441);
    match(OpenCMLParser::T__7);
    setState(442);
    dataExpr();
    setState(449);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__2) {
      setState(443);
      match(OpenCMLParser::T__2);
      setState(444);
      dataExpr();
      setState(447);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__2) {
        setState(445);
        match(OpenCMLParser::T__2);
        setState(446);
        dataExpr();
      }
    }
    setState(451);
    match(OpenCMLParser::T__8);
   
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
  enterRule(_localctx, 72, OpenCMLParser::RuleParentParams);
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
    setState(453);
    match(OpenCMLParser::T__4);
    setState(455);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(454);
      pairedParams();
    }
    setState(458);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(457);
      match(OpenCMLParser::T__5);
    }
    setState(460);
    match(OpenCMLParser::T__6);
   
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
  enterRule(_localctx, 74, OpenCMLParser::RuleParentArgues);
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
    setState(462);
    match(OpenCMLParser::T__4);
    setState(464);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -9223365422605127390) != 0) || ((((_la - 70) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 70)) & 8459903241) != 0)) {
      setState(463);
      argumentList();
    }
    setState(467);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(466);
      match(OpenCMLParser::T__5);
    }
    setState(469);
    match(OpenCMLParser::T__6);
   
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
    setState(471);
    match(OpenCMLParser::T__12);
    setState(473);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(472);
      pairedParams();
    }
    setState(476);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(475);
      match(OpenCMLParser::T__5);
    }
    setState(478);
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
    setState(480);
    match(OpenCMLParser::T__12);
    setState(482);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -9223365422605127390) != 0) || ((((_la - 70) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 70)) & 8459903241) != 0)) {
      setState(481);
      argumentList();
    }
    setState(485);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(484);
      match(OpenCMLParser::T__5);
    }
    setState(487);
    match(OpenCMLParser::T__13);
   
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
  enterRule(_localctx, 80, OpenCMLParser::RuleDataExpr);
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
    setState(490);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WAIT) {
      setState(489);
      match(OpenCMLParser::WAIT);
    }
    setState(492);
    structExpr();
    setState(495);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx)) {
    case 1: {
      setState(493);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8356864) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(494);
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

//----------------- PatternContext ------------------------------------------------------------------

OpenCMLParser::PatternContext::PatternContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::PatternContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::LiteralContext* OpenCMLParser::PatternContext::literal() {
  return getRuleContext<OpenCMLParser::LiteralContext>(0);
}

OpenCMLParser::ValueListContext* OpenCMLParser::PatternContext::valueList() {
  return getRuleContext<OpenCMLParser::ValueListContext>(0);
}

OpenCMLParser::IdentListContext* OpenCMLParser::PatternContext::identList() {
  return getRuleContext<OpenCMLParser::IdentListContext>(0);
}

OpenCMLParser::PairedValuesContext* OpenCMLParser::PatternContext::pairedValues() {
  return getRuleContext<OpenCMLParser::PairedValuesContext>(0);
}


size_t OpenCMLParser::PatternContext::getRuleIndex() const {
  return OpenCMLParser::RulePattern;
}


std::any OpenCMLParser::PatternContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitPattern(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::PatternContext* OpenCMLParser::pattern() {
  PatternContext *_localctx = _tracker.createInstance<PatternContext>(_ctx, getState());
  enterRule(_localctx, 82, OpenCMLParser::RulePattern);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(518);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(497);
        identRef();
        break;
      }

      case OpenCMLParser::NULL_:
      case OpenCMLParser::TRUE:
      case OpenCMLParser::FALSE:
      case OpenCMLParser::MULTI_STR:
      case OpenCMLParser::STRING:
      case OpenCMLParser::FSTRING:
      case OpenCMLParser::INTEGER:
      case OpenCMLParser::REAL: {
        enterOuterAlt(_localctx, 2);
        setState(498);
        literal();
        break;
      }

      case OpenCMLParser::T__4: {
        enterOuterAlt(_localctx, 3);
        setState(499);
        match(OpenCMLParser::T__4);
        setState(502);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 62, _ctx)) {
        case 1: {
          setState(500);
          valueList();
          break;
        }

        case 2: {
          setState(501);
          identList();
          break;
        }

        default:
          break;
        }
        setState(505);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__5) {
          setState(504);
          match(OpenCMLParser::T__5);
        }
        setState(507);
        match(OpenCMLParser::T__6);
        break;
      }

      case OpenCMLParser::T__0: {
        enterOuterAlt(_localctx, 4);
        setState(508);
        match(OpenCMLParser::T__0);
        setState(511);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 64, _ctx)) {
        case 1: {
          setState(509);
          pairedValues();
          break;
        }

        case 2: {
          setState(510);
          identList();
          break;
        }

        default:
          break;
        }
        setState(514);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__5) {
          setState(513);
          match(OpenCMLParser::T__5);
        }
        setState(516);
        match(OpenCMLParser::T__1);
        break;
      }

      case OpenCMLParser::T__22: {
        enterOuterAlt(_localctx, 5);
        setState(517);
        match(OpenCMLParser::T__22);
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

//----------------- MatchCaseContext ------------------------------------------------------------------

OpenCMLParser::MatchCaseContext::MatchCaseContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::MatchCaseContext::CASE() {
  return getToken(OpenCMLParser::CASE, 0);
}

std::vector<OpenCMLParser::PatternContext *> OpenCMLParser::MatchCaseContext::pattern() {
  return getRuleContexts<OpenCMLParser::PatternContext>();
}

OpenCMLParser::PatternContext* OpenCMLParser::MatchCaseContext::pattern(size_t i) {
  return getRuleContext<OpenCMLParser::PatternContext>(i);
}

OpenCMLParser::BlockExprContext* OpenCMLParser::MatchCaseContext::blockExpr() {
  return getRuleContext<OpenCMLParser::BlockExprContext>(0);
}


size_t OpenCMLParser::MatchCaseContext::getRuleIndex() const {
  return OpenCMLParser::RuleMatchCase;
}


std::any OpenCMLParser::MatchCaseContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitMatchCase(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::MatchCaseContext* OpenCMLParser::matchCase() {
  MatchCaseContext *_localctx = _tracker.createInstance<MatchCaseContext>(_ctx, getState());
  enterRule(_localctx, 84, OpenCMLParser::RuleMatchCase);
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
    setState(520);
    match(OpenCMLParser::CASE);
    setState(521);
    pattern();
    setState(526);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__23) {
      setState(522);
      match(OpenCMLParser::T__23);
      setState(523);
      pattern();
      setState(528);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(529);
    match(OpenCMLParser::T__3);
    setState(530);
    blockExpr();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- CatchClauseContext ------------------------------------------------------------------

OpenCMLParser::CatchClauseContext::CatchClauseContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::CatchClauseContext::CATCH() {
  return getToken(OpenCMLParser::CATCH, 0);
}

OpenCMLParser::IdentDefContext* OpenCMLParser::CatchClauseContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::CatchClauseContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

OpenCMLParser::StmtBlockContext* OpenCMLParser::CatchClauseContext::stmtBlock() {
  return getRuleContext<OpenCMLParser::StmtBlockContext>(0);
}


size_t OpenCMLParser::CatchClauseContext::getRuleIndex() const {
  return OpenCMLParser::RuleCatchClause;
}


std::any OpenCMLParser::CatchClauseContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitCatchClause(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::CatchClauseContext* OpenCMLParser::catchClause() {
  CatchClauseContext *_localctx = _tracker.createInstance<CatchClauseContext>(_ctx, getState());
  enterRule(_localctx, 86, OpenCMLParser::RuleCatchClause);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(532);
    match(OpenCMLParser::CATCH);
    setState(533);
    identDef();
    setState(534);
    match(OpenCMLParser::T__2);
    setState(535);
    typeExpr();
    setState(536);
    stmtBlock();
   
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

std::vector<OpenCMLParser::MatchCaseContext *> OpenCMLParser::StructExprContext::matchCase() {
  return getRuleContexts<OpenCMLParser::MatchCaseContext>();
}

OpenCMLParser::MatchCaseContext* OpenCMLParser::StructExprContext::matchCase(size_t i) {
  return getRuleContext<OpenCMLParser::MatchCaseContext>(i);
}

tree::TerminalNode* OpenCMLParser::StructExprContext::TRY() {
  return getToken(OpenCMLParser::TRY, 0);
}

std::vector<OpenCMLParser::StmtBlockContext *> OpenCMLParser::StructExprContext::stmtBlock() {
  return getRuleContexts<OpenCMLParser::StmtBlockContext>();
}

OpenCMLParser::StmtBlockContext* OpenCMLParser::StructExprContext::stmtBlock(size_t i) {
  return getRuleContext<OpenCMLParser::StmtBlockContext>(i);
}

std::vector<OpenCMLParser::CatchClauseContext *> OpenCMLParser::StructExprContext::catchClause() {
  return getRuleContexts<OpenCMLParser::CatchClauseContext>();
}

OpenCMLParser::CatchClauseContext* OpenCMLParser::StructExprContext::catchClause(size_t i) {
  return getRuleContext<OpenCMLParser::CatchClauseContext>(i);
}

tree::TerminalNode* OpenCMLParser::StructExprContext::FINALLY() {
  return getToken(OpenCMLParser::FINALLY, 0);
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
  enterRule(_localctx, 88, OpenCMLParser::RuleStructExpr);
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
    setState(567);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__0:
      case OpenCMLParser::T__4:
      case OpenCMLParser::T__7:
      case OpenCMLParser::T__12:
      case OpenCMLParser::T__33:
      case OpenCMLParser::T__40:
      case OpenCMLParser::T__41:
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
        setState(538);
        logicalOrExpr();
        break;
      }

      case OpenCMLParser::IF: {
        enterOuterAlt(_localctx, 2);
        setState(539);
        match(OpenCMLParser::IF);
        setState(540);
        logicalOrExpr();
        setState(541);
        match(OpenCMLParser::THEN);
        setState(542);
        blockExpr();
        setState(543);
        match(OpenCMLParser::ELSE);
        setState(544);
        blockExpr();
        break;
      }

      case OpenCMLParser::MATCH: {
        enterOuterAlt(_localctx, 3);
        setState(546);
        match(OpenCMLParser::MATCH);
        setState(547);
        identRef();
        setState(548);
        match(OpenCMLParser::T__0);
        setState(550); 
        _errHandler->sync(this);
        _la = _input->LA(1);
        do {
          setState(549);
          matchCase();
          setState(552); 
          _errHandler->sync(this);
          _la = _input->LA(1);
        } while (_la == OpenCMLParser::CASE);
        setState(554);
        match(OpenCMLParser::T__1);
        break;
      }

      case OpenCMLParser::TRY: {
        enterOuterAlt(_localctx, 4);
        setState(556);
        match(OpenCMLParser::TRY);
        setState(557);
        stmtBlock();
        setState(559); 
        _errHandler->sync(this);
        alt = 1;
        do {
          switch (alt) {
            case 1: {
                  setState(558);
                  catchClause();
                  break;
                }

          default:
            throw NoViableAltException(this);
          }
          setState(561); 
          _errHandler->sync(this);
          alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx);
        } while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER);
        setState(565);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx)) {
        case 1: {
          setState(563);
          match(OpenCMLParser::FINALLY);
          setState(564);
          stmtBlock();
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
  enterRule(_localctx, 90, OpenCMLParser::RuleLogicalOrExpr);

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
    setState(569);
    logicalAndExpr();
    setState(574);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 72, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(570);
        match(OpenCMLParser::T__24);
        setState(571);
        logicalAndExpr(); 
      }
      setState(576);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 72, _ctx);
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
  enterRule(_localctx, 92, OpenCMLParser::RuleLogicalAndExpr);

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
    setState(577);
    equalityExpr();
    setState(582);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 73, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(578);
        match(OpenCMLParser::T__25);
        setState(579);
        equalityExpr(); 
      }
      setState(584);
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
  enterRule(_localctx, 94, OpenCMLParser::RuleEqualityExpr);
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
    setState(585);
    relationalExpr();
    setState(590);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(586);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 2013265920) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(587);
        relationalExpr(); 
      }
      setState(592);
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
  enterRule(_localctx, 96, OpenCMLParser::RuleRelationalExpr);
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
    setState(593);
    additiveExpr();
    setState(598);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 75, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(594);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 6442475520) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(595);
        additiveExpr(); 
      }
      setState(600);
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
  enterRule(_localctx, 98, OpenCMLParser::RuleAdditiveExpr);
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
    setState(601);
    multiplicativeExpr();
    setState(606);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 76, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(602);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__32

        || _la == OpenCMLParser::T__33)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(603);
        multiplicativeExpr(); 
      }
      setState(608);
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
  enterRule(_localctx, 100, OpenCMLParser::RuleMultiplicativeExpr);
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
    setState(609);
    nullableExpr();
    setState(614);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 77, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(610);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 515396075520) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(611);
        nullableExpr(); 
      }
      setState(616);
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
  enterRule(_localctx, 102, OpenCMLParser::RuleNullableExpr);
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
    setState(617);
    unaryExpr();
    setState(620);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 78, _ctx)) {
    case 1: {
      setState(618);
      _la = _input->LA(1);
      if (!(_la == OpenCMLParser::T__38

      || _la == OpenCMLParser::T__39)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(619);
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
  enterRule(_localctx, 104, OpenCMLParser::RuleUnaryExpr);
  size_t _la = 0;

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
    switch (_input->LA(1)) {
      case OpenCMLParser::T__0:
      case OpenCMLParser::T__4:
      case OpenCMLParser::T__7:
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
        setState(622);
        linkExpr();
        setState(625);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 79, _ctx)) {
        case 1: {
          setState(623);
          _la = _input->LA(1);
          if (!(_la == OpenCMLParser::AS

          || _la == OpenCMLParser::IS)) {
          _errHandler->recoverInline(this);
          }
          else {
            _errHandler->reportMatch(this);
            consume();
          }
          setState(624);
          typeExpr();
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::T__33:
      case OpenCMLParser::T__40:
      case OpenCMLParser::T__41: {
        enterOuterAlt(_localctx, 2);
        setState(627);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 6614249635840) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(628);
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
  enterRule(_localctx, 106, OpenCMLParser::RuleLinkExpr);
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
    setState(631);
    bindExpr();
    setState(636);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 81, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(632);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__42

        || _la == OpenCMLParser::T__43)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(633);
        bindExpr(); 
      }
      setState(638);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 81, _ctx);
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
  enterRule(_localctx, 108, OpenCMLParser::RuleBindExpr);
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
    setState(639);
    withExpr();
    setState(644);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 82, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(640);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__44

        || _la == OpenCMLParser::T__45)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(641);
        withExpr(); 
      }
      setState(646);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 82, _ctx);
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

std::vector<OpenCMLParser::AnnoExprContext *> OpenCMLParser::WithExprContext::annoExpr() {
  return getRuleContexts<OpenCMLParser::AnnoExprContext>();
}

OpenCMLParser::AnnoExprContext* OpenCMLParser::WithExprContext::annoExpr(size_t i) {
  return getRuleContext<OpenCMLParser::AnnoExprContext>(i);
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
  enterRule(_localctx, 110, OpenCMLParser::RuleWithExpr);
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
    setState(647);
    annoExpr();
    setState(652);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 83, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(648);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__46

        || _la == OpenCMLParser::T__47)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(649);
        annoExpr(); 
      }
      setState(654);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 83, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AnnoExprContext ------------------------------------------------------------------

OpenCMLParser::AnnoExprContext::AnnoExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimaryDataContext* OpenCMLParser::AnnoExprContext::primaryData() {
  return getRuleContext<OpenCMLParser::PrimaryDataContext>(0);
}

std::vector<OpenCMLParser::AnnotationContext *> OpenCMLParser::AnnoExprContext::annotation() {
  return getRuleContexts<OpenCMLParser::AnnotationContext>();
}

OpenCMLParser::AnnotationContext* OpenCMLParser::AnnoExprContext::annotation(size_t i) {
  return getRuleContext<OpenCMLParser::AnnotationContext>(i);
}

std::vector<OpenCMLParser::MemberAccessContext *> OpenCMLParser::AnnoExprContext::memberAccess() {
  return getRuleContexts<OpenCMLParser::MemberAccessContext>();
}

OpenCMLParser::MemberAccessContext* OpenCMLParser::AnnoExprContext::memberAccess(size_t i) {
  return getRuleContext<OpenCMLParser::MemberAccessContext>(i);
}

std::vector<OpenCMLParser::ParentArguesContext *> OpenCMLParser::AnnoExprContext::parentArgues() {
  return getRuleContexts<OpenCMLParser::ParentArguesContext>();
}

OpenCMLParser::ParentArguesContext* OpenCMLParser::AnnoExprContext::parentArgues(size_t i) {
  return getRuleContext<OpenCMLParser::ParentArguesContext>(i);
}

std::vector<OpenCMLParser::AngledValuesContext *> OpenCMLParser::AnnoExprContext::angledValues() {
  return getRuleContexts<OpenCMLParser::AngledValuesContext>();
}

OpenCMLParser::AngledValuesContext* OpenCMLParser::AnnoExprContext::angledValues(size_t i) {
  return getRuleContext<OpenCMLParser::AngledValuesContext>(i);
}


size_t OpenCMLParser::AnnoExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleAnnoExpr;
}


std::any OpenCMLParser::AnnoExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitAnnoExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::AnnoExprContext* OpenCMLParser::annoExpr() {
  AnnoExprContext *_localctx = _tracker.createInstance<AnnoExprContext>(_ctx, getState());
  enterRule(_localctx, 112, OpenCMLParser::RuleAnnoExpr);

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
    setState(655);
    primaryData();
    setState(666);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 86, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(664);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 85, _ctx)) {
        case 1: {
          setState(656);

          if (!(isAdjacent())) throw FailedPredicateException(this, "isAdjacent()");
          setState(661);
          _errHandler->sync(this);
          switch (_input->LA(1)) {
            case OpenCMLParser::T__7: {
              setState(657);
              memberAccess();
              break;
            }

            case OpenCMLParser::T__4: {
              setState(658);
              parentArgues();
              break;
            }

            case OpenCMLParser::T__12: {
              setState(659);
              angledValues();
              break;
            }

            case OpenCMLParser::T__40: {
              setState(660);
              match(OpenCMLParser::T__40);
              break;
            }

          default:
            throw NoViableAltException(this);
          }
          break;
        }

        case 2: {
          setState(663);
          annotation();
          break;
        }

        default:
          break;
        } 
      }
      setState(668);
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
  enterRule(_localctx, 114, OpenCMLParser::RuleDictExpr);
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
    setState(669);
    match(OpenCMLParser::T__0);
    setState(674);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__11 || _la == OpenCMLParser::IDENTIFIER) {
      setState(670);
      pairedValues();
      setState(672);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__5) {
        setState(671);
        match(OpenCMLParser::T__5);
      }
    }
    setState(676);
    match(OpenCMLParser::T__1);
   
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
  enterRule(_localctx, 116, OpenCMLParser::RuleListExpr);
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
    setState(678);
    match(OpenCMLParser::T__7);
    setState(692);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 91, _ctx)) {
    case 1: {
      setState(679);
      indexValues();
      setState(681);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__5) {
        setState(680);
        match(OpenCMLParser::T__5);
      }
      break;
    }

    case 2: {
      setState(683);
      dataExpr();
      setState(684);
      match(OpenCMLParser::FOR);
      setState(685);
      identRef();
      setState(686);
      match(OpenCMLParser::IN);
      setState(687);
      dataExpr();
      setState(690);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::IF) {
        setState(688);
        match(OpenCMLParser::IF);
        setState(689);
        dataExpr();
      }
      break;
    }

    default:
      break;
    }
    setState(694);
    match(OpenCMLParser::T__8);
   
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

OpenCMLParser::ValueListContext* OpenCMLParser::PrimaryDataContext::valueList() {
  return getRuleContext<OpenCMLParser::ValueListContext>(0);
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
  enterRule(_localctx, 118, OpenCMLParser::RulePrimaryData);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(713);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 94, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(696);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(697);
      literal();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(698);
      listExpr();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(699);
      dictExpr();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(700);
      match(OpenCMLParser::T__4);
      setState(701);
      dataExpr();
      setState(702);
      match(OpenCMLParser::T__6);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(704);
      match(OpenCMLParser::T__4);
      setState(706);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & -9223365422605131486) != 0) || ((((_la - 70) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 70)) & 8459903241) != 0)) {
        setState(705);
        valueList();
      }
      setState(709);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__5) {
        setState(708);
        match(OpenCMLParser::T__5);
      }
      setState(711);
      match(OpenCMLParser::T__6);
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(712);
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
  enterRule(_localctx, 120, OpenCMLParser::RuleLiteral);
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
    setState(715);
    _la = _input->LA(1);
    if (!(((((_la - 89) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 89)) & 15623) != 0))) {
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

//----------------- TypeExprContext ------------------------------------------------------------------

OpenCMLParser::TypeExprContext::TypeExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::UnionTypeContext *> OpenCMLParser::TypeExprContext::unionType() {
  return getRuleContexts<OpenCMLParser::UnionTypeContext>();
}

OpenCMLParser::UnionTypeContext* OpenCMLParser::TypeExprContext::unionType(size_t i) {
  return getRuleContext<OpenCMLParser::UnionTypeContext>(i);
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
  enterRule(_localctx, 122, OpenCMLParser::RuleTypeExpr);

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
    unionType();
    setState(722);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 96, _ctx)) {
    case 1: {
      setState(718);
      match(OpenCMLParser::T__48);
      setState(720);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 95, _ctx)) {
      case 1: {
        setState(719);
        unionType();
        break;
      }

      default:
        break;
      }
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

std::vector<OpenCMLParser::UnionUnitContext *> OpenCMLParser::UnionTypeContext::unionUnit() {
  return getRuleContexts<OpenCMLParser::UnionUnitContext>();
}

OpenCMLParser::UnionUnitContext* OpenCMLParser::UnionTypeContext::unionUnit(size_t i) {
  return getRuleContext<OpenCMLParser::UnionUnitContext>(i);
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
  enterRule(_localctx, 124, OpenCMLParser::RuleUnionType);

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
    setState(724);
    unionUnit();
    setState(729);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 97, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(725);
        match(OpenCMLParser::T__23);
        setState(726);
        unionUnit(); 
      }
      setState(731);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 97, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UnionUnitContext ------------------------------------------------------------------

OpenCMLParser::UnionUnitContext::UnionUnitContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::ListTypeContext* OpenCMLParser::UnionUnitContext::listType() {
  return getRuleContext<OpenCMLParser::ListTypeContext>(0);
}

OpenCMLParser::IdentDefContext* OpenCMLParser::UnionUnitContext::identDef() {
  return getRuleContext<OpenCMLParser::IdentDefContext>(0);
}

tree::TerminalNode* OpenCMLParser::UnionUnitContext::OF() {
  return getToken(OpenCMLParser::OF, 0);
}


size_t OpenCMLParser::UnionUnitContext::getRuleIndex() const {
  return OpenCMLParser::RuleUnionUnit;
}


std::any OpenCMLParser::UnionUnitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitUnionUnit(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::UnionUnitContext* OpenCMLParser::unionUnit() {
  UnionUnitContext *_localctx = _tracker.createInstance<UnionUnitContext>(_ctx, getState());
  enterRule(_localctx, 126, OpenCMLParser::RuleUnionUnit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(735);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 98, _ctx)) {
    case 1: {
      setState(732);
      identDef();
      setState(733);
      match(OpenCMLParser::OF);
      break;
    }

    default:
      break;
    }
    setState(737);
    listType();
   
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
  enterRule(_localctx, 128, OpenCMLParser::RuleListType);

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
    setState(739);
    argsType();
    setState(744);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 99, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(740);
        match(OpenCMLParser::T__7);
        setState(741);
        match(OpenCMLParser::T__8); 
      }
      setState(746);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 99, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TypeOrDataContext ------------------------------------------------------------------

OpenCMLParser::TypeOrDataContext::TypeOrDataContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::TypeExprContext* OpenCMLParser::TypeOrDataContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

OpenCMLParser::PrimaryDataContext* OpenCMLParser::TypeOrDataContext::primaryData() {
  return getRuleContext<OpenCMLParser::PrimaryDataContext>(0);
}


size_t OpenCMLParser::TypeOrDataContext::getRuleIndex() const {
  return OpenCMLParser::RuleTypeOrData;
}


std::any OpenCMLParser::TypeOrDataContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitTypeOrData(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::TypeOrDataContext* OpenCMLParser::typeOrData() {
  TypeOrDataContext *_localctx = _tracker.createInstance<TypeOrDataContext>(_ctx, getState());
  enterRule(_localctx, 130, OpenCMLParser::RuleTypeOrData);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(749);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 100, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(747);
      typeExpr();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(748);
      primaryData();
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

//----------------- ArgsTypeContext ------------------------------------------------------------------

OpenCMLParser::ArgsTypeContext::ArgsTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimaryTypeContext* OpenCMLParser::ArgsTypeContext::primaryType() {
  return getRuleContext<OpenCMLParser::PrimaryTypeContext>(0);
}

std::vector<OpenCMLParser::TypeOrDataContext *> OpenCMLParser::ArgsTypeContext::typeOrData() {
  return getRuleContexts<OpenCMLParser::TypeOrDataContext>();
}

OpenCMLParser::TypeOrDataContext* OpenCMLParser::ArgsTypeContext::typeOrData(size_t i) {
  return getRuleContext<OpenCMLParser::TypeOrDataContext>(i);
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
  enterRule(_localctx, 132, OpenCMLParser::RuleArgsType);
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
    setState(751);
    primaryType();
    setState(763);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 102, _ctx)) {
    case 1: {
      setState(752);
      match(OpenCMLParser::T__12);
      setState(753);
      typeOrData();
      setState(758);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == OpenCMLParser::T__5) {
        setState(754);
        match(OpenCMLParser::T__5);
        setState(755);
        typeOrData();
        setState(760);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(761);
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

OpenCMLParser::TupleTypeContext* OpenCMLParser::PrimaryTypeContext::tupleType() {
  return getRuleContext<OpenCMLParser::TupleTypeContext>(0);
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
  enterRule(_localctx, 134, OpenCMLParser::RulePrimaryType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(776);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 103, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(765);
      match(OpenCMLParser::INNER_ATOM_TYPE);
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(766);
      dictExprType();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(767);
      identRef();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(768);
      match(OpenCMLParser::T__4);
      setState(769);
      typeExpr();
      setState(770);
      match(OpenCMLParser::T__6);
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(772);
      tupleType();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(773);
      lambdaType();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(774);
      match(OpenCMLParser::TYPEOF);
      setState(775);
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
  enterRule(_localctx, 136, OpenCMLParser::RuleDictExprType);
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
    setState(778);
    dictType();
    setState(783);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 104, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(779);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__34

        || _la == OpenCMLParser::T__49)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(780);
        dictType(); 
      }
      setState(785);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 104, _ctx);
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

std::vector<OpenCMLParser::KeyTypePairContext *> OpenCMLParser::DictTypeContext::keyTypePair() {
  return getRuleContexts<OpenCMLParser::KeyTypePairContext>();
}

OpenCMLParser::KeyTypePairContext* OpenCMLParser::DictTypeContext::keyTypePair(size_t i) {
  return getRuleContext<OpenCMLParser::KeyTypePairContext>(i);
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
  enterRule(_localctx, 138, OpenCMLParser::RuleDictType);
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
    setState(786);
    match(OpenCMLParser::T__0);
    setState(795);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(787);
      keyTypePair();
      setState(792);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 105, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(788);
          match(OpenCMLParser::T__5);
          setState(789);
          keyTypePair(); 
        }
        setState(794);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 105, _ctx);
      }
    }
    setState(798);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(797);
      match(OpenCMLParser::T__5);
    }
    setState(800);
    match(OpenCMLParser::T__1);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TupleTypeContext ------------------------------------------------------------------

OpenCMLParser::TupleTypeContext::TupleTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

std::vector<OpenCMLParser::TypeExprContext *> OpenCMLParser::TupleTypeContext::typeExpr() {
  return getRuleContexts<OpenCMLParser::TypeExprContext>();
}

OpenCMLParser::TypeExprContext* OpenCMLParser::TupleTypeContext::typeExpr(size_t i) {
  return getRuleContext<OpenCMLParser::TypeExprContext>(i);
}


size_t OpenCMLParser::TupleTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleTupleType;
}


std::any OpenCMLParser::TupleTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitTupleType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::TupleTypeContext* OpenCMLParser::tupleType() {
  TupleTypeContext *_localctx = _tracker.createInstance<TupleTypeContext>(_ctx, getState());
  enterRule(_localctx, 140, OpenCMLParser::RuleTupleType);
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
    setState(802);
    match(OpenCMLParser::T__4);
    setState(811);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8226) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 546291713) != 0)) {
      setState(803);
      typeExpr();
      setState(808);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 108, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(804);
          match(OpenCMLParser::T__5);
          setState(805);
          typeExpr(); 
        }
        setState(810);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 108, _ctx);
      }
    }
    setState(814);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(813);
      match(OpenCMLParser::T__5);
    }
    setState(816);
    match(OpenCMLParser::T__6);
   
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
  enterRule(_localctx, 142, OpenCMLParser::RuleLambdaType);
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
    setState(819);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 83) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 83)) & 63) != 0)) {
      setState(818);
      modifiers();
    }
    setState(822);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__12) {
      setState(821);
      angledParams();
    }
    setState(824);
    parentParams();
    setState(825);
    match(OpenCMLParser::T__3);
    setState(826);
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
  enterRule(_localctx, 144, OpenCMLParser::RuleIdentDef);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(828);
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
  enterRule(_localctx, 146, OpenCMLParser::RuleIdentRef);

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
    setState(834);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 113, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(830);
        match(OpenCMLParser::IDENTIFIER);
        setState(831);
        match(OpenCMLParser::T__50); 
      }
      setState(836);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 113, _ctx);
    }
    setState(837);
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
    case 56: return annoExprSempred(antlrcpp::downCast<AnnoExprContext *>(context), predicateIndex);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::annoExprSempred(AnnoExprContext *_localctx, size_t predicateIndex) {
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
