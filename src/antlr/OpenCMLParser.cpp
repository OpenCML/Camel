
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
      "program", "stmtList", "stmt", "letStmt", "useStmt", "exprStmt", "assignStmt", 
      "withDef", "funcDef", "retStmt", "lambda", "carrier", "annotation", 
      "annotations", "modifiers", "argument", "argsList", "exprList", "keyValExpr", 
      "keyValList", "dictPack", "listPack", "withDecl", "withPack", "argsDecl", 
      "argsPack", "stmtPack", "primEntity", "memberAccess", "entity", "entityLink", 
      "entityChain", "entityUnpack", "entityExpr", "expr", "relaExpr", "addExpr", 
      "multiExpr", "unaryExpr", "primExpr", "literal", "value", "type", 
      "innerType", "numberType", "scalarType", "vectorType", "structType", 
      "identRef"
    },
    std::vector<std::string>{
      "", "':'", "'='", "':='", "'::'", "'=>'", "'@'", "','", "'\\u003F'", 
      "'{'", "'}'", "'['", "']'", "'<'", "'>'", "'('", "')'", "'.'", "'->'", 
      "'...'", "'*='", "'/='", "'%='", "'+='", "'-='", "'<<'", "'>>'", "'<='", 
      "'>='", "'=='", "'!='", "'+'", "'-'", "'^'", "'*'", "'/'", "'%'", 
      "'!'", "';'", "'as'", "'let'", "'use'", "'func'", "'type'", "'enum'", 
      "'with'", "'return'", "'inner'", "'outer'", "'sync'", "'scoped'", 
      "'static'", "'atomic'", "'null'", "'true'", "'false'", "'any'", "'number'", 
      "'string'", "'bool'", "'functor'", "'int'", "'real'", "'complex'", 
      "'array'", "'matrix'", "'list'", "'dict'"
    },
    std::vector<std::string>{
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "SEP", "AS", "LET", "USE", "FUNC", "TYPE", "ENUM", 
      "WITH", "RETURN", "INNER", "OUTER", "SYNC", "SCOPED", "STATIC", "ATOMIC", 
      "NULL", "TRUE", "FALSE", "ANY_TYPE", "NUMBER_TYPE", "STRING_TYPE", 
      "BOOLEAN_TYPE", "FUNCTOR_TYPE", "INTEGER_TYPE", "REAL_TYPE", "COMPLEX_TYPE", 
      "ARRAY_TYPE", "MATRIX_TYPE", "LIST_TYPE", "DICT_TYPE", "SKIP_", "MULTI_STR", 
      "IDENTIFIER", "UNIT", "STRING", "FSTRING", "INTEGER", "REAL"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,75,579,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,1,0,3,
  	0,100,8,0,1,0,1,0,1,1,4,1,105,8,1,11,1,12,1,106,1,2,1,2,1,2,1,2,1,2,1,
  	2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,2,127,8,2,1,3,1,3,
  	1,3,1,3,3,3,133,8,3,1,3,3,3,136,8,3,1,3,1,3,1,3,1,3,1,3,3,3,143,8,3,1,
  	3,1,3,1,3,3,3,148,8,3,1,4,1,4,1,4,3,4,153,8,4,1,4,1,4,1,4,1,4,1,4,1,4,
  	3,4,161,8,4,1,5,3,5,164,8,5,1,5,1,5,1,6,1,6,1,6,1,6,1,7,1,7,1,7,3,7,175,
  	8,7,1,8,3,8,178,8,8,1,8,3,8,181,8,8,1,8,3,8,184,8,8,1,8,1,8,1,8,1,8,1,
  	8,3,8,191,8,8,1,8,1,8,1,9,1,9,3,9,197,8,9,1,10,1,10,1,10,3,10,202,8,10,
  	1,10,1,10,3,10,206,8,10,1,10,1,10,1,11,1,11,1,11,3,11,213,8,11,1,12,1,
  	12,1,12,1,13,1,13,3,13,220,8,13,4,13,222,8,13,11,13,12,13,223,1,14,4,
  	14,227,8,14,11,14,12,14,228,1,15,1,15,1,15,1,15,1,15,1,15,3,15,237,8,
  	15,1,16,1,16,1,16,5,16,242,8,16,10,16,12,16,245,9,16,1,17,1,17,3,17,249,
  	8,17,1,17,1,17,1,17,3,17,254,8,17,5,17,256,8,17,10,17,12,17,259,9,17,
  	1,18,1,18,3,18,263,8,18,1,18,3,18,266,8,18,1,18,1,18,3,18,270,8,18,1,
  	18,1,18,3,18,274,8,18,1,19,1,19,3,19,278,8,19,1,19,1,19,1,19,3,19,283,
  	8,19,5,19,285,8,19,10,19,12,19,288,9,19,1,20,1,20,3,20,292,8,20,1,20,
  	1,20,1,21,1,21,3,21,298,8,21,1,21,1,21,1,22,1,22,3,22,304,8,22,1,22,1,
  	22,1,23,1,23,3,23,310,8,23,1,23,1,23,1,24,1,24,3,24,316,8,24,1,24,1,24,
  	1,25,1,25,3,25,322,8,25,1,25,1,25,1,26,1,26,3,26,328,8,26,1,26,1,26,1,
  	27,1,27,1,27,1,27,1,27,3,27,337,8,27,1,28,1,28,1,28,1,28,1,28,1,28,1,
  	28,3,28,346,8,28,4,28,348,8,28,11,28,12,28,349,1,29,1,29,3,29,354,8,29,
  	1,29,3,29,357,8,29,1,29,3,29,360,8,29,1,29,3,29,363,8,29,1,30,1,30,1,
  	30,1,30,1,30,1,30,5,30,371,8,30,10,30,12,30,374,9,30,1,31,4,31,377,8,
  	31,11,31,12,31,378,1,32,1,32,1,32,1,33,1,33,1,33,3,33,387,8,33,1,34,1,
  	34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,
  	34,1,34,1,34,5,34,407,8,34,10,34,12,34,410,9,34,1,35,1,35,1,35,1,35,1,
  	35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,
  	35,1,35,1,35,5,35,433,8,35,10,35,12,35,436,9,35,1,36,1,36,1,36,1,36,1,
  	36,1,36,1,36,1,36,1,36,5,36,447,8,36,10,36,12,36,450,9,36,1,37,1,37,1,
  	37,1,37,1,37,1,37,1,37,1,37,1,37,1,37,1,37,1,37,1,37,1,37,1,37,5,37,467,
  	8,37,10,37,12,37,470,9,37,1,38,1,38,1,38,1,38,1,38,3,38,477,8,38,1,39,
  	1,39,1,39,1,39,1,39,3,39,484,8,39,1,40,1,40,1,40,1,40,1,40,1,40,1,40,
  	3,40,493,8,40,1,41,1,41,3,41,497,8,41,1,42,1,42,1,42,3,42,502,8,42,1,
  	43,1,43,1,43,1,43,1,43,1,43,3,43,510,8,43,1,44,1,44,3,44,514,8,44,1,45,
  	1,45,1,46,1,46,1,46,1,46,1,46,3,46,523,8,46,1,46,1,46,1,46,3,46,528,8,
  	46,1,46,1,46,1,46,1,46,1,46,3,46,535,8,46,1,46,1,46,1,46,5,46,540,8,46,
  	10,46,12,46,543,9,46,3,46,545,8,46,1,47,1,47,1,47,1,47,1,47,5,47,552,
  	8,47,10,47,12,47,555,9,47,1,47,1,47,3,47,559,8,47,1,47,1,47,1,47,3,47,
  	564,8,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,3,47,573,8,47,3,47,575,8,
  	47,1,48,1,48,1,48,0,5,60,68,70,72,74,49,0,2,4,6,8,10,12,14,16,18,20,22,
  	24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,68,
  	70,72,74,76,78,80,82,84,86,88,90,92,94,96,0,3,1,0,47,52,1,0,74,75,1,0,
  	61,63,635,0,99,1,0,0,0,2,104,1,0,0,0,4,126,1,0,0,0,6,147,1,0,0,0,8,160,
  	1,0,0,0,10,163,1,0,0,0,12,167,1,0,0,0,14,171,1,0,0,0,16,177,1,0,0,0,18,
  	194,1,0,0,0,20,205,1,0,0,0,22,212,1,0,0,0,24,214,1,0,0,0,26,221,1,0,0,
  	0,28,226,1,0,0,0,30,236,1,0,0,0,32,238,1,0,0,0,34,248,1,0,0,0,36,260,
  	1,0,0,0,38,277,1,0,0,0,40,289,1,0,0,0,42,295,1,0,0,0,44,301,1,0,0,0,46,
  	307,1,0,0,0,48,313,1,0,0,0,50,319,1,0,0,0,52,325,1,0,0,0,54,336,1,0,0,
  	0,56,347,1,0,0,0,58,351,1,0,0,0,60,364,1,0,0,0,62,376,1,0,0,0,64,380,
  	1,0,0,0,66,383,1,0,0,0,68,388,1,0,0,0,70,411,1,0,0,0,72,437,1,0,0,0,74,
  	451,1,0,0,0,76,476,1,0,0,0,78,483,1,0,0,0,80,492,1,0,0,0,82,494,1,0,0,
  	0,84,501,1,0,0,0,86,509,1,0,0,0,88,513,1,0,0,0,90,515,1,0,0,0,92,544,
  	1,0,0,0,94,574,1,0,0,0,96,576,1,0,0,0,98,100,3,2,1,0,99,98,1,0,0,0,99,
  	100,1,0,0,0,100,101,1,0,0,0,101,102,5,0,0,1,102,1,1,0,0,0,103,105,3,4,
  	2,0,104,103,1,0,0,0,105,106,1,0,0,0,106,104,1,0,0,0,106,107,1,0,0,0,107,
  	3,1,0,0,0,108,109,3,6,3,0,109,110,5,38,0,0,110,127,1,0,0,0,111,112,3,
  	8,4,0,112,113,5,38,0,0,113,127,1,0,0,0,114,115,3,16,8,0,115,116,5,38,
  	0,0,116,127,1,0,0,0,117,118,3,18,9,0,118,119,5,38,0,0,119,127,1,0,0,0,
  	120,121,3,10,5,0,121,122,5,38,0,0,122,127,1,0,0,0,123,124,3,12,6,0,124,
  	125,5,38,0,0,125,127,1,0,0,0,126,108,1,0,0,0,126,111,1,0,0,0,126,114,
  	1,0,0,0,126,117,1,0,0,0,126,120,1,0,0,0,126,123,1,0,0,0,127,5,1,0,0,0,
  	128,129,5,40,0,0,129,132,3,22,11,0,130,131,5,1,0,0,131,133,3,84,42,0,
  	132,130,1,0,0,0,132,133,1,0,0,0,133,135,1,0,0,0,134,136,5,2,0,0,135,134,
  	1,0,0,0,135,136,1,0,0,0,136,137,1,0,0,0,137,138,3,68,34,0,138,148,1,0,
  	0,0,139,142,3,22,11,0,140,141,5,1,0,0,141,143,3,84,42,0,142,140,1,0,0,
  	0,142,143,1,0,0,0,143,144,1,0,0,0,144,145,5,3,0,0,145,146,3,68,34,0,146,
  	148,1,0,0,0,147,128,1,0,0,0,147,139,1,0,0,0,148,7,1,0,0,0,149,150,5,41,
  	0,0,150,152,3,22,11,0,151,153,5,2,0,0,152,151,1,0,0,0,152,153,1,0,0,0,
  	153,154,1,0,0,0,154,155,3,68,34,0,155,161,1,0,0,0,156,157,3,22,11,0,157,
  	158,5,4,0,0,158,159,3,68,34,0,159,161,1,0,0,0,160,149,1,0,0,0,160,156,
  	1,0,0,0,161,9,1,0,0,0,162,164,3,26,13,0,163,162,1,0,0,0,163,164,1,0,0,
  	0,164,165,1,0,0,0,165,166,3,68,34,0,166,11,1,0,0,0,167,168,3,58,29,0,
  	168,169,5,2,0,0,169,170,3,68,34,0,170,13,1,0,0,0,171,174,5,45,0,0,172,
  	175,3,58,29,0,173,175,3,44,22,0,174,172,1,0,0,0,174,173,1,0,0,0,175,15,
  	1,0,0,0,176,178,3,26,13,0,177,176,1,0,0,0,177,178,1,0,0,0,178,180,1,0,
  	0,0,179,181,3,14,7,0,180,179,1,0,0,0,180,181,1,0,0,0,181,183,1,0,0,0,
  	182,184,3,28,14,0,183,182,1,0,0,0,183,184,1,0,0,0,184,185,1,0,0,0,185,
  	186,5,42,0,0,186,187,3,96,48,0,187,190,3,48,24,0,188,189,5,1,0,0,189,
  	191,3,84,42,0,190,188,1,0,0,0,190,191,1,0,0,0,191,192,1,0,0,0,192,193,
  	3,52,26,0,193,17,1,0,0,0,194,196,5,46,0,0,195,197,3,68,34,0,196,195,1,
  	0,0,0,196,197,1,0,0,0,197,19,1,0,0,0,198,201,3,48,24,0,199,200,5,1,0,
  	0,200,202,3,84,42,0,201,199,1,0,0,0,201,202,1,0,0,0,202,203,1,0,0,0,203,
  	204,5,5,0,0,204,206,1,0,0,0,205,198,1,0,0,0,205,206,1,0,0,0,206,207,1,
  	0,0,0,207,208,3,52,26,0,208,21,1,0,0,0,209,213,3,96,48,0,210,213,3,42,
  	21,0,211,213,3,40,20,0,212,209,1,0,0,0,212,210,1,0,0,0,212,211,1,0,0,
  	0,213,23,1,0,0,0,214,215,5,6,0,0,215,216,3,22,11,0,216,25,1,0,0,0,217,
  	219,3,24,12,0,218,220,5,38,0,0,219,218,1,0,0,0,219,220,1,0,0,0,220,222,
  	1,0,0,0,221,217,1,0,0,0,222,223,1,0,0,0,223,221,1,0,0,0,223,224,1,0,0,
  	0,224,27,1,0,0,0,225,227,7,0,0,0,226,225,1,0,0,0,227,228,1,0,0,0,228,
  	226,1,0,0,0,228,229,1,0,0,0,229,29,1,0,0,0,230,237,3,68,34,0,231,232,
  	3,96,48,0,232,233,5,2,0,0,233,234,3,68,34,0,234,237,1,0,0,0,235,237,3,
  	64,32,0,236,230,1,0,0,0,236,231,1,0,0,0,236,235,1,0,0,0,237,31,1,0,0,
  	0,238,243,3,30,15,0,239,240,5,7,0,0,240,242,3,30,15,0,241,239,1,0,0,0,
  	242,245,1,0,0,0,243,241,1,0,0,0,243,244,1,0,0,0,244,33,1,0,0,0,245,243,
  	1,0,0,0,246,249,3,68,34,0,247,249,3,64,32,0,248,246,1,0,0,0,248,247,1,
  	0,0,0,249,257,1,0,0,0,250,253,5,7,0,0,251,254,3,68,34,0,252,254,3,64,
  	32,0,253,251,1,0,0,0,253,252,1,0,0,0,254,256,1,0,0,0,255,250,1,0,0,0,
  	256,259,1,0,0,0,257,255,1,0,0,0,257,258,1,0,0,0,258,35,1,0,0,0,259,257,
  	1,0,0,0,260,262,3,96,48,0,261,263,5,8,0,0,262,261,1,0,0,0,262,263,1,0,
  	0,0,263,265,1,0,0,0,264,266,3,24,12,0,265,264,1,0,0,0,265,266,1,0,0,0,
  	266,269,1,0,0,0,267,268,5,1,0,0,268,270,3,84,42,0,269,267,1,0,0,0,269,
  	270,1,0,0,0,270,273,1,0,0,0,271,272,5,2,0,0,272,274,3,68,34,0,273,271,
  	1,0,0,0,273,274,1,0,0,0,274,37,1,0,0,0,275,278,3,36,18,0,276,278,3,64,
  	32,0,277,275,1,0,0,0,277,276,1,0,0,0,278,286,1,0,0,0,279,282,5,7,0,0,
  	280,283,3,36,18,0,281,283,3,64,32,0,282,280,1,0,0,0,282,281,1,0,0,0,283,
  	285,1,0,0,0,284,279,1,0,0,0,285,288,1,0,0,0,286,284,1,0,0,0,286,287,1,
  	0,0,0,287,39,1,0,0,0,288,286,1,0,0,0,289,291,5,9,0,0,290,292,3,38,19,
  	0,291,290,1,0,0,0,291,292,1,0,0,0,292,293,1,0,0,0,293,294,5,10,0,0,294,
  	41,1,0,0,0,295,297,5,11,0,0,296,298,3,34,17,0,297,296,1,0,0,0,297,298,
  	1,0,0,0,298,299,1,0,0,0,299,300,5,12,0,0,300,43,1,0,0,0,301,303,5,13,
  	0,0,302,304,3,38,19,0,303,302,1,0,0,0,303,304,1,0,0,0,304,305,1,0,0,0,
  	305,306,5,14,0,0,306,45,1,0,0,0,307,309,5,13,0,0,308,310,3,32,16,0,309,
  	308,1,0,0,0,309,310,1,0,0,0,310,311,1,0,0,0,311,312,5,14,0,0,312,47,1,
  	0,0,0,313,315,5,15,0,0,314,316,3,38,19,0,315,314,1,0,0,0,315,316,1,0,
  	0,0,316,317,1,0,0,0,317,318,5,16,0,0,318,49,1,0,0,0,319,321,5,15,0,0,
  	320,322,3,32,16,0,321,320,1,0,0,0,321,322,1,0,0,0,322,323,1,0,0,0,323,
  	324,5,16,0,0,324,51,1,0,0,0,325,327,5,9,0,0,326,328,3,2,1,0,327,326,1,
  	0,0,0,327,328,1,0,0,0,328,329,1,0,0,0,329,330,5,10,0,0,330,53,1,0,0,0,
  	331,337,3,96,48,0,332,337,3,80,40,0,333,337,3,42,21,0,334,337,3,40,20,
  	0,335,337,3,20,10,0,336,331,1,0,0,0,336,332,1,0,0,0,336,333,1,0,0,0,336,
  	334,1,0,0,0,336,335,1,0,0,0,337,55,1,0,0,0,338,345,5,17,0,0,339,346,3,
  	96,48,0,340,346,5,74,0,0,341,342,5,15,0,0,342,343,3,68,34,0,343,344,5,
  	16,0,0,344,346,1,0,0,0,345,339,1,0,0,0,345,340,1,0,0,0,345,341,1,0,0,
  	0,346,348,1,0,0,0,347,338,1,0,0,0,348,349,1,0,0,0,349,347,1,0,0,0,349,
  	350,1,0,0,0,350,57,1,0,0,0,351,353,3,54,27,0,352,354,3,56,28,0,353,352,
  	1,0,0,0,353,354,1,0,0,0,354,356,1,0,0,0,355,357,3,46,23,0,356,355,1,0,
  	0,0,356,357,1,0,0,0,357,359,1,0,0,0,358,360,3,50,25,0,359,358,1,0,0,0,
  	359,360,1,0,0,0,360,362,1,0,0,0,361,363,3,24,12,0,362,361,1,0,0,0,362,
  	363,1,0,0,0,363,59,1,0,0,0,364,365,6,30,-1,0,365,366,3,58,29,0,366,372,
  	1,0,0,0,367,368,10,2,0,0,368,369,5,18,0,0,369,371,3,58,29,0,370,367,1,
  	0,0,0,371,374,1,0,0,0,372,370,1,0,0,0,372,373,1,0,0,0,373,61,1,0,0,0,
  	374,372,1,0,0,0,375,377,3,60,30,0,376,375,1,0,0,0,377,378,1,0,0,0,378,
  	376,1,0,0,0,378,379,1,0,0,0,379,63,1,0,0,0,380,381,5,19,0,0,381,382,3,
  	58,29,0,382,65,1,0,0,0,383,386,3,62,31,0,384,385,5,39,0,0,385,387,3,84,
  	42,0,386,384,1,0,0,0,386,387,1,0,0,0,387,67,1,0,0,0,388,389,6,34,-1,0,
  	389,390,3,70,35,0,390,408,1,0,0,0,391,392,10,5,0,0,392,393,5,20,0,0,393,
  	407,3,70,35,0,394,395,10,4,0,0,395,396,5,21,0,0,396,407,3,70,35,0,397,
  	398,10,3,0,0,398,399,5,22,0,0,399,407,3,70,35,0,400,401,10,2,0,0,401,
  	402,5,23,0,0,402,407,3,70,35,0,403,404,10,1,0,0,404,405,5,24,0,0,405,
  	407,3,70,35,0,406,391,1,0,0,0,406,394,1,0,0,0,406,397,1,0,0,0,406,400,
  	1,0,0,0,406,403,1,0,0,0,407,410,1,0,0,0,408,406,1,0,0,0,408,409,1,0,0,
  	0,409,69,1,0,0,0,410,408,1,0,0,0,411,412,6,35,-1,0,412,413,3,72,36,0,
  	413,434,1,0,0,0,414,415,10,6,0,0,415,416,5,25,0,0,416,433,3,72,36,0,417,
  	418,10,5,0,0,418,419,5,26,0,0,419,433,3,72,36,0,420,421,10,4,0,0,421,
  	422,5,27,0,0,422,433,3,72,36,0,423,424,10,3,0,0,424,425,5,28,0,0,425,
  	433,3,72,36,0,426,427,10,2,0,0,427,428,5,29,0,0,428,433,3,72,36,0,429,
  	430,10,1,0,0,430,431,5,30,0,0,431,433,3,72,36,0,432,414,1,0,0,0,432,417,
  	1,0,0,0,432,420,1,0,0,0,432,423,1,0,0,0,432,426,1,0,0,0,432,429,1,0,0,
  	0,433,436,1,0,0,0,434,432,1,0,0,0,434,435,1,0,0,0,435,71,1,0,0,0,436,
  	434,1,0,0,0,437,438,6,36,-1,0,438,439,3,74,37,0,439,448,1,0,0,0,440,441,
  	10,2,0,0,441,442,5,31,0,0,442,447,3,74,37,0,443,444,10,1,0,0,444,445,
  	5,32,0,0,445,447,3,74,37,0,446,440,1,0,0,0,446,443,1,0,0,0,447,450,1,
  	0,0,0,448,446,1,0,0,0,448,449,1,0,0,0,449,73,1,0,0,0,450,448,1,0,0,0,
  	451,452,6,37,-1,0,452,453,3,76,38,0,453,468,1,0,0,0,454,455,10,4,0,0,
  	455,456,5,33,0,0,456,467,3,76,38,0,457,458,10,3,0,0,458,459,5,34,0,0,
  	459,467,3,76,38,0,460,461,10,2,0,0,461,462,5,35,0,0,462,467,3,76,38,0,
  	463,464,10,1,0,0,464,465,5,36,0,0,465,467,3,76,38,0,466,454,1,0,0,0,466,
  	457,1,0,0,0,466,460,1,0,0,0,466,463,1,0,0,0,467,470,1,0,0,0,468,466,1,
  	0,0,0,468,469,1,0,0,0,469,75,1,0,0,0,470,468,1,0,0,0,471,477,3,78,39,
  	0,472,473,5,37,0,0,473,477,3,78,39,0,474,475,5,32,0,0,475,477,3,78,39,
  	0,476,471,1,0,0,0,476,472,1,0,0,0,476,474,1,0,0,0,477,77,1,0,0,0,478,
  	484,3,66,33,0,479,480,5,15,0,0,480,481,3,68,34,0,481,482,5,16,0,0,482,
  	484,1,0,0,0,483,478,1,0,0,0,483,479,1,0,0,0,484,79,1,0,0,0,485,493,3,
  	82,41,0,486,493,5,72,0,0,487,493,5,69,0,0,488,493,5,73,0,0,489,493,5,
  	53,0,0,490,493,5,54,0,0,491,493,5,55,0,0,492,485,1,0,0,0,492,486,1,0,
  	0,0,492,487,1,0,0,0,492,488,1,0,0,0,492,489,1,0,0,0,492,490,1,0,0,0,492,
  	491,1,0,0,0,493,81,1,0,0,0,494,496,7,1,0,0,495,497,5,71,0,0,496,495,1,
  	0,0,0,496,497,1,0,0,0,497,83,1,0,0,0,498,502,3,86,43,0,499,502,3,96,48,
  	0,500,502,5,56,0,0,501,498,1,0,0,0,501,499,1,0,0,0,501,500,1,0,0,0,502,
  	85,1,0,0,0,503,510,5,57,0,0,504,510,5,58,0,0,505,510,5,59,0,0,506,510,
  	5,60,0,0,507,510,3,88,44,0,508,510,3,94,47,0,509,503,1,0,0,0,509,504,
  	1,0,0,0,509,505,1,0,0,0,509,506,1,0,0,0,509,507,1,0,0,0,509,508,1,0,0,
  	0,510,87,1,0,0,0,511,514,3,90,45,0,512,514,3,92,46,0,513,511,1,0,0,0,
  	513,512,1,0,0,0,514,89,1,0,0,0,515,516,7,2,0,0,516,91,1,0,0,0,517,522,
  	5,64,0,0,518,519,5,13,0,0,519,520,3,90,45,0,520,521,5,14,0,0,521,523,
  	1,0,0,0,522,518,1,0,0,0,522,523,1,0,0,0,523,527,1,0,0,0,524,525,5,11,
  	0,0,525,526,5,74,0,0,526,528,5,12,0,0,527,524,1,0,0,0,527,528,1,0,0,0,
  	528,545,1,0,0,0,529,534,5,65,0,0,530,531,5,13,0,0,531,532,3,90,45,0,532,
  	533,5,14,0,0,533,535,1,0,0,0,534,530,1,0,0,0,534,535,1,0,0,0,535,541,
  	1,0,0,0,536,537,5,11,0,0,537,538,5,74,0,0,538,540,5,12,0,0,539,536,1,
  	0,0,0,540,543,1,0,0,0,541,539,1,0,0,0,541,542,1,0,0,0,542,545,1,0,0,0,
  	543,541,1,0,0,0,544,517,1,0,0,0,544,529,1,0,0,0,545,93,1,0,0,0,546,558,
  	5,66,0,0,547,548,5,13,0,0,548,553,3,84,42,0,549,550,5,7,0,0,550,552,3,
  	84,42,0,551,549,1,0,0,0,552,555,1,0,0,0,553,551,1,0,0,0,553,554,1,0,0,
  	0,554,556,1,0,0,0,555,553,1,0,0,0,556,557,5,14,0,0,557,559,1,0,0,0,558,
  	547,1,0,0,0,558,559,1,0,0,0,559,563,1,0,0,0,560,561,5,11,0,0,561,562,
  	5,74,0,0,562,564,5,12,0,0,563,560,1,0,0,0,563,564,1,0,0,0,564,575,1,0,
  	0,0,565,572,5,67,0,0,566,567,5,13,0,0,567,568,3,84,42,0,568,569,5,7,0,
  	0,569,570,3,84,42,0,570,571,5,14,0,0,571,573,1,0,0,0,572,566,1,0,0,0,
  	572,573,1,0,0,0,573,575,1,0,0,0,574,546,1,0,0,0,574,565,1,0,0,0,575,95,
  	1,0,0,0,576,577,5,70,0,0,577,97,1,0,0,0,76,99,106,126,132,135,142,147,
  	152,160,163,174,177,180,183,190,196,201,205,212,219,223,228,236,243,248,
  	253,257,262,265,269,273,277,282,286,291,297,303,309,315,321,327,336,345,
  	349,353,356,359,362,372,378,386,406,408,432,434,446,448,466,468,476,483,
  	492,496,501,509,513,522,527,534,541,544,553,558,563,572,574
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
    setState(99);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 72030247981189696) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 123) != 0)) {
      setState(98);
      stmtList();
    }
    setState(101);
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
    enterOuterAlt(_localctx, 1);
    setState(104); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(103);
      stmt();
      setState(106); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 72030247981189696) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 123) != 0));
   
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

tree::TerminalNode* OpenCMLParser::StmtContext::SEP() {
  return getToken(OpenCMLParser::SEP, 0);
}

OpenCMLParser::UseStmtContext* OpenCMLParser::StmtContext::useStmt() {
  return getRuleContext<OpenCMLParser::UseStmtContext>(0);
}

OpenCMLParser::FuncDefContext* OpenCMLParser::StmtContext::funcDef() {
  return getRuleContext<OpenCMLParser::FuncDefContext>(0);
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
    setState(126);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 2, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(108);
      letStmt();
      setState(109);
      match(OpenCMLParser::SEP);
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(111);
      useStmt();
      setState(112);
      match(OpenCMLParser::SEP);
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(114);
      funcDef();
      setState(115);
      match(OpenCMLParser::SEP);
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(117);
      retStmt();
      setState(118);
      match(OpenCMLParser::SEP);
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(120);
      exprStmt();
      setState(121);
      match(OpenCMLParser::SEP);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(123);
      assignStmt();
      setState(124);
      match(OpenCMLParser::SEP);
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
    setState(147);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::LET: {
        enterOuterAlt(_localctx, 1);
        setState(128);
        match(OpenCMLParser::LET);
        setState(129);
        carrier();
        setState(132);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(130);
          match(OpenCMLParser::T__0);
          setState(131);
          type();
        }
        setState(135);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__1) {
          setState(134);
          match(OpenCMLParser::T__1);
        }
        setState(137);
        expr(0);
        break;
      }

      case OpenCMLParser::T__8:
      case OpenCMLParser::T__10:
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
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
        setState(144);
        match(OpenCMLParser::T__2);
        setState(145);
        expr(0);
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
    setState(160);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::USE: {
        enterOuterAlt(_localctx, 1);
        setState(149);
        match(OpenCMLParser::USE);
        setState(150);
        carrier();
        setState(152);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__1) {
          setState(151);
          match(OpenCMLParser::T__1);
        }
        setState(154);
        expr(0);
        break;
      }

      case OpenCMLParser::T__8:
      case OpenCMLParser::T__10:
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(156);
        carrier();
        setState(157);
        match(OpenCMLParser::T__3);
        setState(158);
        expr(0);
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

//----------------- ExprStmtContext ------------------------------------------------------------------

OpenCMLParser::ExprStmtContext::ExprStmtContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::ExprContext* OpenCMLParser::ExprStmtContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
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
  enterRule(_localctx, 10, OpenCMLParser::RuleExprStmt);
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
    setState(163);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(162);
      annotations();
    }
    setState(165);
    expr(0);
   
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

OpenCMLParser::EntityContext* OpenCMLParser::AssignStmtContext::entity() {
  return getRuleContext<OpenCMLParser::EntityContext>(0);
}

OpenCMLParser::ExprContext* OpenCMLParser::AssignStmtContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
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
  enterRule(_localctx, 12, OpenCMLParser::RuleAssignStmt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(167);
    entity();
    setState(168);
    match(OpenCMLParser::T__1);
    setState(169);
    expr(0);
   
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

OpenCMLParser::EntityContext* OpenCMLParser::WithDefContext::entity() {
  return getRuleContext<OpenCMLParser::EntityContext>(0);
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
  enterRule(_localctx, 14, OpenCMLParser::RuleWithDef);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(171);
    match(OpenCMLParser::WITH);
    setState(174);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__8:
      case OpenCMLParser::T__10:
      case OpenCMLParser::T__14:
      case OpenCMLParser::NULL_:
      case OpenCMLParser::TRUE:
      case OpenCMLParser::FALSE:
      case OpenCMLParser::MULTI_STR:
      case OpenCMLParser::IDENTIFIER:
      case OpenCMLParser::STRING:
      case OpenCMLParser::FSTRING:
      case OpenCMLParser::INTEGER:
      case OpenCMLParser::REAL: {
        setState(172);
        entity();
        break;
      }

      case OpenCMLParser::T__12: {
        setState(173);
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

tree::TerminalNode* OpenCMLParser::FuncDefContext::FUNC() {
  return getToken(OpenCMLParser::FUNC, 0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::FuncDefContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::ArgsDeclContext* OpenCMLParser::FuncDefContext::argsDecl() {
  return getRuleContext<OpenCMLParser::ArgsDeclContext>(0);
}

OpenCMLParser::StmtPackContext* OpenCMLParser::FuncDefContext::stmtPack() {
  return getRuleContext<OpenCMLParser::StmtPackContext>(0);
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
  enterRule(_localctx, 16, OpenCMLParser::RuleFuncDef);
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
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(176);
      annotations();
    }
    setState(180);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WITH) {
      setState(179);
      withDef();
    }
    setState(183);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8866461766385664) != 0)) {
      setState(182);
      modifiers();
    }
    setState(185);
    match(OpenCMLParser::FUNC);
    setState(186);
    identRef();
    setState(187);
    argsDecl();
    setState(190);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(188);
      match(OpenCMLParser::T__0);
      setState(189);
      type();
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
    setState(194);
    match(OpenCMLParser::RETURN);
    setState(196);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 63050536517143040) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 123) != 0)) {
      setState(195);
      expr(0);
    }
   
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

OpenCMLParser::ArgsDeclContext* OpenCMLParser::LambdaContext::argsDecl() {
  return getRuleContext<OpenCMLParser::ArgsDeclContext>(0);
}

OpenCMLParser::TypeContext* OpenCMLParser::LambdaContext::type() {
  return getRuleContext<OpenCMLParser::TypeContext>(0);
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
  enterRule(_localctx, 20, OpenCMLParser::RuleLambda);
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
    setState(205);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__14) {
      setState(198);
      argsDecl();
      setState(201);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__0) {
        setState(199);
        match(OpenCMLParser::T__0);
        setState(200);
        type();
      }
      setState(203);
      match(OpenCMLParser::T__4);
    }
    setState(207);
    stmtPack();
   
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

OpenCMLParser::ListPackContext* OpenCMLParser::CarrierContext::listPack() {
  return getRuleContext<OpenCMLParser::ListPackContext>(0);
}

OpenCMLParser::DictPackContext* OpenCMLParser::CarrierContext::dictPack() {
  return getRuleContext<OpenCMLParser::DictPackContext>(0);
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
  enterRule(_localctx, 22, OpenCMLParser::RuleCarrier);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(212);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(209);
        identRef();
        break;
      }

      case OpenCMLParser::T__10: {
        enterOuterAlt(_localctx, 2);
        setState(210);
        listPack();
        break;
      }

      case OpenCMLParser::T__8: {
        enterOuterAlt(_localctx, 3);
        setState(211);
        dictPack();
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
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::CarrierContext* OpenCMLParser::AnnotationContext::carrier() {
  return getRuleContext<OpenCMLParser::CarrierContext>(0);
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
  enterRule(_localctx, 24, OpenCMLParser::RuleAnnotation);

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
    match(OpenCMLParser::T__5);
    setState(215);
    carrier();
   
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
  enterRule(_localctx, 26, OpenCMLParser::RuleAnnotations);
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
    setState(221); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(217);
      annotation();
      setState(219);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::SEP) {
        setState(218);
        match(OpenCMLParser::SEP);
      }
      setState(223); 
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
  enterRule(_localctx, 28, OpenCMLParser::RuleModifiers);
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
    setState(226); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(225);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8866461766385664) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(228); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8866461766385664) != 0));
   
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

OpenCMLParser::ExprContext* OpenCMLParser::ArgumentContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::ArgumentContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::EntityUnpackContext* OpenCMLParser::ArgumentContext::entityUnpack() {
  return getRuleContext<OpenCMLParser::EntityUnpackContext>(0);
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
  enterRule(_localctx, 30, OpenCMLParser::RuleArgument);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(236);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 22, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(230);
      expr(0);
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(231);
      identRef();
      setState(232);
      match(OpenCMLParser::T__1);
      setState(233);
      expr(0);
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(235);
      entityUnpack();
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
  enterRule(_localctx, 32, OpenCMLParser::RuleArgsList);
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
    argument();
    setState(243);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__6) {
      setState(239);
      match(OpenCMLParser::T__6);
      setState(240);
      argument();
      setState(245);
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

//----------------- ExprListContext ------------------------------------------------------------------

OpenCMLParser::ExprListContext::ExprListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::ExprContext *> OpenCMLParser::ExprListContext::expr() {
  return getRuleContexts<OpenCMLParser::ExprContext>();
}

OpenCMLParser::ExprContext* OpenCMLParser::ExprListContext::expr(size_t i) {
  return getRuleContext<OpenCMLParser::ExprContext>(i);
}

std::vector<OpenCMLParser::EntityUnpackContext *> OpenCMLParser::ExprListContext::entityUnpack() {
  return getRuleContexts<OpenCMLParser::EntityUnpackContext>();
}

OpenCMLParser::EntityUnpackContext* OpenCMLParser::ExprListContext::entityUnpack(size_t i) {
  return getRuleContext<OpenCMLParser::EntityUnpackContext>(i);
}


size_t OpenCMLParser::ExprListContext::getRuleIndex() const {
  return OpenCMLParser::RuleExprList;
}


std::any OpenCMLParser::ExprListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitExprList(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ExprListContext* OpenCMLParser::exprList() {
  ExprListContext *_localctx = _tracker.createInstance<ExprListContext>(_ctx, getState());
  enterRule(_localctx, 34, OpenCMLParser::RuleExprList);
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
    switch (_input->LA(1)) {
      case OpenCMLParser::T__8:
      case OpenCMLParser::T__10:
      case OpenCMLParser::T__14:
      case OpenCMLParser::T__31:
      case OpenCMLParser::T__36:
      case OpenCMLParser::NULL_:
      case OpenCMLParser::TRUE:
      case OpenCMLParser::FALSE:
      case OpenCMLParser::MULTI_STR:
      case OpenCMLParser::IDENTIFIER:
      case OpenCMLParser::STRING:
      case OpenCMLParser::FSTRING:
      case OpenCMLParser::INTEGER:
      case OpenCMLParser::REAL: {
        setState(246);
        expr(0);
        break;
      }

      case OpenCMLParser::T__18: {
        setState(247);
        entityUnpack();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(257);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__6) {
      setState(250);
      match(OpenCMLParser::T__6);
      setState(253);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case OpenCMLParser::T__8:
        case OpenCMLParser::T__10:
        case OpenCMLParser::T__14:
        case OpenCMLParser::T__31:
        case OpenCMLParser::T__36:
        case OpenCMLParser::NULL_:
        case OpenCMLParser::TRUE:
        case OpenCMLParser::FALSE:
        case OpenCMLParser::MULTI_STR:
        case OpenCMLParser::IDENTIFIER:
        case OpenCMLParser::STRING:
        case OpenCMLParser::FSTRING:
        case OpenCMLParser::INTEGER:
        case OpenCMLParser::REAL: {
          setState(251);
          expr(0);
          break;
        }

        case OpenCMLParser::T__18: {
          setState(252);
          entityUnpack();
          break;
        }

      default:
        throw NoViableAltException(this);
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

//----------------- KeyValExprContext ------------------------------------------------------------------

OpenCMLParser::KeyValExprContext::KeyValExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::KeyValExprContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::AnnotationContext* OpenCMLParser::KeyValExprContext::annotation() {
  return getRuleContext<OpenCMLParser::AnnotationContext>(0);
}

OpenCMLParser::TypeContext* OpenCMLParser::KeyValExprContext::type() {
  return getRuleContext<OpenCMLParser::TypeContext>(0);
}

OpenCMLParser::ExprContext* OpenCMLParser::KeyValExprContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
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
  enterRule(_localctx, 36, OpenCMLParser::RuleKeyValExpr);
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
    setState(260);
    identRef();
    setState(262);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(261);
      match(OpenCMLParser::T__7);
    }
    setState(265);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(264);
      annotation();
    }
    setState(269);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(267);
      match(OpenCMLParser::T__0);
      setState(268);
      type();
    }
    setState(273);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(271);
      match(OpenCMLParser::T__1);
      setState(272);
      expr(0);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- KeyValListContext ------------------------------------------------------------------

OpenCMLParser::KeyValListContext::KeyValListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::KeyValExprContext *> OpenCMLParser::KeyValListContext::keyValExpr() {
  return getRuleContexts<OpenCMLParser::KeyValExprContext>();
}

OpenCMLParser::KeyValExprContext* OpenCMLParser::KeyValListContext::keyValExpr(size_t i) {
  return getRuleContext<OpenCMLParser::KeyValExprContext>(i);
}

std::vector<OpenCMLParser::EntityUnpackContext *> OpenCMLParser::KeyValListContext::entityUnpack() {
  return getRuleContexts<OpenCMLParser::EntityUnpackContext>();
}

OpenCMLParser::EntityUnpackContext* OpenCMLParser::KeyValListContext::entityUnpack(size_t i) {
  return getRuleContext<OpenCMLParser::EntityUnpackContext>(i);
}


size_t OpenCMLParser::KeyValListContext::getRuleIndex() const {
  return OpenCMLParser::RuleKeyValList;
}


std::any OpenCMLParser::KeyValListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitKeyValList(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::KeyValListContext* OpenCMLParser::keyValList() {
  KeyValListContext *_localctx = _tracker.createInstance<KeyValListContext>(_ctx, getState());
  enterRule(_localctx, 38, OpenCMLParser::RuleKeyValList);
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
    setState(277);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        setState(275);
        keyValExpr();
        break;
      }

      case OpenCMLParser::T__18: {
        setState(276);
        entityUnpack();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(286);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__6) {
      setState(279);
      match(OpenCMLParser::T__6);
      setState(282);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case OpenCMLParser::IDENTIFIER: {
          setState(280);
          keyValExpr();
          break;
        }

        case OpenCMLParser::T__18: {
          setState(281);
          entityUnpack();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(288);
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

//----------------- DictPackContext ------------------------------------------------------------------

OpenCMLParser::DictPackContext::DictPackContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::KeyValListContext* OpenCMLParser::DictPackContext::keyValList() {
  return getRuleContext<OpenCMLParser::KeyValListContext>(0);
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
  enterRule(_localctx, 40, OpenCMLParser::RuleDictPack);
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
    setState(289);
    match(OpenCMLParser::T__8);
    setState(291);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__18

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(290);
      keyValList();
    }
    setState(293);
    match(OpenCMLParser::T__9);
   
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

OpenCMLParser::ExprListContext* OpenCMLParser::ListPackContext::exprList() {
  return getRuleContext<OpenCMLParser::ExprListContext>(0);
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
  enterRule(_localctx, 42, OpenCMLParser::RuleListPack);
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
    setState(295);
    match(OpenCMLParser::T__10);
    setState(297);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 63050536517667328) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 123) != 0)) {
      setState(296);
      exprList();
    }
    setState(299);
    match(OpenCMLParser::T__11);
   
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

OpenCMLParser::KeyValListContext* OpenCMLParser::WithDeclContext::keyValList() {
  return getRuleContext<OpenCMLParser::KeyValListContext>(0);
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
  enterRule(_localctx, 44, OpenCMLParser::RuleWithDecl);
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
    setState(301);
    match(OpenCMLParser::T__12);
    setState(303);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__18

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(302);
      keyValList();
    }
    setState(305);
    match(OpenCMLParser::T__13);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- WithPackContext ------------------------------------------------------------------

OpenCMLParser::WithPackContext::WithPackContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::ArgsListContext* OpenCMLParser::WithPackContext::argsList() {
  return getRuleContext<OpenCMLParser::ArgsListContext>(0);
}


size_t OpenCMLParser::WithPackContext::getRuleIndex() const {
  return OpenCMLParser::RuleWithPack;
}


std::any OpenCMLParser::WithPackContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitWithPack(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::WithPackContext* OpenCMLParser::withPack() {
  WithPackContext *_localctx = _tracker.createInstance<WithPackContext>(_ctx, getState());
  enterRule(_localctx, 46, OpenCMLParser::RuleWithPack);
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
    setState(307);
    match(OpenCMLParser::T__12);
    setState(309);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 63050536517667328) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 123) != 0)) {
      setState(308);
      argsList();
    }
    setState(311);
    match(OpenCMLParser::T__13);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ArgsDeclContext ------------------------------------------------------------------

OpenCMLParser::ArgsDeclContext::ArgsDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::KeyValListContext* OpenCMLParser::ArgsDeclContext::keyValList() {
  return getRuleContext<OpenCMLParser::KeyValListContext>(0);
}


size_t OpenCMLParser::ArgsDeclContext::getRuleIndex() const {
  return OpenCMLParser::RuleArgsDecl;
}


std::any OpenCMLParser::ArgsDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitArgsDecl(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ArgsDeclContext* OpenCMLParser::argsDecl() {
  ArgsDeclContext *_localctx = _tracker.createInstance<ArgsDeclContext>(_ctx, getState());
  enterRule(_localctx, 48, OpenCMLParser::RuleArgsDecl);
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
    setState(313);
    match(OpenCMLParser::T__14);
    setState(315);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__18

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(314);
      keyValList();
    }
    setState(317);
    match(OpenCMLParser::T__15);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ArgsPackContext ------------------------------------------------------------------

OpenCMLParser::ArgsPackContext::ArgsPackContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::ArgsListContext* OpenCMLParser::ArgsPackContext::argsList() {
  return getRuleContext<OpenCMLParser::ArgsListContext>(0);
}


size_t OpenCMLParser::ArgsPackContext::getRuleIndex() const {
  return OpenCMLParser::RuleArgsPack;
}


std::any OpenCMLParser::ArgsPackContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitArgsPack(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::ArgsPackContext* OpenCMLParser::argsPack() {
  ArgsPackContext *_localctx = _tracker.createInstance<ArgsPackContext>(_ctx, getState());
  enterRule(_localctx, 50, OpenCMLParser::RuleArgsPack);
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
    match(OpenCMLParser::T__14);
    setState(321);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 63050536517667328) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 123) != 0)) {
      setState(320);
      argsList();
    }
    setState(323);
    match(OpenCMLParser::T__15);
   
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
  enterRule(_localctx, 52, OpenCMLParser::RuleStmtPack);
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
    match(OpenCMLParser::T__8);
    setState(327);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 72030247981189696) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 123) != 0)) {
      setState(326);
      stmtList();
    }
    setState(329);
    match(OpenCMLParser::T__9);
   
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
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::PrimEntityContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::LiteralContext* OpenCMLParser::PrimEntityContext::literal() {
  return getRuleContext<OpenCMLParser::LiteralContext>(0);
}

OpenCMLParser::ListPackContext* OpenCMLParser::PrimEntityContext::listPack() {
  return getRuleContext<OpenCMLParser::ListPackContext>(0);
}

OpenCMLParser::DictPackContext* OpenCMLParser::PrimEntityContext::dictPack() {
  return getRuleContext<OpenCMLParser::DictPackContext>(0);
}

OpenCMLParser::LambdaContext* OpenCMLParser::PrimEntityContext::lambda() {
  return getRuleContext<OpenCMLParser::LambdaContext>(0);
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
  enterRule(_localctx, 54, OpenCMLParser::RulePrimEntity);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(336);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 41, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(331);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(332);
      literal();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(333);
      listPack();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(334);
      dictPack();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(335);
      lambda();
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
  : ParserRuleContext(parent, invokingState) {
}

std::vector<OpenCMLParser::IdentRefContext *> OpenCMLParser::MemberAccessContext::identRef() {
  return getRuleContexts<OpenCMLParser::IdentRefContext>();
}

OpenCMLParser::IdentRefContext* OpenCMLParser::MemberAccessContext::identRef(size_t i) {
  return getRuleContext<OpenCMLParser::IdentRefContext>(i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::MemberAccessContext::INTEGER() {
  return getTokens(OpenCMLParser::INTEGER);
}

tree::TerminalNode* OpenCMLParser::MemberAccessContext::INTEGER(size_t i) {
  return getToken(OpenCMLParser::INTEGER, i);
}

std::vector<OpenCMLParser::ExprContext *> OpenCMLParser::MemberAccessContext::expr() {
  return getRuleContexts<OpenCMLParser::ExprContext>();
}

OpenCMLParser::ExprContext* OpenCMLParser::MemberAccessContext::expr(size_t i) {
  return getRuleContext<OpenCMLParser::ExprContext>(i);
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
  enterRule(_localctx, 56, OpenCMLParser::RuleMemberAccess);

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
    setState(347); 
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(338);
              match(OpenCMLParser::T__16);
              setState(345);
              _errHandler->sync(this);
              switch (_input->LA(1)) {
                case OpenCMLParser::IDENTIFIER: {
                  setState(339);
                  identRef();
                  break;
                }

                case OpenCMLParser::INTEGER: {
                  setState(340);
                  match(OpenCMLParser::INTEGER);
                  break;
                }

                case OpenCMLParser::T__14: {
                  setState(341);
                  match(OpenCMLParser::T__14);
                  setState(342);
                  expr(0);
                  setState(343);
                  match(OpenCMLParser::T__15);
                  break;
                }

              default:
                throw NoViableAltException(this);
              }
              break;
            }

      default:
        throw NoViableAltException(this);
      }
      setState(349); 
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 43, _ctx);
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

OpenCMLParser::PrimEntityContext* OpenCMLParser::EntityContext::primEntity() {
  return getRuleContext<OpenCMLParser::PrimEntityContext>(0);
}

OpenCMLParser::MemberAccessContext* OpenCMLParser::EntityContext::memberAccess() {
  return getRuleContext<OpenCMLParser::MemberAccessContext>(0);
}

OpenCMLParser::WithPackContext* OpenCMLParser::EntityContext::withPack() {
  return getRuleContext<OpenCMLParser::WithPackContext>(0);
}

OpenCMLParser::ArgsPackContext* OpenCMLParser::EntityContext::argsPack() {
  return getRuleContext<OpenCMLParser::ArgsPackContext>(0);
}

OpenCMLParser::AnnotationContext* OpenCMLParser::EntityContext::annotation() {
  return getRuleContext<OpenCMLParser::AnnotationContext>(0);
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
  enterRule(_localctx, 58, OpenCMLParser::RuleEntity);

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
    primEntity();
    setState(353);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 44, _ctx)) {
    case 1: {
      setState(352);
      memberAccess();
      break;
    }

    default:
      break;
    }
    setState(356);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 45, _ctx)) {
    case 1: {
      setState(355);
      withPack();
      break;
    }

    default:
      break;
    }
    setState(359);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 46, _ctx)) {
    case 1: {
      setState(358);
      argsPack();
      break;
    }

    default:
      break;
    }
    setState(362);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 47, _ctx)) {
    case 1: {
      setState(361);
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

//----------------- EntityLinkContext ------------------------------------------------------------------

OpenCMLParser::EntityLinkContext::EntityLinkContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::EntityContext* OpenCMLParser::EntityLinkContext::entity() {
  return getRuleContext<OpenCMLParser::EntityContext>(0);
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
  size_t startState = 60;
  enterRecursionRule(_localctx, 60, OpenCMLParser::RuleEntityLink, precedence);

    

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
    setState(365);
    entity();
    _ctx->stop = _input->LT(-1);
    setState(372);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 48, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EntityLinkContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEntityLink);
        setState(367);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(368);
        match(OpenCMLParser::T__17);
        setState(369);
        entity(); 
      }
      setState(374);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 48, _ctx);
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
  enterRule(_localctx, 62, OpenCMLParser::RuleEntityChain);

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
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(375);
              entityLink(0);
              break;
            }

      default:
        throw NoViableAltException(this);
      }
      setState(378); 
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 49, _ctx);
    } while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- EntityUnpackContext ------------------------------------------------------------------

OpenCMLParser::EntityUnpackContext::EntityUnpackContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::EntityContext* OpenCMLParser::EntityUnpackContext::entity() {
  return getRuleContext<OpenCMLParser::EntityContext>(0);
}


size_t OpenCMLParser::EntityUnpackContext::getRuleIndex() const {
  return OpenCMLParser::RuleEntityUnpack;
}


std::any OpenCMLParser::EntityUnpackContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEntityUnpack(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::EntityUnpackContext* OpenCMLParser::entityUnpack() {
  EntityUnpackContext *_localctx = _tracker.createInstance<EntityUnpackContext>(_ctx, getState());
  enterRule(_localctx, 64, OpenCMLParser::RuleEntityUnpack);

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
    match(OpenCMLParser::T__18);
    setState(381);
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
  : ParserRuleContext(parent, invokingState) {
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
  enterRule(_localctx, 66, OpenCMLParser::RuleEntityExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(383);
    entityChain();
    setState(386);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 50, _ctx)) {
    case 1: {
      setState(384);
      match(OpenCMLParser::AS);
      setState(385);
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

//----------------- ExprContext ------------------------------------------------------------------

OpenCMLParser::ExprContext::ExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::RelaExprContext* OpenCMLParser::ExprContext::relaExpr() {
  return getRuleContext<OpenCMLParser::RelaExprContext>(0);
}

OpenCMLParser::ExprContext* OpenCMLParser::ExprContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
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
   return expr(0);
}

OpenCMLParser::ExprContext* OpenCMLParser::expr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::ExprContext *_localctx = _tracker.createInstance<ExprContext>(_ctx, parentState);
  OpenCMLParser::ExprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 68;
  enterRecursionRule(_localctx, 68, OpenCMLParser::RuleExpr, precedence);

    

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
    setState(389);
    relaExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(408);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 52, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(406);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 51, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<ExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleExpr);
          setState(391);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(392);
          match(OpenCMLParser::T__19);
          setState(393);
          relaExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<ExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleExpr);
          setState(394);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(395);
          match(OpenCMLParser::T__20);
          setState(396);
          relaExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<ExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleExpr);
          setState(397);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(398);
          match(OpenCMLParser::T__21);
          setState(399);
          relaExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<ExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleExpr);
          setState(400);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(401);
          match(OpenCMLParser::T__22);
          setState(402);
          relaExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<ExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleExpr);
          setState(403);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(404);
          match(OpenCMLParser::T__23);
          setState(405);
          relaExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(410);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 52, _ctx);
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
  size_t startState = 70;
  enterRecursionRule(_localctx, 70, OpenCMLParser::RuleRelaExpr, precedence);

    

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
    setState(412);
    addExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(434);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 54, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(432);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 53, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(414);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(415);
          match(OpenCMLParser::T__24);
          setState(416);
          addExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(417);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(418);
          match(OpenCMLParser::T__25);
          setState(419);
          addExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(420);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(421);
          match(OpenCMLParser::T__26);
          setState(422);
          addExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(423);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(424);
          match(OpenCMLParser::T__27);
          setState(425);
          addExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(426);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(427);
          match(OpenCMLParser::T__28);
          setState(428);
          addExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(429);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(430);
          match(OpenCMLParser::T__29);
          setState(431);
          addExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(436);
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
  size_t startState = 72;
  enterRecursionRule(_localctx, 72, OpenCMLParser::RuleAddExpr, precedence);

    

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
    setState(438);
    multiExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(448);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 56, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(446);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 55, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(440);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(441);
          match(OpenCMLParser::T__30);
          setState(442);
          multiExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(443);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(444);
          match(OpenCMLParser::T__31);
          setState(445);
          multiExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(450);
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
  size_t startState = 74;
  enterRecursionRule(_localctx, 74, OpenCMLParser::RuleMultiExpr, precedence);

    

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
    setState(452);
    unaryExpr();
    _ctx->stop = _input->LT(-1);
    setState(468);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 58, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(466);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 57, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(454);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(455);
          match(OpenCMLParser::T__32);
          setState(456);
          unaryExpr();
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(457);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(458);
          match(OpenCMLParser::T__33);
          setState(459);
          unaryExpr();
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(460);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(461);
          match(OpenCMLParser::T__34);
          setState(462);
          unaryExpr();
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(463);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(464);
          match(OpenCMLParser::T__35);
          setState(465);
          unaryExpr();
          break;
        }

        default:
          break;
        } 
      }
      setState(470);
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
  enterRule(_localctx, 76, OpenCMLParser::RuleUnaryExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(476);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__8:
      case OpenCMLParser::T__10:
      case OpenCMLParser::T__14:
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
        setState(471);
        primExpr();
        break;
      }

      case OpenCMLParser::T__36: {
        enterOuterAlt(_localctx, 2);
        setState(472);
        match(OpenCMLParser::T__36);
        setState(473);
        primExpr();
        break;
      }

      case OpenCMLParser::T__31: {
        enterOuterAlt(_localctx, 3);
        setState(474);
        match(OpenCMLParser::T__31);
        setState(475);
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
  : ParserRuleContext(parent, invokingState) {
}

OpenCMLParser::EntityExprContext* OpenCMLParser::PrimExprContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}

OpenCMLParser::ExprContext* OpenCMLParser::PrimExprContext::expr() {
  return getRuleContext<OpenCMLParser::ExprContext>(0);
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
  enterRule(_localctx, 78, OpenCMLParser::RulePrimExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(483);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 60, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(478);
      entityExpr();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(479);
      match(OpenCMLParser::T__14);
      setState(480);
      expr(0);
      setState(481);
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
  enterRule(_localctx, 80, OpenCMLParser::RuleLiteral);

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
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER:
      case OpenCMLParser::REAL: {
        enterOuterAlt(_localctx, 1);
        setState(485);
        value();
        break;
      }

      case OpenCMLParser::STRING: {
        enterOuterAlt(_localctx, 2);
        setState(486);
        match(OpenCMLParser::STRING);
        break;
      }

      case OpenCMLParser::MULTI_STR: {
        enterOuterAlt(_localctx, 3);
        setState(487);
        match(OpenCMLParser::MULTI_STR);
        break;
      }

      case OpenCMLParser::FSTRING: {
        enterOuterAlt(_localctx, 4);
        setState(488);
        match(OpenCMLParser::FSTRING);
        break;
      }

      case OpenCMLParser::NULL_: {
        enterOuterAlt(_localctx, 5);
        setState(489);
        match(OpenCMLParser::NULL_);
        break;
      }

      case OpenCMLParser::TRUE: {
        enterOuterAlt(_localctx, 6);
        setState(490);
        match(OpenCMLParser::TRUE);
        break;
      }

      case OpenCMLParser::FALSE: {
        enterOuterAlt(_localctx, 7);
        setState(491);
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
  enterRule(_localctx, 82, OpenCMLParser::RuleValue);
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
    setState(494);
    _la = _input->LA(1);
    if (!(_la == OpenCMLParser::INTEGER

    || _la == OpenCMLParser::REAL)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(496);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 62, _ctx)) {
    case 1: {
      setState(495);
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
  enterRule(_localctx, 84, OpenCMLParser::RuleType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(501);
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
        setState(498);
        innerType();
        break;
      }

      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(499);
        identRef();
        break;
      }

      case OpenCMLParser::ANY_TYPE: {
        enterOuterAlt(_localctx, 3);
        setState(500);
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
  enterRule(_localctx, 86, OpenCMLParser::RuleInnerType);

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
      case OpenCMLParser::NUMBER_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(503);
        match(OpenCMLParser::NUMBER_TYPE);
        break;
      }

      case OpenCMLParser::STRING_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(504);
        match(OpenCMLParser::STRING_TYPE);
        break;
      }

      case OpenCMLParser::BOOLEAN_TYPE: {
        enterOuterAlt(_localctx, 3);
        setState(505);
        match(OpenCMLParser::BOOLEAN_TYPE);
        break;
      }

      case OpenCMLParser::FUNCTOR_TYPE: {
        enterOuterAlt(_localctx, 4);
        setState(506);
        match(OpenCMLParser::FUNCTOR_TYPE);
        break;
      }

      case OpenCMLParser::INTEGER_TYPE:
      case OpenCMLParser::REAL_TYPE:
      case OpenCMLParser::COMPLEX_TYPE:
      case OpenCMLParser::ARRAY_TYPE:
      case OpenCMLParser::MATRIX_TYPE: {
        enterOuterAlt(_localctx, 5);
        setState(507);
        numberType();
        break;
      }

      case OpenCMLParser::LIST_TYPE:
      case OpenCMLParser::DICT_TYPE: {
        enterOuterAlt(_localctx, 6);
        setState(508);
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
  enterRule(_localctx, 88, OpenCMLParser::RuleNumberType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(513);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER_TYPE:
      case OpenCMLParser::REAL_TYPE:
      case OpenCMLParser::COMPLEX_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(511);
        scalarType();
        break;
      }

      case OpenCMLParser::ARRAY_TYPE:
      case OpenCMLParser::MATRIX_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(512);
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
  enterRule(_localctx, 90, OpenCMLParser::RuleScalarType);
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
    setState(515);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -2305843009213693952) != 0))) {
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
  enterRule(_localctx, 92, OpenCMLParser::RuleVectorType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    setState(544);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::ARRAY_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(517);
        match(OpenCMLParser::ARRAY_TYPE);
        setState(522);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 66, _ctx)) {
        case 1: {
          setState(518);
          match(OpenCMLParser::T__12);
          setState(519);
          scalarType();
          setState(520);
          match(OpenCMLParser::T__13);
          break;
        }

        default:
          break;
        }
        setState(527);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 67, _ctx)) {
        case 1: {
          setState(524);
          match(OpenCMLParser::T__10);
          setState(525);
          match(OpenCMLParser::INTEGER);
          setState(526);
          match(OpenCMLParser::T__11);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::MATRIX_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(529);
        match(OpenCMLParser::MATRIX_TYPE);
        setState(534);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 68, _ctx)) {
        case 1: {
          setState(530);
          match(OpenCMLParser::T__12);
          setState(531);
          scalarType();
          setState(532);
          match(OpenCMLParser::T__13);
          break;
        }

        default:
          break;
        }
        setState(541);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx);
        while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
          if (alt == 1) {
            setState(536);
            match(OpenCMLParser::T__10);
            setState(537);
            match(OpenCMLParser::INTEGER);
            setState(538);
            match(OpenCMLParser::T__11); 
          }
          setState(543);
          _errHandler->sync(this);
          alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx);
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
  enterRule(_localctx, 94, OpenCMLParser::RuleStructType);
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
    switch (_input->LA(1)) {
      case OpenCMLParser::LIST_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(546);
        match(OpenCMLParser::LIST_TYPE);
        setState(558);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 72, _ctx)) {
        case 1: {
          setState(547);
          match(OpenCMLParser::T__12);
          setState(548);
          type();
          setState(553);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == OpenCMLParser::T__6) {
            setState(549);
            match(OpenCMLParser::T__6);
            setState(550);
            type();
            setState(555);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
          setState(556);
          match(OpenCMLParser::T__13);
          break;
        }

        default:
          break;
        }
        setState(563);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 73, _ctx)) {
        case 1: {
          setState(560);
          match(OpenCMLParser::T__10);
          setState(561);
          match(OpenCMLParser::INTEGER);
          setState(562);
          match(OpenCMLParser::T__11);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::DICT_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(565);
        match(OpenCMLParser::DICT_TYPE);
        setState(572);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx)) {
        case 1: {
          setState(566);
          match(OpenCMLParser::T__12);
          setState(567);
          type();
          setState(568);
          match(OpenCMLParser::T__6);
          setState(569);
          type();
          setState(570);
          match(OpenCMLParser::T__13);
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
  enterRule(_localctx, 96, OpenCMLParser::RuleIdentRef);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(576);
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
    case 30: return entityLinkSempred(antlrcpp::downCast<EntityLinkContext *>(context), predicateIndex);
    case 34: return exprSempred(antlrcpp::downCast<ExprContext *>(context), predicateIndex);
    case 35: return relaExprSempred(antlrcpp::downCast<RelaExprContext *>(context), predicateIndex);
    case 36: return addExprSempred(antlrcpp::downCast<AddExprContext *>(context), predicateIndex);
    case 37: return multiExprSempred(antlrcpp::downCast<MultiExprContext *>(context), predicateIndex);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::entityLinkSempred(EntityLinkContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 0: return precpred(_ctx, 2);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::exprSempred(ExprContext *_localctx, size_t predicateIndex) {
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
