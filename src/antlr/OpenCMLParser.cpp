
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
      "program", "stmtList", "stmt", "letStmt", "useStmt", "withDef", "funcDef", 
      "typeDef", "enumDef", "retStmt", "exprStmt", "assignStmt", "carrier", 
      "biasAnno", "sizeAnno", "annotation", "annotations", "modifiers", 
      "withList", "withDecl", "paramDef", "argsList", "argument", "typePack", 
      "keyValDecl", "keyValExpr", "entityRef", "functorRef", "listUnpack", 
      "dictUnpack", "dictPack", "listPack", "stmtPack", "lambda", "entityExpr", 
      "entityChain", "entity", "normCall", "linkCall", "calcExpr", "relaExpr", 
      "addExpr", "multiExpr", "unaryExpr", "primExpr", "expr", "literal", 
      "value", "type", "innerType", "numberType", "scalarType", "vectorType", 
      "structType", "identRef"
    },
    std::vector<std::string>{
      "", "':'", "'='", "':='", "'::'", "'->'", "'('", "','", "')'", "'['", 
      "']'", "'@'", "'<'", "'>'", "'{'", "'}'", "'\\u003F'", "'.'", "'=>'", 
      "'*='", "'/='", "'%='", "'+='", "'-='", "'<='", "'>='", "'=='", "'!='", 
      "'+'", "'-'", "'^'", "'*'", "'/'", "'%'", "'++'", "'--'", "'!'", "';'", 
      "'as'", "'let'", "'use'", "'func'", "'type'", "'enum'", "'with'", 
      "'return'", "'inner'", "'outer'", "'sync'", "'scoped'", "'static'", 
      "'atomic'", "'null'", "'true'", "'false'", "'any'", "'number'", "'string'", 
      "'bool'", "'functor'", "'int'", "'real'", "'complex'", "'array'", 
      "'matrix'", "'list'", "'dict'"
    },
    std::vector<std::string>{
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "SEP", "AS", "LET", "USE", "FUNC", "TYPE", "ENUM", "WITH", 
      "RETURN", "INNER", "OUTER", "SYNC", "SCOPED", "STATIC", "ATOMIC", 
      "NULL", "TRUE", "FALSE", "ANY_TYPE", "NUMBER_TYPE", "STRING_TYPE", 
      "BOOLEAN_TYPE", "FUNCTOR_TYPE", "INTEGER_TYPE", "REAL_TYPE", "COMPLEX_TYPE", 
      "ARRAY_TYPE", "MATRIX_TYPE", "LIST_TYPE", "DICT_TYPE", "SKIP_", "MULTI_STR", 
      "IDENTIFIER", "UNIT", "STRING", "FSTRING", "INTEGER", "REAL"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,74,726,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,1,0,3,0,112,8,0,
  	1,0,1,0,1,1,1,1,1,1,5,1,119,8,1,10,1,12,1,122,9,1,1,1,3,1,125,8,1,1,2,
  	1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,2,135,8,2,1,3,1,3,1,3,1,3,3,3,141,8,3,1,
  	3,3,3,144,8,3,1,3,1,3,1,3,1,3,1,3,3,3,151,8,3,1,3,1,3,1,3,3,3,156,8,3,
  	1,4,1,4,1,4,3,4,161,8,4,1,4,1,4,1,4,1,4,1,4,1,4,3,4,169,8,4,1,5,1,5,1,
  	5,3,5,174,8,5,1,6,1,6,3,6,178,8,6,1,6,1,6,1,6,1,6,1,6,1,6,3,6,186,8,6,
  	1,6,3,6,189,8,6,1,6,1,6,1,7,1,7,1,7,3,7,196,8,7,1,7,1,7,3,7,200,8,7,1,
  	8,1,8,1,8,3,8,205,8,8,1,8,1,8,1,9,1,9,3,9,211,8,9,1,10,1,10,1,10,1,11,
  	1,11,1,11,3,11,219,8,11,1,11,3,11,222,8,11,1,11,1,11,1,12,1,12,1,12,3,
  	12,229,8,12,1,13,1,13,1,13,1,13,1,13,1,13,1,14,1,14,1,14,1,14,1,14,1,
  	14,1,15,1,15,1,15,1,15,1,15,3,15,248,8,15,1,16,1,16,3,16,252,8,16,5,16,
  	254,8,16,10,16,12,16,257,9,16,1,17,5,17,260,8,17,10,17,12,17,263,9,17,
  	1,18,1,18,1,18,1,18,5,18,269,8,18,10,18,12,18,272,9,18,1,18,3,18,275,
  	8,18,1,18,1,18,1,19,1,19,1,19,1,19,5,19,283,8,19,10,19,12,19,286,9,19,
  	1,19,3,19,289,8,19,1,19,1,19,1,20,1,20,1,20,1,20,5,20,297,8,20,10,20,
  	12,20,300,9,20,3,20,302,8,20,1,20,3,20,305,8,20,1,20,1,20,1,21,1,21,1,
  	21,1,21,5,21,313,8,21,10,21,12,21,316,9,21,3,21,318,8,21,1,21,3,21,321,
  	8,21,1,21,1,21,1,22,1,22,1,22,1,22,3,22,329,8,22,1,23,1,23,1,23,1,23,
  	5,23,335,8,23,10,23,12,23,338,9,23,3,23,340,8,23,1,23,3,23,343,8,23,1,
  	23,1,23,1,24,1,24,3,24,349,8,24,1,24,3,24,352,8,24,1,24,1,24,1,24,1,24,
  	3,24,358,8,24,1,25,1,25,3,25,362,8,25,1,25,1,25,1,25,1,26,1,26,1,26,1,
  	26,3,26,371,8,26,5,26,373,8,26,10,26,12,26,376,9,26,1,26,3,26,379,8,26,
  	1,27,1,27,3,27,383,8,27,1,27,3,27,386,8,27,1,28,1,28,1,28,1,28,5,28,392,
  	8,28,10,28,12,28,395,9,28,3,28,397,8,28,1,28,3,28,400,8,28,1,28,1,28,
  	1,29,1,29,1,29,1,29,5,29,408,8,29,10,29,12,29,411,9,29,3,29,413,8,29,
  	1,29,3,29,416,8,29,1,29,1,29,1,30,1,30,1,30,1,30,5,30,424,8,30,10,30,
  	12,30,427,9,30,3,30,429,8,30,1,30,3,30,432,8,30,1,30,1,30,1,31,1,31,1,
  	31,1,31,5,31,440,8,31,10,31,12,31,443,9,31,3,31,445,8,31,1,31,3,31,448,
  	8,31,1,31,1,31,1,32,1,32,3,32,454,8,32,1,32,1,32,1,33,1,33,1,33,3,33,
  	461,8,33,1,33,1,33,1,34,1,34,1,34,1,34,3,34,469,8,34,1,34,1,34,3,34,473,
  	8,34,1,35,1,35,1,35,4,35,478,8,35,11,35,12,35,479,1,36,1,36,1,36,1,36,
  	1,36,3,36,487,8,36,1,36,3,36,490,8,36,1,36,1,36,3,36,494,8,36,1,37,1,
  	37,1,37,1,38,1,38,1,38,1,38,1,38,1,38,3,38,505,8,38,1,38,1,38,1,38,1,
  	38,1,38,1,38,3,38,513,8,38,5,38,515,8,38,10,38,12,38,518,9,38,1,39,1,
  	39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,
  	39,1,39,1,39,5,39,538,8,39,10,39,12,39,541,9,39,1,40,1,40,1,40,1,40,1,
  	40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,
  	40,1,40,1,40,5,40,564,8,40,10,40,12,40,567,9,40,1,41,1,41,1,41,1,41,1,
  	41,1,41,1,41,1,41,1,41,5,41,578,8,41,10,41,12,41,581,9,41,1,42,1,42,1,
  	42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,5,42,598,
  	8,42,10,42,12,42,601,9,42,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,
  	1,43,1,43,1,43,1,43,1,43,1,43,3,43,618,8,43,1,44,1,44,1,44,1,44,1,44,
  	1,44,1,44,3,44,627,8,44,1,45,1,45,3,45,631,8,45,1,46,1,46,1,46,1,46,1,
  	46,1,46,1,46,3,46,640,8,46,1,47,1,47,3,47,644,8,47,1,48,1,48,1,48,3,48,
  	649,8,48,1,49,1,49,1,49,1,49,1,49,1,49,3,49,657,8,49,1,50,1,50,3,50,661,
  	8,50,1,51,1,51,1,52,1,52,1,52,1,52,1,52,3,52,670,8,52,1,52,1,52,1,52,
  	3,52,675,8,52,1,52,1,52,1,52,1,52,1,52,3,52,682,8,52,1,52,1,52,1,52,5,
  	52,687,8,52,10,52,12,52,690,9,52,3,52,692,8,52,1,53,1,53,1,53,1,53,1,
  	53,5,53,699,8,53,10,53,12,53,702,9,53,1,53,1,53,3,53,706,8,53,1,53,1,
  	53,1,53,3,53,711,8,53,1,53,1,53,1,53,1,53,1,53,1,53,1,53,3,53,720,8,53,
  	3,53,722,8,53,1,54,1,54,1,54,0,5,76,78,80,82,84,55,0,2,4,6,8,10,12,14,
  	16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,
  	62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104,
  	106,108,0,3,1,0,46,51,1,0,73,74,1,0,60,62,817,0,111,1,0,0,0,2,115,1,0,
  	0,0,4,134,1,0,0,0,6,155,1,0,0,0,8,168,1,0,0,0,10,170,1,0,0,0,12,175,1,
  	0,0,0,14,192,1,0,0,0,16,201,1,0,0,0,18,208,1,0,0,0,20,212,1,0,0,0,22,
  	215,1,0,0,0,24,228,1,0,0,0,26,230,1,0,0,0,28,236,1,0,0,0,30,242,1,0,0,
  	0,32,255,1,0,0,0,34,261,1,0,0,0,36,264,1,0,0,0,38,278,1,0,0,0,40,292,
  	1,0,0,0,42,308,1,0,0,0,44,328,1,0,0,0,46,330,1,0,0,0,48,346,1,0,0,0,50,
  	359,1,0,0,0,52,366,1,0,0,0,54,380,1,0,0,0,56,387,1,0,0,0,58,403,1,0,0,
  	0,60,419,1,0,0,0,62,435,1,0,0,0,64,451,1,0,0,0,66,460,1,0,0,0,68,468,
  	1,0,0,0,70,477,1,0,0,0,72,493,1,0,0,0,74,495,1,0,0,0,76,504,1,0,0,0,78,
  	519,1,0,0,0,80,542,1,0,0,0,82,568,1,0,0,0,84,582,1,0,0,0,86,617,1,0,0,
  	0,88,626,1,0,0,0,90,630,1,0,0,0,92,639,1,0,0,0,94,641,1,0,0,0,96,648,
  	1,0,0,0,98,656,1,0,0,0,100,660,1,0,0,0,102,662,1,0,0,0,104,691,1,0,0,
  	0,106,721,1,0,0,0,108,723,1,0,0,0,110,112,3,2,1,0,111,110,1,0,0,0,111,
  	112,1,0,0,0,112,113,1,0,0,0,113,114,5,0,0,1,114,1,1,0,0,0,115,120,3,4,
  	2,0,116,117,5,37,0,0,117,119,3,4,2,0,118,116,1,0,0,0,119,122,1,0,0,0,
  	120,118,1,0,0,0,120,121,1,0,0,0,121,124,1,0,0,0,122,120,1,0,0,0,123,125,
  	5,37,0,0,124,123,1,0,0,0,124,125,1,0,0,0,125,3,1,0,0,0,126,135,3,6,3,
  	0,127,135,3,8,4,0,128,135,3,12,6,0,129,135,3,14,7,0,130,135,3,16,8,0,
  	131,135,3,18,9,0,132,135,3,20,10,0,133,135,3,22,11,0,134,126,1,0,0,0,
  	134,127,1,0,0,0,134,128,1,0,0,0,134,129,1,0,0,0,134,130,1,0,0,0,134,131,
  	1,0,0,0,134,132,1,0,0,0,134,133,1,0,0,0,135,5,1,0,0,0,136,137,5,39,0,
  	0,137,140,3,24,12,0,138,139,5,1,0,0,139,141,3,96,48,0,140,138,1,0,0,0,
  	140,141,1,0,0,0,141,143,1,0,0,0,142,144,5,2,0,0,143,142,1,0,0,0,143,144,
  	1,0,0,0,144,145,1,0,0,0,145,146,3,90,45,0,146,156,1,0,0,0,147,150,3,24,
  	12,0,148,149,5,1,0,0,149,151,3,96,48,0,150,148,1,0,0,0,150,151,1,0,0,
  	0,151,152,1,0,0,0,152,153,5,3,0,0,153,154,3,90,45,0,154,156,1,0,0,0,155,
  	136,1,0,0,0,155,147,1,0,0,0,156,7,1,0,0,0,157,158,5,40,0,0,158,160,3,
  	24,12,0,159,161,5,2,0,0,160,159,1,0,0,0,160,161,1,0,0,0,161,162,1,0,0,
  	0,162,163,3,90,45,0,163,169,1,0,0,0,164,165,3,24,12,0,165,166,5,4,0,0,
  	166,167,3,90,45,0,167,169,1,0,0,0,168,157,1,0,0,0,168,164,1,0,0,0,169,
  	9,1,0,0,0,170,173,5,44,0,0,171,174,3,52,26,0,172,174,3,38,19,0,173,171,
  	1,0,0,0,173,172,1,0,0,0,174,11,1,0,0,0,175,177,3,32,16,0,176,178,3,10,
  	5,0,177,176,1,0,0,0,177,178,1,0,0,0,178,179,1,0,0,0,179,180,3,34,17,0,
  	180,181,5,41,0,0,181,182,3,108,54,0,182,185,3,40,20,0,183,184,5,5,0,0,
  	184,186,3,96,48,0,185,183,1,0,0,0,185,186,1,0,0,0,186,188,1,0,0,0,187,
  	189,5,2,0,0,188,187,1,0,0,0,188,189,1,0,0,0,189,190,1,0,0,0,190,191,3,
  	64,32,0,191,13,1,0,0,0,192,193,5,42,0,0,193,195,3,108,54,0,194,196,5,
  	2,0,0,195,194,1,0,0,0,195,196,1,0,0,0,196,199,1,0,0,0,197,200,3,96,48,
  	0,198,200,3,46,23,0,199,197,1,0,0,0,199,198,1,0,0,0,200,15,1,0,0,0,201,
  	202,5,43,0,0,202,204,3,108,54,0,203,205,5,2,0,0,204,203,1,0,0,0,204,205,
  	1,0,0,0,205,206,1,0,0,0,206,207,3,58,29,0,207,17,1,0,0,0,208,210,5,45,
  	0,0,209,211,3,90,45,0,210,209,1,0,0,0,210,211,1,0,0,0,211,19,1,0,0,0,
  	212,213,3,32,16,0,213,214,3,90,45,0,214,21,1,0,0,0,215,218,3,52,26,0,
  	216,217,5,1,0,0,217,219,3,96,48,0,218,216,1,0,0,0,218,219,1,0,0,0,219,
  	221,1,0,0,0,220,222,5,2,0,0,221,220,1,0,0,0,221,222,1,0,0,0,222,223,1,
  	0,0,0,223,224,3,90,45,0,224,23,1,0,0,0,225,229,3,108,54,0,226,229,3,56,
  	28,0,227,229,3,58,29,0,228,225,1,0,0,0,228,226,1,0,0,0,228,227,1,0,0,
  	0,229,25,1,0,0,0,230,231,5,6,0,0,231,232,3,90,45,0,232,233,5,7,0,0,233,
  	234,3,90,45,0,234,235,5,8,0,0,235,27,1,0,0,0,236,237,5,9,0,0,237,238,
  	3,90,45,0,238,239,5,7,0,0,239,240,3,90,45,0,240,241,5,10,0,0,241,29,1,
  	0,0,0,242,247,5,11,0,0,243,248,3,108,54,0,244,248,3,60,30,0,245,248,3,
  	26,13,0,246,248,3,28,14,0,247,243,1,0,0,0,247,244,1,0,0,0,247,245,1,0,
  	0,0,247,246,1,0,0,0,248,31,1,0,0,0,249,251,3,30,15,0,250,252,5,37,0,0,
  	251,250,1,0,0,0,251,252,1,0,0,0,252,254,1,0,0,0,253,249,1,0,0,0,254,257,
  	1,0,0,0,255,253,1,0,0,0,255,256,1,0,0,0,256,33,1,0,0,0,257,255,1,0,0,
  	0,258,260,7,0,0,0,259,258,1,0,0,0,260,263,1,0,0,0,261,259,1,0,0,0,261,
  	262,1,0,0,0,262,35,1,0,0,0,263,261,1,0,0,0,264,265,5,12,0,0,265,270,3,
  	44,22,0,266,267,5,7,0,0,267,269,3,44,22,0,268,266,1,0,0,0,269,272,1,0,
  	0,0,270,268,1,0,0,0,270,271,1,0,0,0,271,274,1,0,0,0,272,270,1,0,0,0,273,
  	275,5,7,0,0,274,273,1,0,0,0,274,275,1,0,0,0,275,276,1,0,0,0,276,277,5,
  	13,0,0,277,37,1,0,0,0,278,279,5,12,0,0,279,284,3,48,24,0,280,281,5,7,
  	0,0,281,283,3,48,24,0,282,280,1,0,0,0,283,286,1,0,0,0,284,282,1,0,0,0,
  	284,285,1,0,0,0,285,288,1,0,0,0,286,284,1,0,0,0,287,289,5,7,0,0,288,287,
  	1,0,0,0,288,289,1,0,0,0,289,290,1,0,0,0,290,291,5,13,0,0,291,39,1,0,0,
  	0,292,301,5,6,0,0,293,298,3,48,24,0,294,295,5,7,0,0,295,297,3,48,24,0,
  	296,294,1,0,0,0,297,300,1,0,0,0,298,296,1,0,0,0,298,299,1,0,0,0,299,302,
  	1,0,0,0,300,298,1,0,0,0,301,293,1,0,0,0,301,302,1,0,0,0,302,304,1,0,0,
  	0,303,305,5,7,0,0,304,303,1,0,0,0,304,305,1,0,0,0,305,306,1,0,0,0,306,
  	307,5,8,0,0,307,41,1,0,0,0,308,317,5,6,0,0,309,314,3,44,22,0,310,311,
  	5,7,0,0,311,313,3,44,22,0,312,310,1,0,0,0,313,316,1,0,0,0,314,312,1,0,
  	0,0,314,315,1,0,0,0,315,318,1,0,0,0,316,314,1,0,0,0,317,309,1,0,0,0,317,
  	318,1,0,0,0,318,320,1,0,0,0,319,321,5,7,0,0,320,319,1,0,0,0,320,321,1,
  	0,0,0,321,322,1,0,0,0,322,323,5,8,0,0,323,43,1,0,0,0,324,329,3,108,54,
  	0,325,329,3,72,36,0,326,329,3,50,25,0,327,329,3,90,45,0,328,324,1,0,0,
  	0,328,325,1,0,0,0,328,326,1,0,0,0,328,327,1,0,0,0,329,45,1,0,0,0,330,
  	339,5,14,0,0,331,336,3,48,24,0,332,333,5,7,0,0,333,335,3,48,24,0,334,
  	332,1,0,0,0,335,338,1,0,0,0,336,334,1,0,0,0,336,337,1,0,0,0,337,340,1,
  	0,0,0,338,336,1,0,0,0,339,331,1,0,0,0,339,340,1,0,0,0,340,342,1,0,0,0,
  	341,343,5,7,0,0,342,341,1,0,0,0,342,343,1,0,0,0,343,344,1,0,0,0,344,345,
  	5,15,0,0,345,47,1,0,0,0,346,348,3,108,54,0,347,349,5,16,0,0,348,347,1,
  	0,0,0,348,349,1,0,0,0,349,351,1,0,0,0,350,352,3,30,15,0,351,350,1,0,0,
  	0,351,352,1,0,0,0,352,353,1,0,0,0,353,354,5,1,0,0,354,357,3,96,48,0,355,
  	356,5,2,0,0,356,358,3,90,45,0,357,355,1,0,0,0,357,358,1,0,0,0,358,49,
  	1,0,0,0,359,361,3,108,54,0,360,362,3,30,15,0,361,360,1,0,0,0,361,362,
  	1,0,0,0,362,363,1,0,0,0,363,364,5,2,0,0,364,365,3,90,45,0,365,51,1,0,
  	0,0,366,374,3,108,54,0,367,370,5,17,0,0,368,371,5,73,0,0,369,371,3,108,
  	54,0,370,368,1,0,0,0,370,369,1,0,0,0,371,373,1,0,0,0,372,367,1,0,0,0,
  	373,376,1,0,0,0,374,372,1,0,0,0,374,375,1,0,0,0,375,378,1,0,0,0,376,374,
  	1,0,0,0,377,379,3,30,15,0,378,377,1,0,0,0,378,379,1,0,0,0,379,53,1,0,
  	0,0,380,382,3,108,54,0,381,383,3,36,18,0,382,381,1,0,0,0,382,383,1,0,
  	0,0,383,385,1,0,0,0,384,386,3,30,15,0,385,384,1,0,0,0,385,386,1,0,0,0,
  	386,55,1,0,0,0,387,396,5,9,0,0,388,393,3,108,54,0,389,390,5,7,0,0,390,
  	392,3,108,54,0,391,389,1,0,0,0,392,395,1,0,0,0,393,391,1,0,0,0,393,394,
  	1,0,0,0,394,397,1,0,0,0,395,393,1,0,0,0,396,388,1,0,0,0,396,397,1,0,0,
  	0,397,399,1,0,0,0,398,400,5,7,0,0,399,398,1,0,0,0,399,400,1,0,0,0,400,
  	401,1,0,0,0,401,402,5,10,0,0,402,57,1,0,0,0,403,412,5,14,0,0,404,409,
  	3,108,54,0,405,406,5,7,0,0,406,408,3,108,54,0,407,405,1,0,0,0,408,411,
  	1,0,0,0,409,407,1,0,0,0,409,410,1,0,0,0,410,413,1,0,0,0,411,409,1,0,0,
  	0,412,404,1,0,0,0,412,413,1,0,0,0,413,415,1,0,0,0,414,416,5,7,0,0,415,
  	414,1,0,0,0,415,416,1,0,0,0,416,417,1,0,0,0,417,418,5,15,0,0,418,59,1,
  	0,0,0,419,428,5,14,0,0,420,425,3,50,25,0,421,422,5,7,0,0,422,424,3,50,
  	25,0,423,421,1,0,0,0,424,427,1,0,0,0,425,423,1,0,0,0,425,426,1,0,0,0,
  	426,429,1,0,0,0,427,425,1,0,0,0,428,420,1,0,0,0,428,429,1,0,0,0,429,431,
  	1,0,0,0,430,432,5,7,0,0,431,430,1,0,0,0,431,432,1,0,0,0,432,433,1,0,0,
  	0,433,434,5,15,0,0,434,61,1,0,0,0,435,444,5,9,0,0,436,441,3,90,45,0,437,
  	438,5,7,0,0,438,440,3,90,45,0,439,437,1,0,0,0,440,443,1,0,0,0,441,439,
  	1,0,0,0,441,442,1,0,0,0,442,445,1,0,0,0,443,441,1,0,0,0,444,436,1,0,0,
  	0,444,445,1,0,0,0,445,447,1,0,0,0,446,448,5,7,0,0,447,446,1,0,0,0,447,
  	448,1,0,0,0,448,449,1,0,0,0,449,450,5,10,0,0,450,63,1,0,0,0,451,453,5,
  	14,0,0,452,454,3,2,1,0,453,452,1,0,0,0,453,454,1,0,0,0,454,455,1,0,0,
  	0,455,456,5,15,0,0,456,65,1,0,0,0,457,458,3,40,20,0,458,459,5,18,0,0,
  	459,461,1,0,0,0,460,457,1,0,0,0,460,461,1,0,0,0,461,462,1,0,0,0,462,463,
  	3,64,32,0,463,67,1,0,0,0,464,469,3,72,36,0,465,469,3,74,37,0,466,469,
  	3,76,38,0,467,469,3,70,35,0,468,464,1,0,0,0,468,465,1,0,0,0,468,466,1,
  	0,0,0,468,467,1,0,0,0,469,472,1,0,0,0,470,471,5,38,0,0,471,473,3,96,48,
  	0,472,470,1,0,0,0,472,473,1,0,0,0,473,69,1,0,0,0,474,478,3,108,54,0,475,
  	478,3,72,36,0,476,478,3,76,38,0,477,474,1,0,0,0,477,475,1,0,0,0,477,476,
  	1,0,0,0,478,479,1,0,0,0,479,477,1,0,0,0,479,480,1,0,0,0,480,71,1,0,0,
  	0,481,487,3,108,54,0,482,487,3,92,46,0,483,487,3,62,31,0,484,487,3,60,
  	30,0,485,487,3,66,33,0,486,481,1,0,0,0,486,482,1,0,0,0,486,483,1,0,0,
  	0,486,484,1,0,0,0,486,485,1,0,0,0,487,489,1,0,0,0,488,490,3,30,15,0,489,
  	488,1,0,0,0,489,490,1,0,0,0,490,494,1,0,0,0,491,494,3,52,26,0,492,494,
  	3,54,27,0,493,486,1,0,0,0,493,491,1,0,0,0,493,492,1,0,0,0,494,73,1,0,
  	0,0,495,496,3,54,27,0,496,497,3,42,21,0,497,75,1,0,0,0,498,499,6,38,-1,
  	0,499,505,3,108,54,0,500,505,3,52,26,0,501,505,3,72,36,0,502,505,3,54,
  	27,0,503,505,3,74,37,0,504,498,1,0,0,0,504,500,1,0,0,0,504,501,1,0,0,
  	0,504,502,1,0,0,0,504,503,1,0,0,0,505,516,1,0,0,0,506,507,10,6,0,0,507,
  	512,5,5,0,0,508,513,3,108,54,0,509,513,3,54,27,0,510,513,3,72,36,0,511,
  	513,3,74,37,0,512,508,1,0,0,0,512,509,1,0,0,0,512,510,1,0,0,0,512,511,
  	1,0,0,0,513,515,1,0,0,0,514,506,1,0,0,0,515,518,1,0,0,0,516,514,1,0,0,
  	0,516,517,1,0,0,0,517,77,1,0,0,0,518,516,1,0,0,0,519,520,6,39,-1,0,520,
  	521,3,80,40,0,521,539,1,0,0,0,522,523,10,5,0,0,523,524,5,19,0,0,524,538,
  	3,80,40,0,525,526,10,4,0,0,526,527,5,20,0,0,527,538,3,80,40,0,528,529,
  	10,3,0,0,529,530,5,21,0,0,530,538,3,80,40,0,531,532,10,2,0,0,532,533,
  	5,22,0,0,533,538,3,80,40,0,534,535,10,1,0,0,535,536,5,23,0,0,536,538,
  	3,80,40,0,537,522,1,0,0,0,537,525,1,0,0,0,537,528,1,0,0,0,537,531,1,0,
  	0,0,537,534,1,0,0,0,538,541,1,0,0,0,539,537,1,0,0,0,539,540,1,0,0,0,540,
  	79,1,0,0,0,541,539,1,0,0,0,542,543,6,40,-1,0,543,544,3,82,41,0,544,565,
  	1,0,0,0,545,546,10,6,0,0,546,547,5,12,0,0,547,564,3,82,41,0,548,549,10,
  	5,0,0,549,550,5,13,0,0,550,564,3,82,41,0,551,552,10,4,0,0,552,553,5,24,
  	0,0,553,564,3,82,41,0,554,555,10,3,0,0,555,556,5,25,0,0,556,564,3,82,
  	41,0,557,558,10,2,0,0,558,559,5,26,0,0,559,564,3,82,41,0,560,561,10,1,
  	0,0,561,562,5,27,0,0,562,564,3,82,41,0,563,545,1,0,0,0,563,548,1,0,0,
  	0,563,551,1,0,0,0,563,554,1,0,0,0,563,557,1,0,0,0,563,560,1,0,0,0,564,
  	567,1,0,0,0,565,563,1,0,0,0,565,566,1,0,0,0,566,81,1,0,0,0,567,565,1,
  	0,0,0,568,569,6,41,-1,0,569,570,3,84,42,0,570,579,1,0,0,0,571,572,10,
  	2,0,0,572,573,5,28,0,0,573,578,3,84,42,0,574,575,10,1,0,0,575,576,5,29,
  	0,0,576,578,3,84,42,0,577,571,1,0,0,0,577,574,1,0,0,0,578,581,1,0,0,0,
  	579,577,1,0,0,0,579,580,1,0,0,0,580,83,1,0,0,0,581,579,1,0,0,0,582,583,
  	6,42,-1,0,583,584,3,86,43,0,584,599,1,0,0,0,585,586,10,4,0,0,586,587,
  	5,30,0,0,587,598,3,86,43,0,588,589,10,3,0,0,589,590,5,31,0,0,590,598,
  	3,86,43,0,591,592,10,2,0,0,592,593,5,32,0,0,593,598,3,86,43,0,594,595,
  	10,1,0,0,595,596,5,33,0,0,596,598,3,86,43,0,597,585,1,0,0,0,597,588,1,
  	0,0,0,597,591,1,0,0,0,597,594,1,0,0,0,598,601,1,0,0,0,599,597,1,0,0,0,
  	599,600,1,0,0,0,600,85,1,0,0,0,601,599,1,0,0,0,602,618,3,88,44,0,603,
  	604,5,34,0,0,604,618,3,88,44,0,605,606,5,35,0,0,606,618,3,88,44,0,607,
  	608,3,88,44,0,608,609,5,34,0,0,609,618,1,0,0,0,610,611,3,88,44,0,611,
  	612,5,35,0,0,612,618,1,0,0,0,613,614,5,36,0,0,614,618,3,88,44,0,615,616,
  	5,29,0,0,616,618,3,88,44,0,617,602,1,0,0,0,617,603,1,0,0,0,617,605,1,
  	0,0,0,617,607,1,0,0,0,617,610,1,0,0,0,617,613,1,0,0,0,617,615,1,0,0,0,
  	618,87,1,0,0,0,619,627,3,92,46,0,620,627,3,72,36,0,621,627,3,68,34,0,
  	622,623,5,6,0,0,623,624,3,78,39,0,624,625,5,8,0,0,625,627,1,0,0,0,626,
  	619,1,0,0,0,626,620,1,0,0,0,626,621,1,0,0,0,626,622,1,0,0,0,627,89,1,
  	0,0,0,628,631,3,68,34,0,629,631,3,78,39,0,630,628,1,0,0,0,630,629,1,0,
  	0,0,631,91,1,0,0,0,632,640,3,94,47,0,633,640,5,71,0,0,634,640,5,68,0,
  	0,635,640,5,72,0,0,636,640,5,52,0,0,637,640,5,53,0,0,638,640,5,54,0,0,
  	639,632,1,0,0,0,639,633,1,0,0,0,639,634,1,0,0,0,639,635,1,0,0,0,639,636,
  	1,0,0,0,639,637,1,0,0,0,639,638,1,0,0,0,640,93,1,0,0,0,641,643,7,1,0,
  	0,642,644,5,70,0,0,643,642,1,0,0,0,643,644,1,0,0,0,644,95,1,0,0,0,645,
  	649,3,98,49,0,646,649,3,108,54,0,647,649,5,55,0,0,648,645,1,0,0,0,648,
  	646,1,0,0,0,648,647,1,0,0,0,649,97,1,0,0,0,650,657,5,56,0,0,651,657,5,
  	57,0,0,652,657,5,58,0,0,653,657,5,59,0,0,654,657,3,100,50,0,655,657,3,
  	106,53,0,656,650,1,0,0,0,656,651,1,0,0,0,656,652,1,0,0,0,656,653,1,0,
  	0,0,656,654,1,0,0,0,656,655,1,0,0,0,657,99,1,0,0,0,658,661,3,102,51,0,
  	659,661,3,104,52,0,660,658,1,0,0,0,660,659,1,0,0,0,661,101,1,0,0,0,662,
  	663,7,2,0,0,663,103,1,0,0,0,664,669,5,63,0,0,665,666,5,12,0,0,666,667,
  	3,102,51,0,667,668,5,13,0,0,668,670,1,0,0,0,669,665,1,0,0,0,669,670,1,
  	0,0,0,670,674,1,0,0,0,671,672,5,9,0,0,672,673,5,73,0,0,673,675,5,10,0,
  	0,674,671,1,0,0,0,674,675,1,0,0,0,675,692,1,0,0,0,676,681,5,64,0,0,677,
  	678,5,12,0,0,678,679,3,102,51,0,679,680,5,13,0,0,680,682,1,0,0,0,681,
  	677,1,0,0,0,681,682,1,0,0,0,682,688,1,0,0,0,683,684,5,9,0,0,684,685,5,
  	73,0,0,685,687,5,10,0,0,686,683,1,0,0,0,687,690,1,0,0,0,688,686,1,0,0,
  	0,688,689,1,0,0,0,689,692,1,0,0,0,690,688,1,0,0,0,691,664,1,0,0,0,691,
  	676,1,0,0,0,692,105,1,0,0,0,693,705,5,65,0,0,694,695,5,12,0,0,695,700,
  	3,96,48,0,696,697,5,7,0,0,697,699,3,96,48,0,698,696,1,0,0,0,699,702,1,
  	0,0,0,700,698,1,0,0,0,700,701,1,0,0,0,701,703,1,0,0,0,702,700,1,0,0,0,
  	703,704,5,13,0,0,704,706,1,0,0,0,705,694,1,0,0,0,705,706,1,0,0,0,706,
  	710,1,0,0,0,707,708,5,9,0,0,708,709,5,73,0,0,709,711,5,10,0,0,710,707,
  	1,0,0,0,710,711,1,0,0,0,711,722,1,0,0,0,712,719,5,66,0,0,713,714,5,12,
  	0,0,714,715,3,96,48,0,715,716,5,7,0,0,716,717,3,96,48,0,717,718,5,13,
  	0,0,718,720,1,0,0,0,719,713,1,0,0,0,719,720,1,0,0,0,720,722,1,0,0,0,721,
  	693,1,0,0,0,721,712,1,0,0,0,722,107,1,0,0,0,723,724,5,69,0,0,724,109,
  	1,0,0,0,98,111,120,124,134,140,143,150,155,160,168,173,177,185,188,195,
  	199,204,210,218,221,228,247,251,255,261,270,274,284,288,298,301,304,314,
  	317,320,328,336,339,342,348,351,357,361,370,374,378,382,385,393,396,399,
  	409,412,415,425,428,431,441,444,447,453,460,468,472,477,479,486,489,493,
  	504,512,516,537,539,563,565,577,579,597,599,617,626,630,639,643,648,656,
  	660,669,674,681,688,691,700,705,710,719,721
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
  : ParserRuleContext(parent, invokingState) {
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
      ((1ULL << _la) & 36028368059124288) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 123) != 0)) {
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
  : ParserRuleContext(parent, invokingState) {
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
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::LetStmtContext* OpenCMLParser::StmtContext::letStmt() {
  return getRuleContext<OpenCMLParser::LetStmtContext>(0);
}

OpenCMLParser::UseStmtContext* OpenCMLParser::StmtContext::useStmt() {
  return getRuleContext<OpenCMLParser::UseStmtContext>(0);
}

OpenCMLParser::FuncDefContext* OpenCMLParser::StmtContext::funcDef() {
  return getRuleContext<OpenCMLParser::FuncDefContext>(0);
}

OpenCMLParser::TypeDefContext* OpenCMLParser::StmtContext::typeDef() {
  return getRuleContext<OpenCMLParser::TypeDefContext>(0);
}

OpenCMLParser::EnumDefContext* OpenCMLParser::StmtContext::enumDef() {
  return getRuleContext<OpenCMLParser::EnumDefContext>(0);
}

OpenCMLParser::RetStmtContext* OpenCMLParser::StmtContext::retStmt() {
  return getRuleContext<OpenCMLParser::RetStmtContext>(0);
}

OpenCMLParser::ExprStmtContext* OpenCMLParser::StmtContext::exprStmt() {
  return getRuleContext<OpenCMLParser::ExprStmtContext>(0);
}

OpenCMLParser::AssignStmtContext* OpenCMLParser::StmtContext::assignStmt() {
  return getRuleContext<OpenCMLParser::AssignStmtContext>(0);
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
    setState(134);
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
      funcDef();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(129);
      typeDef();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(130);
      enumDef();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(131);
      retStmt();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(132);
      exprStmt();
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(133);
      assignStmt();
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
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::LetStmtContext::LET() {
  return getToken(OpenCMLParser::LET, 0);
}

OpenCMLParser::CarrierContext* OpenCMLParser::LetStmtContext::carrier() {
  return getRuleContext<OpenCMLParser::CarrierContext>(0);
}

OpenCMLParser::ExprContext* OpenCMLParser::LetStmtContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
}

OpenCMLParser::TypeContext* OpenCMLParser::LetStmtContext::type() {
  return getRuleContext<OpenCMLParser::TypeContext>(0);
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
        setState(136);
        match(OpenCMLParser::LET);
        setState(137);
        carrier();
        setState(140);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(138);
          match(OpenCMLParser::T__0);
          setState(139);
          type();
        }
        setState(143);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__1) {
          setState(142);
          match(OpenCMLParser::T__1);
        }
        setState(145);
        expr();
        break;
      }

      case OpenCMLParser::T__8:
      case OpenCMLParser::T__13:
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
          type();
        }
        setState(152);
        match(OpenCMLParser::T__2);
        setState(153);
        expr();
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
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::UseStmtContext::USE() {
  return getToken(OpenCMLParser::USE, 0);
}

OpenCMLParser::CarrierContext* OpenCMLParser::UseStmtContext::carrier() {
  return getRuleContext<OpenCMLParser::CarrierContext>(0);
}

OpenCMLParser::ExprContext* OpenCMLParser::UseStmtContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
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
        expr();
        break;
      }

      case OpenCMLParser::T__8:
      case OpenCMLParser::T__13:
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(164);
        carrier();
        setState(165);
        match(OpenCMLParser::T__3);
        setState(166);
        expr();
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

//----------------- WithDefContext ------------------------------------------------------------------

OpenCMLParser::WithDefContext::WithDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::WithDefContext::WITH() {
  return getToken(OpenCMLParser::WITH, 0);
}

OpenCMLParser::EntityRefContext* OpenCMLParser::WithDefContext::entityRef() {
  return getRuleContext<OpenCMLParser::EntityRefContext>(0);
}

OpenCMLParser::WithDeclContext* OpenCMLParser::WithDefContext::withDecl() {
  return getRuleContext<OpenCMLParser::WithDeclContext>(0);
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
  enterRule(_localctx, 10, OpenCMLParser::RuleWithDef);

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
    match(OpenCMLParser::WITH);
    setState(173);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        setState(171);
        entityRef();
        break;
      }

      case OpenCMLParser::T__11: {
        setState(172);
        withDecl();
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

//----------------- FuncDefContext ------------------------------------------------------------------

OpenCMLParser::FuncDefContext::FuncDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::AnnotationsContext* OpenCMLParser::FuncDefContext::annotations() {
  return getRuleContext<OpenCMLParser::AnnotationsContext>(0);
}

OpenCMLParser::ModifiersContext* OpenCMLParser::FuncDefContext::modifiers() {
  return getRuleContext<OpenCMLParser::ModifiersContext>(0);
}

tree::TerminalNode* OpenCMLParser::FuncDefContext::FUNC() {
  return getToken(OpenCMLParser::FUNC, 0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::FuncDefContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::ParamDefContext* OpenCMLParser::FuncDefContext::paramDef() {
  return getRuleContext<OpenCMLParser::ParamDefContext>(0);
}

OpenCMLParser::StmtPackContext* OpenCMLParser::FuncDefContext::stmtPack() {
  return getRuleContext<OpenCMLParser::StmtPackContext>(0);
}

OpenCMLParser::WithDefContext* OpenCMLParser::FuncDefContext::withDef() {
  return getRuleContext<OpenCMLParser::WithDefContext>(0);
}

OpenCMLParser::TypeContext* OpenCMLParser::FuncDefContext::type() {
  return getRuleContext<OpenCMLParser::TypeContext>(0);
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
  enterRule(_localctx, 12, OpenCMLParser::RuleFuncDef);
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
    setState(175);
    annotations();
    setState(177);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WITH) {
      setState(176);
      withDef();
    }
    setState(179);
    modifiers();
    setState(180);
    match(OpenCMLParser::FUNC);
    setState(181);
    identRef();
    setState(182);
    paramDef();
    setState(185);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__4) {
      setState(183);
      match(OpenCMLParser::T__4);
      setState(184);
      type();
    }
    setState(188);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(187);
      match(OpenCMLParser::T__1);
    }
    setState(190);
    stmtPack();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TypeDefContext ------------------------------------------------------------------

OpenCMLParser::TypeDefContext::TypeDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::TypeDefContext::TYPE() {
  return getToken(OpenCMLParser::TYPE, 0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::TypeDefContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::TypeContext* OpenCMLParser::TypeDefContext::type() {
  return getRuleContext<OpenCMLParser::TypeContext>(0);
}

OpenCMLParser::TypePackContext* OpenCMLParser::TypeDefContext::typePack() {
  return getRuleContext<OpenCMLParser::TypePackContext>(0);
}


size_t OpenCMLParser::TypeDefContext::getRuleIndex() const {
  return OpenCMLParser::RuleTypeDef;
}


std::any OpenCMLParser::TypeDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitTypeDef(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::TypeDefContext* OpenCMLParser::typeDef() {
  TypeDefContext *_localctx = _tracker.createInstance<TypeDefContext>(_ctx, getState());
  enterRule(_localctx, 14, OpenCMLParser::RuleTypeDef);
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
    setState(192);
    match(OpenCMLParser::TYPE);
    setState(193);
    identRef();
    setState(195);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(194);
      match(OpenCMLParser::T__1);
    }
    setState(199);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::ANY_TYPE:
      case OpenCMLParser::NUMBER_TYPE:
      case OpenCMLParser::STRING_TYPE:
      case OpenCMLParser::BOOLEAN_TYPE:
      case OpenCMLParser::FUNCTOR_TYPE:
      case OpenCMLParser::INTEGER_TYPE:
      case OpenCMLParser::REAL_TYPE:
      case OpenCMLParser::COMPLEX_TYPE:
      case OpenCMLParser::ARRAY_TYPE:
      case OpenCMLParser::MATRIX_TYPE:
      case OpenCMLParser::LIST_TYPE:
      case OpenCMLParser::DICT_TYPE:
      case OpenCMLParser::IDENTIFIER: {
        setState(197);
        type();
        break;
      }

      case OpenCMLParser::T__13: {
        setState(198);
        typePack();
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

//----------------- EnumDefContext ------------------------------------------------------------------

OpenCMLParser::EnumDefContext::EnumDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::EnumDefContext::ENUM() {
  return getToken(OpenCMLParser::ENUM, 0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::EnumDefContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::DictUnpackContext* OpenCMLParser::EnumDefContext::dictUnpack() {
  return getRuleContext<OpenCMLParser::DictUnpackContext>(0);
}


size_t OpenCMLParser::EnumDefContext::getRuleIndex() const {
  return OpenCMLParser::RuleEnumDef;
}


std::any OpenCMLParser::EnumDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEnumDef(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::EnumDefContext* OpenCMLParser::enumDef() {
  EnumDefContext *_localctx = _tracker.createInstance<EnumDefContext>(_ctx, getState());
  enterRule(_localctx, 16, OpenCMLParser::RuleEnumDef);
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
    setState(201);
    match(OpenCMLParser::ENUM);
    setState(202);
    identRef();
    setState(204);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(203);
      match(OpenCMLParser::T__1);
    }
    setState(206);
    dictUnpack();
   
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
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::RetStmtContext::RETURN() {
  return getToken(OpenCMLParser::RETURN, 0);
}

OpenCMLParser::ExprContext* OpenCMLParser::RetStmtContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
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
  enterRule(_localctx, 18, OpenCMLParser::RuleRetStmt);
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
    match(OpenCMLParser::RETURN);
    setState(210);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 31525318187565632) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 123) != 0)) {
      setState(209);
      expr();
    }
   
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
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::AnnotationsContext* OpenCMLParser::ExprStmtContext::annotations() {
  return getRuleContext<OpenCMLParser::AnnotationsContext>(0);
}

OpenCMLParser::ExprContext* OpenCMLParser::ExprStmtContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
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
  enterRule(_localctx, 20, OpenCMLParser::RuleExprStmt);

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
    annotations();
    setState(213);
    expr();
   
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
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::EntityRefContext* OpenCMLParser::AssignStmtContext::entityRef() {
  return getRuleContext<OpenCMLParser::EntityRefContext>(0);
}

OpenCMLParser::ExprContext* OpenCMLParser::AssignStmtContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
}

OpenCMLParser::TypeContext* OpenCMLParser::AssignStmtContext::type() {
  return getRuleContext<OpenCMLParser::TypeContext>(0);
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
  enterRule(_localctx, 22, OpenCMLParser::RuleAssignStmt);
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
    setState(215);
    entityRef();
    setState(218);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(216);
      match(OpenCMLParser::T__0);
      setState(217);
      type();
    }
    setState(221);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(220);
      match(OpenCMLParser::T__1);
    }
    setState(223);
    expr();
   
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
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::CarrierContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::ListUnpackContext* OpenCMLParser::CarrierContext::listUnpack() {
  return getRuleContext<OpenCMLParser::ListUnpackContext>(0);
}

OpenCMLParser::DictUnpackContext* OpenCMLParser::CarrierContext::dictUnpack() {
  return getRuleContext<OpenCMLParser::DictUnpackContext>(0);
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
    setState(228);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(225);
        identRef();
        break;
      }

      case OpenCMLParser::T__8: {
        enterOuterAlt(_localctx, 2);
        setState(226);
        listUnpack();
        break;
      }

      case OpenCMLParser::T__13: {
        enterOuterAlt(_localctx, 3);
        setState(227);
        dictUnpack();
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

//----------------- BiasAnnoContext ------------------------------------------------------------------

OpenCMLParser::BiasAnnoContext::BiasAnnoContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::ExprContext *> OpenCMLParser::BiasAnnoContext::expr() {
  return getRuleContexts<OpenCMLParser::ExprContext>();
}

OpenCMLParser::ExprContext* OpenCMLParser::BiasAnnoContext::expr(size_t i) {
  return getRuleContext<OpenCMLParser::ExprContext>(i);
}


size_t OpenCMLParser::BiasAnnoContext::getRuleIndex() const {
  return OpenCMLParser::RuleBiasAnno;
}


std::any OpenCMLParser::BiasAnnoContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitBiasAnno(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::BiasAnnoContext* OpenCMLParser::biasAnno() {
  BiasAnnoContext *_localctx = _tracker.createInstance<BiasAnnoContext>(_ctx, getState());
  enterRule(_localctx, 26, OpenCMLParser::RuleBiasAnno);

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
    match(OpenCMLParser::T__5);
    setState(231);
    expr();
    setState(232);
    match(OpenCMLParser::T__6);
    setState(233);
    expr();
    setState(234);
    match(OpenCMLParser::T__7);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- SizeAnnoContext ------------------------------------------------------------------

OpenCMLParser::SizeAnnoContext::SizeAnnoContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::ExprContext *> OpenCMLParser::SizeAnnoContext::expr() {
  return getRuleContexts<OpenCMLParser::ExprContext>();
}

OpenCMLParser::ExprContext* OpenCMLParser::SizeAnnoContext::expr(size_t i) {
  return getRuleContext<OpenCMLParser::ExprContext>(i);
}


size_t OpenCMLParser::SizeAnnoContext::getRuleIndex() const {
  return OpenCMLParser::RuleSizeAnno;
}


std::any OpenCMLParser::SizeAnnoContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitSizeAnno(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::SizeAnnoContext* OpenCMLParser::sizeAnno() {
  SizeAnnoContext *_localctx = _tracker.createInstance<SizeAnnoContext>(_ctx, getState());
  enterRule(_localctx, 28, OpenCMLParser::RuleSizeAnno);

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
    match(OpenCMLParser::T__8);
    setState(237);
    expr();
    setState(238);
    match(OpenCMLParser::T__6);
    setState(239);
    expr();
    setState(240);
    match(OpenCMLParser::T__9);
   
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
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::AnnotationContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::DictPackContext* OpenCMLParser::AnnotationContext::dictPack() {
  return getRuleContext<OpenCMLParser::DictPackContext>(0);
}

OpenCMLParser::BiasAnnoContext* OpenCMLParser::AnnotationContext::biasAnno() {
  return getRuleContext<OpenCMLParser::BiasAnnoContext>(0);
}

OpenCMLParser::SizeAnnoContext* OpenCMLParser::AnnotationContext::sizeAnno() {
  return getRuleContext<OpenCMLParser::SizeAnnoContext>(0);
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
  enterRule(_localctx, 30, OpenCMLParser::RuleAnnotation);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(242);
    match(OpenCMLParser::T__10);
    setState(247);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        setState(243);
        identRef();
        break;
      }

      case OpenCMLParser::T__13: {
        setState(244);
        dictPack();
        break;
      }

      case OpenCMLParser::T__5: {
        setState(245);
        biasAnno();
        break;
      }

      case OpenCMLParser::T__8: {
        setState(246);
        sizeAnno();
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

//----------------- AnnotationsContext ------------------------------------------------------------------

OpenCMLParser::AnnotationsContext::AnnotationsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::AnnotationContext *> OpenCMLParser::AnnotationsContext::annotation() {
  return getRuleContexts<OpenCMLParser::AnnotationContext>();
}

OpenCMLParser::AnnotationContext* OpenCMLParser::AnnotationsContext::annotation(size_t i) {
  return getRuleContext<OpenCMLParser::AnnotationContext>(i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::AnnotationsContext::SEP() {
  return getTokens(OpenCMLParser::SEP);
}

tree::TerminalNode* OpenCMLParser::AnnotationsContext::SEP(size_t i) {
  return getToken(OpenCMLParser::SEP, i);
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
  enterRule(_localctx, 32, OpenCMLParser::RuleAnnotations);
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
    while (_la == OpenCMLParser::T__10) {
      setState(249);
      annotation();
      setState(251);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::SEP) {
        setState(250);
        match(OpenCMLParser::SEP);
      }
      setState(257);
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

//----------------- ModifiersContext ------------------------------------------------------------------

OpenCMLParser::ModifiersContext::ModifiersContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
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

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::SYNC() {
  return getTokens(OpenCMLParser::SYNC);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::SYNC(size_t i) {
  return getToken(OpenCMLParser::SYNC, i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::SCOPED() {
  return getTokens(OpenCMLParser::SCOPED);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::SCOPED(size_t i) {
  return getToken(OpenCMLParser::SCOPED, i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::STATIC() {
  return getTokens(OpenCMLParser::STATIC);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::STATIC(size_t i) {
  return getToken(OpenCMLParser::STATIC, i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::ATOMIC() {
  return getTokens(OpenCMLParser::ATOMIC);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::ATOMIC(size_t i) {
  return getToken(OpenCMLParser::ATOMIC, i);
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
  enterRule(_localctx, 34, OpenCMLParser::RuleModifiers);
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
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 4433230883192832) != 0)) {
      setState(258);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 4433230883192832) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(263);
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

//----------------- WithListContext ------------------------------------------------------------------

OpenCMLParser::WithListContext::WithListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::ArgumentContext *> OpenCMLParser::WithListContext::argument() {
  return getRuleContexts<OpenCMLParser::ArgumentContext>();
}

OpenCMLParser::ArgumentContext* OpenCMLParser::WithListContext::argument(size_t i) {
  return getRuleContext<OpenCMLParser::ArgumentContext>(i);
}


size_t OpenCMLParser::WithListContext::getRuleIndex() const {
  return OpenCMLParser::RuleWithList;
}


std::any OpenCMLParser::WithListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitWithList(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::WithListContext* OpenCMLParser::withList() {
  WithListContext *_localctx = _tracker.createInstance<WithListContext>(_ctx, getState());
  enterRule(_localctx, 36, OpenCMLParser::RuleWithList);
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
    setState(264);
    match(OpenCMLParser::T__11);
    setState(265);
    argument();
    setState(270);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 25, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(266);
        match(OpenCMLParser::T__6);
        setState(267);
        argument(); 
      }
      setState(272);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 25, _ctx);
    }
    setState(274);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(273);
      match(OpenCMLParser::T__6);
    }
    setState(276);
    match(OpenCMLParser::T__12);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- WithDeclContext ------------------------------------------------------------------

OpenCMLParser::WithDeclContext::WithDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::KeyValDeclContext *> OpenCMLParser::WithDeclContext::keyValDecl() {
  return getRuleContexts<OpenCMLParser::KeyValDeclContext>();
}

OpenCMLParser::KeyValDeclContext* OpenCMLParser::WithDeclContext::keyValDecl(size_t i) {
  return getRuleContext<OpenCMLParser::KeyValDeclContext>(i);
}


size_t OpenCMLParser::WithDeclContext::getRuleIndex() const {
  return OpenCMLParser::RuleWithDecl;
}


std::any OpenCMLParser::WithDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitWithDecl(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::WithDeclContext* OpenCMLParser::withDecl() {
  WithDeclContext *_localctx = _tracker.createInstance<WithDeclContext>(_ctx, getState());
  enterRule(_localctx, 38, OpenCMLParser::RuleWithDecl);
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
    setState(278);
    match(OpenCMLParser::T__11);
    setState(279);
    keyValDecl();
    setState(284);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(280);
        match(OpenCMLParser::T__6);
        setState(281);
        keyValDecl(); 
      }
      setState(286);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx);
    }
    setState(288);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(287);
      match(OpenCMLParser::T__6);
    }
    setState(290);
    match(OpenCMLParser::T__12);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParamDefContext ------------------------------------------------------------------

OpenCMLParser::ParamDefContext::ParamDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::KeyValDeclContext *> OpenCMLParser::ParamDefContext::keyValDecl() {
  return getRuleContexts<OpenCMLParser::KeyValDeclContext>();
}

OpenCMLParser::KeyValDeclContext* OpenCMLParser::ParamDefContext::keyValDecl(size_t i) {
  return getRuleContext<OpenCMLParser::KeyValDeclContext>(i);
}


size_t OpenCMLParser::ParamDefContext::getRuleIndex() const {
  return OpenCMLParser::RuleParamDef;
}


std::any OpenCMLParser::ParamDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitParamDef(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ParamDefContext* OpenCMLParser::paramDef() {
  ParamDefContext *_localctx = _tracker.createInstance<ParamDefContext>(_ctx, getState());
  enterRule(_localctx, 40, OpenCMLParser::RuleParamDef);
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
    setState(292);
    match(OpenCMLParser::T__5);
    setState(301);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(293);
      keyValDecl();
      setState(298);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(294);
          match(OpenCMLParser::T__6);
          setState(295);
          keyValDecl(); 
        }
        setState(300);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx);
      }
    }
    setState(304);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(303);
      match(OpenCMLParser::T__6);
    }
    setState(306);
    match(OpenCMLParser::T__7);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ArgsListContext ------------------------------------------------------------------

OpenCMLParser::ArgsListContext::ArgsListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::ArgumentContext *> OpenCMLParser::ArgsListContext::argument() {
  return getRuleContexts<OpenCMLParser::ArgumentContext>();
}

OpenCMLParser::ArgumentContext* OpenCMLParser::ArgsListContext::argument(size_t i) {
  return getRuleContext<OpenCMLParser::ArgumentContext>(i);
}


size_t OpenCMLParser::ArgsListContext::getRuleIndex() const {
  return OpenCMLParser::RuleArgsList;
}


std::any OpenCMLParser::ArgsListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitArgsList(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ArgsListContext* OpenCMLParser::argsList() {
  ArgsListContext *_localctx = _tracker.createInstance<ArgsListContext>(_ctx, getState());
  enterRule(_localctx, 42, OpenCMLParser::RuleArgsList);
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
    setState(308);
    match(OpenCMLParser::T__5);
    setState(317);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 31525318187565632) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 123) != 0)) {
      setState(309);
      argument();
      setState(314);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(310);
          match(OpenCMLParser::T__6);
          setState(311);
          argument(); 
        }
        setState(316);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx);
      }
    }
    setState(320);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(319);
      match(OpenCMLParser::T__6);
    }
    setState(322);
    match(OpenCMLParser::T__7);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ArgumentContext ------------------------------------------------------------------

OpenCMLParser::ArgumentContext::ArgumentContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::ArgumentContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::EntityContext* OpenCMLParser::ArgumentContext::entity() {
  return getRuleContext<OpenCMLParser::EntityContext>(0);
}

OpenCMLParser::KeyValExprContext* OpenCMLParser::ArgumentContext::keyValExpr() {
  return getRuleContext<OpenCMLParser::KeyValExprContext>(0);
}

OpenCMLParser::ExprContext* OpenCMLParser::ArgumentContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
}


size_t OpenCMLParser::ArgumentContext::getRuleIndex() const {
  return OpenCMLParser::RuleArgument;
}


std::any OpenCMLParser::ArgumentContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitArgument(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ArgumentContext* OpenCMLParser::argument() {
  ArgumentContext *_localctx = _tracker.createInstance<ArgumentContext>(_ctx, getState());
  enterRule(_localctx, 44, OpenCMLParser::RuleArgument);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(328);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(324);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(325);
      entity();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(326);
      keyValExpr();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(327);
      expr();
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

//----------------- TypePackContext ------------------------------------------------------------------

OpenCMLParser::TypePackContext::TypePackContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::KeyValDeclContext *> OpenCMLParser::TypePackContext::keyValDecl() {
  return getRuleContexts<OpenCMLParser::KeyValDeclContext>();
}

OpenCMLParser::KeyValDeclContext* OpenCMLParser::TypePackContext::keyValDecl(size_t i) {
  return getRuleContext<OpenCMLParser::KeyValDeclContext>(i);
}


size_t OpenCMLParser::TypePackContext::getRuleIndex() const {
  return OpenCMLParser::RuleTypePack;
}


std::any OpenCMLParser::TypePackContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitTypePack(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::TypePackContext* OpenCMLParser::typePack() {
  TypePackContext *_localctx = _tracker.createInstance<TypePackContext>(_ctx, getState());
  enterRule(_localctx, 46, OpenCMLParser::RuleTypePack);
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
    setState(330);
    match(OpenCMLParser::T__13);
    setState(339);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(331);
      keyValDecl();
      setState(336);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(332);
          match(OpenCMLParser::T__6);
          setState(333);
          keyValDecl(); 
        }
        setState(338);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx);
      }
    }
    setState(342);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(341);
      match(OpenCMLParser::T__6);
    }
    setState(344);
    match(OpenCMLParser::T__14);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- KeyValDeclContext ------------------------------------------------------------------

OpenCMLParser::KeyValDeclContext::KeyValDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::KeyValDeclContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::TypeContext* OpenCMLParser::KeyValDeclContext::type() {
  return getRuleContext<OpenCMLParser::TypeContext>(0);
}

OpenCMLParser::AnnotationContext* OpenCMLParser::KeyValDeclContext::annotation() {
  return getRuleContext<OpenCMLParser::AnnotationContext>(0);
}

OpenCMLParser::ExprContext* OpenCMLParser::KeyValDeclContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
}


size_t OpenCMLParser::KeyValDeclContext::getRuleIndex() const {
  return OpenCMLParser::RuleKeyValDecl;
}


std::any OpenCMLParser::KeyValDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitKeyValDecl(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::KeyValDeclContext* OpenCMLParser::keyValDecl() {
  KeyValDeclContext *_localctx = _tracker.createInstance<KeyValDeclContext>(_ctx, getState());
  enterRule(_localctx, 48, OpenCMLParser::RuleKeyValDecl);
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
    identRef();
    setState(348);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__15) {
      setState(347);
      match(OpenCMLParser::T__15);
    }
    setState(351);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__10) {
      setState(350);
      annotation();
    }
    setState(353);
    match(OpenCMLParser::T__0);
    setState(354);
    type();
    setState(357);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(355);
      match(OpenCMLParser::T__1);
      setState(356);
      expr();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- KeyValExprContext ------------------------------------------------------------------

OpenCMLParser::KeyValExprContext::KeyValExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::KeyValExprContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::ExprContext* OpenCMLParser::KeyValExprContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
}

OpenCMLParser::AnnotationContext* OpenCMLParser::KeyValExprContext::annotation() {
  return getRuleContext<OpenCMLParser::AnnotationContext>(0);
}


size_t OpenCMLParser::KeyValExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleKeyValExpr;
}


std::any OpenCMLParser::KeyValExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitKeyValExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::KeyValExprContext* OpenCMLParser::keyValExpr() {
  KeyValExprContext *_localctx = _tracker.createInstance<KeyValExprContext>(_ctx, getState());
  enterRule(_localctx, 50, OpenCMLParser::RuleKeyValExpr);
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
    setState(359);
    identRef();
    setState(361);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__10) {
      setState(360);
      annotation();
    }
    setState(363);
    match(OpenCMLParser::T__1);
    setState(364);
    expr();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- EntityRefContext ------------------------------------------------------------------

OpenCMLParser::EntityRefContext::EntityRefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::IdentRefContext *> OpenCMLParser::EntityRefContext::identRef() {
  return getRuleContexts<OpenCMLParser::IdentRefContext>();
}

OpenCMLParser::IdentRefContext* OpenCMLParser::EntityRefContext::identRef(size_t i) {
  return getRuleContext<OpenCMLParser::IdentRefContext>(i);
}

OpenCMLParser::AnnotationContext* OpenCMLParser::EntityRefContext::annotation() {
  return getRuleContext<OpenCMLParser::AnnotationContext>(0);
}

std::vector<tree::TerminalNode *> OpenCMLParser::EntityRefContext::INTEGER() {
  return getTokens(OpenCMLParser::INTEGER);
}

tree::TerminalNode* OpenCMLParser::EntityRefContext::INTEGER(size_t i) {
  return getToken(OpenCMLParser::INTEGER, i);
}


size_t OpenCMLParser::EntityRefContext::getRuleIndex() const {
  return OpenCMLParser::RuleEntityRef;
}


std::any OpenCMLParser::EntityRefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEntityRef(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::EntityRefContext* OpenCMLParser::entityRef() {
  EntityRefContext *_localctx = _tracker.createInstance<EntityRefContext>(_ctx, getState());
  enterRule(_localctx, 52, OpenCMLParser::RuleEntityRef);

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
    setState(366);
    identRef();
    setState(374);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 44, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(367);
        match(OpenCMLParser::T__16);
        setState(370);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case OpenCMLParser::INTEGER: {
            setState(368);
            match(OpenCMLParser::INTEGER);
            break;
          }

          case OpenCMLParser::IDENTIFIER: {
            setState(369);
            identRef();
            break;
          }

        default:
          throw NoViableAltException(this);
        } 
      }
      setState(376);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 44, _ctx);
    }
    setState(378);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 45, _ctx)) {
    case 1: {
      setState(377);
      annotation();
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

//----------------- FunctorRefContext ------------------------------------------------------------------

OpenCMLParser::FunctorRefContext::FunctorRefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::FunctorRefContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::WithListContext* OpenCMLParser::FunctorRefContext::withList() {
  return getRuleContext<OpenCMLParser::WithListContext>(0);
}

OpenCMLParser::AnnotationContext* OpenCMLParser::FunctorRefContext::annotation() {
  return getRuleContext<OpenCMLParser::AnnotationContext>(0);
}


size_t OpenCMLParser::FunctorRefContext::getRuleIndex() const {
  return OpenCMLParser::RuleFunctorRef;
}


std::any OpenCMLParser::FunctorRefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitFunctorRef(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::FunctorRefContext* OpenCMLParser::functorRef() {
  FunctorRefContext *_localctx = _tracker.createInstance<FunctorRefContext>(_ctx, getState());
  enterRule(_localctx, 54, OpenCMLParser::RuleFunctorRef);

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
    identRef();
    setState(382);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 46, _ctx)) {
    case 1: {
      setState(381);
      withList();
      break;
    }

    default:
      break;
    }
    setState(385);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 47, _ctx)) {
    case 1: {
      setState(384);
      annotation();
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

//----------------- ListUnpackContext ------------------------------------------------------------------

OpenCMLParser::ListUnpackContext::ListUnpackContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::IdentRefContext *> OpenCMLParser::ListUnpackContext::identRef() {
  return getRuleContexts<OpenCMLParser::IdentRefContext>();
}

OpenCMLParser::IdentRefContext* OpenCMLParser::ListUnpackContext::identRef(size_t i) {
  return getRuleContext<OpenCMLParser::IdentRefContext>(i);
}


size_t OpenCMLParser::ListUnpackContext::getRuleIndex() const {
  return OpenCMLParser::RuleListUnpack;
}


std::any OpenCMLParser::ListUnpackContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitListUnpack(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ListUnpackContext* OpenCMLParser::listUnpack() {
  ListUnpackContext *_localctx = _tracker.createInstance<ListUnpackContext>(_ctx, getState());
  enterRule(_localctx, 56, OpenCMLParser::RuleListUnpack);
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
    setState(387);
    match(OpenCMLParser::T__8);
    setState(396);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(388);
      identRef();
      setState(393);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 48, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(389);
          match(OpenCMLParser::T__6);
          setState(390);
          identRef(); 
        }
        setState(395);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 48, _ctx);
      }
    }
    setState(399);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(398);
      match(OpenCMLParser::T__6);
    }
    setState(401);
    match(OpenCMLParser::T__9);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- DictUnpackContext ------------------------------------------------------------------

OpenCMLParser::DictUnpackContext::DictUnpackContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::IdentRefContext *> OpenCMLParser::DictUnpackContext::identRef() {
  return getRuleContexts<OpenCMLParser::IdentRefContext>();
}

OpenCMLParser::IdentRefContext* OpenCMLParser::DictUnpackContext::identRef(size_t i) {
  return getRuleContext<OpenCMLParser::IdentRefContext>(i);
}


size_t OpenCMLParser::DictUnpackContext::getRuleIndex() const {
  return OpenCMLParser::RuleDictUnpack;
}


std::any OpenCMLParser::DictUnpackContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitDictUnpack(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::DictUnpackContext* OpenCMLParser::dictUnpack() {
  DictUnpackContext *_localctx = _tracker.createInstance<DictUnpackContext>(_ctx, getState());
  enterRule(_localctx, 58, OpenCMLParser::RuleDictUnpack);
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
    setState(403);
    match(OpenCMLParser::T__13);
    setState(412);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(404);
      identRef();
      setState(409);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 51, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(405);
          match(OpenCMLParser::T__6);
          setState(406);
          identRef(); 
        }
        setState(411);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 51, _ctx);
      }
    }
    setState(415);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(414);
      match(OpenCMLParser::T__6);
    }
    setState(417);
    match(OpenCMLParser::T__14);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- DictPackContext ------------------------------------------------------------------

OpenCMLParser::DictPackContext::DictPackContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::KeyValExprContext *> OpenCMLParser::DictPackContext::keyValExpr() {
  return getRuleContexts<OpenCMLParser::KeyValExprContext>();
}

OpenCMLParser::KeyValExprContext* OpenCMLParser::DictPackContext::keyValExpr(size_t i) {
  return getRuleContext<OpenCMLParser::KeyValExprContext>(i);
}


size_t OpenCMLParser::DictPackContext::getRuleIndex() const {
  return OpenCMLParser::RuleDictPack;
}


std::any OpenCMLParser::DictPackContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitDictPack(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::DictPackContext* OpenCMLParser::dictPack() {
  DictPackContext *_localctx = _tracker.createInstance<DictPackContext>(_ctx, getState());
  enterRule(_localctx, 60, OpenCMLParser::RuleDictPack);
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
    setState(419);
    match(OpenCMLParser::T__13);
    setState(428);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(420);
      keyValExpr();
      setState(425);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 54, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(421);
          match(OpenCMLParser::T__6);
          setState(422);
          keyValExpr(); 
        }
        setState(427);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 54, _ctx);
      }
    }
    setState(431);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(430);
      match(OpenCMLParser::T__6);
    }
    setState(433);
    match(OpenCMLParser::T__14);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ListPackContext ------------------------------------------------------------------

OpenCMLParser::ListPackContext::ListPackContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::ExprContext *> OpenCMLParser::ListPackContext::expr() {
  return getRuleContexts<OpenCMLParser::ExprContext>();
}

OpenCMLParser::ExprContext* OpenCMLParser::ListPackContext::expr(size_t i) {
  return getRuleContext<OpenCMLParser::ExprContext>(i);
}


size_t OpenCMLParser::ListPackContext::getRuleIndex() const {
  return OpenCMLParser::RuleListPack;
}


std::any OpenCMLParser::ListPackContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitListPack(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ListPackContext* OpenCMLParser::listPack() {
  ListPackContext *_localctx = _tracker.createInstance<ListPackContext>(_ctx, getState());
  enterRule(_localctx, 62, OpenCMLParser::RuleListPack);
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
    setState(435);
    match(OpenCMLParser::T__8);
    setState(444);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 31525318187565632) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 123) != 0)) {
      setState(436);
      expr();
      setState(441);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 57, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(437);
          match(OpenCMLParser::T__6);
          setState(438);
          expr(); 
        }
        setState(443);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 57, _ctx);
      }
    }
    setState(447);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(446);
      match(OpenCMLParser::T__6);
    }
    setState(449);
    match(OpenCMLParser::T__9);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StmtPackContext ------------------------------------------------------------------

OpenCMLParser::StmtPackContext::StmtPackContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::StmtListContext* OpenCMLParser::StmtPackContext::stmtList() {
  return getRuleContext<OpenCMLParser::StmtListContext>(0);
}


size_t OpenCMLParser::StmtPackContext::getRuleIndex() const {
  return OpenCMLParser::RuleStmtPack;
}


std::any OpenCMLParser::StmtPackContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitStmtPack(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::StmtPackContext* OpenCMLParser::stmtPack() {
  StmtPackContext *_localctx = _tracker.createInstance<StmtPackContext>(_ctx, getState());
  enterRule(_localctx, 64, OpenCMLParser::RuleStmtPack);
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
    match(OpenCMLParser::T__13);
    setState(453);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 36028368059124288) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 123) != 0)) {
      setState(452);
      stmtList();
    }
    setState(455);
    match(OpenCMLParser::T__14);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LambdaContext ------------------------------------------------------------------

OpenCMLParser::LambdaContext::LambdaContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::StmtPackContext* OpenCMLParser::LambdaContext::stmtPack() {
  return getRuleContext<OpenCMLParser::StmtPackContext>(0);
}

OpenCMLParser::ParamDefContext* OpenCMLParser::LambdaContext::paramDef() {
  return getRuleContext<OpenCMLParser::ParamDefContext>(0);
}


size_t OpenCMLParser::LambdaContext::getRuleIndex() const {
  return OpenCMLParser::RuleLambda;
}


std::any OpenCMLParser::LambdaContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitLambda(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::LambdaContext* OpenCMLParser::lambda() {
  LambdaContext *_localctx = _tracker.createInstance<LambdaContext>(_ctx, getState());
  enterRule(_localctx, 66, OpenCMLParser::RuleLambda);
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
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(457);
      paramDef();
      setState(458);
      match(OpenCMLParser::T__17);
    }
    setState(462);
    stmtPack();
   
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
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::EntityContext* OpenCMLParser::EntityExprContext::entity() {
  return getRuleContext<OpenCMLParser::EntityContext>(0);
}

OpenCMLParser::NormCallContext* OpenCMLParser::EntityExprContext::normCall() {
  return getRuleContext<OpenCMLParser::NormCallContext>(0);
}

OpenCMLParser::LinkCallContext* OpenCMLParser::EntityExprContext::linkCall() {
  return getRuleContext<OpenCMLParser::LinkCallContext>(0);
}

OpenCMLParser::EntityChainContext* OpenCMLParser::EntityExprContext::entityChain() {
  return getRuleContext<OpenCMLParser::EntityChainContext>(0);
}

tree::TerminalNode* OpenCMLParser::EntityExprContext::AS() {
  return getToken(OpenCMLParser::AS, 0);
}

OpenCMLParser::TypeContext* OpenCMLParser::EntityExprContext::type() {
  return getRuleContext<OpenCMLParser::TypeContext>(0);
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
  enterRule(_localctx, 68, OpenCMLParser::RuleEntityExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(468);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 62, _ctx)) {
    case 1: {
      setState(464);
      entity();
      break;
    }

    case 2: {
      setState(465);
      normCall();
      break;
    }

    case 3: {
      setState(466);
      linkCall(0);
      break;
    }

    case 4: {
      setState(467);
      entityChain();
      break;
    }

    default:
      break;
    }
    setState(472);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 63, _ctx)) {
    case 1: {
      setState(470);
      match(OpenCMLParser::AS);
      setState(471);
      type();
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
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::IdentRefContext *> OpenCMLParser::EntityChainContext::identRef() {
  return getRuleContexts<OpenCMLParser::IdentRefContext>();
}

OpenCMLParser::IdentRefContext* OpenCMLParser::EntityChainContext::identRef(size_t i) {
  return getRuleContext<OpenCMLParser::IdentRefContext>(i);
}

std::vector<OpenCMLParser::EntityContext *> OpenCMLParser::EntityChainContext::entity() {
  return getRuleContexts<OpenCMLParser::EntityContext>();
}

OpenCMLParser::EntityContext* OpenCMLParser::EntityChainContext::entity(size_t i) {
  return getRuleContext<OpenCMLParser::EntityContext>(i);
}

std::vector<OpenCMLParser::LinkCallContext *> OpenCMLParser::EntityChainContext::linkCall() {
  return getRuleContexts<OpenCMLParser::LinkCallContext>();
}

OpenCMLParser::LinkCallContext* OpenCMLParser::EntityChainContext::linkCall(size_t i) {
  return getRuleContext<OpenCMLParser::LinkCallContext>(i);
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
  enterRule(_localctx, 70, OpenCMLParser::RuleEntityChain);

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
    setState(477); 
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(477);
              _errHandler->sync(this);
              switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 64, _ctx)) {
              case 1: {
                setState(474);
                identRef();
                break;
              }

              case 2: {
                setState(475);
                entity();
                break;
              }

              case 3: {
                setState(476);
                linkCall(0);
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
      setState(479); 
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 65, _ctx);
    } while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER);
   
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
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::EntityContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::LiteralContext* OpenCMLParser::EntityContext::literal() {
  return getRuleContext<OpenCMLParser::LiteralContext>(0);
}

OpenCMLParser::ListPackContext* OpenCMLParser::EntityContext::listPack() {
  return getRuleContext<OpenCMLParser::ListPackContext>(0);
}

OpenCMLParser::DictPackContext* OpenCMLParser::EntityContext::dictPack() {
  return getRuleContext<OpenCMLParser::DictPackContext>(0);
}

OpenCMLParser::LambdaContext* OpenCMLParser::EntityContext::lambda() {
  return getRuleContext<OpenCMLParser::LambdaContext>(0);
}

OpenCMLParser::AnnotationContext* OpenCMLParser::EntityContext::annotation() {
  return getRuleContext<OpenCMLParser::AnnotationContext>(0);
}

OpenCMLParser::EntityRefContext* OpenCMLParser::EntityContext::entityRef() {
  return getRuleContext<OpenCMLParser::EntityRefContext>(0);
}

OpenCMLParser::FunctorRefContext* OpenCMLParser::EntityContext::functorRef() {
  return getRuleContext<OpenCMLParser::FunctorRefContext>(0);
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
  enterRule(_localctx, 72, OpenCMLParser::RuleEntity);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(493);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 68, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(486);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 66, _ctx)) {
      case 1: {
        setState(481);
        identRef();
        break;
      }

      case 2: {
        setState(482);
        literal();
        break;
      }

      case 3: {
        setState(483);
        listPack();
        break;
      }

      case 4: {
        setState(484);
        dictPack();
        break;
      }

      case 5: {
        setState(485);
        lambda();
        break;
      }

      default:
        break;
      }
      setState(489);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 67, _ctx)) {
      case 1: {
        setState(488);
        annotation();
        break;
      }

      default:
        break;
      }
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(491);
      entityRef();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(492);
      functorRef();
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

//----------------- NormCallContext ------------------------------------------------------------------

OpenCMLParser::NormCallContext::NormCallContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::FunctorRefContext* OpenCMLParser::NormCallContext::functorRef() {
  return getRuleContext<OpenCMLParser::FunctorRefContext>(0);
}

OpenCMLParser::ArgsListContext* OpenCMLParser::NormCallContext::argsList() {
  return getRuleContext<OpenCMLParser::ArgsListContext>(0);
}


size_t OpenCMLParser::NormCallContext::getRuleIndex() const {
  return OpenCMLParser::RuleNormCall;
}


std::any OpenCMLParser::NormCallContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitNormCall(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::NormCallContext* OpenCMLParser::normCall() {
  NormCallContext *_localctx = _tracker.createInstance<NormCallContext>(_ctx, getState());
  enterRule(_localctx, 74, OpenCMLParser::RuleNormCall);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(495);
    functorRef();
    setState(496);
    argsList();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LinkCallContext ------------------------------------------------------------------

OpenCMLParser::LinkCallContext::LinkCallContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::LinkCallContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::EntityRefContext* OpenCMLParser::LinkCallContext::entityRef() {
  return getRuleContext<OpenCMLParser::EntityRefContext>(0);
}

OpenCMLParser::EntityContext* OpenCMLParser::LinkCallContext::entity() {
  return getRuleContext<OpenCMLParser::EntityContext>(0);
}

OpenCMLParser::FunctorRefContext* OpenCMLParser::LinkCallContext::functorRef() {
  return getRuleContext<OpenCMLParser::FunctorRefContext>(0);
}

OpenCMLParser::NormCallContext* OpenCMLParser::LinkCallContext::normCall() {
  return getRuleContext<OpenCMLParser::NormCallContext>(0);
}

OpenCMLParser::LinkCallContext* OpenCMLParser::LinkCallContext::linkCall() {
  return getRuleContext<OpenCMLParser::LinkCallContext>(0);
}


size_t OpenCMLParser::LinkCallContext::getRuleIndex() const {
  return OpenCMLParser::RuleLinkCall;
}


std::any OpenCMLParser::LinkCallContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitLinkCall(this);
  else
    return visitor->visitChildren(this);
}


OpenCMLParser::LinkCallContext* OpenCMLParser::linkCall() {
   return linkCall(0);
}

OpenCMLParser::LinkCallContext* OpenCMLParser::linkCall(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::LinkCallContext *_localctx = _tracker.createInstance<LinkCallContext>(_ctx, parentState);
  OpenCMLParser::LinkCallContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 76;
  enterRecursionRule(_localctx, 76, OpenCMLParser::RuleLinkCall, precedence);

    

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
    setState(504);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx)) {
    case 1: {
      setState(499);
      identRef();
      break;
    }

    case 2: {
      setState(500);
      entityRef();
      break;
    }

    case 3: {
      setState(501);
      entity();
      break;
    }

    case 4: {
      setState(502);
      functorRef();
      break;
    }

    case 5: {
      setState(503);
      normCall();
      break;
    }

    default:
      break;
    }
    _ctx->stop = _input->LT(-1);
    setState(516);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 71, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<LinkCallContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleLinkCall);
        setState(506);

        if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
        setState(507);
        match(OpenCMLParser::T__4);
        setState(512);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx)) {
        case 1: {
          setState(508);
          identRef();
          break;
        }

        case 2: {
          setState(509);
          functorRef();
          break;
        }

        case 3: {
          setState(510);
          entity();
          break;
        }

        case 4: {
          setState(511);
          normCall();
          break;
        }

        default:
          break;
        } 
      }
      setState(518);
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

//----------------- CalcExprContext ------------------------------------------------------------------

OpenCMLParser::CalcExprContext::CalcExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::RelaExprContext* OpenCMLParser::CalcExprContext::relaExpr() {
  return getRuleContext<OpenCMLParser::RelaExprContext>(0);
}

OpenCMLParser::CalcExprContext* OpenCMLParser::CalcExprContext::calcExpr() {
  return getRuleContext<OpenCMLParser::CalcExprContext>(0);
}


size_t OpenCMLParser::CalcExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleCalcExpr;
}


std::any OpenCMLParser::CalcExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitCalcExpr(this);
  else
    return visitor->visitChildren(this);
}


OpenCMLParser::CalcExprContext* OpenCMLParser::calcExpr() {
   return calcExpr(0);
}

OpenCMLParser::CalcExprContext* OpenCMLParser::calcExpr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::CalcExprContext *_localctx = _tracker.createInstance<CalcExprContext>(_ctx, parentState);
  OpenCMLParser::CalcExprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 78;
  enterRecursionRule(_localctx, 78, OpenCMLParser::RuleCalcExpr, precedence);

    

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
    setState(520);
    relaExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(539);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 73, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(537);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 72, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<CalcExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleCalcExpr);
          setState(522);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(523);
          match(OpenCMLParser::T__18);
          setState(524);
          relaExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<CalcExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleCalcExpr);
          setState(525);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(526);
          match(OpenCMLParser::T__19);
          setState(527);
          relaExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<CalcExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleCalcExpr);
          setState(528);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(529);
          match(OpenCMLParser::T__20);
          setState(530);
          relaExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<CalcExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleCalcExpr);
          setState(531);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(532);
          match(OpenCMLParser::T__21);
          setState(533);
          relaExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<CalcExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleCalcExpr);
          setState(534);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(535);
          match(OpenCMLParser::T__22);
          setState(536);
          relaExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(541);
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

//----------------- RelaExprContext ------------------------------------------------------------------

OpenCMLParser::RelaExprContext::RelaExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
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
  size_t startState = 80;
  enterRecursionRule(_localctx, 80, OpenCMLParser::RuleRelaExpr, precedence);

    

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
    setState(543);
    addExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(565);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 75, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(563);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(545);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(546);
          match(OpenCMLParser::T__11);
          setState(547);
          addExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(548);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(549);
          match(OpenCMLParser::T__12);
          setState(550);
          addExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(551);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(552);
          match(OpenCMLParser::T__23);
          setState(553);
          addExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(554);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(555);
          match(OpenCMLParser::T__24);
          setState(556);
          addExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(557);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(558);
          match(OpenCMLParser::T__25);
          setState(559);
          addExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(560);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(561);
          match(OpenCMLParser::T__26);
          setState(562);
          addExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(567);
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

//----------------- AddExprContext ------------------------------------------------------------------

OpenCMLParser::AddExprContext::AddExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
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
  size_t startState = 82;
  enterRecursionRule(_localctx, 82, OpenCMLParser::RuleAddExpr, precedence);

    

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
    setState(569);
    multiExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(579);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 77, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(577);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 76, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(571);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(572);
          match(OpenCMLParser::T__27);
          setState(573);
          multiExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(574);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(575);
          match(OpenCMLParser::T__28);
          setState(576);
          multiExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(581);
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

//----------------- MultiExprContext ------------------------------------------------------------------

OpenCMLParser::MultiExprContext::MultiExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::UnaryExprContext* OpenCMLParser::MultiExprContext::unaryExpr() {
  return getRuleContext<OpenCMLParser::UnaryExprContext>(0);
}

OpenCMLParser::MultiExprContext* OpenCMLParser::MultiExprContext::multiExpr() {
  return getRuleContext<OpenCMLParser::MultiExprContext>(0);
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
  size_t startState = 84;
  enterRecursionRule(_localctx, 84, OpenCMLParser::RuleMultiExpr, precedence);

    

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
    setState(583);
    unaryExpr();
    _ctx->stop = _input->LT(-1);
    setState(599);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 79, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(597);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 78, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(585);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(586);
          match(OpenCMLParser::T__29);
          setState(587);
          unaryExpr();
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(588);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(589);
          match(OpenCMLParser::T__30);
          setState(590);
          unaryExpr();
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(591);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(592);
          match(OpenCMLParser::T__31);
          setState(593);
          unaryExpr();
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(594);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(595);
          match(OpenCMLParser::T__32);
          setState(596);
          unaryExpr();
          break;
        }

        default:
          break;
        } 
      }
      setState(601);
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

//----------------- UnaryExprContext ------------------------------------------------------------------

OpenCMLParser::UnaryExprContext::UnaryExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
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
  enterRule(_localctx, 86, OpenCMLParser::RuleUnaryExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(617);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 80, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(602);
      primExpr();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(603);
      match(OpenCMLParser::T__33);
      setState(604);
      primExpr();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(605);
      match(OpenCMLParser::T__34);
      setState(606);
      primExpr();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(607);
      primExpr();
      setState(608);
      match(OpenCMLParser::T__33);
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(610);
      primExpr();
      setState(611);
      match(OpenCMLParser::T__34);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(613);
      match(OpenCMLParser::T__35);
      setState(614);
      primExpr();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(615);
      match(OpenCMLParser::T__28);
      setState(616);
      primExpr();
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

//----------------- PrimExprContext ------------------------------------------------------------------

OpenCMLParser::PrimExprContext::PrimExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::LiteralContext* OpenCMLParser::PrimExprContext::literal() {
  return getRuleContext<OpenCMLParser::LiteralContext>(0);
}

OpenCMLParser::EntityContext* OpenCMLParser::PrimExprContext::entity() {
  return getRuleContext<OpenCMLParser::EntityContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::PrimExprContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}

OpenCMLParser::CalcExprContext* OpenCMLParser::PrimExprContext::calcExpr() {
  return getRuleContext<OpenCMLParser::CalcExprContext>(0);
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
  enterRule(_localctx, 88, OpenCMLParser::RulePrimExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(626);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 81, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(619);
      literal();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(620);
      entity();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(621);
      entityExpr();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(622);
      match(OpenCMLParser::T__5);
      setState(623);
      calcExpr(0);
      setState(624);
      match(OpenCMLParser::T__7);
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

//----------------- ExprContext ------------------------------------------------------------------

OpenCMLParser::ExprContext::ExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::EntityExprContext* OpenCMLParser::ExprContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}

OpenCMLParser::CalcExprContext* OpenCMLParser::ExprContext::calcExpr() {
  return getRuleContext<OpenCMLParser::CalcExprContext>(0);
}


size_t OpenCMLParser::ExprContext::getRuleIndex() const {
  return OpenCMLParser::RuleExpr;
}


std::any OpenCMLParser::ExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitExpr(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ExprContext* OpenCMLParser::expr() {
  ExprContext *_localctx = _tracker.createInstance<ExprContext>(_ctx, getState());
  enterRule(_localctx, 90, OpenCMLParser::RuleExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(630);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 82, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(628);
      entityExpr();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(629);
      calcExpr(0);
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
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::ValueContext* OpenCMLParser::LiteralContext::value() {
  return getRuleContext<OpenCMLParser::ValueContext>(0);
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

tree::TerminalNode* OpenCMLParser::LiteralContext::NULL_() {
  return getToken(OpenCMLParser::NULL_, 0);
}

tree::TerminalNode* OpenCMLParser::LiteralContext::TRUE() {
  return getToken(OpenCMLParser::TRUE, 0);
}

tree::TerminalNode* OpenCMLParser::LiteralContext::FALSE() {
  return getToken(OpenCMLParser::FALSE, 0);
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
  enterRule(_localctx, 92, OpenCMLParser::RuleLiteral);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(639);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER:
      case OpenCMLParser::REAL: {
        enterOuterAlt(_localctx, 1);
        setState(632);
        value();
        break;
      }

      case OpenCMLParser::STRING: {
        enterOuterAlt(_localctx, 2);
        setState(633);
        match(OpenCMLParser::STRING);
        break;
      }

      case OpenCMLParser::MULTI_STR: {
        enterOuterAlt(_localctx, 3);
        setState(634);
        match(OpenCMLParser::MULTI_STR);
        break;
      }

      case OpenCMLParser::FSTRING: {
        enterOuterAlt(_localctx, 4);
        setState(635);
        match(OpenCMLParser::FSTRING);
        break;
      }

      case OpenCMLParser::NULL_: {
        enterOuterAlt(_localctx, 5);
        setState(636);
        match(OpenCMLParser::NULL_);
        break;
      }

      case OpenCMLParser::TRUE: {
        enterOuterAlt(_localctx, 6);
        setState(637);
        match(OpenCMLParser::TRUE);
        break;
      }

      case OpenCMLParser::FALSE: {
        enterOuterAlt(_localctx, 7);
        setState(638);
        match(OpenCMLParser::FALSE);
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

//----------------- ValueContext ------------------------------------------------------------------

OpenCMLParser::ValueContext::ValueContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::ValueContext::INTEGER() {
  return getToken(OpenCMLParser::INTEGER, 0);
}

tree::TerminalNode* OpenCMLParser::ValueContext::REAL() {
  return getToken(OpenCMLParser::REAL, 0);
}

tree::TerminalNode* OpenCMLParser::ValueContext::UNIT() {
  return getToken(OpenCMLParser::UNIT, 0);
}


size_t OpenCMLParser::ValueContext::getRuleIndex() const {
  return OpenCMLParser::RuleValue;
}


std::any OpenCMLParser::ValueContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitValue(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ValueContext* OpenCMLParser::value() {
  ValueContext *_localctx = _tracker.createInstance<ValueContext>(_ctx, getState());
  enterRule(_localctx, 94, OpenCMLParser::RuleValue);
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
    setState(641);
    _la = _input->LA(1);
    if (!(_la == OpenCMLParser::INTEGER

    || _la == OpenCMLParser::REAL)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(643);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 84, _ctx)) {
    case 1: {
      setState(642);
      match(OpenCMLParser::UNIT);
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

//----------------- TypeContext ------------------------------------------------------------------

OpenCMLParser::TypeContext::TypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::InnerTypeContext* OpenCMLParser::TypeContext::innerType() {
  return getRuleContext<OpenCMLParser::InnerTypeContext>(0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::TypeContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

tree::TerminalNode* OpenCMLParser::TypeContext::ANY_TYPE() {
  return getToken(OpenCMLParser::ANY_TYPE, 0);
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
  enterRule(_localctx, 96, OpenCMLParser::RuleType);

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
    switch (_input->LA(1)) {
      case OpenCMLParser::NUMBER_TYPE:
      case OpenCMLParser::STRING_TYPE:
      case OpenCMLParser::BOOLEAN_TYPE:
      case OpenCMLParser::FUNCTOR_TYPE:
      case OpenCMLParser::INTEGER_TYPE:
      case OpenCMLParser::REAL_TYPE:
      case OpenCMLParser::COMPLEX_TYPE:
      case OpenCMLParser::ARRAY_TYPE:
      case OpenCMLParser::MATRIX_TYPE:
      case OpenCMLParser::LIST_TYPE:
      case OpenCMLParser::DICT_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(645);
        innerType();
        break;
      }

      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(646);
        identRef();
        break;
      }

      case OpenCMLParser::ANY_TYPE: {
        enterOuterAlt(_localctx, 3);
        setState(647);
        match(OpenCMLParser::ANY_TYPE);
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

//----------------- InnerTypeContext ------------------------------------------------------------------

OpenCMLParser::InnerTypeContext::InnerTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::InnerTypeContext::NUMBER_TYPE() {
  return getToken(OpenCMLParser::NUMBER_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::InnerTypeContext::STRING_TYPE() {
  return getToken(OpenCMLParser::STRING_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::InnerTypeContext::BOOLEAN_TYPE() {
  return getToken(OpenCMLParser::BOOLEAN_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::InnerTypeContext::FUNCTOR_TYPE() {
  return getToken(OpenCMLParser::FUNCTOR_TYPE, 0);
}

OpenCMLParser::NumberTypeContext* OpenCMLParser::InnerTypeContext::numberType() {
  return getRuleContext<OpenCMLParser::NumberTypeContext>(0);
}

OpenCMLParser::StructTypeContext* OpenCMLParser::InnerTypeContext::structType() {
  return getRuleContext<OpenCMLParser::StructTypeContext>(0);
}


size_t OpenCMLParser::InnerTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleInnerType;
}


std::any OpenCMLParser::InnerTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitInnerType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::InnerTypeContext* OpenCMLParser::innerType() {
  InnerTypeContext *_localctx = _tracker.createInstance<InnerTypeContext>(_ctx, getState());
  enterRule(_localctx, 98, OpenCMLParser::RuleInnerType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(656);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::NUMBER_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(650);
        match(OpenCMLParser::NUMBER_TYPE);
        break;
      }

      case OpenCMLParser::STRING_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(651);
        match(OpenCMLParser::STRING_TYPE);
        break;
      }

      case OpenCMLParser::BOOLEAN_TYPE: {
        enterOuterAlt(_localctx, 3);
        setState(652);
        match(OpenCMLParser::BOOLEAN_TYPE);
        break;
      }

      case OpenCMLParser::FUNCTOR_TYPE: {
        enterOuterAlt(_localctx, 4);
        setState(653);
        match(OpenCMLParser::FUNCTOR_TYPE);
        break;
      }

      case OpenCMLParser::INTEGER_TYPE:
      case OpenCMLParser::REAL_TYPE:
      case OpenCMLParser::COMPLEX_TYPE:
      case OpenCMLParser::ARRAY_TYPE:
      case OpenCMLParser::MATRIX_TYPE: {
        enterOuterAlt(_localctx, 5);
        setState(654);
        numberType();
        break;
      }

      case OpenCMLParser::LIST_TYPE:
      case OpenCMLParser::DICT_TYPE: {
        enterOuterAlt(_localctx, 6);
        setState(655);
        structType();
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

//----------------- NumberTypeContext ------------------------------------------------------------------

OpenCMLParser::NumberTypeContext::NumberTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::ScalarTypeContext* OpenCMLParser::NumberTypeContext::scalarType() {
  return getRuleContext<OpenCMLParser::ScalarTypeContext>(0);
}

OpenCMLParser::VectorTypeContext* OpenCMLParser::NumberTypeContext::vectorType() {
  return getRuleContext<OpenCMLParser::VectorTypeContext>(0);
}


size_t OpenCMLParser::NumberTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleNumberType;
}


std::any OpenCMLParser::NumberTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitNumberType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::NumberTypeContext* OpenCMLParser::numberType() {
  NumberTypeContext *_localctx = _tracker.createInstance<NumberTypeContext>(_ctx, getState());
  enterRule(_localctx, 100, OpenCMLParser::RuleNumberType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(660);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER_TYPE:
      case OpenCMLParser::REAL_TYPE:
      case OpenCMLParser::COMPLEX_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(658);
        scalarType();
        break;
      }

      case OpenCMLParser::ARRAY_TYPE:
      case OpenCMLParser::MATRIX_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(659);
        vectorType();
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

//----------------- ScalarTypeContext ------------------------------------------------------------------

OpenCMLParser::ScalarTypeContext::ScalarTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::ScalarTypeContext::INTEGER_TYPE() {
  return getToken(OpenCMLParser::INTEGER_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::ScalarTypeContext::REAL_TYPE() {
  return getToken(OpenCMLParser::REAL_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::ScalarTypeContext::COMPLEX_TYPE() {
  return getToken(OpenCMLParser::COMPLEX_TYPE, 0);
}


size_t OpenCMLParser::ScalarTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleScalarType;
}


std::any OpenCMLParser::ScalarTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitScalarType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ScalarTypeContext* OpenCMLParser::scalarType() {
  ScalarTypeContext *_localctx = _tracker.createInstance<ScalarTypeContext>(_ctx, getState());
  enterRule(_localctx, 102, OpenCMLParser::RuleScalarType);
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
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8070450532247928832) != 0))) {
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

//----------------- VectorTypeContext ------------------------------------------------------------------

OpenCMLParser::VectorTypeContext::VectorTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::VectorTypeContext::ARRAY_TYPE() {
  return getToken(OpenCMLParser::ARRAY_TYPE, 0);
}

OpenCMLParser::ScalarTypeContext* OpenCMLParser::VectorTypeContext::scalarType() {
  return getRuleContext<OpenCMLParser::ScalarTypeContext>(0);
}

std::vector<tree::TerminalNode *> OpenCMLParser::VectorTypeContext::INTEGER() {
  return getTokens(OpenCMLParser::INTEGER);
}

tree::TerminalNode* OpenCMLParser::VectorTypeContext::INTEGER(size_t i) {
  return getToken(OpenCMLParser::INTEGER, i);
}

tree::TerminalNode* OpenCMLParser::VectorTypeContext::MATRIX_TYPE() {
  return getToken(OpenCMLParser::MATRIX_TYPE, 0);
}


size_t OpenCMLParser::VectorTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleVectorType;
}


std::any OpenCMLParser::VectorTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitVectorType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::VectorTypeContext* OpenCMLParser::vectorType() {
  VectorTypeContext *_localctx = _tracker.createInstance<VectorTypeContext>(_ctx, getState());
  enterRule(_localctx, 104, OpenCMLParser::RuleVectorType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    setState(691);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::ARRAY_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(664);
        match(OpenCMLParser::ARRAY_TYPE);
        setState(669);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 88, _ctx)) {
        case 1: {
          setState(665);
          match(OpenCMLParser::T__11);
          setState(666);
          scalarType();
          setState(667);
          match(OpenCMLParser::T__12);
          break;
        }

        default:
          break;
        }
        setState(674);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 89, _ctx)) {
        case 1: {
          setState(671);
          match(OpenCMLParser::T__8);
          setState(672);
          match(OpenCMLParser::INTEGER);
          setState(673);
          match(OpenCMLParser::T__9);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::MATRIX_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(676);
        match(OpenCMLParser::MATRIX_TYPE);
        setState(681);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 90, _ctx)) {
        case 1: {
          setState(677);
          match(OpenCMLParser::T__11);
          setState(678);
          scalarType();
          setState(679);
          match(OpenCMLParser::T__12);
          break;
        }

        default:
          break;
        }
        setState(688);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 91, _ctx);
        while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
          if (alt == 1) {
            setState(683);
            match(OpenCMLParser::T__8);
            setState(684);
            match(OpenCMLParser::INTEGER);
            setState(685);
            match(OpenCMLParser::T__9); 
          }
          setState(690);
          _errHandler->sync(this);
          alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 91, _ctx);
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

//----------------- StructTypeContext ------------------------------------------------------------------

OpenCMLParser::StructTypeContext::StructTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::LIST_TYPE() {
  return getToken(OpenCMLParser::LIST_TYPE, 0);
}

std::vector<OpenCMLParser::TypeContext *> OpenCMLParser::StructTypeContext::type() {
  return getRuleContexts<OpenCMLParser::TypeContext>();
}

OpenCMLParser::TypeContext* OpenCMLParser::StructTypeContext::type(size_t i) {
  return getRuleContext<OpenCMLParser::TypeContext>(i);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::INTEGER() {
  return getToken(OpenCMLParser::INTEGER, 0);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::DICT_TYPE() {
  return getToken(OpenCMLParser::DICT_TYPE, 0);
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
    setState(721);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::LIST_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(693);
        match(OpenCMLParser::LIST_TYPE);
        setState(705);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 94, _ctx)) {
        case 1: {
          setState(694);
          match(OpenCMLParser::T__11);
          setState(695);
          type();
          setState(700);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == OpenCMLParser::T__6) {
            setState(696);
            match(OpenCMLParser::T__6);
            setState(697);
            type();
            setState(702);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
          setState(703);
          match(OpenCMLParser::T__12);
          break;
        }

        default:
          break;
        }
        setState(710);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 95, _ctx)) {
        case 1: {
          setState(707);
          match(OpenCMLParser::T__8);
          setState(708);
          match(OpenCMLParser::INTEGER);
          setState(709);
          match(OpenCMLParser::T__9);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::DICT_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(712);
        match(OpenCMLParser::DICT_TYPE);
        setState(719);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 96, _ctx)) {
        case 1: {
          setState(713);
          match(OpenCMLParser::T__11);
          setState(714);
          type();
          setState(715);
          match(OpenCMLParser::T__6);
          setState(716);
          type();
          setState(717);
          match(OpenCMLParser::T__12);
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

//----------------- IdentRefContext ------------------------------------------------------------------

OpenCMLParser::IdentRefContext::IdentRefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
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
    setState(723);
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
    case 38: return linkCallSempred(antlrcpp::downCast<LinkCallContext *>(context), predicateIndex);
    case 39: return calcExprSempred(antlrcpp::downCast<CalcExprContext *>(context), predicateIndex);
    case 40: return relaExprSempred(antlrcpp::downCast<RelaExprContext *>(context), predicateIndex);
    case 41: return addExprSempred(antlrcpp::downCast<AddExprContext *>(context), predicateIndex);
    case 42: return multiExprSempred(antlrcpp::downCast<MultiExprContext *>(context), predicateIndex);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::linkCallSempred(LinkCallContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 0: return precpred(_ctx, 6);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::calcExprSempred(CalcExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 1: return precpred(_ctx, 5);
    case 2: return precpred(_ctx, 4);
    case 3: return precpred(_ctx, 3);
    case 4: return precpred(_ctx, 2);
    case 5: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::relaExprSempred(RelaExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
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

bool OpenCMLParser::addExprSempred(AddExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 12: return precpred(_ctx, 2);
    case 13: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::multiExprSempred(MultiExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 14: return precpred(_ctx, 4);
    case 15: return precpred(_ctx, 3);
    case 16: return precpred(_ctx, 2);
    case 17: return precpred(_ctx, 1);

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
