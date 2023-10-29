
// Generated from OpenCML.g4 by ANTLR 4.13.0


#include "OpenCMLLexer.h"


using namespace antlr4;



using namespace antlr4;

namespace {

struct OpenCMLLexerStaticData final {
  OpenCMLLexerStaticData(std::vector<std::string> ruleNames,
                          std::vector<std::string> channelNames,
                          std::vector<std::string> modeNames,
                          std::vector<std::string> literalNames,
                          std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), channelNames(std::move(channelNames)),
        modeNames(std::move(modeNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  OpenCMLLexerStaticData(const OpenCMLLexerStaticData&) = delete;
  OpenCMLLexerStaticData(OpenCMLLexerStaticData&&) = delete;
  OpenCMLLexerStaticData& operator=(const OpenCMLLexerStaticData&) = delete;
  OpenCMLLexerStaticData& operator=(OpenCMLLexerStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> channelNames;
  const std::vector<std::string> modeNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag opencmllexerLexerOnceFlag;
#if ANTLR4_USE_THREAD_LOCAL_CACHE
static thread_local
#endif
OpenCMLLexerStaticData *opencmllexerLexerStaticData = nullptr;

void opencmllexerLexerInitialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  if (opencmllexerLexerStaticData != nullptr) {
    return;
  }
#else
  assert(opencmllexerLexerStaticData == nullptr);
#endif
  auto staticData = std::make_unique<OpenCMLLexerStaticData>(
    std::vector<std::string>{
      "T__0", "T__1", "T__2", "T__3", "T__4", "T__5", "T__6", "T__7", "T__8", 
      "T__9", "T__10", "T__11", "T__12", "T__13", "T__14", "T__15", "T__16", 
      "T__17", "T__18", "AS", "LET", "USE", "FUNC", "TYPE", "ENUM", "WITH", 
      "RETURN", "INNER", "OUTER", "SYNC", "SCOPED", "STATIC", "ATOMIC", 
      "NULL", "TRUE", "FALSE", "ANY_TYPE", "NUMBER_TYPE", "STRING_TYPE", 
      "BOOLEAN_TYPE", "FUNCTOR_TYPE", "BLOCK_TYPE", "INTEGER_TYPE", "REAL_TYPE", 
      "COMPLEX_TYPE", "ARRAY_TYPE", "MATRIX_TYPE", "LIST_TYPE", "DICT_TYPE", 
      "SKIP_", "LINE_END", "BLANK", "LIN_CMT", "BLK_CMT", "LINE_MID", "MULTI_STR", 
      "IDENTIFIER", "UNIT", "STRING", "FSTRING", "INTEGER", "REAL", "DECIMAL", 
      "OCTAL", "HEXADECIMAL", "BINARY", "FLOAT", "EXPONENT_FLOAT", "EXPONENT_DECIMAL", 
      "EXPONENT"
    },
    std::vector<std::string>{
      "DEFAULT_TOKEN_CHANNEL", "HIDDEN"
    },
    std::vector<std::string>{
      "DEFAULT_MODE"
    },
    std::vector<std::string>{
      "", "':'", "'='", "'('", "','", "')'", "'['", "']'", "'@'", "'<'", 
      "'>'", "'{'", "'}'", "'.'", "'->'", "';'", "'+'", "'-'", "'i'", "'\\u003F'", 
      "'as'", "'let'", "'use'", "'func'", "'type'", "'enum'", "'with'", 
      "'return'", "'inner'", "'outer'", "'sync'", "'scoped'", "'static'", 
      "'atomic'", "'null'", "'true'", "'false'", "'any'", "'number'", "'string'", 
      "'bool'", "'functor'", "'block'", "'int'", "'real'", "'complex'", 
      "'array'", "'matrix'", "'list'", "'dict'"
    },
    std::vector<std::string>{
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "AS", "LET", "USE", "FUNC", "TYPE", "ENUM", "WITH", "RETURN", 
      "INNER", "OUTER", "SYNC", "SCOPED", "STATIC", "ATOMIC", "NULL", "TRUE", 
      "FALSE", "ANY_TYPE", "NUMBER_TYPE", "STRING_TYPE", "BOOLEAN_TYPE", 
      "FUNCTOR_TYPE", "BLOCK_TYPE", "INTEGER_TYPE", "REAL_TYPE", "COMPLEX_TYPE", 
      "ARRAY_TYPE", "MATRIX_TYPE", "LIST_TYPE", "DICT_TYPE", "SKIP_", "LINE_END", 
      "MULTI_STR", "IDENTIFIER", "UNIT", "STRING", "FSTRING", "INTEGER", 
      "REAL"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,0,58,580,6,-1,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,
  	6,2,7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,
  	7,14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,
  	7,21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,
  	7,28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,
  	7,35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,
  	7,42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,
  	7,49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,2,55,7,55,2,56,
  	7,56,2,57,7,57,2,58,7,58,2,59,7,59,2,60,7,60,2,61,7,61,2,62,7,62,2,63,
  	7,63,2,64,7,64,2,65,7,65,2,66,7,66,2,67,7,67,2,68,7,68,2,69,7,69,1,0,
  	1,0,1,1,1,1,1,2,1,2,1,3,1,3,1,4,1,4,1,5,1,5,1,6,1,6,1,7,1,7,1,8,1,8,1,
  	9,1,9,1,10,1,10,1,11,1,11,1,12,1,12,1,13,1,13,1,13,1,14,1,14,1,15,1,15,
  	1,16,1,16,1,17,1,17,1,18,1,18,1,19,1,19,1,19,1,20,1,20,1,20,1,20,1,21,
  	1,21,1,21,1,21,1,22,1,22,1,22,1,22,1,22,1,23,1,23,1,23,1,23,1,23,1,24,
  	1,24,1,24,1,24,1,24,1,25,1,25,1,25,1,25,1,25,1,26,1,26,1,26,1,26,1,26,
  	1,26,1,26,1,27,1,27,1,27,1,27,1,27,1,27,1,28,1,28,1,28,1,28,1,28,1,28,
  	1,29,1,29,1,29,1,29,1,29,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,31,1,31,
  	1,31,1,31,1,31,1,31,1,31,1,32,1,32,1,32,1,32,1,32,1,32,1,32,1,33,1,33,
  	1,33,1,33,1,33,1,34,1,34,1,34,1,34,1,34,1,35,1,35,1,35,1,35,1,35,1,35,
  	1,36,1,36,1,36,1,36,1,37,1,37,1,37,1,37,1,37,1,37,1,37,1,38,1,38,1,38,
  	1,38,1,38,1,38,1,38,1,39,1,39,1,39,1,39,1,39,1,40,1,40,1,40,1,40,1,40,
  	1,40,1,40,1,40,1,41,1,41,1,41,1,41,1,41,1,41,1,42,1,42,1,42,1,42,1,43,
  	1,43,1,43,1,43,1,43,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,44,1,45,1,45,
  	1,45,1,45,1,45,1,45,1,46,1,46,1,46,1,46,1,46,1,46,1,46,1,47,1,47,1,47,
  	1,47,1,47,1,48,1,48,1,48,1,48,1,48,1,49,1,49,1,49,1,49,3,49,354,8,49,
  	1,49,1,49,1,50,4,50,359,8,50,11,50,12,50,360,1,51,4,51,364,8,51,11,51,
  	12,51,365,1,52,1,52,1,52,1,52,5,52,372,8,52,10,52,12,52,375,9,52,1,52,
  	1,52,1,52,1,52,5,52,381,8,52,10,52,12,52,384,9,52,3,52,386,8,52,1,53,
  	1,53,1,53,1,53,5,53,392,8,53,10,53,12,53,395,9,53,1,53,1,53,1,53,1,53,
  	1,53,1,53,1,53,5,53,404,8,53,10,53,12,53,407,9,53,1,53,1,53,1,53,3,53,
  	412,8,53,1,54,1,54,3,54,416,8,54,1,54,1,54,1,55,1,55,1,55,1,55,1,55,5,
  	55,425,8,55,10,55,12,55,428,9,55,1,55,1,55,1,55,1,55,1,55,1,55,1,55,1,
  	55,5,55,438,8,55,10,55,12,55,441,9,55,1,55,1,55,1,55,3,55,446,8,55,1,
  	56,1,56,5,56,450,8,56,10,56,12,56,453,9,56,1,57,1,57,1,57,1,57,1,57,1,
  	57,5,57,461,8,57,10,57,12,57,464,9,57,1,57,1,57,1,58,1,58,1,58,1,58,1,
  	58,1,58,5,58,474,8,58,10,58,12,58,477,9,58,1,58,1,58,1,58,1,58,1,58,1,
  	58,1,58,5,58,486,8,58,10,58,12,58,489,9,58,1,58,3,58,492,8,58,1,59,1,
  	59,1,59,1,60,1,60,1,60,1,60,1,60,3,60,502,8,60,1,61,1,61,3,61,506,8,61,
  	1,62,3,62,509,8,62,1,62,1,62,5,62,513,8,62,10,62,12,62,516,9,62,1,62,
  	3,62,519,8,62,1,63,3,63,522,8,63,1,63,1,63,4,63,526,8,63,11,63,12,63,
  	527,1,64,3,64,531,8,64,1,64,1,64,1,64,1,64,4,64,537,8,64,11,64,12,64,
  	538,1,65,3,65,542,8,65,1,65,1,65,1,65,1,65,4,65,548,8,65,11,65,12,65,
  	549,1,66,3,66,553,8,66,1,66,4,66,556,8,66,11,66,12,66,557,1,66,1,66,4,
  	66,562,8,66,11,66,12,66,563,1,67,1,67,1,67,1,68,1,68,1,68,1,69,1,69,3,
  	69,574,8,69,1,69,4,69,577,8,69,11,69,12,69,578,7,393,405,426,439,462,
  	475,487,0,70,1,1,3,2,5,3,7,4,9,5,11,6,13,7,15,8,17,9,19,10,21,11,23,12,
  	25,13,27,14,29,15,31,16,33,17,35,18,37,19,39,20,41,21,43,22,45,23,47,
  	24,49,25,51,26,53,27,55,28,57,29,59,30,61,31,63,32,65,33,67,34,69,35,
  	71,36,73,37,75,38,77,39,79,40,81,41,83,42,85,43,87,44,89,45,91,46,93,
  	47,95,48,97,49,99,50,101,51,103,0,105,0,107,0,109,0,111,52,113,53,115,
  	54,117,55,119,56,121,57,123,58,125,0,127,0,129,0,131,0,133,0,135,0,137,
  	0,139,0,1,0,12,2,0,10,10,13,13,3,0,9,9,12,12,32,32,2,0,10,10,12,13,3,
  	0,65,90,95,95,97,122,4,0,48,57,65,90,95,95,97,122,2,0,43,43,45,45,1,0,
  	49,57,1,0,48,57,1,0,48,55,3,0,48,57,65,70,97,102,1,0,48,49,2,0,69,69,
  	101,101,612,0,1,1,0,0,0,0,3,1,0,0,0,0,5,1,0,0,0,0,7,1,0,0,0,0,9,1,0,0,
  	0,0,11,1,0,0,0,0,13,1,0,0,0,0,15,1,0,0,0,0,17,1,0,0,0,0,19,1,0,0,0,0,
  	21,1,0,0,0,0,23,1,0,0,0,0,25,1,0,0,0,0,27,1,0,0,0,0,29,1,0,0,0,0,31,1,
  	0,0,0,0,33,1,0,0,0,0,35,1,0,0,0,0,37,1,0,0,0,0,39,1,0,0,0,0,41,1,0,0,
  	0,0,43,1,0,0,0,0,45,1,0,0,0,0,47,1,0,0,0,0,49,1,0,0,0,0,51,1,0,0,0,0,
  	53,1,0,0,0,0,55,1,0,0,0,0,57,1,0,0,0,0,59,1,0,0,0,0,61,1,0,0,0,0,63,1,
  	0,0,0,0,65,1,0,0,0,0,67,1,0,0,0,0,69,1,0,0,0,0,71,1,0,0,0,0,73,1,0,0,
  	0,0,75,1,0,0,0,0,77,1,0,0,0,0,79,1,0,0,0,0,81,1,0,0,0,0,83,1,0,0,0,0,
  	85,1,0,0,0,0,87,1,0,0,0,0,89,1,0,0,0,0,91,1,0,0,0,0,93,1,0,0,0,0,95,1,
  	0,0,0,0,97,1,0,0,0,0,99,1,0,0,0,0,101,1,0,0,0,0,111,1,0,0,0,0,113,1,0,
  	0,0,0,115,1,0,0,0,0,117,1,0,0,0,0,119,1,0,0,0,0,121,1,0,0,0,0,123,1,0,
  	0,0,1,141,1,0,0,0,3,143,1,0,0,0,5,145,1,0,0,0,7,147,1,0,0,0,9,149,1,0,
  	0,0,11,151,1,0,0,0,13,153,1,0,0,0,15,155,1,0,0,0,17,157,1,0,0,0,19,159,
  	1,0,0,0,21,161,1,0,0,0,23,163,1,0,0,0,25,165,1,0,0,0,27,167,1,0,0,0,29,
  	170,1,0,0,0,31,172,1,0,0,0,33,174,1,0,0,0,35,176,1,0,0,0,37,178,1,0,0,
  	0,39,180,1,0,0,0,41,183,1,0,0,0,43,187,1,0,0,0,45,191,1,0,0,0,47,196,
  	1,0,0,0,49,201,1,0,0,0,51,206,1,0,0,0,53,211,1,0,0,0,55,218,1,0,0,0,57,
  	224,1,0,0,0,59,230,1,0,0,0,61,235,1,0,0,0,63,242,1,0,0,0,65,249,1,0,0,
  	0,67,256,1,0,0,0,69,261,1,0,0,0,71,266,1,0,0,0,73,272,1,0,0,0,75,276,
  	1,0,0,0,77,283,1,0,0,0,79,290,1,0,0,0,81,295,1,0,0,0,83,303,1,0,0,0,85,
  	309,1,0,0,0,87,313,1,0,0,0,89,318,1,0,0,0,91,326,1,0,0,0,93,332,1,0,0,
  	0,95,339,1,0,0,0,97,344,1,0,0,0,99,353,1,0,0,0,101,358,1,0,0,0,103,363,
  	1,0,0,0,105,385,1,0,0,0,107,411,1,0,0,0,109,413,1,0,0,0,111,445,1,0,0,
  	0,113,447,1,0,0,0,115,454,1,0,0,0,117,491,1,0,0,0,119,493,1,0,0,0,121,
  	501,1,0,0,0,123,505,1,0,0,0,125,508,1,0,0,0,127,521,1,0,0,0,129,530,1,
  	0,0,0,131,541,1,0,0,0,133,552,1,0,0,0,135,565,1,0,0,0,137,568,1,0,0,0,
  	139,571,1,0,0,0,141,142,5,58,0,0,142,2,1,0,0,0,143,144,5,61,0,0,144,4,
  	1,0,0,0,145,146,5,40,0,0,146,6,1,0,0,0,147,148,5,44,0,0,148,8,1,0,0,0,
  	149,150,5,41,0,0,150,10,1,0,0,0,151,152,5,91,0,0,152,12,1,0,0,0,153,154,
  	5,93,0,0,154,14,1,0,0,0,155,156,5,64,0,0,156,16,1,0,0,0,157,158,5,60,
  	0,0,158,18,1,0,0,0,159,160,5,62,0,0,160,20,1,0,0,0,161,162,5,123,0,0,
  	162,22,1,0,0,0,163,164,5,125,0,0,164,24,1,0,0,0,165,166,5,46,0,0,166,
  	26,1,0,0,0,167,168,5,45,0,0,168,169,5,62,0,0,169,28,1,0,0,0,170,171,5,
  	59,0,0,171,30,1,0,0,0,172,173,5,43,0,0,173,32,1,0,0,0,174,175,5,45,0,
  	0,175,34,1,0,0,0,176,177,5,105,0,0,177,36,1,0,0,0,178,179,5,63,0,0,179,
  	38,1,0,0,0,180,181,5,97,0,0,181,182,5,115,0,0,182,40,1,0,0,0,183,184,
  	5,108,0,0,184,185,5,101,0,0,185,186,5,116,0,0,186,42,1,0,0,0,187,188,
  	5,117,0,0,188,189,5,115,0,0,189,190,5,101,0,0,190,44,1,0,0,0,191,192,
  	5,102,0,0,192,193,5,117,0,0,193,194,5,110,0,0,194,195,5,99,0,0,195,46,
  	1,0,0,0,196,197,5,116,0,0,197,198,5,121,0,0,198,199,5,112,0,0,199,200,
  	5,101,0,0,200,48,1,0,0,0,201,202,5,101,0,0,202,203,5,110,0,0,203,204,
  	5,117,0,0,204,205,5,109,0,0,205,50,1,0,0,0,206,207,5,119,0,0,207,208,
  	5,105,0,0,208,209,5,116,0,0,209,210,5,104,0,0,210,52,1,0,0,0,211,212,
  	5,114,0,0,212,213,5,101,0,0,213,214,5,116,0,0,214,215,5,117,0,0,215,216,
  	5,114,0,0,216,217,5,110,0,0,217,54,1,0,0,0,218,219,5,105,0,0,219,220,
  	5,110,0,0,220,221,5,110,0,0,221,222,5,101,0,0,222,223,5,114,0,0,223,56,
  	1,0,0,0,224,225,5,111,0,0,225,226,5,117,0,0,226,227,5,116,0,0,227,228,
  	5,101,0,0,228,229,5,114,0,0,229,58,1,0,0,0,230,231,5,115,0,0,231,232,
  	5,121,0,0,232,233,5,110,0,0,233,234,5,99,0,0,234,60,1,0,0,0,235,236,5,
  	115,0,0,236,237,5,99,0,0,237,238,5,111,0,0,238,239,5,112,0,0,239,240,
  	5,101,0,0,240,241,5,100,0,0,241,62,1,0,0,0,242,243,5,115,0,0,243,244,
  	5,116,0,0,244,245,5,97,0,0,245,246,5,116,0,0,246,247,5,105,0,0,247,248,
  	5,99,0,0,248,64,1,0,0,0,249,250,5,97,0,0,250,251,5,116,0,0,251,252,5,
  	111,0,0,252,253,5,109,0,0,253,254,5,105,0,0,254,255,5,99,0,0,255,66,1,
  	0,0,0,256,257,5,110,0,0,257,258,5,117,0,0,258,259,5,108,0,0,259,260,5,
  	108,0,0,260,68,1,0,0,0,261,262,5,116,0,0,262,263,5,114,0,0,263,264,5,
  	117,0,0,264,265,5,101,0,0,265,70,1,0,0,0,266,267,5,102,0,0,267,268,5,
  	97,0,0,268,269,5,108,0,0,269,270,5,115,0,0,270,271,5,101,0,0,271,72,1,
  	0,0,0,272,273,5,97,0,0,273,274,5,110,0,0,274,275,5,121,0,0,275,74,1,0,
  	0,0,276,277,5,110,0,0,277,278,5,117,0,0,278,279,5,109,0,0,279,280,5,98,
  	0,0,280,281,5,101,0,0,281,282,5,114,0,0,282,76,1,0,0,0,283,284,5,115,
  	0,0,284,285,5,116,0,0,285,286,5,114,0,0,286,287,5,105,0,0,287,288,5,110,
  	0,0,288,289,5,103,0,0,289,78,1,0,0,0,290,291,5,98,0,0,291,292,5,111,0,
  	0,292,293,5,111,0,0,293,294,5,108,0,0,294,80,1,0,0,0,295,296,5,102,0,
  	0,296,297,5,117,0,0,297,298,5,110,0,0,298,299,5,99,0,0,299,300,5,116,
  	0,0,300,301,5,111,0,0,301,302,5,114,0,0,302,82,1,0,0,0,303,304,5,98,0,
  	0,304,305,5,108,0,0,305,306,5,111,0,0,306,307,5,99,0,0,307,308,5,107,
  	0,0,308,84,1,0,0,0,309,310,5,105,0,0,310,311,5,110,0,0,311,312,5,116,
  	0,0,312,86,1,0,0,0,313,314,5,114,0,0,314,315,5,101,0,0,315,316,5,97,0,
  	0,316,317,5,108,0,0,317,88,1,0,0,0,318,319,5,99,0,0,319,320,5,111,0,0,
  	320,321,5,109,0,0,321,322,5,112,0,0,322,323,5,108,0,0,323,324,5,101,0,
  	0,324,325,5,120,0,0,325,90,1,0,0,0,326,327,5,97,0,0,327,328,5,114,0,0,
  	328,329,5,114,0,0,329,330,5,97,0,0,330,331,5,121,0,0,331,92,1,0,0,0,332,
  	333,5,109,0,0,333,334,5,97,0,0,334,335,5,116,0,0,335,336,5,114,0,0,336,
  	337,5,105,0,0,337,338,5,120,0,0,338,94,1,0,0,0,339,340,5,108,0,0,340,
  	341,5,105,0,0,341,342,5,115,0,0,342,343,5,116,0,0,343,96,1,0,0,0,344,
  	345,5,100,0,0,345,346,5,105,0,0,346,347,5,99,0,0,347,348,5,116,0,0,348,
  	98,1,0,0,0,349,354,3,103,51,0,350,354,3,105,52,0,351,354,3,107,53,0,352,
  	354,3,109,54,0,353,349,1,0,0,0,353,350,1,0,0,0,353,351,1,0,0,0,353,352,
  	1,0,0,0,354,355,1,0,0,0,355,356,6,49,0,0,356,100,1,0,0,0,357,359,7,0,
  	0,0,358,357,1,0,0,0,359,360,1,0,0,0,360,358,1,0,0,0,360,361,1,0,0,0,361,
  	102,1,0,0,0,362,364,7,1,0,0,363,362,1,0,0,0,364,365,1,0,0,0,365,363,1,
  	0,0,0,365,366,1,0,0,0,366,104,1,0,0,0,367,368,5,47,0,0,368,369,5,47,0,
  	0,369,373,1,0,0,0,370,372,8,0,0,0,371,370,1,0,0,0,372,375,1,0,0,0,373,
  	371,1,0,0,0,373,374,1,0,0,0,374,386,1,0,0,0,375,373,1,0,0,0,376,377,5,
  	35,0,0,377,378,5,32,0,0,378,382,1,0,0,0,379,381,8,2,0,0,380,379,1,0,0,
  	0,381,384,1,0,0,0,382,380,1,0,0,0,382,383,1,0,0,0,383,386,1,0,0,0,384,
  	382,1,0,0,0,385,367,1,0,0,0,385,376,1,0,0,0,386,106,1,0,0,0,387,388,5,
  	47,0,0,388,389,5,42,0,0,389,393,1,0,0,0,390,392,9,0,0,0,391,390,1,0,0,
  	0,392,395,1,0,0,0,393,394,1,0,0,0,393,391,1,0,0,0,394,396,1,0,0,0,395,
  	393,1,0,0,0,396,397,5,42,0,0,397,412,5,47,0,0,398,399,5,96,0,0,399,400,
  	5,96,0,0,400,401,5,96,0,0,401,405,1,0,0,0,402,404,9,0,0,0,403,402,1,0,
  	0,0,404,407,1,0,0,0,405,406,1,0,0,0,405,403,1,0,0,0,406,408,1,0,0,0,407,
  	405,1,0,0,0,408,409,5,96,0,0,409,410,5,96,0,0,410,412,5,96,0,0,411,387,
  	1,0,0,0,411,398,1,0,0,0,412,108,1,0,0,0,413,415,5,92,0,0,414,416,5,13,
  	0,0,415,414,1,0,0,0,415,416,1,0,0,0,416,417,1,0,0,0,417,418,5,10,0,0,
  	418,110,1,0,0,0,419,420,5,39,0,0,420,421,5,39,0,0,421,422,5,39,0,0,422,
  	426,1,0,0,0,423,425,9,0,0,0,424,423,1,0,0,0,425,428,1,0,0,0,426,427,1,
  	0,0,0,426,424,1,0,0,0,427,429,1,0,0,0,428,426,1,0,0,0,429,430,5,39,0,
  	0,430,431,5,39,0,0,431,446,5,39,0,0,432,433,5,34,0,0,433,434,5,34,0,0,
  	434,435,5,34,0,0,435,439,1,0,0,0,436,438,9,0,0,0,437,436,1,0,0,0,438,
  	441,1,0,0,0,439,440,1,0,0,0,439,437,1,0,0,0,440,442,1,0,0,0,441,439,1,
  	0,0,0,442,443,5,34,0,0,443,444,5,34,0,0,444,446,5,34,0,0,445,419,1,0,
  	0,0,445,432,1,0,0,0,446,112,1,0,0,0,447,451,7,3,0,0,448,450,7,4,0,0,449,
  	448,1,0,0,0,450,453,1,0,0,0,451,449,1,0,0,0,451,452,1,0,0,0,452,114,1,
  	0,0,0,453,451,1,0,0,0,454,462,5,96,0,0,455,456,5,92,0,0,456,461,5,96,
  	0,0,457,458,5,92,0,0,458,461,5,92,0,0,459,461,9,0,0,0,460,455,1,0,0,0,
  	460,457,1,0,0,0,460,459,1,0,0,0,461,464,1,0,0,0,462,463,1,0,0,0,462,460,
  	1,0,0,0,463,465,1,0,0,0,464,462,1,0,0,0,465,466,5,96,0,0,466,116,1,0,
  	0,0,467,475,5,34,0,0,468,469,5,92,0,0,469,474,5,34,0,0,470,471,5,92,0,
  	0,471,474,5,92,0,0,472,474,9,0,0,0,473,468,1,0,0,0,473,470,1,0,0,0,473,
  	472,1,0,0,0,474,477,1,0,0,0,475,476,1,0,0,0,475,473,1,0,0,0,476,478,1,
  	0,0,0,477,475,1,0,0,0,478,492,5,34,0,0,479,487,5,39,0,0,480,481,5,92,
  	0,0,481,486,5,39,0,0,482,483,5,92,0,0,483,486,5,92,0,0,484,486,9,0,0,
  	0,485,480,1,0,0,0,485,482,1,0,0,0,485,484,1,0,0,0,486,489,1,0,0,0,487,
  	488,1,0,0,0,487,485,1,0,0,0,488,490,1,0,0,0,489,487,1,0,0,0,490,492,5,
  	39,0,0,491,467,1,0,0,0,491,479,1,0,0,0,492,118,1,0,0,0,493,494,5,102,
  	0,0,494,495,3,117,58,0,495,120,1,0,0,0,496,502,3,125,62,0,497,502,3,127,
  	63,0,498,502,3,129,64,0,499,502,3,131,65,0,500,502,3,137,68,0,501,496,
  	1,0,0,0,501,497,1,0,0,0,501,498,1,0,0,0,501,499,1,0,0,0,501,500,1,0,0,
  	0,502,122,1,0,0,0,503,506,3,133,66,0,504,506,3,135,67,0,505,503,1,0,0,
  	0,505,504,1,0,0,0,506,124,1,0,0,0,507,509,7,5,0,0,508,507,1,0,0,0,508,
  	509,1,0,0,0,509,518,1,0,0,0,510,514,7,6,0,0,511,513,7,7,0,0,512,511,1,
  	0,0,0,513,516,1,0,0,0,514,512,1,0,0,0,514,515,1,0,0,0,515,519,1,0,0,0,
  	516,514,1,0,0,0,517,519,5,48,0,0,518,510,1,0,0,0,518,517,1,0,0,0,519,
  	126,1,0,0,0,520,522,7,5,0,0,521,520,1,0,0,0,521,522,1,0,0,0,522,523,1,
  	0,0,0,523,525,5,48,0,0,524,526,7,8,0,0,525,524,1,0,0,0,526,527,1,0,0,
  	0,527,525,1,0,0,0,527,528,1,0,0,0,528,128,1,0,0,0,529,531,7,5,0,0,530,
  	529,1,0,0,0,530,531,1,0,0,0,531,532,1,0,0,0,532,533,5,48,0,0,533,534,
  	5,120,0,0,534,536,1,0,0,0,535,537,7,9,0,0,536,535,1,0,0,0,537,538,1,0,
  	0,0,538,536,1,0,0,0,538,539,1,0,0,0,539,130,1,0,0,0,540,542,7,5,0,0,541,
  	540,1,0,0,0,541,542,1,0,0,0,542,543,1,0,0,0,543,544,5,48,0,0,544,545,
  	5,98,0,0,545,547,1,0,0,0,546,548,7,10,0,0,547,546,1,0,0,0,548,549,1,0,
  	0,0,549,547,1,0,0,0,549,550,1,0,0,0,550,132,1,0,0,0,551,553,7,5,0,0,552,
  	551,1,0,0,0,552,553,1,0,0,0,553,555,1,0,0,0,554,556,7,7,0,0,555,554,1,
  	0,0,0,556,557,1,0,0,0,557,555,1,0,0,0,557,558,1,0,0,0,558,559,1,0,0,0,
  	559,561,5,46,0,0,560,562,7,7,0,0,561,560,1,0,0,0,562,563,1,0,0,0,563,
  	561,1,0,0,0,563,564,1,0,0,0,564,134,1,0,0,0,565,566,3,133,66,0,566,567,
  	3,139,69,0,567,136,1,0,0,0,568,569,3,125,62,0,569,570,3,139,69,0,570,
  	138,1,0,0,0,571,573,7,11,0,0,572,574,7,5,0,0,573,572,1,0,0,0,573,574,
  	1,0,0,0,574,576,1,0,0,0,575,577,7,7,0,0,576,575,1,0,0,0,577,578,1,0,0,
  	0,578,576,1,0,0,0,578,579,1,0,0,0,579,140,1,0,0,0,38,0,353,360,365,373,
  	382,385,393,405,411,415,426,439,445,451,460,462,473,475,485,487,491,501,
  	505,508,514,518,521,527,530,538,541,549,552,557,563,573,578,1,6,0,0
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  opencmllexerLexerStaticData = staticData.release();
}

}

OpenCMLLexer::OpenCMLLexer(CharStream *input) : Lexer(input) {
  OpenCMLLexer::initialize();
  _interpreter = new atn::LexerATNSimulator(this, *opencmllexerLexerStaticData->atn, opencmllexerLexerStaticData->decisionToDFA, opencmllexerLexerStaticData->sharedContextCache);
}

OpenCMLLexer::~OpenCMLLexer() {
  delete _interpreter;
}

std::string OpenCMLLexer::getGrammarFileName() const {
  return "OpenCML.g4";
}

const std::vector<std::string>& OpenCMLLexer::getRuleNames() const {
  return opencmllexerLexerStaticData->ruleNames;
}

const std::vector<std::string>& OpenCMLLexer::getChannelNames() const {
  return opencmllexerLexerStaticData->channelNames;
}

const std::vector<std::string>& OpenCMLLexer::getModeNames() const {
  return opencmllexerLexerStaticData->modeNames;
}

const dfa::Vocabulary& OpenCMLLexer::getVocabulary() const {
  return opencmllexerLexerStaticData->vocabulary;
}

antlr4::atn::SerializedATNView OpenCMLLexer::getSerializedATN() const {
  return opencmllexerLexerStaticData->serializedATN;
}

const atn::ATN& OpenCMLLexer::getATN() const {
  return *opencmllexerLexerStaticData->atn;
}




void OpenCMLLexer::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  opencmllexerLexerInitialize();
#else
  ::antlr4::internal::call_once(opencmllexerLexerOnceFlag, opencmllexerLexerInitialize);
#endif
}