
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
      "bracedIdents", "bracketIdents", "carrier", "letDecl", "useDecl", 
      "retStmt", "typeDecl", "enumDecl", "exprStmt", "annotation", "annotations", 
      "modifiers", "indexValue", "keyTypePair", "keyValuePair", "keyParamPair", 
      "identList", "valueList", "indexValues", "pairedValues", "pairedParams", 
      "argumentList", "memberAccess", "parentParams", "parentArgues", "angledParams", 
      "angledValues", "blockExpr", "dataExpr", "pattern", "matchCase", "catchClause", 
      "structExpr", "logicalOrExpr", "logicalAndExpr", "equalityExpr", "relationalExpr", 
      "additiveExpr", "multiplicativeExpr", "nullableExpr", "unaryExpr", 
      "linkExpr", "bindExpr", "withExpr", "annoExpr", "dictExpr", "listExpr", 
      "primaryData", "literal", "typeExpr", "unionType", "unionUnit", "listType", 
      "typeOrData", "argsType", "primaryType", "dictExprType", "dictType", 
      "tupleType", "lambdaType", "identDef", "identRef"
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
  	4,1,102,833,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,2,55,7,55,2,56,7,
  	56,2,57,7,57,2,58,7,58,2,59,7,59,2,60,7,60,2,61,7,61,2,62,7,62,2,63,7,
  	63,2,64,7,64,2,65,7,65,2,66,7,66,2,67,7,67,2,68,7,68,2,69,7,69,2,70,7,
  	70,2,71,7,71,2,72,7,72,1,0,3,0,148,8,0,1,0,1,0,3,0,152,8,0,5,0,154,8,
  	0,10,0,12,0,157,9,0,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,169,8,
  	1,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,2,179,8,2,1,3,1,3,3,3,183,8,3,1,3,
  	5,3,186,8,3,10,3,12,3,189,9,3,1,3,3,3,192,8,3,1,4,1,4,1,4,1,5,1,5,1,5,
  	1,5,3,5,201,8,5,1,5,1,5,1,5,3,5,206,8,5,1,6,1,6,1,6,1,6,3,6,212,8,6,1,
  	7,3,7,215,8,7,1,7,1,7,3,7,219,8,7,1,7,1,7,1,8,3,8,224,8,8,1,8,3,8,227,
  	8,8,1,8,1,8,1,8,3,8,232,8,8,1,8,1,8,1,8,1,9,3,9,238,8,9,1,9,1,9,3,9,242,
  	8,9,1,9,3,9,245,8,9,1,9,3,9,248,8,9,1,9,1,9,1,9,1,9,1,9,3,9,255,8,9,1,
  	9,1,9,1,10,1,10,3,10,261,8,10,1,10,3,10,264,8,10,1,10,1,10,1,11,1,11,
  	3,11,270,8,11,1,11,3,11,273,8,11,1,11,1,11,1,12,1,12,3,12,279,8,12,1,
  	12,3,12,282,8,12,1,12,1,12,1,13,1,13,1,13,1,13,3,13,290,8,13,1,14,1,14,
  	1,14,1,14,3,14,296,8,14,1,14,1,14,1,14,1,15,1,15,1,15,1,15,3,15,305,8,
  	15,1,15,1,15,1,16,1,16,1,16,1,17,1,17,1,17,1,17,1,17,1,18,1,18,1,18,1,
  	18,3,18,321,8,18,1,18,1,18,1,18,1,18,3,18,327,8,18,1,18,1,18,1,19,3,19,
  	332,8,19,1,19,1,19,1,20,1,20,1,20,1,21,4,21,340,8,21,11,21,12,21,341,
  	1,22,4,22,345,8,22,11,22,12,22,346,1,23,1,23,1,23,3,23,352,8,23,1,24,
  	1,24,1,24,1,24,1,25,1,25,1,25,1,25,1,25,1,25,3,25,364,8,25,1,26,3,26,
  	367,8,26,1,26,1,26,3,26,371,8,26,1,26,1,26,1,26,1,26,3,26,377,8,26,1,
  	26,1,26,3,26,381,8,26,1,27,1,27,1,27,5,27,386,8,27,10,27,12,27,389,9,
  	27,1,28,1,28,1,28,5,28,394,8,28,10,28,12,28,397,9,28,1,29,1,29,1,29,5,
  	29,402,8,29,10,29,12,29,405,9,29,1,30,1,30,1,30,5,30,410,8,30,10,30,12,
  	30,413,9,30,1,31,1,31,1,31,5,31,418,8,31,10,31,12,31,421,9,31,1,32,1,
  	32,1,32,3,32,426,8,32,1,32,3,32,429,8,32,1,33,1,33,1,33,1,33,1,33,1,33,
  	3,33,437,8,33,3,33,439,8,33,1,33,1,33,1,34,1,34,3,34,445,8,34,1,34,3,
  	34,448,8,34,1,34,1,34,1,35,1,35,3,35,454,8,35,1,35,3,35,457,8,35,1,35,
  	1,35,1,36,1,36,3,36,463,8,36,1,36,3,36,466,8,36,1,36,1,36,1,37,1,37,3,
  	37,472,8,37,1,37,3,37,475,8,37,1,37,1,37,1,38,1,38,3,38,481,8,38,1,39,
  	3,39,484,8,39,1,39,1,39,1,39,3,39,489,8,39,1,40,1,40,1,40,1,40,1,40,3,
  	40,496,8,40,1,40,3,40,499,8,40,1,40,1,40,1,40,1,40,3,40,505,8,40,1,40,
  	3,40,508,8,40,1,40,1,40,3,40,512,8,40,1,41,1,41,1,41,1,41,5,41,518,8,
  	41,10,41,12,41,521,9,41,1,41,1,41,1,41,1,42,1,42,1,42,1,42,1,42,1,42,
  	1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,4,43,544,
  	8,43,11,43,12,43,545,1,43,1,43,1,43,1,43,1,43,4,43,553,8,43,11,43,12,
  	43,554,1,43,1,43,3,43,559,8,43,3,43,561,8,43,1,44,1,44,1,44,5,44,566,
  	8,44,10,44,12,44,569,9,44,1,45,1,45,1,45,5,45,574,8,45,10,45,12,45,577,
  	9,45,1,46,1,46,1,46,5,46,582,8,46,10,46,12,46,585,9,46,1,47,1,47,1,47,
  	5,47,590,8,47,10,47,12,47,593,9,47,1,48,1,48,1,48,5,48,598,8,48,10,48,
  	12,48,601,9,48,1,49,1,49,1,49,5,49,606,8,49,10,49,12,49,609,9,49,1,50,
  	1,50,1,50,3,50,614,8,50,1,51,1,51,1,51,3,51,619,8,51,1,51,1,51,3,51,623,
  	8,51,1,52,1,52,1,52,5,52,628,8,52,10,52,12,52,631,9,52,1,53,1,53,1,53,
  	5,53,636,8,53,10,53,12,53,639,9,53,1,54,1,54,1,54,5,54,644,8,54,10,54,
  	12,54,647,9,54,1,55,1,55,1,55,1,55,1,55,1,55,3,55,655,8,55,1,55,5,55,
  	658,8,55,10,55,12,55,661,9,55,1,56,1,56,1,56,3,56,666,8,56,3,56,668,8,
  	56,1,56,1,56,1,57,1,57,1,57,3,57,675,8,57,1,57,1,57,1,57,1,57,1,57,1,
  	57,1,57,3,57,684,8,57,3,57,686,8,57,1,57,1,57,1,58,1,58,1,58,1,58,1,58,
  	1,58,1,58,1,58,1,58,1,58,3,58,700,8,58,1,58,3,58,703,8,58,1,58,1,58,3,
  	58,707,8,58,1,59,1,59,1,60,1,60,1,60,3,60,714,8,60,3,60,716,8,60,1,61,
  	1,61,1,61,5,61,721,8,61,10,61,12,61,724,9,61,1,62,1,62,1,62,3,62,729,
  	8,62,1,62,1,62,1,63,1,63,1,63,5,63,736,8,63,10,63,12,63,739,9,63,1,64,
  	1,64,3,64,743,8,64,1,65,1,65,1,65,1,65,1,65,5,65,750,8,65,10,65,12,65,
  	753,9,65,1,65,1,65,3,65,757,8,65,1,66,1,66,1,66,1,66,1,66,1,66,1,66,1,
  	66,1,66,1,66,1,66,3,66,770,8,66,1,67,1,67,1,67,5,67,775,8,67,10,67,12,
  	67,778,9,67,1,68,1,68,1,68,1,68,5,68,784,8,68,10,68,12,68,787,9,68,3,
  	68,789,8,68,1,68,3,68,792,8,68,1,68,1,68,1,69,1,69,1,69,1,69,5,69,800,
  	8,69,10,69,12,69,803,9,69,3,69,805,8,69,1,69,3,69,808,8,69,1,69,1,69,
  	1,70,3,70,813,8,70,1,70,3,70,816,8,70,1,70,1,70,1,70,1,70,1,71,1,71,1,
  	72,1,72,5,72,826,8,72,10,72,12,72,829,9,72,1,72,1,72,1,72,0,0,73,0,2,
  	4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,
  	52,54,56,58,60,62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,96,
  	98,100,102,104,106,108,110,112,114,116,118,120,122,124,126,128,130,132,
  	134,136,138,140,142,144,0,16,1,0,55,56,2,0,64,64,81,82,1,0,83,88,2,0,
  	10,10,15,22,1,0,27,30,2,0,13,14,31,32,1,0,33,34,1,0,35,38,1,0,39,40,1,
  	0,53,54,2,0,34,34,41,42,1,0,43,44,1,0,45,46,1,0,47,48,3,0,89,91,97,97,
  	99,102,2,0,35,35,50,50,907,0,147,1,0,0,0,2,168,1,0,0,0,4,178,1,0,0,0,
  	6,180,1,0,0,0,8,193,1,0,0,0,10,196,1,0,0,0,12,207,1,0,0,0,14,214,1,0,
  	0,0,16,223,1,0,0,0,18,237,1,0,0,0,20,258,1,0,0,0,22,267,1,0,0,0,24,276,
  	1,0,0,0,26,289,1,0,0,0,28,291,1,0,0,0,30,300,1,0,0,0,32,308,1,0,0,0,34,
  	311,1,0,0,0,36,316,1,0,0,0,38,331,1,0,0,0,40,335,1,0,0,0,42,339,1,0,0,
  	0,44,344,1,0,0,0,46,351,1,0,0,0,48,353,1,0,0,0,50,363,1,0,0,0,52,366,
  	1,0,0,0,54,382,1,0,0,0,56,390,1,0,0,0,58,398,1,0,0,0,60,406,1,0,0,0,62,
  	414,1,0,0,0,64,428,1,0,0,0,66,430,1,0,0,0,68,442,1,0,0,0,70,451,1,0,0,
  	0,72,460,1,0,0,0,74,469,1,0,0,0,76,480,1,0,0,0,78,483,1,0,0,0,80,511,
  	1,0,0,0,82,513,1,0,0,0,84,525,1,0,0,0,86,560,1,0,0,0,88,562,1,0,0,0,90,
  	570,1,0,0,0,92,578,1,0,0,0,94,586,1,0,0,0,96,594,1,0,0,0,98,602,1,0,0,
  	0,100,610,1,0,0,0,102,622,1,0,0,0,104,624,1,0,0,0,106,632,1,0,0,0,108,
  	640,1,0,0,0,110,648,1,0,0,0,112,662,1,0,0,0,114,671,1,0,0,0,116,706,1,
  	0,0,0,118,708,1,0,0,0,120,710,1,0,0,0,122,717,1,0,0,0,124,728,1,0,0,0,
  	126,732,1,0,0,0,128,742,1,0,0,0,130,744,1,0,0,0,132,769,1,0,0,0,134,771,
  	1,0,0,0,136,779,1,0,0,0,138,795,1,0,0,0,140,812,1,0,0,0,142,821,1,0,0,
  	0,144,827,1,0,0,0,146,148,5,52,0,0,147,146,1,0,0,0,147,148,1,0,0,0,148,
  	155,1,0,0,0,149,151,3,2,1,0,150,152,5,52,0,0,151,150,1,0,0,0,151,152,
  	1,0,0,0,152,154,1,0,0,0,153,149,1,0,0,0,154,157,1,0,0,0,155,153,1,0,0,
  	0,155,156,1,0,0,0,156,158,1,0,0,0,157,155,1,0,0,0,158,159,5,0,0,1,159,
  	1,1,0,0,0,160,169,3,8,4,0,161,169,3,10,5,0,162,169,3,12,6,0,163,169,3,
  	28,14,0,164,169,3,30,15,0,165,169,3,18,9,0,166,169,3,34,17,0,167,169,
  	3,36,18,0,168,160,1,0,0,0,168,161,1,0,0,0,168,162,1,0,0,0,168,163,1,0,
  	0,0,168,164,1,0,0,0,168,165,1,0,0,0,168,166,1,0,0,0,168,167,1,0,0,0,169,
  	3,1,0,0,0,170,179,3,28,14,0,171,179,3,30,15,0,172,179,3,18,9,0,173,179,
  	3,34,17,0,174,179,3,36,18,0,175,179,3,32,16,0,176,179,3,38,19,0,177,179,
  	3,14,7,0,178,170,1,0,0,0,178,171,1,0,0,0,178,172,1,0,0,0,178,173,1,0,
  	0,0,178,174,1,0,0,0,178,175,1,0,0,0,178,176,1,0,0,0,178,177,1,0,0,0,179,
  	5,1,0,0,0,180,187,3,4,2,0,181,183,5,52,0,0,182,181,1,0,0,0,182,183,1,
  	0,0,0,183,184,1,0,0,0,184,186,3,4,2,0,185,182,1,0,0,0,186,189,1,0,0,0,
  	187,185,1,0,0,0,187,188,1,0,0,0,188,191,1,0,0,0,189,187,1,0,0,0,190,192,
  	5,52,0,0,191,190,1,0,0,0,191,192,1,0,0,0,192,7,1,0,0,0,193,194,5,65,0,
  	0,194,195,3,142,71,0,195,9,1,0,0,0,196,205,5,66,0,0,197,206,5,99,0,0,
  	198,201,3,142,71,0,199,201,3,22,11,0,200,198,1,0,0,0,200,199,1,0,0,0,
  	201,202,1,0,0,0,202,203,5,58,0,0,203,204,5,99,0,0,204,206,1,0,0,0,205,
  	197,1,0,0,0,205,200,1,0,0,0,206,11,1,0,0,0,207,211,5,67,0,0,208,212,3,
  	28,14,0,209,212,3,34,17,0,210,212,3,22,11,0,211,208,1,0,0,0,211,209,1,
  	0,0,0,211,210,1,0,0,0,212,13,1,0,0,0,213,215,5,83,0,0,214,213,1,0,0,0,
  	214,215,1,0,0,0,215,216,1,0,0,0,216,218,5,1,0,0,217,219,3,6,3,0,218,217,
  	1,0,0,0,218,219,1,0,0,0,219,220,1,0,0,0,220,221,5,2,0,0,221,15,1,0,0,
  	0,222,224,3,44,22,0,223,222,1,0,0,0,223,224,1,0,0,0,224,226,1,0,0,0,225,
  	227,3,72,36,0,226,225,1,0,0,0,226,227,1,0,0,0,227,228,1,0,0,0,228,231,
  	3,68,34,0,229,230,5,3,0,0,230,232,3,120,60,0,231,229,1,0,0,0,231,232,
  	1,0,0,0,232,233,1,0,0,0,233,234,5,4,0,0,234,235,3,76,38,0,235,17,1,0,
  	0,0,236,238,3,42,21,0,237,236,1,0,0,0,237,238,1,0,0,0,238,241,1,0,0,0,
  	239,240,5,62,0,0,240,242,3,72,36,0,241,239,1,0,0,0,241,242,1,0,0,0,242,
  	244,1,0,0,0,243,245,5,67,0,0,244,243,1,0,0,0,244,245,1,0,0,0,245,247,
  	1,0,0,0,246,248,3,44,22,0,247,246,1,0,0,0,247,248,1,0,0,0,248,249,1,0,
  	0,0,249,250,5,59,0,0,250,251,3,142,71,0,251,254,3,68,34,0,252,253,5,3,
  	0,0,253,255,3,120,60,0,254,252,1,0,0,0,254,255,1,0,0,0,255,256,1,0,0,
  	0,256,257,3,14,7,0,257,19,1,0,0,0,258,260,5,5,0,0,259,261,3,54,27,0,260,
  	259,1,0,0,0,260,261,1,0,0,0,261,263,1,0,0,0,262,264,5,6,0,0,263,262,1,
  	0,0,0,263,264,1,0,0,0,264,265,1,0,0,0,265,266,5,7,0,0,266,21,1,0,0,0,
  	267,269,5,1,0,0,268,270,3,54,27,0,269,268,1,0,0,0,269,270,1,0,0,0,270,
  	272,1,0,0,0,271,273,5,6,0,0,272,271,1,0,0,0,272,273,1,0,0,0,273,274,1,
  	0,0,0,274,275,5,2,0,0,275,23,1,0,0,0,276,278,5,8,0,0,277,279,3,54,27,
  	0,278,277,1,0,0,0,278,279,1,0,0,0,279,281,1,0,0,0,280,282,5,6,0,0,281,
  	280,1,0,0,0,281,282,1,0,0,0,282,283,1,0,0,0,283,284,5,9,0,0,284,25,1,
  	0,0,0,285,290,3,142,71,0,286,290,3,20,10,0,287,290,3,22,11,0,288,290,
  	3,24,12,0,289,285,1,0,0,0,289,286,1,0,0,0,289,287,1,0,0,0,289,288,1,0,
  	0,0,290,27,1,0,0,0,291,292,7,0,0,0,292,295,3,26,13,0,293,294,5,3,0,0,
  	294,296,3,120,60,0,295,293,1,0,0,0,295,296,1,0,0,0,296,297,1,0,0,0,297,
  	298,5,10,0,0,298,299,3,78,39,0,299,29,1,0,0,0,300,304,5,57,0,0,301,302,
  	3,142,71,0,302,303,5,10,0,0,303,305,1,0,0,0,304,301,1,0,0,0,304,305,1,
  	0,0,0,305,306,1,0,0,0,306,307,3,144,72,0,307,31,1,0,0,0,308,309,7,1,0,
  	0,309,310,3,78,39,0,310,33,1,0,0,0,311,312,5,60,0,0,312,313,3,142,71,
  	0,313,314,5,10,0,0,314,315,3,120,60,0,315,35,1,0,0,0,316,317,5,61,0,0,
  	317,320,3,142,71,0,318,319,5,77,0,0,319,321,3,120,60,0,320,318,1,0,0,
  	0,320,321,1,0,0,0,321,322,1,0,0,0,322,323,5,10,0,0,323,324,5,1,0,0,324,
  	326,3,60,30,0,325,327,5,6,0,0,326,325,1,0,0,0,326,327,1,0,0,0,327,328,
  	1,0,0,0,328,329,5,2,0,0,329,37,1,0,0,0,330,332,3,42,21,0,331,330,1,0,
  	0,0,331,332,1,0,0,0,332,333,1,0,0,0,333,334,3,78,39,0,334,39,1,0,0,0,
  	335,336,5,11,0,0,336,337,3,116,58,0,337,41,1,0,0,0,338,340,3,40,20,0,
  	339,338,1,0,0,0,340,341,1,0,0,0,341,339,1,0,0,0,341,342,1,0,0,0,342,43,
  	1,0,0,0,343,345,7,2,0,0,344,343,1,0,0,0,345,346,1,0,0,0,346,344,1,0,0,
  	0,346,347,1,0,0,0,347,45,1,0,0,0,348,352,3,78,39,0,349,350,5,12,0,0,350,
  	352,3,78,39,0,351,348,1,0,0,0,351,349,1,0,0,0,352,47,1,0,0,0,353,354,
  	3,142,71,0,354,355,5,3,0,0,355,356,3,120,60,0,356,49,1,0,0,0,357,358,
  	3,142,71,0,358,359,5,3,0,0,359,360,3,78,39,0,360,364,1,0,0,0,361,362,
  	5,12,0,0,362,364,3,78,39,0,363,357,1,0,0,0,363,361,1,0,0,0,364,51,1,0,
  	0,0,365,367,5,56,0,0,366,365,1,0,0,0,366,367,1,0,0,0,367,368,1,0,0,0,
  	368,370,3,142,71,0,369,371,3,40,20,0,370,369,1,0,0,0,370,371,1,0,0,0,
  	371,372,1,0,0,0,372,376,5,3,0,0,373,377,3,120,60,0,374,375,5,68,0,0,375,
  	377,3,142,71,0,376,373,1,0,0,0,376,374,1,0,0,0,377,380,1,0,0,0,378,379,
  	5,10,0,0,379,381,3,78,39,0,380,378,1,0,0,0,380,381,1,0,0,0,381,53,1,0,
  	0,0,382,387,3,142,71,0,383,384,5,6,0,0,384,386,3,142,71,0,385,383,1,0,
  	0,0,386,389,1,0,0,0,387,385,1,0,0,0,387,388,1,0,0,0,388,55,1,0,0,0,389,
  	387,1,0,0,0,390,395,3,78,39,0,391,392,5,6,0,0,392,394,3,78,39,0,393,391,
  	1,0,0,0,394,397,1,0,0,0,395,393,1,0,0,0,395,396,1,0,0,0,396,57,1,0,0,
  	0,397,395,1,0,0,0,398,403,3,46,23,0,399,400,5,6,0,0,400,402,3,46,23,0,
  	401,399,1,0,0,0,402,405,1,0,0,0,403,401,1,0,0,0,403,404,1,0,0,0,404,59,
  	1,0,0,0,405,403,1,0,0,0,406,411,3,50,25,0,407,408,5,6,0,0,408,410,3,50,
  	25,0,409,407,1,0,0,0,410,413,1,0,0,0,411,409,1,0,0,0,411,412,1,0,0,0,
  	412,61,1,0,0,0,413,411,1,0,0,0,414,419,3,52,26,0,415,416,5,6,0,0,416,
  	418,3,52,26,0,417,415,1,0,0,0,418,421,1,0,0,0,419,417,1,0,0,0,419,420,
  	1,0,0,0,420,63,1,0,0,0,421,419,1,0,0,0,422,425,3,58,29,0,423,424,5,6,
  	0,0,424,426,3,60,30,0,425,423,1,0,0,0,425,426,1,0,0,0,426,429,1,0,0,0,
  	427,429,3,60,30,0,428,422,1,0,0,0,428,427,1,0,0,0,429,65,1,0,0,0,430,
  	431,5,8,0,0,431,438,3,78,39,0,432,433,5,3,0,0,433,436,3,78,39,0,434,435,
  	5,3,0,0,435,437,3,78,39,0,436,434,1,0,0,0,436,437,1,0,0,0,437,439,1,0,
  	0,0,438,432,1,0,0,0,438,439,1,0,0,0,439,440,1,0,0,0,440,441,5,9,0,0,441,
  	67,1,0,0,0,442,444,5,5,0,0,443,445,3,62,31,0,444,443,1,0,0,0,444,445,
  	1,0,0,0,445,447,1,0,0,0,446,448,5,6,0,0,447,446,1,0,0,0,447,448,1,0,0,
  	0,448,449,1,0,0,0,449,450,5,7,0,0,450,69,1,0,0,0,451,453,5,5,0,0,452,
  	454,3,64,32,0,453,452,1,0,0,0,453,454,1,0,0,0,454,456,1,0,0,0,455,457,
  	5,6,0,0,456,455,1,0,0,0,456,457,1,0,0,0,457,458,1,0,0,0,458,459,5,7,0,
  	0,459,71,1,0,0,0,460,462,5,13,0,0,461,463,3,62,31,0,462,461,1,0,0,0,462,
  	463,1,0,0,0,463,465,1,0,0,0,464,466,5,6,0,0,465,464,1,0,0,0,465,466,1,
  	0,0,0,466,467,1,0,0,0,467,468,5,14,0,0,468,73,1,0,0,0,469,471,5,13,0,
  	0,470,472,3,64,32,0,471,470,1,0,0,0,471,472,1,0,0,0,472,474,1,0,0,0,473,
  	475,5,6,0,0,474,473,1,0,0,0,474,475,1,0,0,0,475,476,1,0,0,0,476,477,5,
  	14,0,0,477,75,1,0,0,0,478,481,3,14,7,0,479,481,3,78,39,0,480,478,1,0,
  	0,0,480,479,1,0,0,0,481,77,1,0,0,0,482,484,5,63,0,0,483,482,1,0,0,0,483,
  	484,1,0,0,0,484,485,1,0,0,0,485,488,3,86,43,0,486,487,7,3,0,0,487,489,
  	3,86,43,0,488,486,1,0,0,0,488,489,1,0,0,0,489,79,1,0,0,0,490,512,3,144,
  	72,0,491,512,3,118,59,0,492,495,5,5,0,0,493,496,3,56,28,0,494,496,3,54,
  	27,0,495,493,1,0,0,0,495,494,1,0,0,0,495,496,1,0,0,0,496,498,1,0,0,0,
  	497,499,5,6,0,0,498,497,1,0,0,0,498,499,1,0,0,0,499,500,1,0,0,0,500,512,
  	5,7,0,0,501,504,5,1,0,0,502,505,3,60,30,0,503,505,3,54,27,0,504,502,1,
  	0,0,0,504,503,1,0,0,0,504,505,1,0,0,0,505,507,1,0,0,0,506,508,5,6,0,0,
  	507,506,1,0,0,0,507,508,1,0,0,0,508,509,1,0,0,0,509,512,5,2,0,0,510,512,
  	5,23,0,0,511,490,1,0,0,0,511,491,1,0,0,0,511,492,1,0,0,0,511,501,1,0,
  	0,0,511,510,1,0,0,0,512,81,1,0,0,0,513,514,5,74,0,0,514,519,3,80,40,0,
  	515,516,5,24,0,0,516,518,3,80,40,0,517,515,1,0,0,0,518,521,1,0,0,0,519,
  	517,1,0,0,0,519,520,1,0,0,0,520,522,1,0,0,0,521,519,1,0,0,0,522,523,5,
  	4,0,0,523,524,3,76,38,0,524,83,1,0,0,0,525,526,5,79,0,0,526,527,3,142,
  	71,0,527,528,5,3,0,0,528,529,3,120,60,0,529,530,3,14,7,0,530,85,1,0,0,
  	0,531,561,3,88,44,0,532,533,5,70,0,0,533,534,3,88,44,0,534,535,5,71,0,
  	0,535,536,3,76,38,0,536,537,5,72,0,0,537,538,3,76,38,0,538,561,1,0,0,
  	0,539,540,5,73,0,0,540,541,3,144,72,0,541,543,5,1,0,0,542,544,3,82,41,
  	0,543,542,1,0,0,0,544,545,1,0,0,0,545,543,1,0,0,0,545,546,1,0,0,0,546,
  	547,1,0,0,0,547,548,5,2,0,0,548,561,1,0,0,0,549,550,5,78,0,0,550,552,
  	3,14,7,0,551,553,3,84,42,0,552,551,1,0,0,0,553,554,1,0,0,0,554,552,1,
  	0,0,0,554,555,1,0,0,0,555,558,1,0,0,0,556,557,5,80,0,0,557,559,3,14,7,
  	0,558,556,1,0,0,0,558,559,1,0,0,0,559,561,1,0,0,0,560,531,1,0,0,0,560,
  	532,1,0,0,0,560,539,1,0,0,0,560,549,1,0,0,0,561,87,1,0,0,0,562,567,3,
  	90,45,0,563,564,5,25,0,0,564,566,3,90,45,0,565,563,1,0,0,0,566,569,1,
  	0,0,0,567,565,1,0,0,0,567,568,1,0,0,0,568,89,1,0,0,0,569,567,1,0,0,0,
  	570,575,3,92,46,0,571,572,5,26,0,0,572,574,3,92,46,0,573,571,1,0,0,0,
  	574,577,1,0,0,0,575,573,1,0,0,0,575,576,1,0,0,0,576,91,1,0,0,0,577,575,
  	1,0,0,0,578,583,3,94,47,0,579,580,7,4,0,0,580,582,3,94,47,0,581,579,1,
  	0,0,0,582,585,1,0,0,0,583,581,1,0,0,0,583,584,1,0,0,0,584,93,1,0,0,0,
  	585,583,1,0,0,0,586,591,3,96,48,0,587,588,7,5,0,0,588,590,3,96,48,0,589,
  	587,1,0,0,0,590,593,1,0,0,0,591,589,1,0,0,0,591,592,1,0,0,0,592,95,1,
  	0,0,0,593,591,1,0,0,0,594,599,3,98,49,0,595,596,7,6,0,0,596,598,3,98,
  	49,0,597,595,1,0,0,0,598,601,1,0,0,0,599,597,1,0,0,0,599,600,1,0,0,0,
  	600,97,1,0,0,0,601,599,1,0,0,0,602,607,3,100,50,0,603,604,7,7,0,0,604,
  	606,3,100,50,0,605,603,1,0,0,0,606,609,1,0,0,0,607,605,1,0,0,0,607,608,
  	1,0,0,0,608,99,1,0,0,0,609,607,1,0,0,0,610,613,3,102,51,0,611,612,7,8,
  	0,0,612,614,3,78,39,0,613,611,1,0,0,0,613,614,1,0,0,0,614,101,1,0,0,0,
  	615,618,3,104,52,0,616,617,7,9,0,0,617,619,3,120,60,0,618,616,1,0,0,0,
  	618,619,1,0,0,0,619,623,1,0,0,0,620,621,7,10,0,0,621,623,3,104,52,0,622,
  	615,1,0,0,0,622,620,1,0,0,0,623,103,1,0,0,0,624,629,3,106,53,0,625,626,
  	7,11,0,0,626,628,3,106,53,0,627,625,1,0,0,0,628,631,1,0,0,0,629,627,1,
  	0,0,0,629,630,1,0,0,0,630,105,1,0,0,0,631,629,1,0,0,0,632,637,3,108,54,
  	0,633,634,7,12,0,0,634,636,3,108,54,0,635,633,1,0,0,0,636,639,1,0,0,0,
  	637,635,1,0,0,0,637,638,1,0,0,0,638,107,1,0,0,0,639,637,1,0,0,0,640,645,
  	3,110,55,0,641,642,7,13,0,0,642,644,3,110,55,0,643,641,1,0,0,0,644,647,
  	1,0,0,0,645,643,1,0,0,0,645,646,1,0,0,0,646,109,1,0,0,0,647,645,1,0,0,
  	0,648,659,3,116,58,0,649,654,4,55,0,0,650,655,3,66,33,0,651,655,3,70,
  	35,0,652,655,3,74,37,0,653,655,5,41,0,0,654,650,1,0,0,0,654,651,1,0,0,
  	0,654,652,1,0,0,0,654,653,1,0,0,0,655,658,1,0,0,0,656,658,3,40,20,0,657,
  	649,1,0,0,0,657,656,1,0,0,0,658,661,1,0,0,0,659,657,1,0,0,0,659,660,1,
  	0,0,0,660,111,1,0,0,0,661,659,1,0,0,0,662,667,5,1,0,0,663,665,3,60,30,
  	0,664,666,5,6,0,0,665,664,1,0,0,0,665,666,1,0,0,0,666,668,1,0,0,0,667,
  	663,1,0,0,0,667,668,1,0,0,0,668,669,1,0,0,0,669,670,5,2,0,0,670,113,1,
  	0,0,0,671,685,5,8,0,0,672,674,3,58,29,0,673,675,5,6,0,0,674,673,1,0,0,
  	0,674,675,1,0,0,0,675,686,1,0,0,0,676,677,3,78,39,0,677,678,5,75,0,0,
  	678,679,3,144,72,0,679,680,5,76,0,0,680,683,3,78,39,0,681,682,5,70,0,
  	0,682,684,3,78,39,0,683,681,1,0,0,0,683,684,1,0,0,0,684,686,1,0,0,0,685,
  	672,1,0,0,0,685,676,1,0,0,0,685,686,1,0,0,0,686,687,1,0,0,0,687,688,5,
  	9,0,0,688,115,1,0,0,0,689,707,3,144,72,0,690,707,3,118,59,0,691,707,3,
  	114,57,0,692,707,3,112,56,0,693,694,5,5,0,0,694,695,3,78,39,0,695,696,
  	5,7,0,0,696,707,1,0,0,0,697,699,5,5,0,0,698,700,3,56,28,0,699,698,1,0,
  	0,0,699,700,1,0,0,0,700,702,1,0,0,0,701,703,5,6,0,0,702,701,1,0,0,0,702,
  	703,1,0,0,0,703,704,1,0,0,0,704,707,5,7,0,0,705,707,3,16,8,0,706,689,
  	1,0,0,0,706,690,1,0,0,0,706,691,1,0,0,0,706,692,1,0,0,0,706,693,1,0,0,
  	0,706,697,1,0,0,0,706,705,1,0,0,0,707,117,1,0,0,0,708,709,7,14,0,0,709,
  	119,1,0,0,0,710,715,3,122,61,0,711,713,5,49,0,0,712,714,3,122,61,0,713,
  	712,1,0,0,0,713,714,1,0,0,0,714,716,1,0,0,0,715,711,1,0,0,0,715,716,1,
  	0,0,0,716,121,1,0,0,0,717,722,3,124,62,0,718,719,5,24,0,0,719,721,3,124,
  	62,0,720,718,1,0,0,0,721,724,1,0,0,0,722,720,1,0,0,0,722,723,1,0,0,0,
  	723,123,1,0,0,0,724,722,1,0,0,0,725,726,3,142,71,0,726,727,5,77,0,0,727,
  	729,1,0,0,0,728,725,1,0,0,0,728,729,1,0,0,0,729,730,1,0,0,0,730,731,3,
  	126,63,0,731,125,1,0,0,0,732,737,3,130,65,0,733,734,5,8,0,0,734,736,5,
  	9,0,0,735,733,1,0,0,0,736,739,1,0,0,0,737,735,1,0,0,0,737,738,1,0,0,0,
  	738,127,1,0,0,0,739,737,1,0,0,0,740,743,3,120,60,0,741,743,3,116,58,0,
  	742,740,1,0,0,0,742,741,1,0,0,0,743,129,1,0,0,0,744,756,3,132,66,0,745,
  	746,5,13,0,0,746,751,3,128,64,0,747,748,5,6,0,0,748,750,3,128,64,0,749,
  	747,1,0,0,0,750,753,1,0,0,0,751,749,1,0,0,0,751,752,1,0,0,0,752,754,1,
  	0,0,0,753,751,1,0,0,0,754,755,5,14,0,0,755,757,1,0,0,0,756,745,1,0,0,
  	0,756,757,1,0,0,0,757,131,1,0,0,0,758,770,5,92,0,0,759,770,3,134,67,0,
  	760,770,3,144,72,0,761,762,5,5,0,0,762,763,3,120,60,0,763,764,5,7,0,0,
  	764,770,1,0,0,0,765,770,3,138,69,0,766,770,3,140,70,0,767,768,5,69,0,
  	0,768,770,3,78,39,0,769,758,1,0,0,0,769,759,1,0,0,0,769,760,1,0,0,0,769,
  	761,1,0,0,0,769,765,1,0,0,0,769,766,1,0,0,0,769,767,1,0,0,0,770,133,1,
  	0,0,0,771,776,3,136,68,0,772,773,7,15,0,0,773,775,3,136,68,0,774,772,
  	1,0,0,0,775,778,1,0,0,0,776,774,1,0,0,0,776,777,1,0,0,0,777,135,1,0,0,
  	0,778,776,1,0,0,0,779,788,5,1,0,0,780,785,3,48,24,0,781,782,5,6,0,0,782,
  	784,3,48,24,0,783,781,1,0,0,0,784,787,1,0,0,0,785,783,1,0,0,0,785,786,
  	1,0,0,0,786,789,1,0,0,0,787,785,1,0,0,0,788,780,1,0,0,0,788,789,1,0,0,
  	0,789,791,1,0,0,0,790,792,5,6,0,0,791,790,1,0,0,0,791,792,1,0,0,0,792,
  	793,1,0,0,0,793,794,5,2,0,0,794,137,1,0,0,0,795,804,5,5,0,0,796,801,3,
  	120,60,0,797,798,5,6,0,0,798,800,3,120,60,0,799,797,1,0,0,0,800,803,1,
  	0,0,0,801,799,1,0,0,0,801,802,1,0,0,0,802,805,1,0,0,0,803,801,1,0,0,0,
  	804,796,1,0,0,0,804,805,1,0,0,0,805,807,1,0,0,0,806,808,5,6,0,0,807,806,
  	1,0,0,0,807,808,1,0,0,0,808,809,1,0,0,0,809,810,5,7,0,0,810,139,1,0,0,
  	0,811,813,3,44,22,0,812,811,1,0,0,0,812,813,1,0,0,0,813,815,1,0,0,0,814,
  	816,3,72,36,0,815,814,1,0,0,0,815,816,1,0,0,0,816,817,1,0,0,0,817,818,
  	3,68,34,0,818,819,5,4,0,0,819,820,3,120,60,0,820,141,1,0,0,0,821,822,
  	5,98,0,0,822,143,1,0,0,0,823,824,5,98,0,0,824,826,5,51,0,0,825,823,1,
  	0,0,0,826,829,1,0,0,0,827,825,1,0,0,0,827,828,1,0,0,0,828,830,1,0,0,0,
  	829,827,1,0,0,0,830,831,5,98,0,0,831,145,1,0,0,0,113,147,151,155,168,
  	178,182,187,191,200,205,211,214,218,223,226,231,237,241,244,247,254,260,
  	263,269,272,278,281,289,295,304,320,326,331,341,346,351,363,366,370,376,
  	380,387,395,403,411,419,425,428,436,438,444,447,453,456,462,465,471,474,
  	480,483,488,495,498,504,507,511,519,545,554,558,560,567,575,583,591,599,
  	607,613,618,622,629,637,645,654,657,659,665,667,674,683,685,699,702,706,
  	713,715,722,728,737,742,751,756,769,776,785,788,791,801,804,807,812,815,
  	827
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
    setState(147);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SEP) {
      setState(146);
      match(OpenCMLParser::SEP);
    }
    setState(155);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8899112863684102144) != 0) || ((((_la - 65) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 65)) & 16515079) != 0)) {
      setState(149);
      decl();
      setState(151);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::SEP) {
        setState(150);
        match(OpenCMLParser::SEP);
      }
      setState(157);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(158);
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
    setState(168);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 3, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(160);
      moduleDecl();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(161);
      importDecl();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(162);
      exportDecl();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(163);
      letDecl();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(164);
      useDecl();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(165);
      funcDecl();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(166);
      typeDecl();
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(167);
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
    setState(178);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 4, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(170);
      letDecl();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(171);
      useDecl();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(172);
      funcDecl();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(173);
      typeDecl();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(174);
      enumDecl();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(175);
      retStmt();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(176);
      exprStmt();
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(177);
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
    setState(180);
    stmt();
    setState(187);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 6, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(182);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::SEP) {
          setState(181);
          match(OpenCMLParser::SEP);
        }
        setState(184);
        stmt(); 
      }
      setState(189);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 6, _ctx);
    }
    setState(191);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SEP) {
      setState(190);
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
    setState(193);
    match(OpenCMLParser::MODULE);
    setState(194);
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
    setState(196);
    match(OpenCMLParser::IMPORT);
    setState(205);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::STRING: {
        setState(197);
        match(OpenCMLParser::STRING);
        break;
      }

      case OpenCMLParser::T__0:
      case OpenCMLParser::IDENTIFIER: {
        setState(200);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case OpenCMLParser::IDENTIFIER: {
            setState(198);
            identDef();
            break;
          }

          case OpenCMLParser::T__0: {
            setState(199);
            bracedIdents();
            break;
          }

        default:
          throw NoViableAltException(this);
        }
        setState(202);
        match(OpenCMLParser::FROM);
        setState(203);
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
    setState(207);
    match(OpenCMLParser::EXPORT);
    setState(211);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::LET:
      case OpenCMLParser::VAR: {
        setState(208);
        letDecl();
        break;
      }

      case OpenCMLParser::TYPE: {
        setState(209);
        typeDecl();
        break;
      }

      case OpenCMLParser::T__0: {
        setState(210);
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
    setState(214);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SYNC) {
      setState(213);
      match(OpenCMLParser::SYNC);
    }
    setState(216);
    match(OpenCMLParser::T__0);
    setState(218);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -324252558921029342) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 541434200649) != 0)) {
      setState(217);
      stmtList();
    }
    setState(220);
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
    setState(223);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 83) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 83)) & 63) != 0)) {
      setState(222);
      modifiers();
    }
    setState(226);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__12) {
      setState(225);
      angledParams();
    }
    setState(228);
    parentParams();
    setState(231);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__2) {
      setState(229);
      match(OpenCMLParser::T__2);
      setState(230);
      typeExpr();
    }
    setState(233);
    match(OpenCMLParser::T__3);
    setState(234);
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
    setState(237);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__10) {
      setState(236);
      annotations();
    }
    setState(241);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WITH) {
      setState(239);
      match(OpenCMLParser::WITH);
      setState(240);
      angledParams();
    }
    setState(244);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::EXPORT) {
      setState(243);
      match(OpenCMLParser::EXPORT);
    }
    setState(247);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 83) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 83)) & 63) != 0)) {
      setState(246);
      modifiers();
    }
    setState(249);
    match(OpenCMLParser::FUNC);
    setState(250);
    identDef();
    setState(251);
    parentParams();
    setState(254);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__2) {
      setState(252);
      match(OpenCMLParser::T__2);
      setState(253);
      typeExpr();
    }
    setState(256);
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
    setState(258);
    match(OpenCMLParser::T__4);
    setState(260);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(259);
      identList();
    }
    setState(263);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(262);
      match(OpenCMLParser::T__5);
    }
    setState(265);
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
    setState(267);
    match(OpenCMLParser::T__0);
    setState(269);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(268);
      identList();
    }
    setState(272);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(271);
      match(OpenCMLParser::T__5);
    }
    setState(274);
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
    setState(276);
    match(OpenCMLParser::T__7);
    setState(278);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(277);
      identList();
    }
    setState(281);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(280);
      match(OpenCMLParser::T__5);
    }
    setState(283);
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
    setState(289);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(285);
        identDef();
        break;
      }

      case OpenCMLParser::T__4: {
        enterOuterAlt(_localctx, 2);
        setState(286);
        parentIdents();
        break;
      }

      case OpenCMLParser::T__0: {
        enterOuterAlt(_localctx, 3);
        setState(287);
        bracedIdents();
        break;
      }

      case OpenCMLParser::T__7: {
        enterOuterAlt(_localctx, 4);
        setState(288);
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
  enterRule(_localctx, 28, OpenCMLParser::RuleLetDecl);
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
    setState(291);
    _la = _input->LA(1);
    if (!(_la == OpenCMLParser::LET

    || _la == OpenCMLParser::VAR)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(292);
    carrier();
    setState(295);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__2) {
      setState(293);
      match(OpenCMLParser::T__2);
      setState(294);
      typeExpr();
    }
    setState(297);
    match(OpenCMLParser::T__9);
    setState(298);
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
  enterRule(_localctx, 30, OpenCMLParser::RuleUseDecl);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(300);
    match(OpenCMLParser::USE);
    setState(304);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx)) {
    case 1: {
      setState(301);
      identDef();
      setState(302);
      match(OpenCMLParser::T__9);
      break;
    }

    default:
      break;
    }
    setState(306);
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
    setState(308);
    _la = _input->LA(1);
    if (!(((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 393217) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
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
  enterRule(_localctx, 34, OpenCMLParser::RuleTypeDecl);

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
    match(OpenCMLParser::TYPE);
    setState(312);
    identDef();
    setState(313);
    match(OpenCMLParser::T__9);
    setState(314);
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
  enterRule(_localctx, 36, OpenCMLParser::RuleEnumDecl);
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
    setState(316);
    match(OpenCMLParser::ENUM);
    setState(317);
    identDef();
    setState(320);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::OF) {
      setState(318);
      match(OpenCMLParser::OF);
      setState(319);
      typeExpr();
    }
    setState(322);
    match(OpenCMLParser::T__9);
    setState(323);
    match(OpenCMLParser::T__0);
    setState(324);
    pairedValues();
    setState(326);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(325);
      match(OpenCMLParser::T__5);
    }
    setState(328);
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
    setState(331);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__10) {
      setState(330);
      annotations();
    }
    setState(333);
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
    setState(335);
    match(OpenCMLParser::T__10);
    setState(336);
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
    setState(339); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(338);
      annotation();
      setState(341); 
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
    setState(344); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(343);
      _la = _input->LA(1);
      if (!(((((_la - 83) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 83)) & 63) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(346); 
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
    setState(351);
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
        setState(348);
        dataExpr();
        break;
      }

      case OpenCMLParser::T__11: {
        enterOuterAlt(_localctx, 2);
        setState(349);
        match(OpenCMLParser::T__11);
        setState(350);
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
    setState(353);
    identDef();
    setState(354);
    match(OpenCMLParser::T__2);
    setState(355);
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
    setState(363);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(357);
        identDef();
        setState(358);
        match(OpenCMLParser::T__2);
        setState(359);
        dataExpr();
        break;
      }

      case OpenCMLParser::T__11: {
        enterOuterAlt(_localctx, 2);
        setState(361);
        match(OpenCMLParser::T__11);
        setState(362);
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
    setState(366);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR) {
      setState(365);
      match(OpenCMLParser::VAR);
    }
    setState(368);
    identDef();
    setState(370);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__10) {
      setState(369);
      annotation();
    }
    setState(372);
    match(OpenCMLParser::T__2);
    setState(376);
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
        setState(373);
        typeExpr();
        break;
      }

      case OpenCMLParser::TYPEAS: {
        setState(374);
        match(OpenCMLParser::TYPEAS);
        setState(375);
        identDef();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(380);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__9) {
      setState(378);
      match(OpenCMLParser::T__9);
      setState(379);
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
    setState(382);
    identDef();
    setState(387);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 41, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(383);
        match(OpenCMLParser::T__5);
        setState(384);
        identDef(); 
      }
      setState(389);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 41, _ctx);
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
    setState(390);
    dataExpr();
    setState(395);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 42, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(391);
        match(OpenCMLParser::T__5);
        setState(392);
        dataExpr(); 
      }
      setState(397);
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
    setState(398);
    indexValue();
    setState(403);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 43, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(399);
        match(OpenCMLParser::T__5);
        setState(400);
        indexValue(); 
      }
      setState(405);
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
    setState(406);
    keyValuePair();
    setState(411);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 44, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(407);
        match(OpenCMLParser::T__5);
        setState(408);
        keyValuePair(); 
      }
      setState(413);
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
    setState(414);
    keyParamPair();
    setState(419);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 45, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(415);
        match(OpenCMLParser::T__5);
        setState(416);
        keyParamPair(); 
      }
      setState(421);
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
    setState(428);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 47, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(422);
      indexValues();
      setState(425);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 46, _ctx)) {
      case 1: {
        setState(423);
        match(OpenCMLParser::T__5);
        setState(424);
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
      setState(427);
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
    setState(430);
    match(OpenCMLParser::T__7);
    setState(431);
    dataExpr();
    setState(438);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__2) {
      setState(432);
      match(OpenCMLParser::T__2);
      setState(433);
      dataExpr();
      setState(436);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__2) {
        setState(434);
        match(OpenCMLParser::T__2);
        setState(435);
        dataExpr();
      }
    }
    setState(440);
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
    setState(442);
    match(OpenCMLParser::T__4);
    setState(444);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(443);
      pairedParams();
    }
    setState(447);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(446);
      match(OpenCMLParser::T__5);
    }
    setState(449);
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
    setState(451);
    match(OpenCMLParser::T__4);
    setState(453);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -9223365422605127390) != 0) || ((((_la - 70) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 70)) & 8459903241) != 0)) {
      setState(452);
      argumentList();
    }
    setState(456);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(455);
      match(OpenCMLParser::T__5);
    }
    setState(458);
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
    setState(460);
    match(OpenCMLParser::T__12);
    setState(462);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::VAR

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(461);
      pairedParams();
    }
    setState(465);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(464);
      match(OpenCMLParser::T__5);
    }
    setState(467);
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
    setState(469);
    match(OpenCMLParser::T__12);
    setState(471);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -9223365422605127390) != 0) || ((((_la - 70) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 70)) & 8459903241) != 0)) {
      setState(470);
      argumentList();
    }
    setState(474);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(473);
      match(OpenCMLParser::T__5);
    }
    setState(476);
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
    setState(480);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 58, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(478);
      stmtBlock();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(479);
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
    setState(483);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WAIT) {
      setState(482);
      match(OpenCMLParser::WAIT);
    }
    setState(485);
    structExpr();
    setState(488);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 60, _ctx)) {
    case 1: {
      setState(486);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8356864) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(487);
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
    setState(511);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(490);
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
        setState(491);
        literal();
        break;
      }

      case OpenCMLParser::T__4: {
        enterOuterAlt(_localctx, 3);
        setState(492);
        match(OpenCMLParser::T__4);
        setState(495);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx)) {
        case 1: {
          setState(493);
          valueList();
          break;
        }

        case 2: {
          setState(494);
          identList();
          break;
        }

        default:
          break;
        }
        setState(498);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__5) {
          setState(497);
          match(OpenCMLParser::T__5);
        }
        setState(500);
        match(OpenCMLParser::T__6);
        break;
      }

      case OpenCMLParser::T__0: {
        enterOuterAlt(_localctx, 4);
        setState(501);
        match(OpenCMLParser::T__0);
        setState(504);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 63, _ctx)) {
        case 1: {
          setState(502);
          pairedValues();
          break;
        }

        case 2: {
          setState(503);
          identList();
          break;
        }

        default:
          break;
        }
        setState(507);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__5) {
          setState(506);
          match(OpenCMLParser::T__5);
        }
        setState(509);
        match(OpenCMLParser::T__1);
        break;
      }

      case OpenCMLParser::T__22: {
        enterOuterAlt(_localctx, 5);
        setState(510);
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
    setState(513);
    match(OpenCMLParser::CASE);
    setState(514);
    pattern();
    setState(519);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__23) {
      setState(515);
      match(OpenCMLParser::T__23);
      setState(516);
      pattern();
      setState(521);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(522);
    match(OpenCMLParser::T__3);
    setState(523);
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
  enterRule(_localctx, 84, OpenCMLParser::RuleCatchClause);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(525);
    match(OpenCMLParser::CATCH);
    setState(526);
    identDef();
    setState(527);
    match(OpenCMLParser::T__2);
    setState(528);
    typeExpr();
    setState(529);
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
  enterRule(_localctx, 86, OpenCMLParser::RuleStructExpr);
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
    setState(560);
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
        setState(531);
        logicalOrExpr();
        break;
      }

      case OpenCMLParser::IF: {
        enterOuterAlt(_localctx, 2);
        setState(532);
        match(OpenCMLParser::IF);
        setState(533);
        logicalOrExpr();
        setState(534);
        match(OpenCMLParser::THEN);
        setState(535);
        blockExpr();
        setState(536);
        match(OpenCMLParser::ELSE);
        setState(537);
        blockExpr();
        break;
      }

      case OpenCMLParser::MATCH: {
        enterOuterAlt(_localctx, 3);
        setState(539);
        match(OpenCMLParser::MATCH);
        setState(540);
        identRef();
        setState(541);
        match(OpenCMLParser::T__0);
        setState(543); 
        _errHandler->sync(this);
        _la = _input->LA(1);
        do {
          setState(542);
          matchCase();
          setState(545); 
          _errHandler->sync(this);
          _la = _input->LA(1);
        } while (_la == OpenCMLParser::CASE);
        setState(547);
        match(OpenCMLParser::T__1);
        break;
      }

      case OpenCMLParser::TRY: {
        enterOuterAlt(_localctx, 4);
        setState(549);
        match(OpenCMLParser::TRY);
        setState(550);
        stmtBlock();
        setState(552); 
        _errHandler->sync(this);
        alt = 1;
        do {
          switch (alt) {
            case 1: {
                  setState(551);
                  catchClause();
                  break;
                }

          default:
            throw NoViableAltException(this);
          }
          setState(554); 
          _errHandler->sync(this);
          alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 68, _ctx);
        } while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER);
        setState(558);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx)) {
        case 1: {
          setState(556);
          match(OpenCMLParser::FINALLY);
          setState(557);
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
  enterRule(_localctx, 88, OpenCMLParser::RuleLogicalOrExpr);

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
    setState(562);
    logicalAndExpr();
    setState(567);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 71, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(563);
        match(OpenCMLParser::T__24);
        setState(564);
        logicalAndExpr(); 
      }
      setState(569);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 71, _ctx);
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
    setState(570);
    equalityExpr();
    setState(575);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 72, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(571);
        match(OpenCMLParser::T__25);
        setState(572);
        equalityExpr(); 
      }
      setState(577);
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
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(578);
    relationalExpr();
    setState(583);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 73, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(579);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 2013265920) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(580);
        relationalExpr(); 
      }
      setState(585);
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
    setState(586);
    additiveExpr();
    setState(591);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(587);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 6442475520) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(588);
        additiveExpr(); 
      }
      setState(593);
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
    setState(594);
    multiplicativeExpr();
    setState(599);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 75, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(595);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__32

        || _la == OpenCMLParser::T__33)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(596);
        multiplicativeExpr(); 
      }
      setState(601);
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
    setState(602);
    nullableExpr();
    setState(607);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 76, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(603);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 515396075520) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(604);
        nullableExpr(); 
      }
      setState(609);
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
  enterRule(_localctx, 100, OpenCMLParser::RuleNullableExpr);
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
    unaryExpr();
    setState(613);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 77, _ctx)) {
    case 1: {
      setState(611);
      _la = _input->LA(1);
      if (!(_la == OpenCMLParser::T__38

      || _la == OpenCMLParser::T__39)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(612);
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
  enterRule(_localctx, 102, OpenCMLParser::RuleUnaryExpr);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(622);
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
        setState(615);
        linkExpr();
        setState(618);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 78, _ctx)) {
        case 1: {
          setState(616);
          _la = _input->LA(1);
          if (!(_la == OpenCMLParser::AS

          || _la == OpenCMLParser::IS)) {
          _errHandler->recoverInline(this);
          }
          else {
            _errHandler->reportMatch(this);
            consume();
          }
          setState(617);
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
        setState(620);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 6614249635840) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(621);
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
  enterRule(_localctx, 104, OpenCMLParser::RuleLinkExpr);
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
    setState(624);
    bindExpr();
    setState(629);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 80, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(625);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__42

        || _la == OpenCMLParser::T__43)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(626);
        bindExpr(); 
      }
      setState(631);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 80, _ctx);
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
  enterRule(_localctx, 106, OpenCMLParser::RuleBindExpr);
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
    setState(632);
    withExpr();
    setState(637);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 81, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(633);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__44

        || _la == OpenCMLParser::T__45)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(634);
        withExpr(); 
      }
      setState(639);
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
  enterRule(_localctx, 108, OpenCMLParser::RuleWithExpr);
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
    setState(640);
    annoExpr();
    setState(645);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 82, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(641);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__46

        || _la == OpenCMLParser::T__47)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(642);
        annoExpr(); 
      }
      setState(647);
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
  enterRule(_localctx, 110, OpenCMLParser::RuleAnnoExpr);

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
    setState(648);
    primaryData();
    setState(659);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 85, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(657);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 84, _ctx)) {
        case 1: {
          setState(649);

          if (!(isAdjacent())) throw FailedPredicateException(this, "isAdjacent()");
          setState(654);
          _errHandler->sync(this);
          switch (_input->LA(1)) {
            case OpenCMLParser::T__7: {
              setState(650);
              memberAccess();
              break;
            }

            case OpenCMLParser::T__4: {
              setState(651);
              parentArgues();
              break;
            }

            case OpenCMLParser::T__12: {
              setState(652);
              angledValues();
              break;
            }

            case OpenCMLParser::T__40: {
              setState(653);
              match(OpenCMLParser::T__40);
              break;
            }

          default:
            throw NoViableAltException(this);
          }
          break;
        }

        case 2: {
          setState(656);
          annotation();
          break;
        }

        default:
          break;
        } 
      }
      setState(661);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 85, _ctx);
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
  enterRule(_localctx, 112, OpenCMLParser::RuleDictExpr);
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
    setState(662);
    match(OpenCMLParser::T__0);
    setState(667);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__11 || _la == OpenCMLParser::IDENTIFIER) {
      setState(663);
      pairedValues();
      setState(665);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__5) {
        setState(664);
        match(OpenCMLParser::T__5);
      }
    }
    setState(669);
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
  enterRule(_localctx, 114, OpenCMLParser::RuleListExpr);
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
    setState(671);
    match(OpenCMLParser::T__7);
    setState(685);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 90, _ctx)) {
    case 1: {
      setState(672);
      indexValues();
      setState(674);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__5) {
        setState(673);
        match(OpenCMLParser::T__5);
      }
      break;
    }

    case 2: {
      setState(676);
      dataExpr();
      setState(677);
      match(OpenCMLParser::FOR);
      setState(678);
      identRef();
      setState(679);
      match(OpenCMLParser::IN);
      setState(680);
      dataExpr();
      setState(683);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::IF) {
        setState(681);
        match(OpenCMLParser::IF);
        setState(682);
        dataExpr();
      }
      break;
    }

    default:
      break;
    }
    setState(687);
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
  enterRule(_localctx, 116, OpenCMLParser::RulePrimaryData);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(706);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 93, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(689);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(690);
      literal();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(691);
      listExpr();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(692);
      dictExpr();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(693);
      match(OpenCMLParser::T__4);
      setState(694);
      dataExpr();
      setState(695);
      match(OpenCMLParser::T__6);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(697);
      match(OpenCMLParser::T__4);
      setState(699);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & -9223365422605131486) != 0) || ((((_la - 70) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 70)) & 8459903241) != 0)) {
        setState(698);
        valueList();
      }
      setState(702);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__5) {
        setState(701);
        match(OpenCMLParser::T__5);
      }
      setState(704);
      match(OpenCMLParser::T__6);
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(705);
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
  enterRule(_localctx, 118, OpenCMLParser::RuleLiteral);
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
    setState(708);
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
    setState(710);
    unionType();
    setState(715);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 95, _ctx)) {
    case 1: {
      setState(711);
      match(OpenCMLParser::T__48);
      setState(713);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 94, _ctx)) {
      case 1: {
        setState(712);
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
    setState(717);
    unionUnit();
    setState(722);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 96, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(718);
        match(OpenCMLParser::T__23);
        setState(719);
        unionUnit(); 
      }
      setState(724);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 96, _ctx);
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
  enterRule(_localctx, 124, OpenCMLParser::RuleUnionUnit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(728);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 97, _ctx)) {
    case 1: {
      setState(725);
      identDef();
      setState(726);
      match(OpenCMLParser::OF);
      break;
    }

    default:
      break;
    }
    setState(730);
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
  enterRule(_localctx, 126, OpenCMLParser::RuleListType);

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
    setState(732);
    argsType();
    setState(737);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 98, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(733);
        match(OpenCMLParser::T__7);
        setState(734);
        match(OpenCMLParser::T__8); 
      }
      setState(739);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 98, _ctx);
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
  enterRule(_localctx, 128, OpenCMLParser::RuleTypeOrData);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(742);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 99, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(740);
      typeExpr();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(741);
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
  enterRule(_localctx, 130, OpenCMLParser::RuleArgsType);
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
    setState(744);
    primaryType();
    setState(756);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 101, _ctx)) {
    case 1: {
      setState(745);
      match(OpenCMLParser::T__12);
      setState(746);
      typeOrData();
      setState(751);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == OpenCMLParser::T__5) {
        setState(747);
        match(OpenCMLParser::T__5);
        setState(748);
        typeOrData();
        setState(753);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(754);
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
  enterRule(_localctx, 132, OpenCMLParser::RulePrimaryType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(769);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 102, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(758);
      match(OpenCMLParser::INNER_ATOM_TYPE);
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(759);
      dictExprType();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(760);
      identRef();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(761);
      match(OpenCMLParser::T__4);
      setState(762);
      typeExpr();
      setState(763);
      match(OpenCMLParser::T__6);
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(765);
      tupleType();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(766);
      lambdaType();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(767);
      match(OpenCMLParser::TYPEOF);
      setState(768);
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
  enterRule(_localctx, 134, OpenCMLParser::RuleDictExprType);
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
    setState(771);
    dictType();
    setState(776);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 103, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(772);
        _la = _input->LA(1);
        if (!(_la == OpenCMLParser::T__34

        || _la == OpenCMLParser::T__49)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(773);
        dictType(); 
      }
      setState(778);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 103, _ctx);
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
  enterRule(_localctx, 136, OpenCMLParser::RuleDictType);
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
    setState(779);
    match(OpenCMLParser::T__0);
    setState(788);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(780);
      keyTypePair();
      setState(785);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 104, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(781);
          match(OpenCMLParser::T__5);
          setState(782);
          keyTypePair(); 
        }
        setState(787);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 104, _ctx);
      }
    }
    setState(791);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(790);
      match(OpenCMLParser::T__5);
    }
    setState(793);
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
  enterRule(_localctx, 138, OpenCMLParser::RuleTupleType);
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
    setState(795);
    match(OpenCMLParser::T__4);
    setState(804);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8226) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 546291713) != 0)) {
      setState(796);
      typeExpr();
      setState(801);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 107, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(797);
          match(OpenCMLParser::T__5);
          setState(798);
          typeExpr(); 
        }
        setState(803);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 107, _ctx);
      }
    }
    setState(807);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(806);
      match(OpenCMLParser::T__5);
    }
    setState(809);
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
  enterRule(_localctx, 140, OpenCMLParser::RuleLambdaType);
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
    setState(812);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 83) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 83)) & 63) != 0)) {
      setState(811);
      modifiers();
    }
    setState(815);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__12) {
      setState(814);
      angledParams();
    }
    setState(817);
    parentParams();
    setState(818);
    match(OpenCMLParser::T__3);
    setState(819);
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
  enterRule(_localctx, 142, OpenCMLParser::RuleIdentDef);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(821);
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
  enterRule(_localctx, 144, OpenCMLParser::RuleIdentRef);

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
    setState(827);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 112, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(823);
        match(OpenCMLParser::IDENTIFIER);
        setState(824);
        match(OpenCMLParser::T__50); 
      }
      setState(829);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 112, _ctx);
    }
    setState(830);
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
    case 55: return annoExprSempred(antlrcpp::downCast<AnnoExprContext *>(context), predicateIndex);

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
