
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
      "exportDecl", "stmtBlock", "lambdaExpr", "funcDecl", "parentIdents", 
      "bracedIdents", "bracketIdents", "carrier", "letStmt", "useStmt", 
      "retStmt", "typeStmt", "enumStmt", "exprStmt", "annotation", "annotations", 
      "modifiers", "indexValue", "keyTypePair", "keyValuePair", "keyParamPair", 
      "identList", "valueList", "indexValues", "pairedValues", "pairedParams", 
      "argumentList", "memberAccess", "parentParams", "parentArgues", "angledParams", 
      "angledValues", "blockExpr", "dataExpr", "pattern", "matchCase", "structExpr", 
      "logicalOrExpr", "logicalAndExpr", "equalityExpr", "relationalExpr", 
      "additiveExpr", "multiplicativeExpr", "nullableExpr", "unaryExpr", 
      "linkExpr", "bindExpr", "withExpr", "annoExpr", "dictExpr", "listExpr", 
      "primaryData", "literal", "typeExpr", "unionType", "unionUnit", "listType", 
      "argsType", "primaryType", "dictExprType", "dictType", "tupleType", 
      "lambdaType", "identDef", "identRef"
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
  	4,1,102,828,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,2,55,7,55,2,56,7,
  	56,2,57,7,57,2,58,7,58,2,59,7,59,2,60,7,60,2,61,7,61,2,62,7,62,2,63,7,
  	63,2,64,7,64,2,65,7,65,2,66,7,66,2,67,7,67,2,68,7,68,2,69,7,69,2,70,7,
  	70,1,0,3,0,144,8,0,1,0,1,0,3,0,148,8,0,1,0,3,0,151,8,0,5,0,153,8,0,10,
  	0,12,0,156,9,0,1,0,1,0,1,1,1,1,1,1,1,1,3,1,164,8,1,1,2,1,2,1,2,1,2,1,
  	2,1,2,1,2,3,2,173,8,2,1,3,1,3,3,3,177,8,3,1,3,5,3,180,8,3,10,3,12,3,183,
  	9,3,1,3,3,3,186,8,3,1,4,1,4,1,4,1,5,1,5,1,5,1,5,3,5,195,8,5,1,5,1,5,1,
  	5,3,5,200,8,5,1,6,1,6,1,6,1,6,3,6,206,8,6,1,7,3,7,209,8,7,1,7,1,7,3,7,
  	213,8,7,1,7,1,7,1,8,3,8,218,8,8,1,8,3,8,221,8,8,1,8,1,8,1,8,3,8,226,8,
  	8,1,8,1,8,1,8,1,9,3,9,232,8,9,1,9,1,9,3,9,236,8,9,1,9,3,9,239,8,9,1,9,
  	3,9,242,8,9,1,9,1,9,1,9,1,9,1,9,3,9,249,8,9,1,9,1,9,1,10,1,10,3,10,255,
  	8,10,1,10,3,10,258,8,10,1,10,1,10,1,11,1,11,3,11,264,8,11,1,11,3,11,267,
  	8,11,1,11,1,11,1,12,1,12,3,12,273,8,12,1,12,3,12,276,8,12,1,12,1,12,1,
  	13,1,13,1,13,1,13,3,13,284,8,13,1,14,1,14,1,14,1,14,3,14,290,8,14,1,14,
  	1,14,1,14,1,15,1,15,1,15,1,15,3,15,299,8,15,1,15,1,15,1,16,1,16,1,16,
  	1,17,1,17,1,17,1,17,1,17,1,18,1,18,1,18,1,18,3,18,315,8,18,1,18,1,18,
  	1,18,1,18,3,18,321,8,18,1,18,1,18,1,19,3,19,326,8,19,1,19,1,19,1,20,1,
  	20,1,20,1,21,4,21,334,8,21,11,21,12,21,335,1,22,4,22,339,8,22,11,22,12,
  	22,340,1,23,1,23,1,23,3,23,346,8,23,1,24,1,24,1,24,1,24,1,25,1,25,1,25,
  	1,25,1,25,1,25,3,25,358,8,25,1,26,3,26,361,8,26,1,26,1,26,3,26,365,8,
  	26,1,26,1,26,1,26,1,26,3,26,371,8,26,1,26,1,26,3,26,375,8,26,1,27,1,27,
  	1,27,5,27,380,8,27,10,27,12,27,383,9,27,1,28,1,28,1,28,5,28,388,8,28,
  	10,28,12,28,391,9,28,1,29,1,29,1,29,5,29,396,8,29,10,29,12,29,399,9,29,
  	1,30,1,30,1,30,5,30,404,8,30,10,30,12,30,407,9,30,1,31,1,31,1,31,5,31,
  	412,8,31,10,31,12,31,415,9,31,1,32,1,32,1,32,3,32,420,8,32,1,32,3,32,
  	423,8,32,1,33,1,33,1,33,1,33,1,33,1,33,3,33,431,8,33,3,33,433,8,33,1,
  	33,1,33,1,34,1,34,3,34,439,8,34,1,34,3,34,442,8,34,1,34,1,34,1,35,1,35,
  	3,35,448,8,35,1,35,3,35,451,8,35,1,35,1,35,1,36,1,36,3,36,457,8,36,1,
  	36,3,36,460,8,36,1,36,1,36,1,37,1,37,3,37,466,8,37,1,37,3,37,469,8,37,
  	1,37,1,37,1,38,1,38,3,38,475,8,38,1,39,3,39,478,8,39,1,39,1,39,1,39,3,
  	39,483,8,39,1,40,1,40,1,40,1,40,1,40,3,40,490,8,40,1,40,3,40,493,8,40,
  	1,40,1,40,1,40,1,40,3,40,499,8,40,1,40,3,40,502,8,40,1,40,1,40,3,40,506,
  	8,40,1,41,1,41,1,41,1,41,5,41,512,8,41,10,41,12,41,515,9,41,1,41,1,41,
  	1,41,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,4,42,
  	532,8,42,11,42,12,42,533,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,
  	1,42,4,42,546,8,42,11,42,12,42,547,1,42,1,42,3,42,552,8,42,3,42,554,8,
  	42,1,43,1,43,1,43,5,43,559,8,43,10,43,12,43,562,9,43,1,44,1,44,1,44,5,
  	44,567,8,44,10,44,12,44,570,9,44,1,45,1,45,1,45,5,45,575,8,45,10,45,12,
  	45,578,9,45,1,46,1,46,1,46,5,46,583,8,46,10,46,12,46,586,9,46,1,47,1,
  	47,1,47,5,47,591,8,47,10,47,12,47,594,9,47,1,48,1,48,1,48,5,48,599,8,
  	48,10,48,12,48,602,9,48,1,49,1,49,1,49,3,49,607,8,49,1,50,1,50,1,50,3,
  	50,612,8,50,1,50,1,50,3,50,616,8,50,1,51,1,51,1,51,5,51,621,8,51,10,51,
  	12,51,624,9,51,1,52,1,52,1,52,5,52,629,8,52,10,52,12,52,632,9,52,1,53,
  	1,53,1,53,5,53,637,8,53,10,53,12,53,640,9,53,1,54,1,54,1,54,1,54,1,54,
  	1,54,3,54,648,8,54,1,54,5,54,651,8,54,10,54,12,54,654,9,54,1,55,1,55,
  	1,55,3,55,659,8,55,3,55,661,8,55,1,55,1,55,1,56,1,56,1,56,3,56,668,8,
  	56,1,56,1,56,1,56,1,56,1,56,1,56,1,56,3,56,677,8,56,3,56,679,8,56,1,56,
  	1,56,1,57,1,57,1,57,1,57,1,57,1,57,1,57,1,57,1,57,1,57,3,57,693,8,57,
  	1,57,3,57,696,8,57,1,57,1,57,3,57,700,8,57,1,58,1,58,1,59,1,59,1,59,3,
  	59,707,8,59,3,59,709,8,59,1,60,1,60,1,60,5,60,714,8,60,10,60,12,60,717,
  	9,60,1,61,1,61,1,61,3,61,722,8,61,1,61,1,61,1,62,1,62,1,62,5,62,729,8,
  	62,10,62,12,62,732,9,62,1,63,1,63,1,63,1,63,3,63,738,8,63,1,63,1,63,1,
  	63,3,63,743,8,63,5,63,745,8,63,10,63,12,63,748,9,63,1,63,1,63,3,63,752,
  	8,63,1,64,1,64,1,64,1,64,1,64,1,64,1,64,1,64,1,64,1,64,1,64,3,64,765,
  	8,64,1,65,1,65,1,65,5,65,770,8,65,10,65,12,65,773,9,65,1,66,1,66,1,66,
  	1,66,5,66,779,8,66,10,66,12,66,782,9,66,3,66,784,8,66,1,66,3,66,787,8,
  	66,1,66,1,66,1,67,1,67,1,67,1,67,5,67,795,8,67,10,67,12,67,798,9,67,3,
  	67,800,8,67,1,67,3,67,803,8,67,1,67,1,67,1,68,3,68,808,8,68,1,68,3,68,
  	811,8,68,1,68,1,68,1,68,1,68,1,69,1,69,1,70,1,70,5,70,821,8,70,10,70,
  	12,70,824,9,70,1,70,1,70,1,70,0,0,71,0,2,4,6,8,10,12,14,16,18,20,22,24,
  	26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,68,70,
  	72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104,106,108,110,112,
  	114,116,118,120,122,124,126,128,130,132,134,136,138,140,0,16,1,0,55,56,
  	2,0,64,64,81,82,1,0,83,88,2,0,10,10,15,22,1,0,27,30,2,0,13,14,31,32,1,
  	0,33,34,1,0,35,38,1,0,39,40,1,0,53,54,2,0,34,34,41,42,1,0,43,44,1,0,45,
  	46,1,0,47,48,3,0,89,91,97,97,99,102,2,0,35,35,50,50,901,0,143,1,0,0,0,
  	2,163,1,0,0,0,4,172,1,0,0,0,6,174,1,0,0,0,8,187,1,0,0,0,10,190,1,0,0,
  	0,12,201,1,0,0,0,14,208,1,0,0,0,16,217,1,0,0,0,18,231,1,0,0,0,20,252,
  	1,0,0,0,22,261,1,0,0,0,24,270,1,0,0,0,26,283,1,0,0,0,28,285,1,0,0,0,30,
  	294,1,0,0,0,32,302,1,0,0,0,34,305,1,0,0,0,36,310,1,0,0,0,38,325,1,0,0,
  	0,40,329,1,0,0,0,42,333,1,0,0,0,44,338,1,0,0,0,46,345,1,0,0,0,48,347,
  	1,0,0,0,50,357,1,0,0,0,52,360,1,0,0,0,54,376,1,0,0,0,56,384,1,0,0,0,58,
  	392,1,0,0,0,60,400,1,0,0,0,62,408,1,0,0,0,64,422,1,0,0,0,66,424,1,0,0,
  	0,68,436,1,0,0,0,70,445,1,0,0,0,72,454,1,0,0,0,74,463,1,0,0,0,76,474,
  	1,0,0,0,78,477,1,0,0,0,80,505,1,0,0,0,82,507,1,0,0,0,84,553,1,0,0,0,86,
  	555,1,0,0,0,88,563,1,0,0,0,90,571,1,0,0,0,92,579,1,0,0,0,94,587,1,0,0,
  	0,96,595,1,0,0,0,98,603,1,0,0,0,100,615,1,0,0,0,102,617,1,0,0,0,104,625,
  	1,0,0,0,106,633,1,0,0,0,108,641,1,0,0,0,110,655,1,0,0,0,112,664,1,0,0,
  	0,114,699,1,0,0,0,116,701,1,0,0,0,118,703,1,0,0,0,120,710,1,0,0,0,122,
  	721,1,0,0,0,124,725,1,0,0,0,126,733,1,0,0,0,128,764,1,0,0,0,130,766,1,
  	0,0,0,132,774,1,0,0,0,134,790,1,0,0,0,136,807,1,0,0,0,138,816,1,0,0,0,
  	140,822,1,0,0,0,142,144,5,52,0,0,143,142,1,0,0,0,143,144,1,0,0,0,144,
  	154,1,0,0,0,145,148,3,2,1,0,146,148,3,4,2,0,147,145,1,0,0,0,147,146,1,
  	0,0,0,148,150,1,0,0,0,149,151,5,52,0,0,150,149,1,0,0,0,150,151,1,0,0,
  	0,151,153,1,0,0,0,152,147,1,0,0,0,153,156,1,0,0,0,154,152,1,0,0,0,154,
  	155,1,0,0,0,155,157,1,0,0,0,156,154,1,0,0,0,157,158,5,0,0,1,158,1,1,0,
  	0,0,159,164,3,8,4,0,160,164,3,10,5,0,161,164,3,12,6,0,162,164,3,18,9,
  	0,163,159,1,0,0,0,163,160,1,0,0,0,163,161,1,0,0,0,163,162,1,0,0,0,164,
  	3,1,0,0,0,165,173,3,28,14,0,166,173,3,30,15,0,167,173,3,32,16,0,168,173,
  	3,34,17,0,169,173,3,36,18,0,170,173,3,38,19,0,171,173,3,14,7,0,172,165,
  	1,0,0,0,172,166,1,0,0,0,172,167,1,0,0,0,172,168,1,0,0,0,172,169,1,0,0,
  	0,172,170,1,0,0,0,172,171,1,0,0,0,173,5,1,0,0,0,174,181,3,4,2,0,175,177,
  	5,52,0,0,176,175,1,0,0,0,176,177,1,0,0,0,177,178,1,0,0,0,178,180,3,4,
  	2,0,179,176,1,0,0,0,180,183,1,0,0,0,181,179,1,0,0,0,181,182,1,0,0,0,182,
  	185,1,0,0,0,183,181,1,0,0,0,184,186,5,52,0,0,185,184,1,0,0,0,185,186,
  	1,0,0,0,186,7,1,0,0,0,187,188,5,65,0,0,188,189,3,138,69,0,189,9,1,0,0,
  	0,190,199,5,66,0,0,191,200,5,99,0,0,192,195,3,138,69,0,193,195,3,22,11,
  	0,194,192,1,0,0,0,194,193,1,0,0,0,195,196,1,0,0,0,196,197,5,58,0,0,197,
  	198,5,99,0,0,198,200,1,0,0,0,199,191,1,0,0,0,199,194,1,0,0,0,200,11,1,
  	0,0,0,201,205,5,67,0,0,202,206,3,28,14,0,203,206,3,34,17,0,204,206,3,
  	22,11,0,205,202,1,0,0,0,205,203,1,0,0,0,205,204,1,0,0,0,206,13,1,0,0,
  	0,207,209,5,83,0,0,208,207,1,0,0,0,208,209,1,0,0,0,209,210,1,0,0,0,210,
  	212,5,1,0,0,211,213,3,6,3,0,212,211,1,0,0,0,212,213,1,0,0,0,213,214,1,
  	0,0,0,214,215,5,2,0,0,215,15,1,0,0,0,216,218,3,44,22,0,217,216,1,0,0,
  	0,217,218,1,0,0,0,218,220,1,0,0,0,219,221,3,72,36,0,220,219,1,0,0,0,220,
  	221,1,0,0,0,221,222,1,0,0,0,222,225,3,68,34,0,223,224,5,3,0,0,224,226,
  	3,118,59,0,225,223,1,0,0,0,225,226,1,0,0,0,226,227,1,0,0,0,227,228,5,
  	4,0,0,228,229,3,76,38,0,229,17,1,0,0,0,230,232,3,42,21,0,231,230,1,0,
  	0,0,231,232,1,0,0,0,232,235,1,0,0,0,233,234,5,62,0,0,234,236,3,72,36,
  	0,235,233,1,0,0,0,235,236,1,0,0,0,236,238,1,0,0,0,237,239,5,67,0,0,238,
  	237,1,0,0,0,238,239,1,0,0,0,239,241,1,0,0,0,240,242,3,44,22,0,241,240,
  	1,0,0,0,241,242,1,0,0,0,242,243,1,0,0,0,243,244,5,59,0,0,244,245,3,138,
  	69,0,245,248,3,68,34,0,246,247,5,3,0,0,247,249,3,118,59,0,248,246,1,0,
  	0,0,248,249,1,0,0,0,249,250,1,0,0,0,250,251,3,14,7,0,251,19,1,0,0,0,252,
  	254,5,5,0,0,253,255,3,54,27,0,254,253,1,0,0,0,254,255,1,0,0,0,255,257,
  	1,0,0,0,256,258,5,6,0,0,257,256,1,0,0,0,257,258,1,0,0,0,258,259,1,0,0,
  	0,259,260,5,7,0,0,260,21,1,0,0,0,261,263,5,1,0,0,262,264,3,54,27,0,263,
  	262,1,0,0,0,263,264,1,0,0,0,264,266,1,0,0,0,265,267,5,6,0,0,266,265,1,
  	0,0,0,266,267,1,0,0,0,267,268,1,0,0,0,268,269,5,2,0,0,269,23,1,0,0,0,
  	270,272,5,8,0,0,271,273,3,54,27,0,272,271,1,0,0,0,272,273,1,0,0,0,273,
  	275,1,0,0,0,274,276,5,6,0,0,275,274,1,0,0,0,275,276,1,0,0,0,276,277,1,
  	0,0,0,277,278,5,9,0,0,278,25,1,0,0,0,279,284,3,138,69,0,280,284,3,20,
  	10,0,281,284,3,22,11,0,282,284,3,24,12,0,283,279,1,0,0,0,283,280,1,0,
  	0,0,283,281,1,0,0,0,283,282,1,0,0,0,284,27,1,0,0,0,285,286,7,0,0,0,286,
  	289,3,26,13,0,287,288,5,3,0,0,288,290,3,118,59,0,289,287,1,0,0,0,289,
  	290,1,0,0,0,290,291,1,0,0,0,291,292,5,10,0,0,292,293,3,78,39,0,293,29,
  	1,0,0,0,294,298,5,57,0,0,295,296,3,138,69,0,296,297,5,10,0,0,297,299,
  	1,0,0,0,298,295,1,0,0,0,298,299,1,0,0,0,299,300,1,0,0,0,300,301,3,140,
  	70,0,301,31,1,0,0,0,302,303,7,1,0,0,303,304,3,78,39,0,304,33,1,0,0,0,
  	305,306,5,60,0,0,306,307,3,138,69,0,307,308,5,10,0,0,308,309,3,118,59,
  	0,309,35,1,0,0,0,310,311,5,61,0,0,311,314,3,138,69,0,312,313,5,77,0,0,
  	313,315,3,118,59,0,314,312,1,0,0,0,314,315,1,0,0,0,315,316,1,0,0,0,316,
  	317,5,10,0,0,317,318,5,1,0,0,318,320,3,60,30,0,319,321,5,6,0,0,320,319,
  	1,0,0,0,320,321,1,0,0,0,321,322,1,0,0,0,322,323,5,2,0,0,323,37,1,0,0,
  	0,324,326,3,42,21,0,325,324,1,0,0,0,325,326,1,0,0,0,326,327,1,0,0,0,327,
  	328,3,78,39,0,328,39,1,0,0,0,329,330,5,11,0,0,330,331,3,114,57,0,331,
  	41,1,0,0,0,332,334,3,40,20,0,333,332,1,0,0,0,334,335,1,0,0,0,335,333,
  	1,0,0,0,335,336,1,0,0,0,336,43,1,0,0,0,337,339,7,2,0,0,338,337,1,0,0,
  	0,339,340,1,0,0,0,340,338,1,0,0,0,340,341,1,0,0,0,341,45,1,0,0,0,342,
  	346,3,78,39,0,343,344,5,12,0,0,344,346,3,78,39,0,345,342,1,0,0,0,345,
  	343,1,0,0,0,346,47,1,0,0,0,347,348,3,138,69,0,348,349,5,3,0,0,349,350,
  	3,118,59,0,350,49,1,0,0,0,351,352,3,138,69,0,352,353,5,3,0,0,353,354,
  	3,78,39,0,354,358,1,0,0,0,355,356,5,12,0,0,356,358,3,78,39,0,357,351,
  	1,0,0,0,357,355,1,0,0,0,358,51,1,0,0,0,359,361,5,56,0,0,360,359,1,0,0,
  	0,360,361,1,0,0,0,361,362,1,0,0,0,362,364,3,138,69,0,363,365,3,40,20,
  	0,364,363,1,0,0,0,364,365,1,0,0,0,365,366,1,0,0,0,366,370,5,3,0,0,367,
  	371,3,118,59,0,368,369,5,68,0,0,369,371,3,138,69,0,370,367,1,0,0,0,370,
  	368,1,0,0,0,371,374,1,0,0,0,372,373,5,10,0,0,373,375,3,78,39,0,374,372,
  	1,0,0,0,374,375,1,0,0,0,375,53,1,0,0,0,376,381,3,138,69,0,377,378,5,6,
  	0,0,378,380,3,138,69,0,379,377,1,0,0,0,380,383,1,0,0,0,381,379,1,0,0,
  	0,381,382,1,0,0,0,382,55,1,0,0,0,383,381,1,0,0,0,384,389,3,78,39,0,385,
  	386,5,6,0,0,386,388,3,78,39,0,387,385,1,0,0,0,388,391,1,0,0,0,389,387,
  	1,0,0,0,389,390,1,0,0,0,390,57,1,0,0,0,391,389,1,0,0,0,392,397,3,46,23,
  	0,393,394,5,6,0,0,394,396,3,46,23,0,395,393,1,0,0,0,396,399,1,0,0,0,397,
  	395,1,0,0,0,397,398,1,0,0,0,398,59,1,0,0,0,399,397,1,0,0,0,400,405,3,
  	50,25,0,401,402,5,6,0,0,402,404,3,50,25,0,403,401,1,0,0,0,404,407,1,0,
  	0,0,405,403,1,0,0,0,405,406,1,0,0,0,406,61,1,0,0,0,407,405,1,0,0,0,408,
  	413,3,52,26,0,409,410,5,6,0,0,410,412,3,52,26,0,411,409,1,0,0,0,412,415,
  	1,0,0,0,413,411,1,0,0,0,413,414,1,0,0,0,414,63,1,0,0,0,415,413,1,0,0,
  	0,416,419,3,58,29,0,417,418,5,6,0,0,418,420,3,60,30,0,419,417,1,0,0,0,
  	419,420,1,0,0,0,420,423,1,0,0,0,421,423,3,60,30,0,422,416,1,0,0,0,422,
  	421,1,0,0,0,423,65,1,0,0,0,424,425,5,8,0,0,425,432,3,78,39,0,426,427,
  	5,3,0,0,427,430,3,78,39,0,428,429,5,3,0,0,429,431,3,78,39,0,430,428,1,
  	0,0,0,430,431,1,0,0,0,431,433,1,0,0,0,432,426,1,0,0,0,432,433,1,0,0,0,
  	433,434,1,0,0,0,434,435,5,9,0,0,435,67,1,0,0,0,436,438,5,5,0,0,437,439,
  	3,62,31,0,438,437,1,0,0,0,438,439,1,0,0,0,439,441,1,0,0,0,440,442,5,6,
  	0,0,441,440,1,0,0,0,441,442,1,0,0,0,442,443,1,0,0,0,443,444,5,7,0,0,444,
  	69,1,0,0,0,445,447,5,5,0,0,446,448,3,64,32,0,447,446,1,0,0,0,447,448,
  	1,0,0,0,448,450,1,0,0,0,449,451,5,6,0,0,450,449,1,0,0,0,450,451,1,0,0,
  	0,451,452,1,0,0,0,452,453,5,7,0,0,453,71,1,0,0,0,454,456,5,13,0,0,455,
  	457,3,62,31,0,456,455,1,0,0,0,456,457,1,0,0,0,457,459,1,0,0,0,458,460,
  	5,6,0,0,459,458,1,0,0,0,459,460,1,0,0,0,460,461,1,0,0,0,461,462,5,14,
  	0,0,462,73,1,0,0,0,463,465,5,13,0,0,464,466,3,64,32,0,465,464,1,0,0,0,
  	465,466,1,0,0,0,466,468,1,0,0,0,467,469,5,6,0,0,468,467,1,0,0,0,468,469,
  	1,0,0,0,469,470,1,0,0,0,470,471,5,14,0,0,471,75,1,0,0,0,472,475,3,14,
  	7,0,473,475,3,78,39,0,474,472,1,0,0,0,474,473,1,0,0,0,475,77,1,0,0,0,
  	476,478,5,63,0,0,477,476,1,0,0,0,477,478,1,0,0,0,478,479,1,0,0,0,479,
  	482,3,84,42,0,480,481,7,3,0,0,481,483,3,84,42,0,482,480,1,0,0,0,482,483,
  	1,0,0,0,483,79,1,0,0,0,484,506,3,140,70,0,485,506,3,116,58,0,486,489,
  	5,5,0,0,487,490,3,56,28,0,488,490,3,54,27,0,489,487,1,0,0,0,489,488,1,
  	0,0,0,489,490,1,0,0,0,490,492,1,0,0,0,491,493,5,6,0,0,492,491,1,0,0,0,
  	492,493,1,0,0,0,493,494,1,0,0,0,494,506,5,7,0,0,495,498,5,1,0,0,496,499,
  	3,60,30,0,497,499,3,54,27,0,498,496,1,0,0,0,498,497,1,0,0,0,498,499,1,
  	0,0,0,499,501,1,0,0,0,500,502,5,6,0,0,501,500,1,0,0,0,501,502,1,0,0,0,
  	502,503,1,0,0,0,503,506,5,2,0,0,504,506,5,23,0,0,505,484,1,0,0,0,505,
  	485,1,0,0,0,505,486,1,0,0,0,505,495,1,0,0,0,505,504,1,0,0,0,506,81,1,
  	0,0,0,507,508,5,74,0,0,508,513,3,80,40,0,509,510,5,24,0,0,510,512,3,80,
  	40,0,511,509,1,0,0,0,512,515,1,0,0,0,513,511,1,0,0,0,513,514,1,0,0,0,
  	514,516,1,0,0,0,515,513,1,0,0,0,516,517,5,4,0,0,517,518,3,76,38,0,518,
  	83,1,0,0,0,519,554,3,86,43,0,520,521,5,70,0,0,521,522,3,86,43,0,522,523,
  	5,71,0,0,523,524,3,76,38,0,524,525,5,72,0,0,525,526,3,76,38,0,526,554,
  	1,0,0,0,527,528,5,73,0,0,528,529,3,140,70,0,529,531,5,1,0,0,530,532,3,
  	82,41,0,531,530,1,0,0,0,532,533,1,0,0,0,533,531,1,0,0,0,533,534,1,0,0,
  	0,534,535,1,0,0,0,535,536,5,2,0,0,536,554,1,0,0,0,537,538,5,78,0,0,538,
  	545,3,14,7,0,539,540,5,79,0,0,540,541,3,138,69,0,541,542,5,3,0,0,542,
  	543,3,118,59,0,543,544,3,14,7,0,544,546,1,0,0,0,545,539,1,0,0,0,546,547,
  	1,0,0,0,547,545,1,0,0,0,547,548,1,0,0,0,548,551,1,0,0,0,549,550,5,80,
  	0,0,550,552,3,14,7,0,551,549,1,0,0,0,551,552,1,0,0,0,552,554,1,0,0,0,
  	553,519,1,0,0,0,553,520,1,0,0,0,553,527,1,0,0,0,553,537,1,0,0,0,554,85,
  	1,0,0,0,555,560,3,88,44,0,556,557,5,25,0,0,557,559,3,88,44,0,558,556,
  	1,0,0,0,559,562,1,0,0,0,560,558,1,0,0,0,560,561,1,0,0,0,561,87,1,0,0,
  	0,562,560,1,0,0,0,563,568,3,90,45,0,564,565,5,26,0,0,565,567,3,90,45,
  	0,566,564,1,0,0,0,567,570,1,0,0,0,568,566,1,0,0,0,568,569,1,0,0,0,569,
  	89,1,0,0,0,570,568,1,0,0,0,571,576,3,92,46,0,572,573,7,4,0,0,573,575,
  	3,92,46,0,574,572,1,0,0,0,575,578,1,0,0,0,576,574,1,0,0,0,576,577,1,0,
  	0,0,577,91,1,0,0,0,578,576,1,0,0,0,579,584,3,94,47,0,580,581,7,5,0,0,
  	581,583,3,94,47,0,582,580,1,0,0,0,583,586,1,0,0,0,584,582,1,0,0,0,584,
  	585,1,0,0,0,585,93,1,0,0,0,586,584,1,0,0,0,587,592,3,96,48,0,588,589,
  	7,6,0,0,589,591,3,96,48,0,590,588,1,0,0,0,591,594,1,0,0,0,592,590,1,0,
  	0,0,592,593,1,0,0,0,593,95,1,0,0,0,594,592,1,0,0,0,595,600,3,98,49,0,
  	596,597,7,7,0,0,597,599,3,98,49,0,598,596,1,0,0,0,599,602,1,0,0,0,600,
  	598,1,0,0,0,600,601,1,0,0,0,601,97,1,0,0,0,602,600,1,0,0,0,603,606,3,
  	100,50,0,604,605,7,8,0,0,605,607,3,78,39,0,606,604,1,0,0,0,606,607,1,
  	0,0,0,607,99,1,0,0,0,608,611,3,102,51,0,609,610,7,9,0,0,610,612,3,118,
  	59,0,611,609,1,0,0,0,611,612,1,0,0,0,612,616,1,0,0,0,613,614,7,10,0,0,
  	614,616,3,102,51,0,615,608,1,0,0,0,615,613,1,0,0,0,616,101,1,0,0,0,617,
  	622,3,104,52,0,618,619,7,11,0,0,619,621,3,104,52,0,620,618,1,0,0,0,621,
  	624,1,0,0,0,622,620,1,0,0,0,622,623,1,0,0,0,623,103,1,0,0,0,624,622,1,
  	0,0,0,625,630,3,106,53,0,626,627,7,12,0,0,627,629,3,106,53,0,628,626,
  	1,0,0,0,629,632,1,0,0,0,630,628,1,0,0,0,630,631,1,0,0,0,631,105,1,0,0,
  	0,632,630,1,0,0,0,633,638,3,108,54,0,634,635,7,13,0,0,635,637,3,108,54,
  	0,636,634,1,0,0,0,637,640,1,0,0,0,638,636,1,0,0,0,638,639,1,0,0,0,639,
  	107,1,0,0,0,640,638,1,0,0,0,641,652,3,114,57,0,642,647,4,54,0,0,643,648,
  	3,66,33,0,644,648,3,70,35,0,645,648,3,74,37,0,646,648,5,41,0,0,647,643,
  	1,0,0,0,647,644,1,0,0,0,647,645,1,0,0,0,647,646,1,0,0,0,648,651,1,0,0,
  	0,649,651,3,40,20,0,650,642,1,0,0,0,650,649,1,0,0,0,651,654,1,0,0,0,652,
  	650,1,0,0,0,652,653,1,0,0,0,653,109,1,0,0,0,654,652,1,0,0,0,655,660,5,
  	1,0,0,656,658,3,60,30,0,657,659,5,6,0,0,658,657,1,0,0,0,658,659,1,0,0,
  	0,659,661,1,0,0,0,660,656,1,0,0,0,660,661,1,0,0,0,661,662,1,0,0,0,662,
  	663,5,2,0,0,663,111,1,0,0,0,664,678,5,8,0,0,665,667,3,58,29,0,666,668,
  	5,6,0,0,667,666,1,0,0,0,667,668,1,0,0,0,668,679,1,0,0,0,669,670,3,78,
  	39,0,670,671,5,75,0,0,671,672,3,140,70,0,672,673,5,76,0,0,673,676,3,78,
  	39,0,674,675,5,70,0,0,675,677,3,78,39,0,676,674,1,0,0,0,676,677,1,0,0,
  	0,677,679,1,0,0,0,678,665,1,0,0,0,678,669,1,0,0,0,678,679,1,0,0,0,679,
  	680,1,0,0,0,680,681,5,9,0,0,681,113,1,0,0,0,682,700,3,140,70,0,683,700,
  	3,116,58,0,684,700,3,112,56,0,685,700,3,110,55,0,686,687,5,5,0,0,687,
  	688,3,78,39,0,688,689,5,7,0,0,689,700,1,0,0,0,690,692,5,5,0,0,691,693,
  	3,56,28,0,692,691,1,0,0,0,692,693,1,0,0,0,693,695,1,0,0,0,694,696,5,6,
  	0,0,695,694,1,0,0,0,695,696,1,0,0,0,696,697,1,0,0,0,697,700,5,7,0,0,698,
  	700,3,16,8,0,699,682,1,0,0,0,699,683,1,0,0,0,699,684,1,0,0,0,699,685,
  	1,0,0,0,699,686,1,0,0,0,699,690,1,0,0,0,699,698,1,0,0,0,700,115,1,0,0,
  	0,701,702,7,14,0,0,702,117,1,0,0,0,703,708,3,120,60,0,704,706,5,49,0,
  	0,705,707,3,120,60,0,706,705,1,0,0,0,706,707,1,0,0,0,707,709,1,0,0,0,
  	708,704,1,0,0,0,708,709,1,0,0,0,709,119,1,0,0,0,710,715,3,122,61,0,711,
  	712,5,24,0,0,712,714,3,122,61,0,713,711,1,0,0,0,714,717,1,0,0,0,715,713,
  	1,0,0,0,715,716,1,0,0,0,716,121,1,0,0,0,717,715,1,0,0,0,718,719,3,138,
  	69,0,719,720,5,77,0,0,720,722,1,0,0,0,721,718,1,0,0,0,721,722,1,0,0,0,
  	722,723,1,0,0,0,723,724,3,124,62,0,724,123,1,0,0,0,725,730,3,126,63,0,
  	726,727,5,8,0,0,727,729,5,9,0,0,728,726,1,0,0,0,729,732,1,0,0,0,730,728,
  	1,0,0,0,730,731,1,0,0,0,731,125,1,0,0,0,732,730,1,0,0,0,733,751,3,128,
  	64,0,734,737,5,13,0,0,735,738,3,118,59,0,736,738,3,114,57,0,737,735,1,
  	0,0,0,737,736,1,0,0,0,738,746,1,0,0,0,739,742,5,6,0,0,740,743,3,118,59,
  	0,741,743,3,114,57,0,742,740,1,0,0,0,742,741,1,0,0,0,743,745,1,0,0,0,
  	744,739,1,0,0,0,745,748,1,0,0,0,746,744,1,0,0,0,746,747,1,0,0,0,747,749,
  	1,0,0,0,748,746,1,0,0,0,749,750,5,14,0,0,750,752,1,0,0,0,751,734,1,0,
  	0,0,751,752,1,0,0,0,752,127,1,0,0,0,753,765,5,92,0,0,754,765,3,130,65,
  	0,755,765,3,140,70,0,756,757,5,5,0,0,757,758,3,118,59,0,758,759,5,7,0,
  	0,759,765,1,0,0,0,760,765,3,134,67,0,761,765,3,136,68,0,762,763,5,69,
  	0,0,763,765,3,78,39,0,764,753,1,0,0,0,764,754,1,0,0,0,764,755,1,0,0,0,
  	764,756,1,0,0,0,764,760,1,0,0,0,764,761,1,0,0,0,764,762,1,0,0,0,765,129,
  	1,0,0,0,766,771,3,132,66,0,767,768,7,15,0,0,768,770,3,132,66,0,769,767,
  	1,0,0,0,770,773,1,0,0,0,771,769,1,0,0,0,771,772,1,0,0,0,772,131,1,0,0,
  	0,773,771,1,0,0,0,774,783,5,1,0,0,775,780,3,48,24,0,776,777,5,6,0,0,777,
  	779,3,48,24,0,778,776,1,0,0,0,779,782,1,0,0,0,780,778,1,0,0,0,780,781,
  	1,0,0,0,781,784,1,0,0,0,782,780,1,0,0,0,783,775,1,0,0,0,783,784,1,0,0,
  	0,784,786,1,0,0,0,785,787,5,6,0,0,786,785,1,0,0,0,786,787,1,0,0,0,787,
  	788,1,0,0,0,788,789,5,2,0,0,789,133,1,0,0,0,790,799,5,5,0,0,791,796,3,
  	118,59,0,792,793,5,6,0,0,793,795,3,118,59,0,794,792,1,0,0,0,795,798,1,
  	0,0,0,796,794,1,0,0,0,796,797,1,0,0,0,797,800,1,0,0,0,798,796,1,0,0,0,
  	799,791,1,0,0,0,799,800,1,0,0,0,800,802,1,0,0,0,801,803,5,6,0,0,802,801,
  	1,0,0,0,802,803,1,0,0,0,803,804,1,0,0,0,804,805,5,7,0,0,805,135,1,0,0,
  	0,806,808,3,44,22,0,807,806,1,0,0,0,807,808,1,0,0,0,808,810,1,0,0,0,809,
  	811,3,72,36,0,810,809,1,0,0,0,810,811,1,0,0,0,811,812,1,0,0,0,812,813,
  	3,68,34,0,813,814,5,4,0,0,814,815,3,118,59,0,815,137,1,0,0,0,816,817,
  	5,98,0,0,817,139,1,0,0,0,818,819,5,98,0,0,819,821,5,51,0,0,820,818,1,
  	0,0,0,821,824,1,0,0,0,822,820,1,0,0,0,822,823,1,0,0,0,823,825,1,0,0,0,
  	824,822,1,0,0,0,825,826,5,98,0,0,826,141,1,0,0,0,115,143,147,150,154,
  	163,172,176,181,185,194,199,205,208,212,217,220,225,231,235,238,241,248,
  	254,257,263,266,272,275,283,289,298,314,320,325,335,340,345,357,360,364,
  	370,374,381,389,397,405,413,419,422,430,432,438,441,447,450,456,459,465,
  	468,474,477,482,489,492,498,501,505,513,533,547,551,553,560,568,576,584,
  	592,600,606,611,615,622,630,638,647,650,652,658,660,667,676,678,692,695,
  	699,706,708,715,721,730,737,742,746,751,764,771,780,783,786,796,799,802,
  	807,810,822
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
    setState(143);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SEP) {
      setState(142);
      match(OpenCMLParser::SEP);
    }
    setState(154);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -324252558921029342) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 541434200655) != 0)) {
      setState(147);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 1, _ctx)) {
      case 1: {
        setState(145);
        decl();
        break;
      }

      case 2: {
        setState(146);
        stmt();
        break;
      }

      default:
        break;
      }
      setState(150);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::SEP) {
        setState(149);
        match(OpenCMLParser::SEP);
      }
      setState(156);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(157);
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
    setState(163);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 4, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(159);
      moduleDecl();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(160);
      importDecl();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(161);
      exportDecl();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(162);
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

OpenCMLParser::RetStmtContext* OpenCMLParser::StmtContext::retStmt() {
  return getRuleContext<OpenCMLParser::RetStmtContext>(0);
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
    setState(172);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 5, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(165);
      letStmt();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(166);
      useStmt();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(167);
      retStmt();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(168);
      typeStmt();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(169);
      enumStmt();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(170);
      exprStmt();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(171);
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
    setState(174);
    stmt();
    setState(181);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 7, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(176);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::SEP) {
          setState(175);
          match(OpenCMLParser::SEP);
        }
        setState(178);
        stmt(); 
      }
      setState(183);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 7, _ctx);
    }
    setState(185);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SEP) {
      setState(184);
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
    setState(187);
    match(OpenCMLParser::MODULE);
    setState(188);
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
    setState(190);
    match(OpenCMLParser::IMPORT);
    setState(199);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::STRING: {
        setState(191);
        match(OpenCMLParser::STRING);
        break;
      }

      case OpenCMLParser::T__0:
      case OpenCMLParser::IDENTIFIER: {
        setState(194);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case OpenCMLParser::IDENTIFIER: {
            setState(192);
            identDef();
            break;
          }

          case OpenCMLParser::T__0: {
            setState(193);
            bracedIdents();
            break;
          }

        default:
          throw NoViableAltException(this);
        }
        setState(196);
        match(OpenCMLParser::FROM);
        setState(197);
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
    setState(201);
    match(OpenCMLParser::EXPORT);
    setState(205);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::LET:
      case OpenCMLParser::VAR: {
        setState(202);
        letStmt();
        break;
      }

      case OpenCMLParser::TYPE: {
        setState(203);
        typeStmt();
        break;
      }

      case OpenCMLParser::T__0: {
        setState(204);
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
    setState(208);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SYNC) {
      setState(207);
      match(OpenCMLParser::SYNC);
    }
    setState(210);
    match(OpenCMLParser::T__0);
    setState(212);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -5512399329651840734) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 541434200641) != 0)) {
      setState(211);
      stmtList();
    }
    setState(214);
    match(OpenCMLParser::T__1);
   
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
  enterRule(_localctx, 16, OpenCMLParser::RuleLambdaExpr);
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
    if (((((_la - 83) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 83)) & 63) != 0)) {
      setState(216);
      modifiers();
    }
    setState(220);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__12) {
      setState(219);
      angledParams();
    }
    setState(222);
    parentParams();
    setState(225);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__2) {
      setState(223);
      match(OpenCMLParser::T__2);
      setState(224);
      typeExpr();
    }
    setState(227);
    match(OpenCMLParser::T__3);
    setState(228);
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
  enterRule(_localctx, 18, OpenCMLParser::RuleFuncDecl);
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
    setState(231);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__10) {
      setState(230);
      annotations();
    }
    setState(235);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WITH) {
      setState(233);
      match(OpenCMLParser::WITH);
      setState(234);
      angledParams();
    }
    setState(238);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::EXPORT) {
      setState(237);
      match(OpenCMLParser::EXPORT);
    }
    setState(241);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 83) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 83)) & 63) != 0)) {
      setState(240);
      modifiers();
    }
    setState(243);
    match(OpenCMLParser::FUNC);
    setState(244);
    identDef();
    setState(245);
    parentParams();
    setState(248);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__2) {
      setState(246);
      match(OpenCMLParser::T__2);
      setState(247);
      typeExpr();
    }
    setState(250);
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
  enterRule(_localctx, 20, OpenCMLParser::RuleParentIdents);
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
    setState(252);
    match(OpenCMLParser::T__4);
    setState(254);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(253);
      identList();
    }
    setState(257);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(256);
      match(OpenCMLParser::T__5);
    }
    setState(259);
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
  enterRule(_localctx, 22, OpenCMLParser::RuleBracedIdents);
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
    match(OpenCMLParser::T__0);
    setState(263);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(262);
      identList();
    }
    setState(266);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(265);
      match(OpenCMLParser::T__5);
    }
    setState(268);
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
  enterRule(_localctx, 24, OpenCMLParser::RuleBracketIdents);
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
    setState(270);
    match(OpenCMLParser::T__7);
    setState(272);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(271);
      identList();
    }
    setState(275);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(274);
      match(OpenCMLParser::T__5);
    }
    setState(277);
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
  enterRule(_localctx, 26, OpenCMLParser::RuleCarrier);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(283);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(279);
        identDef();
        break;
      }

      case OpenCMLParser::T__4: {
        enterOuterAlt(_localctx, 2);
        setState(280);
        parentIdents();
        break;
      }

      case OpenCMLParser::T__0: {
        enterOuterAlt(_localctx, 3);
        setState(281);
        bracedIdents();
        break;
      }

      case OpenCMLParser::T__7: {
        enterOuterAlt(_localctx, 4);
        setState(282);
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
  enterRule(_localctx, 28, OpenCMLParser::RuleLetStmt);
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
    setState(285);
    _la = _input->LA(1);
    if (!(_la == OpenCMLParser::LET

    || _la == OpenCMLParser::VAR)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(286);
    carrier();
    setState(289);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__2) {
      setState(287);
      match(OpenCMLParser::T__2);
      setState(288);
      typeExpr();
    }
    setState(291);
    match(OpenCMLParser::T__9);
    setState(292);
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
  enterRule(_localctx, 30, OpenCMLParser::RuleUseStmt);

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
    match(OpenCMLParser::USE);
    setState(298);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx)) {
    case 1: {
      setState(295);
      identDef();
      setState(296);
      match(OpenCMLParser::T__9);
      break;
    }

    default:
      break;
    }
    setState(300);
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
  enterRule(_localctx, 32, OpenCMLParser::RuleRetStmt);
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
    if (!(((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 393217) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(303);
    dataExpr();
   
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
  enterRule(_localctx, 34, OpenCMLParser::RuleTypeStmt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(305);
    match(OpenCMLParser::TYPE);
    setState(306);
    identDef();
    setState(307);
    match(OpenCMLParser::T__9);
    setState(308);
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

OpenCMLParser::PairedValuesContext* OpenCMLParser::EnumStmtContext::pairedValues() {
  return getRuleContext<OpenCMLParser::PairedValuesContext>(0);
}

tree::TerminalNode* OpenCMLParser::EnumStmtContext::OF() {
  return getToken(OpenCMLParser::OF, 0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::EnumStmtContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
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
  enterRule(_localctx, 36, OpenCMLParser::RuleEnumStmt);
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
    setState(310);
    match(OpenCMLParser::ENUM);
    setState(311);
    identDef();
    setState(314);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::OF) {
      setState(312);
      match(OpenCMLParser::OF);
      setState(313);
      typeExpr();
    }
    setState(316);
    match(OpenCMLParser::T__9);
    setState(317);
    match(OpenCMLParser::T__0);
    setState(318);
    pairedValues();
    setState(320);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(319);
      match(OpenCMLParser::T__5);
    }
    setState(322);
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
  enterRule(_localctx, 38, OpenCMLParser::RuleExprStmt);
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
    setState(325);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__10) {
      setState(324);
      annotations();
    }
    setState(327);
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
  enterRule(_localctx, 40, OpenCMLParser::RuleAnnotation);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(329);
    match(OpenCMLParser::T__10);
    setState(330);
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
  enterRule(_localctx, 42, OpenCMLParser::RuleAnnotations);
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
    setState(333); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(332);
      annotation();
      setState(335); 
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
  enterRule(_localctx, 44, OpenCMLParser::RuleModifiers);
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
    setState(338); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(337);
      _la = _input->LA(1);
      if (!(((((_la - 83) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 83)) & 63) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(340); 
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
  enterRule(_localctx, 46, OpenCMLParser::RuleIndexValue);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(345);
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
        setState(342);
        dataExpr();
        break;
      }

      case OpenCMLParser::T__11: {
        enterOuterAlt(_localctx, 2);
        setState(343);
        match(OpenCMLParser::T__11);
        setState(344);
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
  enterRule(_localctx, 48, OpenCMLParser::RuleKeyTypePair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(347);
    identDef();
    setState(348);
    match(OpenCMLParser::T__2);
    setState(349);
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
  enterRule(_localctx, 50, OpenCMLParser::RuleKeyValuePair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(357);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(351);
        identDef();
        setState(352);
        match(OpenCMLParser::T__2);
        setState(353);
        dataExpr();
        break;
      }

      case OpenCMLParser::T__11: {
        enterOuterAlt(_localctx, 2);
        setState(355);
        match(OpenCMLParser::T__11);
        setState(356);
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
  enterRule(_localctx, 52, OpenCMLParser::RuleKeyParamPair);
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
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR) {
      setState(359);
      match(OpenCMLParser::VAR);
    }
    setState(362);
    identDef();
    setState(364);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__10) {
      setState(363);
      annotation();
    }
    setState(366);
    match(OpenCMLParser::T__2);
    setState(370);
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
        setState(367);
        typeExpr();
        break;
      }

      case OpenCMLParser::TYPEAS: {
        setState(368);
        match(OpenCMLParser::TYPEAS);
        setState(369);
        identDef();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(374);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__9) {
      setState(372);
      match(OpenCMLParser::T__9);
      setState(373);
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
  enterRule(_localctx, 54, OpenCMLParser::RuleIdentList);

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
    identDef();
    setState(381);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 42, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(377);
        match(OpenCMLParser::T__5);
        setState(378);
        identDef(); 
      }
      setState(383);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 42, _ctx);
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
  enterRule(_localctx, 56, OpenCMLParser::RuleValueList);

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
    dataExpr();
    setState(389);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 43, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(385);
        match(OpenCMLParser::T__5);
        setState(386);
        dataExpr(); 
      }
      setState(391);
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
  enterRule(_localctx, 58, OpenCMLParser::RuleIndexValues);

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
    setState(392);
    indexValue();
    setState(397);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 44, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(393);
        match(OpenCMLParser::T__5);
        setState(394);
        indexValue(); 
      }
      setState(399);
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
  enterRule(_localctx, 60, OpenCMLParser::RulePairedValues);

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
    setState(400);
    keyValuePair();
    setState(405);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 45, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(401);
        match(OpenCMLParser::T__5);
        setState(402);
        keyValuePair(); 
      }
      setState(407);
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
  enterRule(_localctx, 62, OpenCMLParser::RulePairedParams);

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
    setState(408);
    keyParamPair();
    setState(413);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 46, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(409);
        match(OpenCMLParser::T__5);
        setState(410);
        keyParamPair(); 
      }
      setState(415);
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
  enterRule(_localctx, 64, OpenCMLParser::RuleArgumentList);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(422);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 48, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(416);
      indexValues();
      setState(419);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 47, _ctx)) {
      case 1: {
        setState(417);
        match(OpenCMLParser::T__5);
        setState(418);
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
      setState(421);
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
  enterRule(_localctx, 66, OpenCMLParser::RuleMemberAccess);
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
    setState(424);
    match(OpenCMLParser::T__7);
    setState(425);
    dataExpr();
    setState(432);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__2) {
      setState(426);
      match(OpenCMLParser::T__2);
      setState(427);
      dataExpr();
      setState(430);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__2) {
        setState(428);
        match(OpenCMLParser::T__2);
        setState(429);
        dataExpr();
      }
    }
    setState(434);
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
  enterRule(_localctx, 68, OpenCMLParser::RuleParentParams);
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
    match(OpenCMLParser::T__4);
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
    if (_la == OpenCMLParser::T__5) {
      setState(440);
      match(OpenCMLParser::T__5);
    }
    setState(443);
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
  enterRule(_localctx, 70, OpenCMLParser::RuleParentArgues);
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
    match(OpenCMLParser::T__4);
    setState(447);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -9223365422605127390) != 0) || ((((_la - 70) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 70)) & 8459903241) != 0)) {
      setState(446);
      argumentList();
    }
    setState(450);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(449);
      match(OpenCMLParser::T__5);
    }
    setState(452);
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
  enterRule(_localctx, 72, OpenCMLParser::RuleAngledParams);
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
    match(OpenCMLParser::T__12);
    setState(456);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(455);
      pairedParams();
    }
    setState(459);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(458);
      match(OpenCMLParser::T__5);
    }
    setState(461);
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
  enterRule(_localctx, 74, OpenCMLParser::RuleAngledValues);
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
    setState(463);
    match(OpenCMLParser::T__12);
    setState(465);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -9223365422605127390) != 0) || ((((_la - 70) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 70)) & 8459903241) != 0)) {
      setState(464);
      argumentList();
    }
    setState(468);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(467);
      match(OpenCMLParser::T__5);
    }
    setState(470);
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
  enterRule(_localctx, 76, OpenCMLParser::RuleBlockExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(474);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 59, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(472);
      stmtBlock();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(473);
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
  enterRule(_localctx, 78, OpenCMLParser::RuleDataExpr);
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
    setState(477);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WAIT) {
      setState(476);
      match(OpenCMLParser::WAIT);
    }
    setState(479);
    structExpr();
    setState(482);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx)) {
    case 1: {
      setState(480);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8356864) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(481);
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
  enterRule(_localctx, 80, OpenCMLParser::RulePattern);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(505);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(484);
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
        setState(485);
        literal();
        break;
      }

      case OpenCMLParser::T__4: {
        enterOuterAlt(_localctx, 3);
        setState(486);
        match(OpenCMLParser::T__4);
        setState(489);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 62, _ctx)) {
        case 1: {
          setState(487);
          valueList();
          break;
        }

        case 2: {
          setState(488);
          identList();
          break;
        }

        default:
          break;
        }
        setState(492);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__5) {
          setState(491);
          match(OpenCMLParser::T__5);
        }
        setState(494);
        match(OpenCMLParser::T__6);
        break;
      }

      case OpenCMLParser::T__0: {
        enterOuterAlt(_localctx, 4);
        setState(495);
        match(OpenCMLParser::T__0);
        setState(498);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 64, _ctx)) {
        case 1: {
          setState(496);
          pairedValues();
          break;
        }

        case 2: {
          setState(497);
          identList();
          break;
        }

        default:
          break;
        }
        setState(501);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__5) {
          setState(500);
          match(OpenCMLParser::T__5);
        }
        setState(503);
        match(OpenCMLParser::T__1);
        break;
      }

      case OpenCMLParser::T__22: {
        enterOuterAlt(_localctx, 5);
        setState(504);
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
  enterRule(_localctx, 82, OpenCMLParser::RuleMatchCase);
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
    setState(507);
    match(OpenCMLParser::CASE);
    setState(508);
    pattern();
    setState(513);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__23) {
      setState(509);
      match(OpenCMLParser::T__23);
      setState(510);
      pattern();
      setState(515);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(516);
    match(OpenCMLParser::T__3);
    setState(517);
    blockExpr();
   
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

std::vector<tree::TerminalNode *> OpenCMLParser::StructExprContext::CATCH() {
  return getTokens(OpenCMLParser::CATCH);
}

tree::TerminalNode* OpenCMLParser::StructExprContext::CATCH(size_t i) {
  return getToken(OpenCMLParser::CATCH, i);
}

std::vector<OpenCMLParser::IdentDefContext *> OpenCMLParser::StructExprContext::identDef() {
  return getRuleContexts<OpenCMLParser::IdentDefContext>();
}

OpenCMLParser::IdentDefContext* OpenCMLParser::StructExprContext::identDef(size_t i) {
  return getRuleContext<OpenCMLParser::IdentDefContext>(i);
}

std::vector<OpenCMLParser::TypeExprContext *> OpenCMLParser::StructExprContext::typeExpr() {
  return getRuleContexts<OpenCMLParser::TypeExprContext>();
}

OpenCMLParser::TypeExprContext* OpenCMLParser::StructExprContext::typeExpr(size_t i) {
  return getRuleContext<OpenCMLParser::TypeExprContext>(i);
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
    size_t alt;
    setState(553);
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
        setState(519);
        logicalOrExpr();
        break;
      }

      case OpenCMLParser::IF: {
        enterOuterAlt(_localctx, 2);
        setState(520);
        match(OpenCMLParser::IF);
        setState(521);
        logicalOrExpr();
        setState(522);
        match(OpenCMLParser::THEN);
        setState(523);
        blockExpr();
        setState(524);
        match(OpenCMLParser::ELSE);
        setState(525);
        blockExpr();
        break;
      }

      case OpenCMLParser::MATCH: {
        enterOuterAlt(_localctx, 3);
        setState(527);
        match(OpenCMLParser::MATCH);
        setState(528);
        identRef();
        setState(529);
        match(OpenCMLParser::T__0);
        setState(531); 
        _errHandler->sync(this);
        _la = _input->LA(1);
        do {
          setState(530);
          matchCase();
          setState(533); 
          _errHandler->sync(this);
          _la = _input->LA(1);
        } while (_la == OpenCMLParser::CASE);
        setState(535);
        match(OpenCMLParser::T__1);
        break;
      }

      case OpenCMLParser::TRY: {
        enterOuterAlt(_localctx, 4);
        setState(537);
        match(OpenCMLParser::TRY);
        setState(538);
        stmtBlock();
        setState(545); 
        _errHandler->sync(this);
        alt = 1;
        do {
          switch (alt) {
            case 1: {
                  setState(539);
                  match(OpenCMLParser::CATCH);
                  setState(540);
                  identDef();
                  setState(541);
                  match(OpenCMLParser::T__2);
                  setState(542);
                  typeExpr();
                  setState(543);
                  stmtBlock();
                  break;
                }

          default:
            throw NoViableAltException(this);
          }
          setState(547); 
          _errHandler->sync(this);
          alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx);
        } while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER);
        setState(551);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx)) {
        case 1: {
          setState(549);
          match(OpenCMLParser::FINALLY);
          setState(550);
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
    setState(555);
    logicalAndExpr();
    setState(560);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 72, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(556);
        match(OpenCMLParser::T__24);
        setState(557);
        logicalAndExpr(); 
      }
      setState(562);
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
    setState(563);
    equalityExpr();
    setState(568);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 73, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(564);
        match(OpenCMLParser::T__25);
        setState(565);
        equalityExpr(); 
      }
      setState(570);
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
    setState(571);
    relationalExpr();
    setState(576);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(572);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 2013265920) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(573);
        relationalExpr(); 
      }
      setState(578);
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
    setState(579);
    additiveExpr();
    setState(584);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 75, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(580);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 6442475520) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(581);
        additiveExpr(); 
      }
      setState(586);
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
    setState(587);
    multiplicativeExpr();
    setState(592);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 76, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(588);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__32

        || _la == OpenCMLParser::T__33)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(589);
        multiplicativeExpr(); 
      }
      setState(594);
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
    setState(595);
    nullableExpr();
    setState(600);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 77, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(596);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 515396075520) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(597);
        nullableExpr(); 
      }
      setState(602);
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
  enterRule(_localctx, 98, OpenCMLParser::RuleNullableExpr);
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
    setState(603);
    unaryExpr();
    setState(606);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 78, _ctx)) {
    case 1: {
      setState(604);
      _la = _input->LA(1);
      if (!(_la == OpenCMLParser::T__38

      || _la == OpenCMLParser::T__39)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(605);
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
    setState(615);
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
        setState(608);
        linkExpr();
        setState(611);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 79, _ctx)) {
        case 1: {
          setState(609);
          _la = _input->LA(1);
          if (!(_la == OpenCMLParser::AS

          || _la == OpenCMLParser::IS)) {
          _errHandler->recoverInline(this);
          }
          else {
            _errHandler->reportMatch(this);
            consume();
          }
          setState(610);
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
        setState(613);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 6614249635840) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(614);
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
    setState(617);
    bindExpr();
    setState(622);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 81, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(618);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__42

        || _la == OpenCMLParser::T__43)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(619);
        bindExpr(); 
      }
      setState(624);
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
    setState(625);
    withExpr();
    setState(630);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 82, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(626);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__44

        || _la == OpenCMLParser::T__45)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(627);
        withExpr(); 
      }
      setState(632);
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
    setState(633);
    annoExpr();
    setState(638);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 83, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(634);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__46

        || _la == OpenCMLParser::T__47)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(635);
        annoExpr(); 
      }
      setState(640);
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
  enterRule(_localctx, 108, OpenCMLParser::RuleAnnoExpr);

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
    setState(641);
    primaryData();
    setState(652);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 86, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(650);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 85, _ctx)) {
        case 1: {
          setState(642);

          if (!(isAdjacent())) throw FailedPredicateException(this, "isAdjacent()");
          setState(647);
          _errHandler->sync(this);
          switch (_input->LA(1)) {
            case OpenCMLParser::T__7: {
              setState(643);
              memberAccess();
              break;
            }

            case OpenCMLParser::T__4: {
              setState(644);
              parentArgues();
              break;
            }

            case OpenCMLParser::T__12: {
              setState(645);
              angledValues();
              break;
            }

            case OpenCMLParser::T__40: {
              setState(646);
              match(OpenCMLParser::T__40);
              break;
            }

          default:
            throw NoViableAltException(this);
          }
          break;
        }

        case 2: {
          setState(649);
          annotation();
          break;
        }

        default:
          break;
        } 
      }
      setState(654);
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
    setState(655);
    match(OpenCMLParser::T__0);
    setState(660);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__11 || _la == OpenCMLParser::IDENTIFIER) {
      setState(656);
      pairedValues();
      setState(658);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__5) {
        setState(657);
        match(OpenCMLParser::T__5);
      }
    }
    setState(662);
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
    setState(664);
    match(OpenCMLParser::T__7);
    setState(678);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 91, _ctx)) {
    case 1: {
      setState(665);
      indexValues();
      setState(667);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__5) {
        setState(666);
        match(OpenCMLParser::T__5);
      }
      break;
    }

    case 2: {
      setState(669);
      dataExpr();
      setState(670);
      match(OpenCMLParser::FOR);
      setState(671);
      identRef();
      setState(672);
      match(OpenCMLParser::IN);
      setState(673);
      dataExpr();
      setState(676);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::IF) {
        setState(674);
        match(OpenCMLParser::IF);
        setState(675);
        dataExpr();
      }
      break;
    }

    default:
      break;
    }
    setState(680);
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
  enterRule(_localctx, 114, OpenCMLParser::RulePrimaryData);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(699);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 94, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(682);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(683);
      literal();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(684);
      listExpr();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(685);
      dictExpr();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(686);
      match(OpenCMLParser::T__4);
      setState(687);
      dataExpr();
      setState(688);
      match(OpenCMLParser::T__6);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(690);
      match(OpenCMLParser::T__4);
      setState(692);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & -9223365422605131486) != 0) || ((((_la - 70) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 70)) & 8459903241) != 0)) {
        setState(691);
        valueList();
      }
      setState(695);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__5) {
        setState(694);
        match(OpenCMLParser::T__5);
      }
      setState(697);
      match(OpenCMLParser::T__6);
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(698);
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
    setState(701);
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
  enterRule(_localctx, 118, OpenCMLParser::RuleTypeExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(703);
    unionType();
    setState(708);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 96, _ctx)) {
    case 1: {
      setState(704);
      match(OpenCMLParser::T__48);
      setState(706);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 95, _ctx)) {
      case 1: {
        setState(705);
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
  enterRule(_localctx, 120, OpenCMLParser::RuleUnionType);

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
    setState(710);
    unionUnit();
    setState(715);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 97, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(711);
        match(OpenCMLParser::T__23);
        setState(712);
        unionUnit(); 
      }
      setState(717);
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
  enterRule(_localctx, 122, OpenCMLParser::RuleUnionUnit);

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
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 98, _ctx)) {
    case 1: {
      setState(718);
      identDef();
      setState(719);
      match(OpenCMLParser::OF);
      break;
    }

    default:
      break;
    }
    setState(723);
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
    setState(725);
    argsType();
    setState(730);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 99, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(726);
        match(OpenCMLParser::T__7);
        setState(727);
        match(OpenCMLParser::T__8); 
      }
      setState(732);
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

//----------------- ArgsTypeContext ------------------------------------------------------------------

OpenCMLParser::ArgsTypeContext::ArgsTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimaryTypeContext* OpenCMLParser::ArgsTypeContext::primaryType() {
  return getRuleContext<OpenCMLParser::PrimaryTypeContext>(0);
}

std::vector<OpenCMLParser::TypeExprContext *> OpenCMLParser::ArgsTypeContext::typeExpr() {
  return getRuleContexts<OpenCMLParser::TypeExprContext>();
}

OpenCMLParser::TypeExprContext* OpenCMLParser::ArgsTypeContext::typeExpr(size_t i) {
  return getRuleContext<OpenCMLParser::TypeExprContext>(i);
}

std::vector<OpenCMLParser::PrimaryDataContext *> OpenCMLParser::ArgsTypeContext::primaryData() {
  return getRuleContexts<OpenCMLParser::PrimaryDataContext>();
}

OpenCMLParser::PrimaryDataContext* OpenCMLParser::ArgsTypeContext::primaryData(size_t i) {
  return getRuleContext<OpenCMLParser::PrimaryDataContext>(i);
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
    setState(733);
    primaryType();
    setState(751);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 103, _ctx)) {
    case 1: {
      setState(734);
      match(OpenCMLParser::T__12);
      setState(737);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 100, _ctx)) {
      case 1: {
        setState(735);
        typeExpr();
        break;
      }

      case 2: {
        setState(736);
        primaryData();
        break;
      }

      default:
        break;
      }
      setState(746);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == OpenCMLParser::T__5) {
        setState(739);
        match(OpenCMLParser::T__5);
        setState(742);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 101, _ctx)) {
        case 1: {
          setState(740);
          typeExpr();
          break;
        }

        case 2: {
          setState(741);
          primaryData();
          break;
        }

        default:
          break;
        }
        setState(748);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(749);
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
  enterRule(_localctx, 128, OpenCMLParser::RulePrimaryType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(764);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 104, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(753);
      match(OpenCMLParser::INNER_ATOM_TYPE);
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(754);
      dictExprType();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(755);
      identRef();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(756);
      match(OpenCMLParser::T__4);
      setState(757);
      typeExpr();
      setState(758);
      match(OpenCMLParser::T__6);
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(760);
      tupleType();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(761);
      lambdaType();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(762);
      match(OpenCMLParser::TYPEOF);
      setState(763);
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
    setState(766);
    dictType();
    setState(771);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 105, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(767);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__34

        || _la == OpenCMLParser::T__49)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(768);
        dictType(); 
      }
      setState(773);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 105, _ctx);
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
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(774);
    match(OpenCMLParser::T__0);
    setState(783);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(775);
      keyTypePair();
      setState(780);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 106, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(776);
          match(OpenCMLParser::T__5);
          setState(777);
          keyTypePair(); 
        }
        setState(782);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 106, _ctx);
      }
    }
    setState(786);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(785);
      match(OpenCMLParser::T__5);
    }
    setState(788);
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
  enterRule(_localctx, 134, OpenCMLParser::RuleTupleType);
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
    setState(790);
    match(OpenCMLParser::T__4);
    setState(799);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8226) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 546291713) != 0)) {
      setState(791);
      typeExpr();
      setState(796);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 109, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(792);
          match(OpenCMLParser::T__5);
          setState(793);
          typeExpr(); 
        }
        setState(798);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 109, _ctx);
      }
    }
    setState(802);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(801);
      match(OpenCMLParser::T__5);
    }
    setState(804);
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
  enterRule(_localctx, 136, OpenCMLParser::RuleLambdaType);
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
    setState(807);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 83) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 83)) & 63) != 0)) {
      setState(806);
      modifiers();
    }
    setState(810);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__12) {
      setState(809);
      angledParams();
    }
    setState(812);
    parentParams();
    setState(813);
    match(OpenCMLParser::T__3);
    setState(814);
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
  enterRule(_localctx, 138, OpenCMLParser::RuleIdentDef);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(816);
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
  enterRule(_localctx, 140, OpenCMLParser::RuleIdentRef);

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
    setState(822);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 114, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(818);
        match(OpenCMLParser::IDENTIFIER);
        setState(819);
        match(OpenCMLParser::T__50); 
      }
      setState(824);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 114, _ctx);
    }
    setState(825);
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
    case 54: return annoExprSempred(antlrcpp::downCast<AnnoExprContext *>(context), predicateIndex);

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
