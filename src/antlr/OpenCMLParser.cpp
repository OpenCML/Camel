
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
  	4,1,75,582,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
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
  	8,3,8,191,8,8,1,8,1,8,1,9,1,9,3,9,197,8,9,1,10,3,10,200,8,10,1,10,1,10,
  	1,10,3,10,205,8,10,1,10,1,10,3,10,209,8,10,1,10,1,10,1,11,1,11,1,11,3,
  	11,216,8,11,1,12,1,12,1,12,1,13,1,13,3,13,223,8,13,4,13,225,8,13,11,13,
  	12,13,226,1,14,4,14,230,8,14,11,14,12,14,231,1,15,1,15,1,15,1,15,1,15,
  	1,15,3,15,240,8,15,1,16,1,16,1,16,5,16,245,8,16,10,16,12,16,248,9,16,
  	1,17,1,17,3,17,252,8,17,1,17,1,17,1,17,3,17,257,8,17,5,17,259,8,17,10,
  	17,12,17,262,9,17,1,18,1,18,3,18,266,8,18,1,18,3,18,269,8,18,1,18,1,18,
  	3,18,273,8,18,1,18,1,18,3,18,277,8,18,1,19,1,19,3,19,281,8,19,1,19,1,
  	19,1,19,3,19,286,8,19,5,19,288,8,19,10,19,12,19,291,9,19,1,20,1,20,3,
  	20,295,8,20,1,20,1,20,1,21,1,21,3,21,301,8,21,1,21,1,21,1,22,1,22,3,22,
  	307,8,22,1,22,1,22,1,23,1,23,3,23,313,8,23,1,23,1,23,1,24,1,24,3,24,319,
  	8,24,1,24,1,24,1,25,1,25,3,25,325,8,25,1,25,1,25,1,26,1,26,3,26,331,8,
  	26,1,26,1,26,1,27,1,27,1,27,1,27,1,27,3,27,340,8,27,1,28,1,28,1,28,1,
  	28,1,28,1,28,1,28,3,28,349,8,28,4,28,351,8,28,11,28,12,28,352,1,29,1,
  	29,3,29,357,8,29,1,29,3,29,360,8,29,1,29,3,29,363,8,29,1,29,3,29,366,
  	8,29,1,30,1,30,1,30,1,30,1,30,1,30,5,30,374,8,30,10,30,12,30,377,9,30,
  	1,31,4,31,380,8,31,11,31,12,31,381,1,32,1,32,1,32,1,33,1,33,1,33,3,33,
  	390,8,33,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,
  	1,34,1,34,1,34,1,34,1,34,1,34,5,34,410,8,34,10,34,12,34,413,9,34,1,35,
  	1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,
  	1,35,1,35,1,35,1,35,1,35,1,35,5,35,436,8,35,10,35,12,35,439,9,35,1,36,
  	1,36,1,36,1,36,1,36,1,36,1,36,1,36,1,36,5,36,450,8,36,10,36,12,36,453,
  	9,36,1,37,1,37,1,37,1,37,1,37,1,37,1,37,1,37,1,37,1,37,1,37,1,37,1,37,
  	1,37,1,37,5,37,470,8,37,10,37,12,37,473,9,37,1,38,1,38,1,38,1,38,1,38,
  	3,38,480,8,38,1,39,1,39,1,39,1,39,1,39,3,39,487,8,39,1,40,1,40,1,40,1,
  	40,1,40,1,40,1,40,3,40,496,8,40,1,41,1,41,3,41,500,8,41,1,42,1,42,1,42,
  	3,42,505,8,42,1,43,1,43,1,43,1,43,1,43,1,43,3,43,513,8,43,1,44,1,44,3,
  	44,517,8,44,1,45,1,45,1,46,1,46,1,46,1,46,1,46,3,46,526,8,46,1,46,1,46,
  	1,46,3,46,531,8,46,1,46,1,46,1,46,1,46,1,46,3,46,538,8,46,1,46,1,46,1,
  	46,5,46,543,8,46,10,46,12,46,546,9,46,3,46,548,8,46,1,47,1,47,1,47,1,
  	47,1,47,5,47,555,8,47,10,47,12,47,558,9,47,1,47,1,47,3,47,562,8,47,1,
  	47,1,47,1,47,3,47,567,8,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,3,47,576,
  	8,47,3,47,578,8,47,1,48,1,48,1,48,0,5,60,68,70,72,74,49,0,2,4,6,8,10,
  	12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,
  	58,60,62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,96,0,3,1,0,47,
  	52,1,0,74,75,1,0,61,63,639,0,99,1,0,0,0,2,104,1,0,0,0,4,126,1,0,0,0,6,
  	147,1,0,0,0,8,160,1,0,0,0,10,163,1,0,0,0,12,167,1,0,0,0,14,171,1,0,0,
  	0,16,177,1,0,0,0,18,194,1,0,0,0,20,199,1,0,0,0,22,215,1,0,0,0,24,217,
  	1,0,0,0,26,224,1,0,0,0,28,229,1,0,0,0,30,239,1,0,0,0,32,241,1,0,0,0,34,
  	251,1,0,0,0,36,263,1,0,0,0,38,280,1,0,0,0,40,292,1,0,0,0,42,298,1,0,0,
  	0,44,304,1,0,0,0,46,310,1,0,0,0,48,316,1,0,0,0,50,322,1,0,0,0,52,328,
  	1,0,0,0,54,339,1,0,0,0,56,350,1,0,0,0,58,354,1,0,0,0,60,367,1,0,0,0,62,
  	379,1,0,0,0,64,383,1,0,0,0,66,386,1,0,0,0,68,391,1,0,0,0,70,414,1,0,0,
  	0,72,440,1,0,0,0,74,454,1,0,0,0,76,479,1,0,0,0,78,486,1,0,0,0,80,495,
  	1,0,0,0,82,497,1,0,0,0,84,504,1,0,0,0,86,512,1,0,0,0,88,516,1,0,0,0,90,
  	518,1,0,0,0,92,547,1,0,0,0,94,577,1,0,0,0,96,579,1,0,0,0,98,100,3,2,1,
  	0,99,98,1,0,0,0,99,100,1,0,0,0,100,101,1,0,0,0,101,102,5,0,0,1,102,1,
  	1,0,0,0,103,105,3,4,2,0,104,103,1,0,0,0,105,106,1,0,0,0,106,104,1,0,0,
  	0,106,107,1,0,0,0,107,3,1,0,0,0,108,109,3,6,3,0,109,110,5,38,0,0,110,
  	127,1,0,0,0,111,112,3,8,4,0,112,113,5,38,0,0,113,127,1,0,0,0,114,115,
  	3,16,8,0,115,116,5,38,0,0,116,127,1,0,0,0,117,118,3,18,9,0,118,119,5,
  	38,0,0,119,127,1,0,0,0,120,121,3,10,5,0,121,122,5,38,0,0,122,127,1,0,
  	0,0,123,124,3,12,6,0,124,125,5,38,0,0,125,127,1,0,0,0,126,108,1,0,0,0,
  	126,111,1,0,0,0,126,114,1,0,0,0,126,117,1,0,0,0,126,120,1,0,0,0,126,123,
  	1,0,0,0,127,5,1,0,0,0,128,129,5,40,0,0,129,132,3,22,11,0,130,131,5,1,
  	0,0,131,133,3,84,42,0,132,130,1,0,0,0,132,133,1,0,0,0,133,135,1,0,0,0,
  	134,136,5,2,0,0,135,134,1,0,0,0,135,136,1,0,0,0,136,137,1,0,0,0,137,138,
  	3,68,34,0,138,148,1,0,0,0,139,142,3,22,11,0,140,141,5,1,0,0,141,143,3,
  	84,42,0,142,140,1,0,0,0,142,143,1,0,0,0,143,144,1,0,0,0,144,145,5,3,0,
  	0,145,146,3,68,34,0,146,148,1,0,0,0,147,128,1,0,0,0,147,139,1,0,0,0,148,
  	7,1,0,0,0,149,150,5,41,0,0,150,152,3,22,11,0,151,153,5,2,0,0,152,151,
  	1,0,0,0,152,153,1,0,0,0,153,154,1,0,0,0,154,155,3,68,34,0,155,161,1,0,
  	0,0,156,157,3,22,11,0,157,158,5,4,0,0,158,159,3,68,34,0,159,161,1,0,0,
  	0,160,149,1,0,0,0,160,156,1,0,0,0,161,9,1,0,0,0,162,164,3,26,13,0,163,
  	162,1,0,0,0,163,164,1,0,0,0,164,165,1,0,0,0,165,166,3,68,34,0,166,11,
  	1,0,0,0,167,168,3,58,29,0,168,169,5,2,0,0,169,170,3,68,34,0,170,13,1,
  	0,0,0,171,174,5,45,0,0,172,175,3,58,29,0,173,175,3,44,22,0,174,172,1,
  	0,0,0,174,173,1,0,0,0,175,15,1,0,0,0,176,178,3,26,13,0,177,176,1,0,0,
  	0,177,178,1,0,0,0,178,180,1,0,0,0,179,181,3,14,7,0,180,179,1,0,0,0,180,
  	181,1,0,0,0,181,183,1,0,0,0,182,184,3,28,14,0,183,182,1,0,0,0,183,184,
  	1,0,0,0,184,185,1,0,0,0,185,186,5,42,0,0,186,187,3,96,48,0,187,190,3,
  	48,24,0,188,189,5,1,0,0,189,191,3,84,42,0,190,188,1,0,0,0,190,191,1,0,
  	0,0,191,192,1,0,0,0,192,193,3,52,26,0,193,17,1,0,0,0,194,196,5,46,0,0,
  	195,197,3,68,34,0,196,195,1,0,0,0,196,197,1,0,0,0,197,19,1,0,0,0,198,
  	200,3,28,14,0,199,198,1,0,0,0,199,200,1,0,0,0,200,208,1,0,0,0,201,204,
  	3,48,24,0,202,203,5,1,0,0,203,205,3,84,42,0,204,202,1,0,0,0,204,205,1,
  	0,0,0,205,206,1,0,0,0,206,207,5,5,0,0,207,209,1,0,0,0,208,201,1,0,0,0,
  	208,209,1,0,0,0,209,210,1,0,0,0,210,211,3,52,26,0,211,21,1,0,0,0,212,
  	216,3,96,48,0,213,216,3,42,21,0,214,216,3,40,20,0,215,212,1,0,0,0,215,
  	213,1,0,0,0,215,214,1,0,0,0,216,23,1,0,0,0,217,218,5,6,0,0,218,219,3,
  	22,11,0,219,25,1,0,0,0,220,222,3,24,12,0,221,223,5,38,0,0,222,221,1,0,
  	0,0,222,223,1,0,0,0,223,225,1,0,0,0,224,220,1,0,0,0,225,226,1,0,0,0,226,
  	224,1,0,0,0,226,227,1,0,0,0,227,27,1,0,0,0,228,230,7,0,0,0,229,228,1,
  	0,0,0,230,231,1,0,0,0,231,229,1,0,0,0,231,232,1,0,0,0,232,29,1,0,0,0,
  	233,240,3,68,34,0,234,235,3,96,48,0,235,236,5,2,0,0,236,237,3,68,34,0,
  	237,240,1,0,0,0,238,240,3,64,32,0,239,233,1,0,0,0,239,234,1,0,0,0,239,
  	238,1,0,0,0,240,31,1,0,0,0,241,246,3,30,15,0,242,243,5,7,0,0,243,245,
  	3,30,15,0,244,242,1,0,0,0,245,248,1,0,0,0,246,244,1,0,0,0,246,247,1,0,
  	0,0,247,33,1,0,0,0,248,246,1,0,0,0,249,252,3,68,34,0,250,252,3,64,32,
  	0,251,249,1,0,0,0,251,250,1,0,0,0,252,260,1,0,0,0,253,256,5,7,0,0,254,
  	257,3,68,34,0,255,257,3,64,32,0,256,254,1,0,0,0,256,255,1,0,0,0,257,259,
  	1,0,0,0,258,253,1,0,0,0,259,262,1,0,0,0,260,258,1,0,0,0,260,261,1,0,0,
  	0,261,35,1,0,0,0,262,260,1,0,0,0,263,265,3,96,48,0,264,266,5,8,0,0,265,
  	264,1,0,0,0,265,266,1,0,0,0,266,268,1,0,0,0,267,269,3,24,12,0,268,267,
  	1,0,0,0,268,269,1,0,0,0,269,272,1,0,0,0,270,271,5,1,0,0,271,273,3,84,
  	42,0,272,270,1,0,0,0,272,273,1,0,0,0,273,276,1,0,0,0,274,275,5,2,0,0,
  	275,277,3,68,34,0,276,274,1,0,0,0,276,277,1,0,0,0,277,37,1,0,0,0,278,
  	281,3,36,18,0,279,281,3,64,32,0,280,278,1,0,0,0,280,279,1,0,0,0,281,289,
  	1,0,0,0,282,285,5,7,0,0,283,286,3,36,18,0,284,286,3,64,32,0,285,283,1,
  	0,0,0,285,284,1,0,0,0,286,288,1,0,0,0,287,282,1,0,0,0,288,291,1,0,0,0,
  	289,287,1,0,0,0,289,290,1,0,0,0,290,39,1,0,0,0,291,289,1,0,0,0,292,294,
  	5,9,0,0,293,295,3,38,19,0,294,293,1,0,0,0,294,295,1,0,0,0,295,296,1,0,
  	0,0,296,297,5,10,0,0,297,41,1,0,0,0,298,300,5,11,0,0,299,301,3,34,17,
  	0,300,299,1,0,0,0,300,301,1,0,0,0,301,302,1,0,0,0,302,303,5,12,0,0,303,
  	43,1,0,0,0,304,306,5,13,0,0,305,307,3,38,19,0,306,305,1,0,0,0,306,307,
  	1,0,0,0,307,308,1,0,0,0,308,309,5,14,0,0,309,45,1,0,0,0,310,312,5,13,
  	0,0,311,313,3,32,16,0,312,311,1,0,0,0,312,313,1,0,0,0,313,314,1,0,0,0,
  	314,315,5,14,0,0,315,47,1,0,0,0,316,318,5,15,0,0,317,319,3,38,19,0,318,
  	317,1,0,0,0,318,319,1,0,0,0,319,320,1,0,0,0,320,321,5,16,0,0,321,49,1,
  	0,0,0,322,324,5,15,0,0,323,325,3,32,16,0,324,323,1,0,0,0,324,325,1,0,
  	0,0,325,326,1,0,0,0,326,327,5,16,0,0,327,51,1,0,0,0,328,330,5,9,0,0,329,
  	331,3,2,1,0,330,329,1,0,0,0,330,331,1,0,0,0,331,332,1,0,0,0,332,333,5,
  	10,0,0,333,53,1,0,0,0,334,340,3,96,48,0,335,340,3,80,40,0,336,340,3,42,
  	21,0,337,340,3,40,20,0,338,340,3,20,10,0,339,334,1,0,0,0,339,335,1,0,
  	0,0,339,336,1,0,0,0,339,337,1,0,0,0,339,338,1,0,0,0,340,55,1,0,0,0,341,
  	348,5,17,0,0,342,349,3,96,48,0,343,349,5,74,0,0,344,345,5,15,0,0,345,
  	346,3,68,34,0,346,347,5,16,0,0,347,349,1,0,0,0,348,342,1,0,0,0,348,343,
  	1,0,0,0,348,344,1,0,0,0,349,351,1,0,0,0,350,341,1,0,0,0,351,352,1,0,0,
  	0,352,350,1,0,0,0,352,353,1,0,0,0,353,57,1,0,0,0,354,356,3,54,27,0,355,
  	357,3,56,28,0,356,355,1,0,0,0,356,357,1,0,0,0,357,359,1,0,0,0,358,360,
  	3,46,23,0,359,358,1,0,0,0,359,360,1,0,0,0,360,362,1,0,0,0,361,363,3,50,
  	25,0,362,361,1,0,0,0,362,363,1,0,0,0,363,365,1,0,0,0,364,366,3,24,12,
  	0,365,364,1,0,0,0,365,366,1,0,0,0,366,59,1,0,0,0,367,368,6,30,-1,0,368,
  	369,3,58,29,0,369,375,1,0,0,0,370,371,10,2,0,0,371,372,5,18,0,0,372,374,
  	3,58,29,0,373,370,1,0,0,0,374,377,1,0,0,0,375,373,1,0,0,0,375,376,1,0,
  	0,0,376,61,1,0,0,0,377,375,1,0,0,0,378,380,3,60,30,0,379,378,1,0,0,0,
  	380,381,1,0,0,0,381,379,1,0,0,0,381,382,1,0,0,0,382,63,1,0,0,0,383,384,
  	5,19,0,0,384,385,3,58,29,0,385,65,1,0,0,0,386,389,3,62,31,0,387,388,5,
  	39,0,0,388,390,3,84,42,0,389,387,1,0,0,0,389,390,1,0,0,0,390,67,1,0,0,
  	0,391,392,6,34,-1,0,392,393,3,70,35,0,393,411,1,0,0,0,394,395,10,5,0,
  	0,395,396,5,20,0,0,396,410,3,70,35,0,397,398,10,4,0,0,398,399,5,21,0,
  	0,399,410,3,70,35,0,400,401,10,3,0,0,401,402,5,22,0,0,402,410,3,70,35,
  	0,403,404,10,2,0,0,404,405,5,23,0,0,405,410,3,70,35,0,406,407,10,1,0,
  	0,407,408,5,24,0,0,408,410,3,70,35,0,409,394,1,0,0,0,409,397,1,0,0,0,
  	409,400,1,0,0,0,409,403,1,0,0,0,409,406,1,0,0,0,410,413,1,0,0,0,411,409,
  	1,0,0,0,411,412,1,0,0,0,412,69,1,0,0,0,413,411,1,0,0,0,414,415,6,35,-1,
  	0,415,416,3,72,36,0,416,437,1,0,0,0,417,418,10,6,0,0,418,419,5,25,0,0,
  	419,436,3,72,36,0,420,421,10,5,0,0,421,422,5,26,0,0,422,436,3,72,36,0,
  	423,424,10,4,0,0,424,425,5,27,0,0,425,436,3,72,36,0,426,427,10,3,0,0,
  	427,428,5,28,0,0,428,436,3,72,36,0,429,430,10,2,0,0,430,431,5,29,0,0,
  	431,436,3,72,36,0,432,433,10,1,0,0,433,434,5,30,0,0,434,436,3,72,36,0,
  	435,417,1,0,0,0,435,420,1,0,0,0,435,423,1,0,0,0,435,426,1,0,0,0,435,429,
  	1,0,0,0,435,432,1,0,0,0,436,439,1,0,0,0,437,435,1,0,0,0,437,438,1,0,0,
  	0,438,71,1,0,0,0,439,437,1,0,0,0,440,441,6,36,-1,0,441,442,3,74,37,0,
  	442,451,1,0,0,0,443,444,10,2,0,0,444,445,5,31,0,0,445,450,3,74,37,0,446,
  	447,10,1,0,0,447,448,5,32,0,0,448,450,3,74,37,0,449,443,1,0,0,0,449,446,
  	1,0,0,0,450,453,1,0,0,0,451,449,1,0,0,0,451,452,1,0,0,0,452,73,1,0,0,
  	0,453,451,1,0,0,0,454,455,6,37,-1,0,455,456,3,76,38,0,456,471,1,0,0,0,
  	457,458,10,4,0,0,458,459,5,33,0,0,459,470,3,76,38,0,460,461,10,3,0,0,
  	461,462,5,34,0,0,462,470,3,76,38,0,463,464,10,2,0,0,464,465,5,35,0,0,
  	465,470,3,76,38,0,466,467,10,1,0,0,467,468,5,36,0,0,468,470,3,76,38,0,
  	469,457,1,0,0,0,469,460,1,0,0,0,469,463,1,0,0,0,469,466,1,0,0,0,470,473,
  	1,0,0,0,471,469,1,0,0,0,471,472,1,0,0,0,472,75,1,0,0,0,473,471,1,0,0,
  	0,474,480,3,78,39,0,475,476,5,37,0,0,476,480,3,78,39,0,477,478,5,32,0,
  	0,478,480,3,78,39,0,479,474,1,0,0,0,479,475,1,0,0,0,479,477,1,0,0,0,480,
  	77,1,0,0,0,481,487,3,66,33,0,482,483,5,15,0,0,483,484,3,68,34,0,484,485,
  	5,16,0,0,485,487,1,0,0,0,486,481,1,0,0,0,486,482,1,0,0,0,487,79,1,0,0,
  	0,488,496,3,82,41,0,489,496,5,72,0,0,490,496,5,69,0,0,491,496,5,73,0,
  	0,492,496,5,53,0,0,493,496,5,54,0,0,494,496,5,55,0,0,495,488,1,0,0,0,
  	495,489,1,0,0,0,495,490,1,0,0,0,495,491,1,0,0,0,495,492,1,0,0,0,495,493,
  	1,0,0,0,495,494,1,0,0,0,496,81,1,0,0,0,497,499,7,1,0,0,498,500,5,71,0,
  	0,499,498,1,0,0,0,499,500,1,0,0,0,500,83,1,0,0,0,501,505,3,86,43,0,502,
  	505,3,96,48,0,503,505,5,56,0,0,504,501,1,0,0,0,504,502,1,0,0,0,504,503,
  	1,0,0,0,505,85,1,0,0,0,506,513,5,57,0,0,507,513,5,58,0,0,508,513,5,59,
  	0,0,509,513,5,60,0,0,510,513,3,88,44,0,511,513,3,94,47,0,512,506,1,0,
  	0,0,512,507,1,0,0,0,512,508,1,0,0,0,512,509,1,0,0,0,512,510,1,0,0,0,512,
  	511,1,0,0,0,513,87,1,0,0,0,514,517,3,90,45,0,515,517,3,92,46,0,516,514,
  	1,0,0,0,516,515,1,0,0,0,517,89,1,0,0,0,518,519,7,2,0,0,519,91,1,0,0,0,
  	520,525,5,64,0,0,521,522,5,13,0,0,522,523,3,90,45,0,523,524,5,14,0,0,
  	524,526,1,0,0,0,525,521,1,0,0,0,525,526,1,0,0,0,526,530,1,0,0,0,527,528,
  	5,11,0,0,528,529,5,74,0,0,529,531,5,12,0,0,530,527,1,0,0,0,530,531,1,
  	0,0,0,531,548,1,0,0,0,532,537,5,65,0,0,533,534,5,13,0,0,534,535,3,90,
  	45,0,535,536,5,14,0,0,536,538,1,0,0,0,537,533,1,0,0,0,537,538,1,0,0,0,
  	538,544,1,0,0,0,539,540,5,11,0,0,540,541,5,74,0,0,541,543,5,12,0,0,542,
  	539,1,0,0,0,543,546,1,0,0,0,544,542,1,0,0,0,544,545,1,0,0,0,545,548,1,
  	0,0,0,546,544,1,0,0,0,547,520,1,0,0,0,547,532,1,0,0,0,548,93,1,0,0,0,
  	549,561,5,66,0,0,550,551,5,13,0,0,551,556,3,84,42,0,552,553,5,7,0,0,553,
  	555,3,84,42,0,554,552,1,0,0,0,555,558,1,0,0,0,556,554,1,0,0,0,556,557,
  	1,0,0,0,557,559,1,0,0,0,558,556,1,0,0,0,559,560,5,14,0,0,560,562,1,0,
  	0,0,561,550,1,0,0,0,561,562,1,0,0,0,562,566,1,0,0,0,563,564,5,11,0,0,
  	564,565,5,74,0,0,565,567,5,12,0,0,566,563,1,0,0,0,566,567,1,0,0,0,567,
  	578,1,0,0,0,568,575,5,67,0,0,569,570,5,13,0,0,570,571,3,84,42,0,571,572,
  	5,7,0,0,572,573,3,84,42,0,573,574,5,14,0,0,574,576,1,0,0,0,575,569,1,
  	0,0,0,575,576,1,0,0,0,576,578,1,0,0,0,577,549,1,0,0,0,577,568,1,0,0,0,
  	578,95,1,0,0,0,579,580,5,70,0,0,580,97,1,0,0,0,77,99,106,126,132,135,
  	142,147,152,160,163,174,177,180,183,190,196,199,204,208,215,222,226,231,
  	239,246,251,256,260,265,268,272,276,280,285,289,294,300,306,312,318,324,
  	330,339,348,352,356,359,362,365,375,381,389,409,411,435,437,449,451,469,
  	471,479,486,495,499,504,512,516,525,530,537,544,547,556,561,566,575,577
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
      case OpenCMLParser::INNER:
      case OpenCMLParser::OUTER:
      case OpenCMLParser::SYNC:
      case OpenCMLParser::SCOPED:
      case OpenCMLParser::STATIC:
      case OpenCMLParser::ATOMIC:
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
      ((1ULL << _la) & 71916998283528704) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
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

OpenCMLParser::ModifiersContext* OpenCMLParser::LambdaContext::modifiers() {
  return getRuleContext<OpenCMLParser::ModifiersContext>(0);
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
    setState(199);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8866461766385664) != 0)) {
      setState(198);
      modifiers();
    }
    setState(208);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__14) {
      setState(201);
      argsDecl();
      setState(204);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__0) {
        setState(202);
        match(OpenCMLParser::T__0);
        setState(203);
        type();
      }
      setState(206);
      match(OpenCMLParser::T__4);
    }
    setState(210);
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
    setState(215);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(212);
        identRef();
        break;
      }

      case OpenCMLParser::T__10: {
        enterOuterAlt(_localctx, 2);
        setState(213);
        listPack();
        break;
      }

      case OpenCMLParser::T__8: {
        enterOuterAlt(_localctx, 3);
        setState(214);
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
    setState(217);
    match(OpenCMLParser::T__5);
    setState(218);
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
    setState(224); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(220);
      annotation();
      setState(222);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::SEP) {
        setState(221);
        match(OpenCMLParser::SEP);
      }
      setState(226); 
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
    setState(229); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(228);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8866461766385664) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(231); 
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
    setState(239);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(233);
      expr(0);
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(234);
      identRef();
      setState(235);
      match(OpenCMLParser::T__1);
      setState(236);
      expr(0);
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(238);
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
    setState(241);
    argument();
    setState(246);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__6) {
      setState(242);
      match(OpenCMLParser::T__6);
      setState(243);
      argument();
      setState(248);
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
    setState(251);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__8:
      case OpenCMLParser::T__10:
      case OpenCMLParser::T__14:
      case OpenCMLParser::T__31:
      case OpenCMLParser::T__36:
      case OpenCMLParser::INNER:
      case OpenCMLParser::OUTER:
      case OpenCMLParser::SYNC:
      case OpenCMLParser::SCOPED:
      case OpenCMLParser::STATIC:
      case OpenCMLParser::ATOMIC:
      case OpenCMLParser::NULL_:
      case OpenCMLParser::TRUE:
      case OpenCMLParser::FALSE:
      case OpenCMLParser::MULTI_STR:
      case OpenCMLParser::IDENTIFIER:
      case OpenCMLParser::STRING:
      case OpenCMLParser::FSTRING:
      case OpenCMLParser::INTEGER:
      case OpenCMLParser::REAL: {
        setState(249);
        expr(0);
        break;
      }

      case OpenCMLParser::T__18: {
        setState(250);
        entityUnpack();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(260);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__6) {
      setState(253);
      match(OpenCMLParser::T__6);
      setState(256);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case OpenCMLParser::T__8:
        case OpenCMLParser::T__10:
        case OpenCMLParser::T__14:
        case OpenCMLParser::T__31:
        case OpenCMLParser::T__36:
        case OpenCMLParser::INNER:
        case OpenCMLParser::OUTER:
        case OpenCMLParser::SYNC:
        case OpenCMLParser::SCOPED:
        case OpenCMLParser::STATIC:
        case OpenCMLParser::ATOMIC:
        case OpenCMLParser::NULL_:
        case OpenCMLParser::TRUE:
        case OpenCMLParser::FALSE:
        case OpenCMLParser::MULTI_STR:
        case OpenCMLParser::IDENTIFIER:
        case OpenCMLParser::STRING:
        case OpenCMLParser::FSTRING:
        case OpenCMLParser::INTEGER:
        case OpenCMLParser::REAL: {
          setState(254);
          expr(0);
          break;
        }

        case OpenCMLParser::T__18: {
          setState(255);
          entityUnpack();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(262);
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
    setState(263);
    identRef();
    setState(265);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__7) {
      setState(264);
      match(OpenCMLParser::T__7);
    }
    setState(268);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(267);
      annotation();
    }
    setState(272);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(270);
      match(OpenCMLParser::T__0);
      setState(271);
      type();
    }
    setState(276);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(274);
      match(OpenCMLParser::T__1);
      setState(275);
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
    setState(280);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        setState(278);
        keyValExpr();
        break;
      }

      case OpenCMLParser::T__18: {
        setState(279);
        entityUnpack();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(289);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == OpenCMLParser::T__6) {
      setState(282);
      match(OpenCMLParser::T__6);
      setState(285);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case OpenCMLParser::IDENTIFIER: {
          setState(283);
          keyValExpr();
          break;
        }

        case OpenCMLParser::T__18: {
          setState(284);
          entityUnpack();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(291);
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
    setState(292);
    match(OpenCMLParser::T__8);
    setState(294);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__18

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(293);
      keyValList();
    }
    setState(296);
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
    setState(298);
    match(OpenCMLParser::T__10);
    setState(300);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 71916998284052992) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 123) != 0)) {
      setState(299);
      exprList();
    }
    setState(302);
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
    setState(304);
    match(OpenCMLParser::T__12);
    setState(306);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__18

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(305);
      keyValList();
    }
    setState(308);
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
    setState(310);
    match(OpenCMLParser::T__12);
    setState(312);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 71916998284052992) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 123) != 0)) {
      setState(311);
      argsList();
    }
    setState(314);
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
    setState(316);
    match(OpenCMLParser::T__14);
    setState(318);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__18

    || _la == OpenCMLParser::IDENTIFIER) {
      setState(317);
      keyValList();
    }
    setState(320);
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
    setState(322);
    match(OpenCMLParser::T__14);
    setState(324);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 71916998284052992) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 123) != 0)) {
      setState(323);
      argsList();
    }
    setState(326);
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
    setState(328);
    match(OpenCMLParser::T__8);
    setState(330);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 72030247981189696) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 123) != 0)) {
      setState(329);
      stmtList();
    }
    setState(332);
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
    setState(339);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 42, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(334);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(335);
      literal();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(336);
      listPack();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(337);
      dictPack();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(338);
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
    setState(350); 
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(341);
              match(OpenCMLParser::T__16);
              setState(348);
              _errHandler->sync(this);
              switch (_input->LA(1)) {
                case OpenCMLParser::IDENTIFIER: {
                  setState(342);
                  identRef();
                  break;
                }

                case OpenCMLParser::INTEGER: {
                  setState(343);
                  match(OpenCMLParser::INTEGER);
                  break;
                }

                case OpenCMLParser::T__14: {
                  setState(344);
                  match(OpenCMLParser::T__14);
                  setState(345);
                  expr(0);
                  setState(346);
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
      setState(352); 
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 44, _ctx);
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
    setState(354);
    primEntity();
    setState(356);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 45, _ctx)) {
    case 1: {
      setState(355);
      memberAccess();
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
      withPack();
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
      argsPack();
      break;
    }

    default:
      break;
    }
    setState(365);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 48, _ctx)) {
    case 1: {
      setState(364);
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
    setState(368);
    entity();
    _ctx->stop = _input->LT(-1);
    setState(375);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 49, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EntityLinkContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEntityLink);
        setState(370);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(371);
        match(OpenCMLParser::T__17);
        setState(372);
        entity(); 
      }
      setState(377);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 49, _ctx);
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
    setState(379); 
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(378);
              entityLink(0);
              break;
            }

      default:
        throw NoViableAltException(this);
      }
      setState(381); 
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 50, _ctx);
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
    setState(383);
    match(OpenCMLParser::T__18);
    setState(384);
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
    setState(386);
    entityChain();
    setState(389);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 51, _ctx)) {
    case 1: {
      setState(387);
      match(OpenCMLParser::AS);
      setState(388);
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
    setState(392);
    relaExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(411);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 53, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(409);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 52, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<ExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleExpr);
          setState(394);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(395);
          match(OpenCMLParser::T__19);
          setState(396);
          relaExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<ExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleExpr);
          setState(397);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(398);
          match(OpenCMLParser::T__20);
          setState(399);
          relaExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<ExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleExpr);
          setState(400);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(401);
          match(OpenCMLParser::T__21);
          setState(402);
          relaExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<ExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleExpr);
          setState(403);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(404);
          match(OpenCMLParser::T__22);
          setState(405);
          relaExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<ExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleExpr);
          setState(406);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(407);
          match(OpenCMLParser::T__23);
          setState(408);
          relaExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(413);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 53, _ctx);
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
    setState(415);
    addExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(437);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 55, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(435);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 54, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(417);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(418);
          match(OpenCMLParser::T__24);
          setState(419);
          addExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(420);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(421);
          match(OpenCMLParser::T__25);
          setState(422);
          addExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(423);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(424);
          match(OpenCMLParser::T__26);
          setState(425);
          addExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(426);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(427);
          match(OpenCMLParser::T__27);
          setState(428);
          addExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(429);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(430);
          match(OpenCMLParser::T__28);
          setState(431);
          addExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(432);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(433);
          match(OpenCMLParser::T__29);
          setState(434);
          addExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(439);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 55, _ctx);
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
    setState(441);
    multiExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(451);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 57, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(449);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 56, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(443);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(444);
          match(OpenCMLParser::T__30);
          setState(445);
          multiExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(446);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(447);
          match(OpenCMLParser::T__31);
          setState(448);
          multiExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(453);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 57, _ctx);
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
    setState(455);
    unaryExpr();
    _ctx->stop = _input->LT(-1);
    setState(471);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 59, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(469);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 58, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(457);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(458);
          match(OpenCMLParser::T__32);
          setState(459);
          unaryExpr();
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(460);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(461);
          match(OpenCMLParser::T__33);
          setState(462);
          unaryExpr();
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(463);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(464);
          match(OpenCMLParser::T__34);
          setState(465);
          unaryExpr();
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(466);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(467);
          match(OpenCMLParser::T__35);
          setState(468);
          unaryExpr();
          break;
        }

        default:
          break;
        } 
      }
      setState(473);
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
    setState(479);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::T__8:
      case OpenCMLParser::T__10:
      case OpenCMLParser::T__14:
      case OpenCMLParser::INNER:
      case OpenCMLParser::OUTER:
      case OpenCMLParser::SYNC:
      case OpenCMLParser::SCOPED:
      case OpenCMLParser::STATIC:
      case OpenCMLParser::ATOMIC:
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
        setState(474);
        primExpr();
        break;
      }

      case OpenCMLParser::T__36: {
        enterOuterAlt(_localctx, 2);
        setState(475);
        match(OpenCMLParser::T__36);
        setState(476);
        primExpr();
        break;
      }

      case OpenCMLParser::T__31: {
        enterOuterAlt(_localctx, 3);
        setState(477);
        match(OpenCMLParser::T__31);
        setState(478);
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
    setState(486);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(481);
      entityExpr();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(482);
      match(OpenCMLParser::T__14);
      setState(483);
      expr(0);
      setState(484);
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
    setState(495);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER:
      case OpenCMLParser::REAL: {
        enterOuterAlt(_localctx, 1);
        setState(488);
        value();
        break;
      }

      case OpenCMLParser::STRING: {
        enterOuterAlt(_localctx, 2);
        setState(489);
        match(OpenCMLParser::STRING);
        break;
      }

      case OpenCMLParser::MULTI_STR: {
        enterOuterAlt(_localctx, 3);
        setState(490);
        match(OpenCMLParser::MULTI_STR);
        break;
      }

      case OpenCMLParser::FSTRING: {
        enterOuterAlt(_localctx, 4);
        setState(491);
        match(OpenCMLParser::FSTRING);
        break;
      }

      case OpenCMLParser::NULL_: {
        enterOuterAlt(_localctx, 5);
        setState(492);
        match(OpenCMLParser::NULL_);
        break;
      }

      case OpenCMLParser::TRUE: {
        enterOuterAlt(_localctx, 6);
        setState(493);
        match(OpenCMLParser::TRUE);
        break;
      }

      case OpenCMLParser::FALSE: {
        enterOuterAlt(_localctx, 7);
        setState(494);
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
    setState(497);
    _la = _input->LA(1);
    if (!(_la == OpenCMLParser::INTEGER

    || _la == OpenCMLParser::REAL)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(499);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 63, _ctx)) {
    case 1: {
      setState(498);
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
    setState(504);
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
        setState(501);
        innerType();
        break;
      }

      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(502);
        identRef();
        break;
      }

      case OpenCMLParser::ANY_TYPE: {
        enterOuterAlt(_localctx, 3);
        setState(503);
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
    setState(512);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::NUMBER_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(506);
        match(OpenCMLParser::NUMBER_TYPE);
        break;
      }

      case OpenCMLParser::STRING_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(507);
        match(OpenCMLParser::STRING_TYPE);
        break;
      }

      case OpenCMLParser::BOOLEAN_TYPE: {
        enterOuterAlt(_localctx, 3);
        setState(508);
        match(OpenCMLParser::BOOLEAN_TYPE);
        break;
      }

      case OpenCMLParser::FUNCTOR_TYPE: {
        enterOuterAlt(_localctx, 4);
        setState(509);
        match(OpenCMLParser::FUNCTOR_TYPE);
        break;
      }

      case OpenCMLParser::INTEGER_TYPE:
      case OpenCMLParser::REAL_TYPE:
      case OpenCMLParser::COMPLEX_TYPE:
      case OpenCMLParser::ARRAY_TYPE:
      case OpenCMLParser::MATRIX_TYPE: {
        enterOuterAlt(_localctx, 5);
        setState(510);
        numberType();
        break;
      }

      case OpenCMLParser::LIST_TYPE:
      case OpenCMLParser::DICT_TYPE: {
        enterOuterAlt(_localctx, 6);
        setState(511);
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
    setState(516);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER_TYPE:
      case OpenCMLParser::REAL_TYPE:
      case OpenCMLParser::COMPLEX_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(514);
        scalarType();
        break;
      }

      case OpenCMLParser::ARRAY_TYPE:
      case OpenCMLParser::MATRIX_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(515);
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
    setState(518);
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
    setState(547);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::ARRAY_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(520);
        match(OpenCMLParser::ARRAY_TYPE);
        setState(525);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 67, _ctx)) {
        case 1: {
          setState(521);
          match(OpenCMLParser::T__12);
          setState(522);
          scalarType();
          setState(523);
          match(OpenCMLParser::T__13);
          break;
        }

        default:
          break;
        }
        setState(530);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 68, _ctx)) {
        case 1: {
          setState(527);
          match(OpenCMLParser::T__10);
          setState(528);
          match(OpenCMLParser::INTEGER);
          setState(529);
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
        setState(532);
        match(OpenCMLParser::MATRIX_TYPE);
        setState(537);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx)) {
        case 1: {
          setState(533);
          match(OpenCMLParser::T__12);
          setState(534);
          scalarType();
          setState(535);
          match(OpenCMLParser::T__13);
          break;
        }

        default:
          break;
        }
        setState(544);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx);
        while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
          if (alt == 1) {
            setState(539);
            match(OpenCMLParser::T__10);
            setState(540);
            match(OpenCMLParser::INTEGER);
            setState(541);
            match(OpenCMLParser::T__11); 
          }
          setState(546);
          _errHandler->sync(this);
          alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx);
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
    setState(577);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::LIST_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(549);
        match(OpenCMLParser::LIST_TYPE);
        setState(561);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 73, _ctx)) {
        case 1: {
          setState(550);
          match(OpenCMLParser::T__12);
          setState(551);
          type();
          setState(556);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == OpenCMLParser::T__6) {
            setState(552);
            match(OpenCMLParser::T__6);
            setState(553);
            type();
            setState(558);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
          setState(559);
          match(OpenCMLParser::T__13);
          break;
        }

        default:
          break;
        }
        setState(566);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx)) {
        case 1: {
          setState(563);
          match(OpenCMLParser::T__10);
          setState(564);
          match(OpenCMLParser::INTEGER);
          setState(565);
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
        setState(568);
        match(OpenCMLParser::DICT_TYPE);
        setState(575);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 75, _ctx)) {
        case 1: {
          setState(569);
          match(OpenCMLParser::T__12);
          setState(570);
          type();
          setState(571);
          match(OpenCMLParser::T__6);
          setState(572);
          type();
          setState(573);
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
    setState(579);
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
