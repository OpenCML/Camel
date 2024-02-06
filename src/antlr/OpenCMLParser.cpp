
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
      "structType", "nullableType", "identRef"
    },
    std::vector<std::string>{
      "", "':'", "'='", "':='", "'::'", "'->'", "'('", "','", "')'", "'['", 
      "']'", "'@'", "'<'", "'>'", "'{'", "'}'", "'.'", "'=>'", "'*='", "'/='", 
      "'%='", "'+='", "'-='", "'<='", "'>='", "'=='", "'!='", "'+'", "'-'", 
      "'^'", "'*'", "'/'", "'%'", "'++'", "'--'", "'!'", "'\\u003F'", "';'", 
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
  	4,1,74,743,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,2,55,7,55,1,0,3,
  	0,114,8,0,1,0,1,0,1,1,1,1,1,1,5,1,121,8,1,10,1,12,1,124,9,1,1,1,3,1,127,
  	8,1,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,2,137,8,2,1,3,1,3,1,3,1,3,3,3,143,
  	8,3,1,3,3,3,146,8,3,1,3,1,3,1,3,1,3,1,3,3,3,153,8,3,1,3,1,3,1,3,3,3,158,
  	8,3,1,4,1,4,1,4,3,4,163,8,4,1,4,1,4,1,4,1,4,1,4,1,4,3,4,171,8,4,1,5,1,
  	5,1,5,3,5,176,8,5,1,6,1,6,3,6,180,8,6,1,6,1,6,1,6,1,6,1,6,1,6,3,6,188,
  	8,6,1,6,3,6,191,8,6,1,6,1,6,1,7,1,7,1,7,3,7,198,8,7,1,7,1,7,3,7,202,8,
  	7,1,8,1,8,1,8,3,8,207,8,8,1,8,1,8,1,9,1,9,3,9,213,8,9,1,10,1,10,1,10,
  	1,11,1,11,1,11,3,11,221,8,11,1,11,3,11,224,8,11,1,11,1,11,1,12,1,12,1,
  	12,3,12,231,8,12,1,13,1,13,1,13,1,13,1,13,1,13,1,14,1,14,1,14,1,14,1,
  	14,1,14,1,15,1,15,1,15,1,15,1,15,3,15,250,8,15,1,16,1,16,3,16,254,8,16,
  	5,16,256,8,16,10,16,12,16,259,9,16,1,17,5,17,262,8,17,10,17,12,17,265,
  	9,17,1,18,1,18,1,18,1,18,5,18,271,8,18,10,18,12,18,274,9,18,1,18,3,18,
  	277,8,18,1,18,1,18,1,19,1,19,1,19,1,19,5,19,285,8,19,10,19,12,19,288,
  	9,19,1,19,3,19,291,8,19,1,19,1,19,1,20,1,20,1,20,1,20,5,20,299,8,20,10,
  	20,12,20,302,9,20,3,20,304,8,20,1,20,3,20,307,8,20,1,20,1,20,1,21,1,21,
  	1,21,1,21,5,21,315,8,21,10,21,12,21,318,9,21,3,21,320,8,21,1,21,3,21,
  	323,8,21,1,21,1,21,1,22,1,22,1,22,1,22,3,22,331,8,22,1,23,1,23,1,23,1,
  	23,5,23,337,8,23,10,23,12,23,340,9,23,3,23,342,8,23,1,23,3,23,345,8,23,
  	1,23,1,23,1,24,1,24,3,24,351,8,24,1,24,1,24,1,24,1,24,3,24,357,8,24,1,
  	25,1,25,3,25,361,8,25,1,25,1,25,1,25,1,26,1,26,1,26,1,26,3,26,370,8,26,
  	5,26,372,8,26,10,26,12,26,375,9,26,1,26,3,26,378,8,26,1,27,1,27,3,27,
  	382,8,27,1,27,3,27,385,8,27,1,28,1,28,1,28,1,28,5,28,391,8,28,10,28,12,
  	28,394,9,28,3,28,396,8,28,1,28,3,28,399,8,28,1,28,1,28,1,29,1,29,1,29,
  	1,29,5,29,407,8,29,10,29,12,29,410,9,29,3,29,412,8,29,1,29,3,29,415,8,
  	29,1,29,1,29,1,30,1,30,1,30,1,30,5,30,423,8,30,10,30,12,30,426,9,30,3,
  	30,428,8,30,1,30,3,30,431,8,30,1,30,1,30,1,31,1,31,1,31,1,31,5,31,439,
  	8,31,10,31,12,31,442,9,31,3,31,444,8,31,1,31,3,31,447,8,31,1,31,1,31,
  	1,32,1,32,3,32,453,8,32,1,32,1,32,1,33,1,33,1,33,3,33,460,8,33,1,33,1,
  	33,1,34,1,34,1,34,1,34,3,34,468,8,34,1,34,1,34,3,34,472,8,34,1,35,1,35,
  	1,35,4,35,477,8,35,11,35,12,35,478,1,36,1,36,1,36,1,36,1,36,3,36,486,
  	8,36,1,36,3,36,489,8,36,1,36,1,36,3,36,493,8,36,1,37,1,37,1,37,1,38,1,
  	38,1,38,1,38,1,38,1,38,3,38,504,8,38,1,38,1,38,1,38,1,38,1,38,3,38,511,
  	8,38,1,38,1,38,1,38,1,38,1,38,3,38,518,8,38,1,38,1,38,1,38,1,38,1,38,
  	1,38,3,38,526,8,38,5,38,528,8,38,10,38,12,38,531,9,38,1,39,1,39,1,39,
  	1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,1,39,
  	1,39,5,39,551,8,39,10,39,12,39,554,9,39,1,40,1,40,1,40,1,40,1,40,1,40,
  	1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,
  	1,40,5,40,577,8,40,10,40,12,40,580,9,40,1,41,1,41,1,41,1,41,1,41,1,41,
  	1,41,1,41,1,41,5,41,591,8,41,10,41,12,41,594,9,41,1,42,1,42,1,42,1,42,
  	1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,1,42,5,42,611,8,42,
  	10,42,12,42,614,9,42,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,1,43,
  	1,43,1,43,1,43,1,43,1,43,3,43,631,8,43,1,44,1,44,1,44,1,44,1,44,1,44,
  	1,44,3,44,640,8,44,1,45,1,45,3,45,644,8,45,1,46,1,46,1,46,1,46,1,46,1,
  	46,1,46,3,46,653,8,46,1,47,1,47,3,47,657,8,47,1,48,1,48,1,48,3,48,662,
  	8,48,1,49,1,49,1,49,1,49,1,49,1,49,3,49,670,8,49,1,50,1,50,3,50,674,8,
  	50,1,51,1,51,1,52,1,52,1,52,1,52,1,52,3,52,683,8,52,1,52,1,52,1,52,3,
  	52,688,8,52,1,52,1,52,1,52,1,52,1,52,3,52,695,8,52,1,52,1,52,1,52,5,52,
  	700,8,52,10,52,12,52,703,9,52,3,52,705,8,52,1,53,1,53,1,53,1,53,1,53,
  	5,53,712,8,53,10,53,12,53,715,9,53,1,53,1,53,3,53,719,8,53,1,53,1,53,
  	1,53,3,53,724,8,53,1,53,1,53,1,53,1,53,1,53,1,53,1,53,3,53,733,8,53,3,
  	53,735,8,53,1,54,1,54,3,54,739,8,54,1,55,1,55,1,55,0,5,76,78,80,82,84,
  	56,0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,
  	48,50,52,54,56,58,60,62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,
  	94,96,98,100,102,104,106,108,110,0,3,1,0,46,51,1,0,73,74,1,0,60,62,841,
  	0,113,1,0,0,0,2,117,1,0,0,0,4,136,1,0,0,0,6,157,1,0,0,0,8,170,1,0,0,0,
  	10,172,1,0,0,0,12,177,1,0,0,0,14,194,1,0,0,0,16,203,1,0,0,0,18,210,1,
  	0,0,0,20,214,1,0,0,0,22,217,1,0,0,0,24,230,1,0,0,0,26,232,1,0,0,0,28,
  	238,1,0,0,0,30,244,1,0,0,0,32,257,1,0,0,0,34,263,1,0,0,0,36,266,1,0,0,
  	0,38,280,1,0,0,0,40,294,1,0,0,0,42,310,1,0,0,0,44,330,1,0,0,0,46,332,
  	1,0,0,0,48,348,1,0,0,0,50,358,1,0,0,0,52,365,1,0,0,0,54,379,1,0,0,0,56,
  	386,1,0,0,0,58,402,1,0,0,0,60,418,1,0,0,0,62,434,1,0,0,0,64,450,1,0,0,
  	0,66,459,1,0,0,0,68,467,1,0,0,0,70,476,1,0,0,0,72,492,1,0,0,0,74,494,
  	1,0,0,0,76,517,1,0,0,0,78,532,1,0,0,0,80,555,1,0,0,0,82,581,1,0,0,0,84,
  	595,1,0,0,0,86,630,1,0,0,0,88,639,1,0,0,0,90,643,1,0,0,0,92,652,1,0,0,
  	0,94,654,1,0,0,0,96,661,1,0,0,0,98,669,1,0,0,0,100,673,1,0,0,0,102,675,
  	1,0,0,0,104,704,1,0,0,0,106,734,1,0,0,0,108,736,1,0,0,0,110,740,1,0,0,
  	0,112,114,3,2,1,0,113,112,1,0,0,0,113,114,1,0,0,0,114,115,1,0,0,0,115,
  	116,5,0,0,1,116,1,1,0,0,0,117,122,3,4,2,0,118,119,5,37,0,0,119,121,3,
  	4,2,0,120,118,1,0,0,0,121,124,1,0,0,0,122,120,1,0,0,0,122,123,1,0,0,0,
  	123,126,1,0,0,0,124,122,1,0,0,0,125,127,5,37,0,0,126,125,1,0,0,0,126,
  	127,1,0,0,0,127,3,1,0,0,0,128,137,3,6,3,0,129,137,3,8,4,0,130,137,3,12,
  	6,0,131,137,3,14,7,0,132,137,3,16,8,0,133,137,3,18,9,0,134,137,3,20,10,
  	0,135,137,3,22,11,0,136,128,1,0,0,0,136,129,1,0,0,0,136,130,1,0,0,0,136,
  	131,1,0,0,0,136,132,1,0,0,0,136,133,1,0,0,0,136,134,1,0,0,0,136,135,1,
  	0,0,0,137,5,1,0,0,0,138,139,5,39,0,0,139,142,3,24,12,0,140,141,5,1,0,
  	0,141,143,3,96,48,0,142,140,1,0,0,0,142,143,1,0,0,0,143,145,1,0,0,0,144,
  	146,5,2,0,0,145,144,1,0,0,0,145,146,1,0,0,0,146,147,1,0,0,0,147,148,3,
  	90,45,0,148,158,1,0,0,0,149,152,3,24,12,0,150,151,5,1,0,0,151,153,3,96,
  	48,0,152,150,1,0,0,0,152,153,1,0,0,0,153,154,1,0,0,0,154,155,5,3,0,0,
  	155,156,3,90,45,0,156,158,1,0,0,0,157,138,1,0,0,0,157,149,1,0,0,0,158,
  	7,1,0,0,0,159,160,5,40,0,0,160,162,3,24,12,0,161,163,5,2,0,0,162,161,
  	1,0,0,0,162,163,1,0,0,0,163,164,1,0,0,0,164,165,3,90,45,0,165,171,1,0,
  	0,0,166,167,3,24,12,0,167,168,5,4,0,0,168,169,3,90,45,0,169,171,1,0,0,
  	0,170,159,1,0,0,0,170,166,1,0,0,0,171,9,1,0,0,0,172,175,5,44,0,0,173,
  	176,3,52,26,0,174,176,3,38,19,0,175,173,1,0,0,0,175,174,1,0,0,0,176,11,
  	1,0,0,0,177,179,3,32,16,0,178,180,3,10,5,0,179,178,1,0,0,0,179,180,1,
  	0,0,0,180,181,1,0,0,0,181,182,3,34,17,0,182,183,5,41,0,0,183,184,3,110,
  	55,0,184,187,3,40,20,0,185,186,5,5,0,0,186,188,3,96,48,0,187,185,1,0,
  	0,0,187,188,1,0,0,0,188,190,1,0,0,0,189,191,5,2,0,0,190,189,1,0,0,0,190,
  	191,1,0,0,0,191,192,1,0,0,0,192,193,3,64,32,0,193,13,1,0,0,0,194,195,
  	5,42,0,0,195,197,3,110,55,0,196,198,5,2,0,0,197,196,1,0,0,0,197,198,1,
  	0,0,0,198,201,1,0,0,0,199,202,3,96,48,0,200,202,3,46,23,0,201,199,1,0,
  	0,0,201,200,1,0,0,0,202,15,1,0,0,0,203,204,5,43,0,0,204,206,3,110,55,
  	0,205,207,5,2,0,0,206,205,1,0,0,0,206,207,1,0,0,0,207,208,1,0,0,0,208,
  	209,3,58,29,0,209,17,1,0,0,0,210,212,5,45,0,0,211,213,3,90,45,0,212,211,
  	1,0,0,0,212,213,1,0,0,0,213,19,1,0,0,0,214,215,3,32,16,0,215,216,3,90,
  	45,0,216,21,1,0,0,0,217,220,3,52,26,0,218,219,5,1,0,0,219,221,3,96,48,
  	0,220,218,1,0,0,0,220,221,1,0,0,0,221,223,1,0,0,0,222,224,5,2,0,0,223,
  	222,1,0,0,0,223,224,1,0,0,0,224,225,1,0,0,0,225,226,3,90,45,0,226,23,
  	1,0,0,0,227,231,3,110,55,0,228,231,3,56,28,0,229,231,3,58,29,0,230,227,
  	1,0,0,0,230,228,1,0,0,0,230,229,1,0,0,0,231,25,1,0,0,0,232,233,5,6,0,
  	0,233,234,3,90,45,0,234,235,5,7,0,0,235,236,3,90,45,0,236,237,5,8,0,0,
  	237,27,1,0,0,0,238,239,5,9,0,0,239,240,3,90,45,0,240,241,5,7,0,0,241,
  	242,3,90,45,0,242,243,5,10,0,0,243,29,1,0,0,0,244,249,5,11,0,0,245,250,
  	3,110,55,0,246,250,3,60,30,0,247,250,3,26,13,0,248,250,3,28,14,0,249,
  	245,1,0,0,0,249,246,1,0,0,0,249,247,1,0,0,0,249,248,1,0,0,0,250,31,1,
  	0,0,0,251,253,3,30,15,0,252,254,5,37,0,0,253,252,1,0,0,0,253,254,1,0,
  	0,0,254,256,1,0,0,0,255,251,1,0,0,0,256,259,1,0,0,0,257,255,1,0,0,0,257,
  	258,1,0,0,0,258,33,1,0,0,0,259,257,1,0,0,0,260,262,7,0,0,0,261,260,1,
  	0,0,0,262,265,1,0,0,0,263,261,1,0,0,0,263,264,1,0,0,0,264,35,1,0,0,0,
  	265,263,1,0,0,0,266,267,5,12,0,0,267,272,3,44,22,0,268,269,5,7,0,0,269,
  	271,3,44,22,0,270,268,1,0,0,0,271,274,1,0,0,0,272,270,1,0,0,0,272,273,
  	1,0,0,0,273,276,1,0,0,0,274,272,1,0,0,0,275,277,5,7,0,0,276,275,1,0,0,
  	0,276,277,1,0,0,0,277,278,1,0,0,0,278,279,5,13,0,0,279,37,1,0,0,0,280,
  	281,5,12,0,0,281,286,3,48,24,0,282,283,5,7,0,0,283,285,3,48,24,0,284,
  	282,1,0,0,0,285,288,1,0,0,0,286,284,1,0,0,0,286,287,1,0,0,0,287,290,1,
  	0,0,0,288,286,1,0,0,0,289,291,5,7,0,0,290,289,1,0,0,0,290,291,1,0,0,0,
  	291,292,1,0,0,0,292,293,5,13,0,0,293,39,1,0,0,0,294,303,5,6,0,0,295,300,
  	3,48,24,0,296,297,5,7,0,0,297,299,3,48,24,0,298,296,1,0,0,0,299,302,1,
  	0,0,0,300,298,1,0,0,0,300,301,1,0,0,0,301,304,1,0,0,0,302,300,1,0,0,0,
  	303,295,1,0,0,0,303,304,1,0,0,0,304,306,1,0,0,0,305,307,5,7,0,0,306,305,
  	1,0,0,0,306,307,1,0,0,0,307,308,1,0,0,0,308,309,5,8,0,0,309,41,1,0,0,
  	0,310,319,5,6,0,0,311,316,3,44,22,0,312,313,5,7,0,0,313,315,3,44,22,0,
  	314,312,1,0,0,0,315,318,1,0,0,0,316,314,1,0,0,0,316,317,1,0,0,0,317,320,
  	1,0,0,0,318,316,1,0,0,0,319,311,1,0,0,0,319,320,1,0,0,0,320,322,1,0,0,
  	0,321,323,5,7,0,0,322,321,1,0,0,0,322,323,1,0,0,0,323,324,1,0,0,0,324,
  	325,5,8,0,0,325,43,1,0,0,0,326,331,3,110,55,0,327,331,3,72,36,0,328,331,
  	3,50,25,0,329,331,3,90,45,0,330,326,1,0,0,0,330,327,1,0,0,0,330,328,1,
  	0,0,0,330,329,1,0,0,0,331,45,1,0,0,0,332,341,5,14,0,0,333,338,3,48,24,
  	0,334,335,5,7,0,0,335,337,3,48,24,0,336,334,1,0,0,0,337,340,1,0,0,0,338,
  	336,1,0,0,0,338,339,1,0,0,0,339,342,1,0,0,0,340,338,1,0,0,0,341,333,1,
  	0,0,0,341,342,1,0,0,0,342,344,1,0,0,0,343,345,5,7,0,0,344,343,1,0,0,0,
  	344,345,1,0,0,0,345,346,1,0,0,0,346,347,5,15,0,0,347,47,1,0,0,0,348,350,
  	3,110,55,0,349,351,3,30,15,0,350,349,1,0,0,0,350,351,1,0,0,0,351,352,
  	1,0,0,0,352,353,5,1,0,0,353,356,3,108,54,0,354,355,5,2,0,0,355,357,3,
  	90,45,0,356,354,1,0,0,0,356,357,1,0,0,0,357,49,1,0,0,0,358,360,3,110,
  	55,0,359,361,3,30,15,0,360,359,1,0,0,0,360,361,1,0,0,0,361,362,1,0,0,
  	0,362,363,5,2,0,0,363,364,3,90,45,0,364,51,1,0,0,0,365,373,3,110,55,0,
  	366,369,5,16,0,0,367,370,5,73,0,0,368,370,3,110,55,0,369,367,1,0,0,0,
  	369,368,1,0,0,0,370,372,1,0,0,0,371,366,1,0,0,0,372,375,1,0,0,0,373,371,
  	1,0,0,0,373,374,1,0,0,0,374,377,1,0,0,0,375,373,1,0,0,0,376,378,3,30,
  	15,0,377,376,1,0,0,0,377,378,1,0,0,0,378,53,1,0,0,0,379,381,3,110,55,
  	0,380,382,3,36,18,0,381,380,1,0,0,0,381,382,1,0,0,0,382,384,1,0,0,0,383,
  	385,3,30,15,0,384,383,1,0,0,0,384,385,1,0,0,0,385,55,1,0,0,0,386,395,
  	5,9,0,0,387,392,3,110,55,0,388,389,5,7,0,0,389,391,3,110,55,0,390,388,
  	1,0,0,0,391,394,1,0,0,0,392,390,1,0,0,0,392,393,1,0,0,0,393,396,1,0,0,
  	0,394,392,1,0,0,0,395,387,1,0,0,0,395,396,1,0,0,0,396,398,1,0,0,0,397,
  	399,5,7,0,0,398,397,1,0,0,0,398,399,1,0,0,0,399,400,1,0,0,0,400,401,5,
  	10,0,0,401,57,1,0,0,0,402,411,5,14,0,0,403,408,3,110,55,0,404,405,5,7,
  	0,0,405,407,3,110,55,0,406,404,1,0,0,0,407,410,1,0,0,0,408,406,1,0,0,
  	0,408,409,1,0,0,0,409,412,1,0,0,0,410,408,1,0,0,0,411,403,1,0,0,0,411,
  	412,1,0,0,0,412,414,1,0,0,0,413,415,5,7,0,0,414,413,1,0,0,0,414,415,1,
  	0,0,0,415,416,1,0,0,0,416,417,5,15,0,0,417,59,1,0,0,0,418,427,5,14,0,
  	0,419,424,3,50,25,0,420,421,5,7,0,0,421,423,3,50,25,0,422,420,1,0,0,0,
  	423,426,1,0,0,0,424,422,1,0,0,0,424,425,1,0,0,0,425,428,1,0,0,0,426,424,
  	1,0,0,0,427,419,1,0,0,0,427,428,1,0,0,0,428,430,1,0,0,0,429,431,5,7,0,
  	0,430,429,1,0,0,0,430,431,1,0,0,0,431,432,1,0,0,0,432,433,5,15,0,0,433,
  	61,1,0,0,0,434,443,5,9,0,0,435,440,3,90,45,0,436,437,5,7,0,0,437,439,
  	3,90,45,0,438,436,1,0,0,0,439,442,1,0,0,0,440,438,1,0,0,0,440,441,1,0,
  	0,0,441,444,1,0,0,0,442,440,1,0,0,0,443,435,1,0,0,0,443,444,1,0,0,0,444,
  	446,1,0,0,0,445,447,5,7,0,0,446,445,1,0,0,0,446,447,1,0,0,0,447,448,1,
  	0,0,0,448,449,5,10,0,0,449,63,1,0,0,0,450,452,5,14,0,0,451,453,3,2,1,
  	0,452,451,1,0,0,0,452,453,1,0,0,0,453,454,1,0,0,0,454,455,5,15,0,0,455,
  	65,1,0,0,0,456,457,3,40,20,0,457,458,5,17,0,0,458,460,1,0,0,0,459,456,
  	1,0,0,0,459,460,1,0,0,0,460,461,1,0,0,0,461,462,3,64,32,0,462,67,1,0,
  	0,0,463,468,3,72,36,0,464,468,3,74,37,0,465,468,3,76,38,0,466,468,3,70,
  	35,0,467,463,1,0,0,0,467,464,1,0,0,0,467,465,1,0,0,0,467,466,1,0,0,0,
  	468,471,1,0,0,0,469,470,5,38,0,0,470,472,3,96,48,0,471,469,1,0,0,0,471,
  	472,1,0,0,0,472,69,1,0,0,0,473,477,3,110,55,0,474,477,3,72,36,0,475,477,
  	3,76,38,0,476,473,1,0,0,0,476,474,1,0,0,0,476,475,1,0,0,0,477,478,1,0,
  	0,0,478,476,1,0,0,0,478,479,1,0,0,0,479,71,1,0,0,0,480,486,3,110,55,0,
  	481,486,3,92,46,0,482,486,3,62,31,0,483,486,3,60,30,0,484,486,3,66,33,
  	0,485,480,1,0,0,0,485,481,1,0,0,0,485,482,1,0,0,0,485,483,1,0,0,0,485,
  	484,1,0,0,0,486,488,1,0,0,0,487,489,3,30,15,0,488,487,1,0,0,0,488,489,
  	1,0,0,0,489,493,1,0,0,0,490,493,3,52,26,0,491,493,3,54,27,0,492,485,1,
  	0,0,0,492,490,1,0,0,0,492,491,1,0,0,0,493,73,1,0,0,0,494,495,3,54,27,
  	0,495,496,3,42,21,0,496,75,1,0,0,0,497,503,6,38,-1,0,498,504,3,110,55,
  	0,499,504,3,52,26,0,500,504,3,72,36,0,501,504,3,54,27,0,502,504,3,74,
  	37,0,503,498,1,0,0,0,503,499,1,0,0,0,503,500,1,0,0,0,503,501,1,0,0,0,
  	503,502,1,0,0,0,504,505,1,0,0,0,505,510,5,5,0,0,506,511,3,110,55,0,507,
  	511,3,54,27,0,508,511,3,72,36,0,509,511,3,74,37,0,510,506,1,0,0,0,510,
  	507,1,0,0,0,510,508,1,0,0,0,510,509,1,0,0,0,511,518,1,0,0,0,512,518,3,
  	110,55,0,513,518,3,52,26,0,514,518,3,72,36,0,515,518,3,54,27,0,516,518,
  	3,74,37,0,517,497,1,0,0,0,517,512,1,0,0,0,517,513,1,0,0,0,517,514,1,0,
  	0,0,517,515,1,0,0,0,517,516,1,0,0,0,518,529,1,0,0,0,519,520,10,7,0,0,
  	520,525,5,5,0,0,521,526,3,110,55,0,522,526,3,54,27,0,523,526,3,72,36,
  	0,524,526,3,74,37,0,525,521,1,0,0,0,525,522,1,0,0,0,525,523,1,0,0,0,525,
  	524,1,0,0,0,526,528,1,0,0,0,527,519,1,0,0,0,528,531,1,0,0,0,529,527,1,
  	0,0,0,529,530,1,0,0,0,530,77,1,0,0,0,531,529,1,0,0,0,532,533,6,39,-1,
  	0,533,534,3,80,40,0,534,552,1,0,0,0,535,536,10,5,0,0,536,537,5,18,0,0,
  	537,551,3,80,40,0,538,539,10,4,0,0,539,540,5,19,0,0,540,551,3,80,40,0,
  	541,542,10,3,0,0,542,543,5,20,0,0,543,551,3,80,40,0,544,545,10,2,0,0,
  	545,546,5,21,0,0,546,551,3,80,40,0,547,548,10,1,0,0,548,549,5,22,0,0,
  	549,551,3,80,40,0,550,535,1,0,0,0,550,538,1,0,0,0,550,541,1,0,0,0,550,
  	544,1,0,0,0,550,547,1,0,0,0,551,554,1,0,0,0,552,550,1,0,0,0,552,553,1,
  	0,0,0,553,79,1,0,0,0,554,552,1,0,0,0,555,556,6,40,-1,0,556,557,3,82,41,
  	0,557,578,1,0,0,0,558,559,10,6,0,0,559,560,5,12,0,0,560,577,3,82,41,0,
  	561,562,10,5,0,0,562,563,5,13,0,0,563,577,3,82,41,0,564,565,10,4,0,0,
  	565,566,5,23,0,0,566,577,3,82,41,0,567,568,10,3,0,0,568,569,5,24,0,0,
  	569,577,3,82,41,0,570,571,10,2,0,0,571,572,5,25,0,0,572,577,3,82,41,0,
  	573,574,10,1,0,0,574,575,5,26,0,0,575,577,3,82,41,0,576,558,1,0,0,0,576,
  	561,1,0,0,0,576,564,1,0,0,0,576,567,1,0,0,0,576,570,1,0,0,0,576,573,1,
  	0,0,0,577,580,1,0,0,0,578,576,1,0,0,0,578,579,1,0,0,0,579,81,1,0,0,0,
  	580,578,1,0,0,0,581,582,6,41,-1,0,582,583,3,84,42,0,583,592,1,0,0,0,584,
  	585,10,2,0,0,585,586,5,27,0,0,586,591,3,84,42,0,587,588,10,1,0,0,588,
  	589,5,28,0,0,589,591,3,84,42,0,590,584,1,0,0,0,590,587,1,0,0,0,591,594,
  	1,0,0,0,592,590,1,0,0,0,592,593,1,0,0,0,593,83,1,0,0,0,594,592,1,0,0,
  	0,595,596,6,42,-1,0,596,597,3,86,43,0,597,612,1,0,0,0,598,599,10,4,0,
  	0,599,600,5,29,0,0,600,611,3,86,43,0,601,602,10,3,0,0,602,603,5,30,0,
  	0,603,611,3,86,43,0,604,605,10,2,0,0,605,606,5,31,0,0,606,611,3,86,43,
  	0,607,608,10,1,0,0,608,609,5,32,0,0,609,611,3,86,43,0,610,598,1,0,0,0,
  	610,601,1,0,0,0,610,604,1,0,0,0,610,607,1,0,0,0,611,614,1,0,0,0,612,610,
  	1,0,0,0,612,613,1,0,0,0,613,85,1,0,0,0,614,612,1,0,0,0,615,631,3,88,44,
  	0,616,617,5,33,0,0,617,631,3,88,44,0,618,619,5,34,0,0,619,631,3,88,44,
  	0,620,621,3,88,44,0,621,622,5,33,0,0,622,631,1,0,0,0,623,624,3,88,44,
  	0,624,625,5,34,0,0,625,631,1,0,0,0,626,627,5,35,0,0,627,631,3,88,44,0,
  	628,629,5,28,0,0,629,631,3,88,44,0,630,615,1,0,0,0,630,616,1,0,0,0,630,
  	618,1,0,0,0,630,620,1,0,0,0,630,623,1,0,0,0,630,626,1,0,0,0,630,628,1,
  	0,0,0,631,87,1,0,0,0,632,640,3,92,46,0,633,640,3,72,36,0,634,640,3,68,
  	34,0,635,636,5,6,0,0,636,637,3,78,39,0,637,638,5,8,0,0,638,640,1,0,0,
  	0,639,632,1,0,0,0,639,633,1,0,0,0,639,634,1,0,0,0,639,635,1,0,0,0,640,
  	89,1,0,0,0,641,644,3,68,34,0,642,644,3,78,39,0,643,641,1,0,0,0,643,642,
  	1,0,0,0,644,91,1,0,0,0,645,653,3,94,47,0,646,653,5,71,0,0,647,653,5,68,
  	0,0,648,653,5,72,0,0,649,653,5,52,0,0,650,653,5,53,0,0,651,653,5,54,0,
  	0,652,645,1,0,0,0,652,646,1,0,0,0,652,647,1,0,0,0,652,648,1,0,0,0,652,
  	649,1,0,0,0,652,650,1,0,0,0,652,651,1,0,0,0,653,93,1,0,0,0,654,656,7,
  	1,0,0,655,657,5,70,0,0,656,655,1,0,0,0,656,657,1,0,0,0,657,95,1,0,0,0,
  	658,662,3,98,49,0,659,662,3,110,55,0,660,662,5,55,0,0,661,658,1,0,0,0,
  	661,659,1,0,0,0,661,660,1,0,0,0,662,97,1,0,0,0,663,670,5,56,0,0,664,670,
  	5,57,0,0,665,670,5,58,0,0,666,670,5,59,0,0,667,670,3,100,50,0,668,670,
  	3,106,53,0,669,663,1,0,0,0,669,664,1,0,0,0,669,665,1,0,0,0,669,666,1,
  	0,0,0,669,667,1,0,0,0,669,668,1,0,0,0,670,99,1,0,0,0,671,674,3,102,51,
  	0,672,674,3,104,52,0,673,671,1,0,0,0,673,672,1,0,0,0,674,101,1,0,0,0,
  	675,676,7,2,0,0,676,103,1,0,0,0,677,682,5,63,0,0,678,679,5,12,0,0,679,
  	680,3,102,51,0,680,681,5,13,0,0,681,683,1,0,0,0,682,678,1,0,0,0,682,683,
  	1,0,0,0,683,687,1,0,0,0,684,685,5,9,0,0,685,686,5,73,0,0,686,688,5,10,
  	0,0,687,684,1,0,0,0,687,688,1,0,0,0,688,705,1,0,0,0,689,694,5,64,0,0,
  	690,691,5,12,0,0,691,692,3,102,51,0,692,693,5,13,0,0,693,695,1,0,0,0,
  	694,690,1,0,0,0,694,695,1,0,0,0,695,701,1,0,0,0,696,697,5,9,0,0,697,698,
  	5,73,0,0,698,700,5,10,0,0,699,696,1,0,0,0,700,703,1,0,0,0,701,699,1,0,
  	0,0,701,702,1,0,0,0,702,705,1,0,0,0,703,701,1,0,0,0,704,677,1,0,0,0,704,
  	689,1,0,0,0,705,105,1,0,0,0,706,718,5,65,0,0,707,708,5,12,0,0,708,713,
  	3,108,54,0,709,710,5,7,0,0,710,712,3,108,54,0,711,709,1,0,0,0,712,715,
  	1,0,0,0,713,711,1,0,0,0,713,714,1,0,0,0,714,716,1,0,0,0,715,713,1,0,0,
  	0,716,717,5,13,0,0,717,719,1,0,0,0,718,707,1,0,0,0,718,719,1,0,0,0,719,
  	723,1,0,0,0,720,721,5,9,0,0,721,722,5,73,0,0,722,724,5,10,0,0,723,720,
  	1,0,0,0,723,724,1,0,0,0,724,735,1,0,0,0,725,732,5,66,0,0,726,727,5,12,
  	0,0,727,728,3,96,48,0,728,729,5,7,0,0,729,730,3,108,54,0,730,731,5,13,
  	0,0,731,733,1,0,0,0,732,726,1,0,0,0,732,733,1,0,0,0,733,735,1,0,0,0,734,
  	706,1,0,0,0,734,725,1,0,0,0,735,107,1,0,0,0,736,738,3,96,48,0,737,739,
  	5,36,0,0,738,737,1,0,0,0,738,739,1,0,0,0,739,109,1,0,0,0,740,741,5,69,
  	0,0,741,111,1,0,0,0,100,113,122,126,136,142,145,152,157,162,170,175,179,
  	187,190,197,201,206,212,220,223,230,249,253,257,263,272,276,286,290,300,
  	303,306,316,319,322,330,338,341,344,350,356,360,369,373,377,381,384,392,
  	395,398,408,411,414,424,427,430,440,443,446,452,459,467,471,476,478,485,
  	488,492,503,510,517,525,529,550,552,576,578,590,592,610,612,630,639,643,
  	652,656,661,669,673,682,687,694,701,704,713,718,723,732,734,738
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
    setState(113);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 36028307661146688) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 123) != 0)) {
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
    setState(136);
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
      funcDef();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(131);
      typeDef();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(132);
      enumDef();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(133);
      retStmt();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(134);
      exprStmt();
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(135);
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
    setState(157);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::LET: {
        enterOuterAlt(_localctx, 1);
        setState(138);
        match(OpenCMLParser::LET);
        setState(139);
        carrier();
        setState(142);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(140);
          match(OpenCMLParser::T__0);
          setState(141);
          type();
        }
        setState(145);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__1) {
          setState(144);
          match(OpenCMLParser::T__1);
        }
        setState(147);
        expr();
        break;
      }

      case OpenCMLParser::T__8:
      case OpenCMLParser::T__13:
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
          type();
        }
        setState(154);
        match(OpenCMLParser::T__2);
        setState(155);
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
        expr();
        break;
      }

      case OpenCMLParser::T__8:
      case OpenCMLParser::T__13:
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(166);
        carrier();
        setState(167);
        match(OpenCMLParser::T__3);
        setState(168);
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
    setState(172);
    match(OpenCMLParser::WITH);
    setState(175);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        setState(173);
        entityRef();
        break;
      }

      case OpenCMLParser::T__11: {
        setState(174);
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
    setState(177);
    annotations();
    setState(179);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WITH) {
      setState(178);
      withDef();
    }
    setState(181);
    modifiers();
    setState(182);
    match(OpenCMLParser::FUNC);
    setState(183);
    identRef();
    setState(184);
    paramDef();
    setState(187);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__4) {
      setState(185);
      match(OpenCMLParser::T__4);
      setState(186);
      type();
    }
    setState(190);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(189);
      match(OpenCMLParser::T__1);
    }
    setState(192);
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
    setState(194);
    match(OpenCMLParser::TYPE);
    setState(195);
    identRef();
    setState(197);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(196);
      match(OpenCMLParser::T__1);
    }
    setState(201);
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
        setState(199);
        type();
        break;
      }

      case OpenCMLParser::T__13: {
        setState(200);
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
    setState(203);
    match(OpenCMLParser::ENUM);
    setState(204);
    identRef();
    setState(206);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(205);
      match(OpenCMLParser::T__1);
    }
    setState(208);
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
    setState(210);
    match(OpenCMLParser::RETURN);
    setState(212);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 31525257789588032) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 123) != 0)) {
      setState(211);
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
    setState(214);
    annotations();
    setState(215);
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
    setState(217);
    entityRef();
    setState(220);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(218);
      match(OpenCMLParser::T__0);
      setState(219);
      type();
    }
    setState(223);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(222);
      match(OpenCMLParser::T__1);
    }
    setState(225);
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
    setState(230);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(227);
        identRef();
        break;
      }

      case OpenCMLParser::T__8: {
        enterOuterAlt(_localctx, 2);
        setState(228);
        listUnpack();
        break;
      }

      case OpenCMLParser::T__13: {
        enterOuterAlt(_localctx, 3);
        setState(229);
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
    setState(232);
    match(OpenCMLParser::T__5);
    setState(233);
    expr();
    setState(234);
    match(OpenCMLParser::T__6);
    setState(235);
    expr();
    setState(236);
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
    setState(238);
    match(OpenCMLParser::T__8);
    setState(239);
    expr();
    setState(240);
    match(OpenCMLParser::T__6);
    setState(241);
    expr();
    setState(242);
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
    setState(244);
    match(OpenCMLParser::T__10);
    setState(249);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        setState(245);
        identRef();
        break;
      }

      case OpenCMLParser::T__13: {
        setState(246);
        dictPack();
        break;
      }

      case OpenCMLParser::T__5: {
        setState(247);
        biasAnno();
        break;
      }

      case OpenCMLParser::T__8: {
        setState(248);
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
    setState(257);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__10) {
      setState(251);
      annotation();
      setState(253);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::SEP) {
        setState(252);
        match(OpenCMLParser::SEP);
      }
      setState(259);
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
    setState(263);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 4433230883192832) != 0)) {
      setState(260);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 4433230883192832) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(265);
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
    setState(266);
    match(OpenCMLParser::T__11);
    setState(267);
    argument();
    setState(272);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 25, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(268);
        match(OpenCMLParser::T__6);
        setState(269);
        argument(); 
      }
      setState(274);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 25, _ctx);
    }
    setState(276);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(275);
      match(OpenCMLParser::T__6);
    }
    setState(278);
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
    setState(280);
    match(OpenCMLParser::T__11);
    setState(281);
    keyValDecl();
    setState(286);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(282);
        match(OpenCMLParser::T__6);
        setState(283);
        keyValDecl(); 
      }
      setState(288);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx);
    }
    setState(290);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(289);
      match(OpenCMLParser::T__6);
    }
    setState(292);
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
    setState(294);
    match(OpenCMLParser::T__5);
    setState(303);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(295);
      keyValDecl();
      setState(300);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(296);
          match(OpenCMLParser::T__6);
          setState(297);
          keyValDecl(); 
        }
        setState(302);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx);
      }
    }
    setState(306);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(305);
      match(OpenCMLParser::T__6);
    }
    setState(308);
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
    setState(310);
    match(OpenCMLParser::T__5);
    setState(319);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 31525257789588032) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 123) != 0)) {
      setState(311);
      argument();
      setState(316);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(312);
          match(OpenCMLParser::T__6);
          setState(313);
          argument(); 
        }
        setState(318);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx);
      }
    }
    setState(322);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(321);
      match(OpenCMLParser::T__6);
    }
    setState(324);
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
    setState(330);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(326);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(327);
      entity();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(328);
      keyValExpr();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(329);
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
    setState(332);
    match(OpenCMLParser::T__13);
    setState(341);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(333);
      keyValDecl();
      setState(338);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(334);
          match(OpenCMLParser::T__6);
          setState(335);
          keyValDecl(); 
        }
        setState(340);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx);
      }
    }
    setState(344);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(343);
      match(OpenCMLParser::T__6);
    }
    setState(346);
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

OpenCMLParser::NullableTypeContext* OpenCMLParser::KeyValDeclContext::nullableType() {
  return getRuleContext<OpenCMLParser::NullableTypeContext>(0);
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
    setState(348);
    identRef();
    setState(350);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__10) {
      setState(349);
      annotation();
    }
    setState(352);
    match(OpenCMLParser::T__0);
    setState(353);
    nullableType();
    setState(356);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(354);
      match(OpenCMLParser::T__1);
      setState(355);
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
    setState(358);
    identRef();
    setState(360);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__10) {
      setState(359);
      annotation();
    }
    setState(362);
    match(OpenCMLParser::T__1);
    setState(363);
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
    setState(365);
    identRef();
    setState(373);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 43, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(366);
        match(OpenCMLParser::T__15);
        setState(369);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case OpenCMLParser::INTEGER: {
            setState(367);
            match(OpenCMLParser::INTEGER);
            break;
          }

          case OpenCMLParser::IDENTIFIER: {
            setState(368);
            identRef();
            break;
          }

        default:
          throw NoViableAltException(this);
        } 
      }
      setState(375);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 43, _ctx);
    }
    setState(377);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 44, _ctx)) {
    case 1: {
      setState(376);
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
    setState(379);
    identRef();
    setState(381);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 45, _ctx)) {
    case 1: {
      setState(380);
      withList();
      break;
    }

    default:
      break;
    }
    setState(384);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 46, _ctx)) {
    case 1: {
      setState(383);
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
    setState(386);
    match(OpenCMLParser::T__8);
    setState(395);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(387);
      identRef();
      setState(392);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 47, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(388);
          match(OpenCMLParser::T__6);
          setState(389);
          identRef(); 
        }
        setState(394);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 47, _ctx);
      }
    }
    setState(398);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(397);
      match(OpenCMLParser::T__6);
    }
    setState(400);
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
    setState(402);
    match(OpenCMLParser::T__13);
    setState(411);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(403);
      identRef();
      setState(408);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 50, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(404);
          match(OpenCMLParser::T__6);
          setState(405);
          identRef(); 
        }
        setState(410);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 50, _ctx);
      }
    }
    setState(414);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(413);
      match(OpenCMLParser::T__6);
    }
    setState(416);
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
    setState(418);
    match(OpenCMLParser::T__13);
    setState(427);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(419);
      keyValExpr();
      setState(424);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 53, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(420);
          match(OpenCMLParser::T__6);
          setState(421);
          keyValExpr(); 
        }
        setState(426);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 53, _ctx);
      }
    }
    setState(430);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(429);
      match(OpenCMLParser::T__6);
    }
    setState(432);
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
    setState(434);
    match(OpenCMLParser::T__8);
    setState(443);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 31525257789588032) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 123) != 0)) {
      setState(435);
      expr();
      setState(440);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 56, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(436);
          match(OpenCMLParser::T__6);
          setState(437);
          expr(); 
        }
        setState(442);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 56, _ctx);
      }
    }
    setState(446);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(445);
      match(OpenCMLParser::T__6);
    }
    setState(448);
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
    setState(450);
    match(OpenCMLParser::T__13);
    setState(452);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 36028307661146688) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 123) != 0)) {
      setState(451);
      stmtList();
    }
    setState(454);
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
    setState(459);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(456);
      paramDef();
      setState(457);
      match(OpenCMLParser::T__16);
    }
    setState(461);
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
    setState(467);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx)) {
    case 1: {
      setState(463);
      entity();
      break;
    }

    case 2: {
      setState(464);
      normCall();
      break;
    }

    case 3: {
      setState(465);
      linkCall(0);
      break;
    }

    case 4: {
      setState(466);
      entityChain();
      break;
    }

    default:
      break;
    }
    setState(471);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 62, _ctx)) {
    case 1: {
      setState(469);
      match(OpenCMLParser::AS);
      setState(470);
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
    setState(476); 
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(476);
              _errHandler->sync(this);
              switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 63, _ctx)) {
              case 1: {
                setState(473);
                identRef();
                break;
              }

              case 2: {
                setState(474);
                entity();
                break;
              }

              case 3: {
                setState(475);
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
      setState(478); 
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 64, _ctx);
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
    setState(492);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 67, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(485);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 65, _ctx)) {
      case 1: {
        setState(480);
        identRef();
        break;
      }

      case 2: {
        setState(481);
        literal();
        break;
      }

      case 3: {
        setState(482);
        listPack();
        break;
      }

      case 4: {
        setState(483);
        dictPack();
        break;
      }

      case 5: {
        setState(484);
        lambda();
        break;
      }

      default:
        break;
      }
      setState(488);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 66, _ctx)) {
      case 1: {
        setState(487);
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
      setState(490);
      entityRef();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(491);
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
    setState(494);
    functorRef();
    setState(495);
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

std::vector<OpenCMLParser::IdentRefContext *> OpenCMLParser::LinkCallContext::identRef() {
  return getRuleContexts<OpenCMLParser::IdentRefContext>();
}

OpenCMLParser::IdentRefContext* OpenCMLParser::LinkCallContext::identRef(size_t i) {
  return getRuleContext<OpenCMLParser::IdentRefContext>(i);
}

OpenCMLParser::EntityRefContext* OpenCMLParser::LinkCallContext::entityRef() {
  return getRuleContext<OpenCMLParser::EntityRefContext>(0);
}

std::vector<OpenCMLParser::EntityContext *> OpenCMLParser::LinkCallContext::entity() {
  return getRuleContexts<OpenCMLParser::EntityContext>();
}

OpenCMLParser::EntityContext* OpenCMLParser::LinkCallContext::entity(size_t i) {
  return getRuleContext<OpenCMLParser::EntityContext>(i);
}

std::vector<OpenCMLParser::FunctorRefContext *> OpenCMLParser::LinkCallContext::functorRef() {
  return getRuleContexts<OpenCMLParser::FunctorRefContext>();
}

OpenCMLParser::FunctorRefContext* OpenCMLParser::LinkCallContext::functorRef(size_t i) {
  return getRuleContext<OpenCMLParser::FunctorRefContext>(i);
}

std::vector<OpenCMLParser::NormCallContext *> OpenCMLParser::LinkCallContext::normCall() {
  return getRuleContexts<OpenCMLParser::NormCallContext>();
}

OpenCMLParser::NormCallContext* OpenCMLParser::LinkCallContext::normCall(size_t i) {
  return getRuleContext<OpenCMLParser::NormCallContext>(i);
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
    setState(517);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx)) {
    case 1: {
      setState(503);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 68, _ctx)) {
      case 1: {
        setState(498);
        identRef();
        break;
      }

      case 2: {
        setState(499);
        entityRef();
        break;
      }

      case 3: {
        setState(500);
        entity();
        break;
      }

      case 4: {
        setState(501);
        functorRef();
        break;
      }

      case 5: {
        setState(502);
        normCall();
        break;
      }

      default:
        break;
      }
      setState(505);
      match(OpenCMLParser::T__4);
      setState(510);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx)) {
      case 1: {
        setState(506);
        identRef();
        break;
      }

      case 2: {
        setState(507);
        functorRef();
        break;
      }

      case 3: {
        setState(508);
        entity();
        break;
      }

      case 4: {
        setState(509);
        normCall();
        break;
      }

      default:
        break;
      }
      break;
    }

    case 2: {
      setState(512);
      identRef();
      break;
    }

    case 3: {
      setState(513);
      entityRef();
      break;
    }

    case 4: {
      setState(514);
      entity();
      break;
    }

    case 5: {
      setState(515);
      functorRef();
      break;
    }

    case 6: {
      setState(516);
      normCall();
      break;
    }

    default:
      break;
    }
    _ctx->stop = _input->LT(-1);
    setState(529);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 72, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<LinkCallContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleLinkCall);
        setState(519);

        if (!(precpred(_ctx, 7))) throw FailedPredicateException(this, "precpred(_ctx, 7)");
        setState(520);
        match(OpenCMLParser::T__4);
        setState(525);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 71, _ctx)) {
        case 1: {
          setState(521);
          identRef();
          break;
        }

        case 2: {
          setState(522);
          functorRef();
          break;
        }

        case 3: {
          setState(523);
          entity();
          break;
        }

        case 4: {
          setState(524);
          normCall();
          break;
        }

        default:
          break;
        } 
      }
      setState(531);
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
    setState(533);
    relaExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(552);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(550);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 73, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<CalcExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleCalcExpr);
          setState(535);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(536);
          match(OpenCMLParser::T__17);
          setState(537);
          relaExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<CalcExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleCalcExpr);
          setState(538);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(539);
          match(OpenCMLParser::T__18);
          setState(540);
          relaExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<CalcExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleCalcExpr);
          setState(541);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(542);
          match(OpenCMLParser::T__19);
          setState(543);
          relaExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<CalcExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleCalcExpr);
          setState(544);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(545);
          match(OpenCMLParser::T__20);
          setState(546);
          relaExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<CalcExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleCalcExpr);
          setState(547);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(548);
          match(OpenCMLParser::T__21);
          setState(549);
          relaExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(554);
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
    setState(556);
    addExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(578);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 76, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(576);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 75, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(558);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(559);
          match(OpenCMLParser::T__11);
          setState(560);
          addExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(561);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(562);
          match(OpenCMLParser::T__12);
          setState(563);
          addExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(564);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(565);
          match(OpenCMLParser::T__22);
          setState(566);
          addExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(567);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(568);
          match(OpenCMLParser::T__23);
          setState(569);
          addExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(570);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(571);
          match(OpenCMLParser::T__24);
          setState(572);
          addExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(573);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(574);
          match(OpenCMLParser::T__25);
          setState(575);
          addExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(580);
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
    setState(582);
    multiExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(592);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 78, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(590);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 77, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(584);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(585);
          match(OpenCMLParser::T__26);
          setState(586);
          multiExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(587);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(588);
          match(OpenCMLParser::T__27);
          setState(589);
          multiExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(594);
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
    setState(596);
    unaryExpr();
    _ctx->stop = _input->LT(-1);
    setState(612);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 80, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(610);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 79, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(598);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(599);
          match(OpenCMLParser::T__28);
          setState(600);
          unaryExpr();
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(601);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(602);
          match(OpenCMLParser::T__29);
          setState(603);
          unaryExpr();
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(604);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(605);
          match(OpenCMLParser::T__30);
          setState(606);
          unaryExpr();
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(607);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(608);
          match(OpenCMLParser::T__31);
          setState(609);
          unaryExpr();
          break;
        }

        default:
          break;
        } 
      }
      setState(614);
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
    setState(630);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 81, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(615);
      primExpr();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(616);
      match(OpenCMLParser::T__32);
      setState(617);
      primExpr();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(618);
      match(OpenCMLParser::T__33);
      setState(619);
      primExpr();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(620);
      primExpr();
      setState(621);
      match(OpenCMLParser::T__32);
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(623);
      primExpr();
      setState(624);
      match(OpenCMLParser::T__33);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(626);
      match(OpenCMLParser::T__34);
      setState(627);
      primExpr();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(628);
      match(OpenCMLParser::T__27);
      setState(629);
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
    setState(639);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 82, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(632);
      literal();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(633);
      entity();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(634);
      entityExpr();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(635);
      match(OpenCMLParser::T__5);
      setState(636);
      calcExpr(0);
      setState(637);
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
    setState(643);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 83, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(641);
      entityExpr();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(642);
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
    setState(652);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER:
      case OpenCMLParser::REAL: {
        enterOuterAlt(_localctx, 1);
        setState(645);
        value();
        break;
      }

      case OpenCMLParser::STRING: {
        enterOuterAlt(_localctx, 2);
        setState(646);
        match(OpenCMLParser::STRING);
        break;
      }

      case OpenCMLParser::MULTI_STR: {
        enterOuterAlt(_localctx, 3);
        setState(647);
        match(OpenCMLParser::MULTI_STR);
        break;
      }

      case OpenCMLParser::FSTRING: {
        enterOuterAlt(_localctx, 4);
        setState(648);
        match(OpenCMLParser::FSTRING);
        break;
      }

      case OpenCMLParser::NULL_: {
        enterOuterAlt(_localctx, 5);
        setState(649);
        match(OpenCMLParser::NULL_);
        break;
      }

      case OpenCMLParser::TRUE: {
        enterOuterAlt(_localctx, 6);
        setState(650);
        match(OpenCMLParser::TRUE);
        break;
      }

      case OpenCMLParser::FALSE: {
        enterOuterAlt(_localctx, 7);
        setState(651);
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
    setState(654);
    _la = _input->LA(1);
    if (!(_la == OpenCMLParser::INTEGER

    || _la == OpenCMLParser::REAL)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(656);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 85, _ctx)) {
    case 1: {
      setState(655);
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
    setState(661);
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
        setState(658);
        innerType();
        break;
      }

      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(659);
        identRef();
        break;
      }

      case OpenCMLParser::ANY_TYPE: {
        enterOuterAlt(_localctx, 3);
        setState(660);
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
    setState(669);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::NUMBER_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(663);
        match(OpenCMLParser::NUMBER_TYPE);
        break;
      }

      case OpenCMLParser::STRING_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(664);
        match(OpenCMLParser::STRING_TYPE);
        break;
      }

      case OpenCMLParser::BOOLEAN_TYPE: {
        enterOuterAlt(_localctx, 3);
        setState(665);
        match(OpenCMLParser::BOOLEAN_TYPE);
        break;
      }

      case OpenCMLParser::FUNCTOR_TYPE: {
        enterOuterAlt(_localctx, 4);
        setState(666);
        match(OpenCMLParser::FUNCTOR_TYPE);
        break;
      }

      case OpenCMLParser::INTEGER_TYPE:
      case OpenCMLParser::REAL_TYPE:
      case OpenCMLParser::COMPLEX_TYPE:
      case OpenCMLParser::ARRAY_TYPE:
      case OpenCMLParser::MATRIX_TYPE: {
        enterOuterAlt(_localctx, 5);
        setState(667);
        numberType();
        break;
      }

      case OpenCMLParser::LIST_TYPE:
      case OpenCMLParser::DICT_TYPE: {
        enterOuterAlt(_localctx, 6);
        setState(668);
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
    setState(673);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER_TYPE:
      case OpenCMLParser::REAL_TYPE:
      case OpenCMLParser::COMPLEX_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(671);
        scalarType();
        break;
      }

      case OpenCMLParser::ARRAY_TYPE:
      case OpenCMLParser::MATRIX_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(672);
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
    setState(675);
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
    setState(704);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::ARRAY_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(677);
        match(OpenCMLParser::ARRAY_TYPE);
        setState(682);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 89, _ctx)) {
        case 1: {
          setState(678);
          match(OpenCMLParser::T__11);
          setState(679);
          scalarType();
          setState(680);
          match(OpenCMLParser::T__12);
          break;
        }

        default:
          break;
        }
        setState(687);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 90, _ctx)) {
        case 1: {
          setState(684);
          match(OpenCMLParser::T__8);
          setState(685);
          match(OpenCMLParser::INTEGER);
          setState(686);
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
        setState(689);
        match(OpenCMLParser::MATRIX_TYPE);
        setState(694);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 91, _ctx)) {
        case 1: {
          setState(690);
          match(OpenCMLParser::T__11);
          setState(691);
          scalarType();
          setState(692);
          match(OpenCMLParser::T__12);
          break;
        }

        default:
          break;
        }
        setState(701);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 92, _ctx);
        while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
          if (alt == 1) {
            setState(696);
            match(OpenCMLParser::T__8);
            setState(697);
            match(OpenCMLParser::INTEGER);
            setState(698);
            match(OpenCMLParser::T__9); 
          }
          setState(703);
          _errHandler->sync(this);
          alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 92, _ctx);
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

std::vector<OpenCMLParser::NullableTypeContext *> OpenCMLParser::StructTypeContext::nullableType() {
  return getRuleContexts<OpenCMLParser::NullableTypeContext>();
}

OpenCMLParser::NullableTypeContext* OpenCMLParser::StructTypeContext::nullableType(size_t i) {
  return getRuleContext<OpenCMLParser::NullableTypeContext>(i);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::INTEGER() {
  return getToken(OpenCMLParser::INTEGER, 0);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::DICT_TYPE() {
  return getToken(OpenCMLParser::DICT_TYPE, 0);
}

OpenCMLParser::TypeContext* OpenCMLParser::StructTypeContext::type() {
  return getRuleContext<OpenCMLParser::TypeContext>(0);
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
      case OpenCMLParser::LIST_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(706);
        match(OpenCMLParser::LIST_TYPE);
        setState(718);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 95, _ctx)) {
        case 1: {
          setState(707);
          match(OpenCMLParser::T__11);
          setState(708);
          nullableType();
          setState(713);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == OpenCMLParser::T__6) {
            setState(709);
            match(OpenCMLParser::T__6);
            setState(710);
            nullableType();
            setState(715);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
          setState(716);
          match(OpenCMLParser::T__12);
          break;
        }

        default:
          break;
        }
        setState(723);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 96, _ctx)) {
        case 1: {
          setState(720);
          match(OpenCMLParser::T__8);
          setState(721);
          match(OpenCMLParser::INTEGER);
          setState(722);
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
        setState(725);
        match(OpenCMLParser::DICT_TYPE);
        setState(732);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 97, _ctx)) {
        case 1: {
          setState(726);
          match(OpenCMLParser::T__11);
          setState(727);
          type();
          setState(728);
          match(OpenCMLParser::T__6);
          setState(729);
          nullableType();
          setState(730);
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

//----------------- NullableTypeContext ------------------------------------------------------------------

OpenCMLParser::NullableTypeContext::NullableTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::TypeContext* OpenCMLParser::NullableTypeContext::type() {
  return getRuleContext<OpenCMLParser::TypeContext>(0);
}


size_t OpenCMLParser::NullableTypeContext::getRuleIndex() const {
  return OpenCMLParser::RuleNullableType;
}


std::any OpenCMLParser::NullableTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitNullableType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::NullableTypeContext* OpenCMLParser::nullableType() {
  NullableTypeContext *_localctx = _tracker.createInstance<NullableTypeContext>(_ctx, getState());
  enterRule(_localctx, 108, OpenCMLParser::RuleNullableType);
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
    type();
    setState(738);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__35) {
      setState(737);
      match(OpenCMLParser::T__35);
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
    setState(740);
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
    case 0: return precpred(_ctx, 7);

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
