
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
      "assignStmt", "withDef", "funcDef", "retStmt", "lambdaExpr", "carrier", 
      "annotation", "annotations", "modifiers", "keyTypePair", "keyValuePair", 
      "keyParamPair", "indexKTPair", "indexKVPair", "typeList", "identList", 
      "valueList", "pairedTypes", "pairedValues", "pairedParams", "indexKVPairs", 
      "argumentList", "bracedValues", "bracedIndexKVPairs", "bracedPairedValues", 
      "bracedIdents", "bracedStmts", "bracketIdents", "bracketValues", "parentParams", 
      "parentValues", "angledParams", "angledValues", "primEntity", "memberAccess", 
      "entity", "entityChain", "entityLink", "entityCall", "entitySpread", 
      "entityExpr", "relaExpr", "addExpr", "multiExpr", "unaryExpr", "primExpr", 
      "literal", "typeExpr", "type", "primType", "structType", "specialType", 
      "identRef"
    },
    std::vector<std::string>{
      "", "':'", "'='", "':='", "'::'", "'=>'", "'@'", "'['", "']'", "','", 
      "'{'", "'}'", "'('", "')'", "'<'", "'>'", "'->'", "'.'", "'...'", 
      "'+='", "'-='", "'*='", "'/='", "'%='", "'^='", "'&='", "'|='", "'<='", 
      "'>='", "'=='", "'!='", "'&&'", "'||'", "'+'", "'-'", "'&'", "'|'", 
      "'^'", "'*'", "'/'", "'%'", "'!'", "'++'", "'--'", "';'", "'as'", 
      "'is'", "'let'", "'use'", "'func'", "'type'", "'with'", "'return'", 
      "'inner'", "'outer'", "'atomic'", "'static'", "'sync'", "'null'", 
      "'true'", "'false'", "'Int'", "'Int32'", "'Int64'", "'Real'", "'Float'", 
      "'Double'", "'Number'", "'String'", "'Bool'", "'Char'", "'Set'", "'Map'", 
      "'List'", "'Dict'", "'Array'", "'Tuple'", "'Union'", "'Vector'", "'Tensor'", 
      "'Any'", "'Void'", "'Functor'"
    },
    std::vector<std::string>{
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
      "", "", "", "", "", "", "", "", "", "", "SEP", "AS", "IS", "LET", 
      "USE", "FUNC", "TYPE", "WITH", "RETURN", "INNER", "OUTER", "ATOMIC", 
      "STATIC", "SYNC", "NULL", "TRUE", "FALSE", "INTEGER_TYPE", "INTEGER32_TYPE", 
      "INTEGER64_TYPE", "REAL_TYPE", "FLOAT_TYPE", "DOUBLE_TYPE", "NUMBER_TYPE", 
      "STRING_TYPE", "BOOL_TYPE", "CHAR_TYPE", "SET_TYPE", "MAP_TYPE", "LIST_TYPE", 
      "DICT_TYPE", "ARRAY_TYPE", "TUPLE_TYPE", "UNION_TYPE", "VECTOR_TYPE", 
      "TENSOR_TYPE", "ANY_TYPE", "VOID_TYPE", "FUNCTOR_TYPE", "SKIP_", "MULTI_STR", 
      "IDENTIFIER", "UNIT", "STRING", "FSTRING", "INTEGER", "REAL"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,90,777,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,2,55,7,55,2,56,7,
  	56,2,57,7,57,2,58,7,58,2,59,7,59,1,0,3,0,122,8,0,1,0,1,0,1,1,1,1,1,1,
  	5,1,129,8,1,10,1,12,1,132,9,1,1,1,3,1,135,8,1,1,2,1,2,1,2,1,2,1,2,1,2,
  	1,2,3,2,144,8,2,1,3,1,3,1,3,1,3,3,3,150,8,3,1,3,3,3,153,8,3,1,3,3,3,156,
  	8,3,1,3,1,3,1,3,3,3,161,8,3,1,3,1,3,1,3,3,3,166,8,3,1,4,1,4,1,4,3,4,171,
  	8,4,1,4,1,4,1,4,1,4,1,4,1,4,3,4,179,8,4,1,5,1,5,1,5,3,5,184,8,5,1,5,1,
  	5,1,6,3,6,189,8,6,1,6,1,6,1,7,1,7,3,7,195,8,7,1,7,1,7,1,7,1,8,1,8,1,8,
  	1,9,3,9,204,8,9,1,9,3,9,207,8,9,1,9,3,9,210,8,9,1,9,1,9,1,9,1,9,1,9,3,
  	9,217,8,9,1,9,1,9,1,10,1,10,3,10,223,8,10,1,11,3,11,226,8,11,1,11,1,11,
  	1,11,3,11,231,8,11,1,11,1,11,3,11,235,8,11,1,11,1,11,1,12,1,12,1,12,3,
  	12,242,8,12,1,13,1,13,1,13,1,14,4,14,248,8,14,11,14,12,14,249,1,15,4,
  	15,253,8,15,11,15,12,15,254,1,16,1,16,1,16,1,16,1,17,1,17,1,17,1,17,1,
  	18,1,18,3,18,267,8,18,1,18,1,18,1,18,1,18,3,18,273,8,18,1,19,1,19,1,19,
  	1,19,1,19,1,19,1,20,1,20,1,20,1,20,1,20,1,20,1,21,1,21,1,21,5,21,290,
  	8,21,10,21,12,21,293,9,21,1,22,1,22,1,22,5,22,298,8,22,10,22,12,22,301,
  	9,22,1,23,1,23,1,23,5,23,306,8,23,10,23,12,23,309,9,23,1,24,1,24,1,24,
  	5,24,314,8,24,10,24,12,24,317,9,24,1,25,1,25,1,25,5,25,322,8,25,10,25,
  	12,25,325,9,25,1,26,1,26,1,26,5,26,330,8,26,10,26,12,26,333,9,26,1,27,
  	1,27,1,27,5,27,338,8,27,10,27,12,27,341,9,27,1,28,1,28,1,28,3,28,346,
  	8,28,1,28,3,28,349,8,28,1,29,1,29,3,29,353,8,29,1,29,3,29,356,8,29,1,
  	29,1,29,1,30,1,30,3,30,362,8,30,1,30,3,30,365,8,30,1,30,1,30,1,31,1,31,
  	3,31,371,8,31,1,31,3,31,374,8,31,1,31,1,31,1,32,1,32,3,32,380,8,32,1,
  	32,3,32,383,8,32,1,32,1,32,1,33,1,33,3,33,389,8,33,1,33,1,33,1,34,1,34,
  	3,34,395,8,34,1,34,3,34,398,8,34,1,34,1,34,1,35,1,35,3,35,404,8,35,1,
  	35,3,35,407,8,35,1,35,1,35,1,36,1,36,3,36,413,8,36,1,36,3,36,416,8,36,
  	1,36,1,36,1,37,1,37,3,37,422,8,37,1,37,3,37,425,8,37,1,37,1,37,1,38,1,
  	38,3,38,431,8,38,1,38,3,38,434,8,38,1,38,1,38,1,39,1,39,3,39,440,8,39,
  	1,39,3,39,443,8,39,1,39,1,39,1,40,1,40,1,40,1,40,1,40,1,40,1,40,1,40,
  	1,40,1,40,1,40,3,40,458,8,40,1,41,1,41,1,41,1,41,1,42,1,42,1,42,1,42,
  	1,42,5,42,469,8,42,10,42,12,42,472,9,42,1,43,4,43,475,8,43,11,43,12,43,
  	476,1,44,1,44,1,44,1,44,1,44,1,44,5,44,485,8,44,10,44,12,44,488,9,44,
  	1,45,1,45,1,45,1,45,1,45,1,45,5,45,496,8,45,10,45,12,45,499,9,45,1,46,
  	1,46,1,46,1,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,
  	1,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,1,47,
  	1,47,5,47,531,8,47,10,47,12,47,534,9,47,1,48,1,48,1,48,1,48,1,48,1,48,
  	1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,1,48,
  	1,48,1,48,1,48,1,48,1,48,1,48,1,48,5,48,563,8,48,10,48,12,48,566,9,48,
  	1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,1,49,
  	1,49,5,49,583,8,49,10,49,12,49,586,9,49,1,50,1,50,1,50,1,50,1,50,1,50,
  	1,50,1,50,1,50,1,50,1,50,1,50,1,50,1,50,1,50,1,50,1,50,1,50,1,50,1,50,
  	1,50,5,50,609,8,50,10,50,12,50,612,9,50,1,51,1,51,1,51,1,51,1,51,1,51,
  	1,51,1,51,1,51,1,51,1,51,1,51,1,51,3,51,627,8,51,1,52,1,52,1,52,1,52,
  	1,52,3,52,634,8,52,1,53,1,53,3,53,638,8,53,1,53,1,53,3,53,642,8,53,1,
  	53,1,53,1,53,1,53,1,53,1,53,3,53,650,8,53,1,54,1,54,1,54,1,54,3,54,656,
  	8,54,1,54,1,54,1,54,1,54,1,54,1,54,5,54,664,8,54,10,54,12,54,667,9,54,
  	1,55,1,55,1,55,1,55,1,55,1,55,1,55,1,55,3,55,677,8,55,1,56,1,56,1,57,
  	1,57,1,57,1,57,1,57,3,57,686,8,57,1,57,1,57,1,57,1,57,1,57,1,57,1,57,
  	3,57,695,8,57,1,57,1,57,1,57,1,57,1,57,1,57,1,57,1,57,3,57,705,8,57,1,
  	57,3,57,708,8,57,1,57,1,57,1,57,1,57,1,57,1,57,3,57,716,8,57,1,57,1,57,
  	1,57,3,57,721,8,57,1,57,3,57,724,8,57,1,57,3,57,727,8,57,1,57,1,57,1,
  	57,3,57,732,8,57,1,57,3,57,735,8,57,1,57,3,57,738,8,57,1,57,1,57,1,57,
  	1,57,1,57,3,57,745,8,57,1,57,1,57,3,57,749,8,57,1,57,1,57,1,57,1,57,1,
  	57,1,57,1,57,1,57,5,57,759,8,57,10,57,12,57,762,9,57,1,57,3,57,765,8,
  	57,1,57,1,57,3,57,769,8,57,3,57,771,8,57,1,58,1,58,1,59,1,59,1,59,0,7,
  	88,90,94,96,98,100,108,60,0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,
  	32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,68,70,72,74,76,
  	78,80,82,84,86,88,90,92,94,96,98,100,102,104,106,108,110,112,114,116,
  	118,0,3,1,0,53,57,1,0,61,70,1,0,80,82,867,0,121,1,0,0,0,2,125,1,0,0,0,
  	4,143,1,0,0,0,6,165,1,0,0,0,8,178,1,0,0,0,10,180,1,0,0,0,12,188,1,0,0,
  	0,14,192,1,0,0,0,16,199,1,0,0,0,18,203,1,0,0,0,20,220,1,0,0,0,22,225,
  	1,0,0,0,24,241,1,0,0,0,26,243,1,0,0,0,28,247,1,0,0,0,30,252,1,0,0,0,32,
  	256,1,0,0,0,34,260,1,0,0,0,36,264,1,0,0,0,38,274,1,0,0,0,40,280,1,0,0,
  	0,42,286,1,0,0,0,44,294,1,0,0,0,46,302,1,0,0,0,48,310,1,0,0,0,50,318,
  	1,0,0,0,52,326,1,0,0,0,54,334,1,0,0,0,56,348,1,0,0,0,58,350,1,0,0,0,60,
  	359,1,0,0,0,62,368,1,0,0,0,64,377,1,0,0,0,66,386,1,0,0,0,68,392,1,0,0,
  	0,70,401,1,0,0,0,72,410,1,0,0,0,74,419,1,0,0,0,76,428,1,0,0,0,78,437,
  	1,0,0,0,80,457,1,0,0,0,82,459,1,0,0,0,84,463,1,0,0,0,86,474,1,0,0,0,88,
  	478,1,0,0,0,90,489,1,0,0,0,92,500,1,0,0,0,94,503,1,0,0,0,96,535,1,0,0,
  	0,98,567,1,0,0,0,100,587,1,0,0,0,102,626,1,0,0,0,104,633,1,0,0,0,106,
  	649,1,0,0,0,108,651,1,0,0,0,110,676,1,0,0,0,112,678,1,0,0,0,114,770,1,
  	0,0,0,116,772,1,0,0,0,118,774,1,0,0,0,120,122,3,2,1,0,121,120,1,0,0,0,
  	121,122,1,0,0,0,122,123,1,0,0,0,123,124,5,0,0,1,124,1,1,0,0,0,125,130,
  	3,4,2,0,126,127,5,44,0,0,127,129,3,4,2,0,128,126,1,0,0,0,129,132,1,0,
  	0,0,130,128,1,0,0,0,130,131,1,0,0,0,131,134,1,0,0,0,132,130,1,0,0,0,133,
  	135,5,44,0,0,134,133,1,0,0,0,134,135,1,0,0,0,135,3,1,0,0,0,136,144,3,
  	6,3,0,137,144,3,8,4,0,138,144,3,10,5,0,139,144,3,12,6,0,140,144,3,14,
  	7,0,141,144,3,18,9,0,142,144,3,20,10,0,143,136,1,0,0,0,143,137,1,0,0,
  	0,143,138,1,0,0,0,143,139,1,0,0,0,143,140,1,0,0,0,143,141,1,0,0,0,143,
  	142,1,0,0,0,144,5,1,0,0,0,145,146,5,47,0,0,146,149,3,24,12,0,147,148,
  	5,1,0,0,148,150,3,108,54,0,149,147,1,0,0,0,149,150,1,0,0,0,150,155,1,
  	0,0,0,151,153,5,2,0,0,152,151,1,0,0,0,152,153,1,0,0,0,153,154,1,0,0,0,
  	154,156,3,94,47,0,155,152,1,0,0,0,155,156,1,0,0,0,156,166,1,0,0,0,157,
  	160,3,24,12,0,158,159,5,1,0,0,159,161,3,108,54,0,160,158,1,0,0,0,160,
  	161,1,0,0,0,161,162,1,0,0,0,162,163,5,3,0,0,163,164,3,94,47,0,164,166,
  	1,0,0,0,165,145,1,0,0,0,165,157,1,0,0,0,166,7,1,0,0,0,167,168,5,48,0,
  	0,168,170,3,24,12,0,169,171,5,2,0,0,170,169,1,0,0,0,170,171,1,0,0,0,171,
  	172,1,0,0,0,172,173,3,94,47,0,173,179,1,0,0,0,174,175,3,24,12,0,175,176,
  	5,4,0,0,176,177,3,94,47,0,177,179,1,0,0,0,178,167,1,0,0,0,178,174,1,0,
  	0,0,179,9,1,0,0,0,180,181,5,50,0,0,181,183,3,118,59,0,182,184,5,2,0,0,
  	183,182,1,0,0,0,183,184,1,0,0,0,184,185,1,0,0,0,185,186,3,108,54,0,186,
  	11,1,0,0,0,187,189,3,28,14,0,188,187,1,0,0,0,188,189,1,0,0,0,189,190,
  	1,0,0,0,190,191,3,94,47,0,191,13,1,0,0,0,192,194,3,118,59,0,193,195,3,
  	82,41,0,194,193,1,0,0,0,194,195,1,0,0,0,195,196,1,0,0,0,196,197,5,2,0,
  	0,197,198,3,94,47,0,198,15,1,0,0,0,199,200,5,51,0,0,200,201,3,76,38,0,
  	201,17,1,0,0,0,202,204,3,28,14,0,203,202,1,0,0,0,203,204,1,0,0,0,204,
  	206,1,0,0,0,205,207,3,16,8,0,206,205,1,0,0,0,206,207,1,0,0,0,207,209,
  	1,0,0,0,208,210,3,30,15,0,209,208,1,0,0,0,209,210,1,0,0,0,210,211,1,0,
  	0,0,211,212,5,49,0,0,212,213,3,118,59,0,213,216,3,72,36,0,214,215,5,1,
  	0,0,215,217,3,108,54,0,216,214,1,0,0,0,216,217,1,0,0,0,217,218,1,0,0,
  	0,218,219,3,66,33,0,219,19,1,0,0,0,220,222,5,52,0,0,221,223,3,94,47,0,
  	222,221,1,0,0,0,222,223,1,0,0,0,223,21,1,0,0,0,224,226,3,30,15,0,225,
  	224,1,0,0,0,225,226,1,0,0,0,226,234,1,0,0,0,227,230,3,72,36,0,228,229,
  	5,1,0,0,229,231,3,108,54,0,230,228,1,0,0,0,230,231,1,0,0,0,231,232,1,
  	0,0,0,232,233,5,5,0,0,233,235,1,0,0,0,234,227,1,0,0,0,234,235,1,0,0,0,
  	235,236,1,0,0,0,236,237,3,66,33,0,237,23,1,0,0,0,238,242,3,118,59,0,239,
  	242,3,64,32,0,240,242,3,68,34,0,241,238,1,0,0,0,241,239,1,0,0,0,241,240,
  	1,0,0,0,242,25,1,0,0,0,243,244,5,6,0,0,244,245,3,80,40,0,245,27,1,0,0,
  	0,246,248,3,26,13,0,247,246,1,0,0,0,248,249,1,0,0,0,249,247,1,0,0,0,249,
  	250,1,0,0,0,250,29,1,0,0,0,251,253,7,0,0,0,252,251,1,0,0,0,253,254,1,
  	0,0,0,254,252,1,0,0,0,254,255,1,0,0,0,255,31,1,0,0,0,256,257,3,118,59,
  	0,257,258,5,1,0,0,258,259,3,108,54,0,259,33,1,0,0,0,260,261,3,94,47,0,
  	261,262,5,1,0,0,262,263,3,94,47,0,263,35,1,0,0,0,264,266,3,118,59,0,265,
  	267,3,26,13,0,266,265,1,0,0,0,266,267,1,0,0,0,267,268,1,0,0,0,268,269,
  	5,1,0,0,269,272,3,108,54,0,270,271,5,2,0,0,271,273,3,94,47,0,272,270,
  	1,0,0,0,272,273,1,0,0,0,273,37,1,0,0,0,274,275,5,7,0,0,275,276,3,94,47,
  	0,276,277,5,8,0,0,277,278,5,1,0,0,278,279,3,108,54,0,279,39,1,0,0,0,280,
  	281,5,7,0,0,281,282,3,94,47,0,282,283,5,8,0,0,283,284,5,1,0,0,284,285,
  	3,94,47,0,285,41,1,0,0,0,286,291,3,108,54,0,287,288,5,9,0,0,288,290,3,
  	108,54,0,289,287,1,0,0,0,290,293,1,0,0,0,291,289,1,0,0,0,291,292,1,0,
  	0,0,292,43,1,0,0,0,293,291,1,0,0,0,294,299,3,118,59,0,295,296,5,9,0,0,
  	296,298,3,118,59,0,297,295,1,0,0,0,298,301,1,0,0,0,299,297,1,0,0,0,299,
  	300,1,0,0,0,300,45,1,0,0,0,301,299,1,0,0,0,302,307,3,94,47,0,303,304,
  	5,9,0,0,304,306,3,94,47,0,305,303,1,0,0,0,306,309,1,0,0,0,307,305,1,0,
  	0,0,307,308,1,0,0,0,308,47,1,0,0,0,309,307,1,0,0,0,310,315,3,32,16,0,
  	311,312,5,9,0,0,312,314,3,32,16,0,313,311,1,0,0,0,314,317,1,0,0,0,315,
  	313,1,0,0,0,315,316,1,0,0,0,316,49,1,0,0,0,317,315,1,0,0,0,318,323,3,
  	34,17,0,319,320,5,9,0,0,320,322,3,34,17,0,321,319,1,0,0,0,322,325,1,0,
  	0,0,323,321,1,0,0,0,323,324,1,0,0,0,324,51,1,0,0,0,325,323,1,0,0,0,326,
  	331,3,36,18,0,327,328,5,9,0,0,328,330,3,36,18,0,329,327,1,0,0,0,330,333,
  	1,0,0,0,331,329,1,0,0,0,331,332,1,0,0,0,332,53,1,0,0,0,333,331,1,0,0,
  	0,334,339,3,40,20,0,335,336,5,9,0,0,336,338,3,40,20,0,337,335,1,0,0,0,
  	338,341,1,0,0,0,339,337,1,0,0,0,339,340,1,0,0,0,340,55,1,0,0,0,341,339,
  	1,0,0,0,342,345,3,46,23,0,343,344,5,9,0,0,344,346,3,50,25,0,345,343,1,
  	0,0,0,345,346,1,0,0,0,346,349,1,0,0,0,347,349,3,50,25,0,348,342,1,0,0,
  	0,348,347,1,0,0,0,349,57,1,0,0,0,350,352,5,10,0,0,351,353,3,46,23,0,352,
  	351,1,0,0,0,352,353,1,0,0,0,353,355,1,0,0,0,354,356,5,9,0,0,355,354,1,
  	0,0,0,355,356,1,0,0,0,356,357,1,0,0,0,357,358,5,11,0,0,358,59,1,0,0,0,
  	359,361,5,10,0,0,360,362,3,54,27,0,361,360,1,0,0,0,361,362,1,0,0,0,362,
  	364,1,0,0,0,363,365,5,9,0,0,364,363,1,0,0,0,364,365,1,0,0,0,365,366,1,
  	0,0,0,366,367,5,11,0,0,367,61,1,0,0,0,368,370,5,10,0,0,369,371,3,50,25,
  	0,370,369,1,0,0,0,370,371,1,0,0,0,371,373,1,0,0,0,372,374,5,9,0,0,373,
  	372,1,0,0,0,373,374,1,0,0,0,374,375,1,0,0,0,375,376,5,11,0,0,376,63,1,
  	0,0,0,377,379,5,10,0,0,378,380,3,44,22,0,379,378,1,0,0,0,379,380,1,0,
  	0,0,380,382,1,0,0,0,381,383,5,9,0,0,382,381,1,0,0,0,382,383,1,0,0,0,383,
  	384,1,0,0,0,384,385,5,11,0,0,385,65,1,0,0,0,386,388,5,10,0,0,387,389,
  	3,2,1,0,388,387,1,0,0,0,388,389,1,0,0,0,389,390,1,0,0,0,390,391,5,11,
  	0,0,391,67,1,0,0,0,392,394,5,7,0,0,393,395,3,44,22,0,394,393,1,0,0,0,
  	394,395,1,0,0,0,395,397,1,0,0,0,396,398,5,9,0,0,397,396,1,0,0,0,397,398,
  	1,0,0,0,398,399,1,0,0,0,399,400,5,8,0,0,400,69,1,0,0,0,401,403,5,7,0,
  	0,402,404,3,46,23,0,403,402,1,0,0,0,403,404,1,0,0,0,404,406,1,0,0,0,405,
  	407,5,9,0,0,406,405,1,0,0,0,406,407,1,0,0,0,407,408,1,0,0,0,408,409,5,
  	8,0,0,409,71,1,0,0,0,410,412,5,12,0,0,411,413,3,52,26,0,412,411,1,0,0,
  	0,412,413,1,0,0,0,413,415,1,0,0,0,414,416,5,9,0,0,415,414,1,0,0,0,415,
  	416,1,0,0,0,416,417,1,0,0,0,417,418,5,13,0,0,418,73,1,0,0,0,419,421,5,
  	12,0,0,420,422,3,56,28,0,421,420,1,0,0,0,421,422,1,0,0,0,422,424,1,0,
  	0,0,423,425,5,9,0,0,424,423,1,0,0,0,424,425,1,0,0,0,425,426,1,0,0,0,426,
  	427,5,13,0,0,427,75,1,0,0,0,428,430,5,14,0,0,429,431,3,52,26,0,430,429,
  	1,0,0,0,430,431,1,0,0,0,431,433,1,0,0,0,432,434,5,9,0,0,433,432,1,0,0,
  	0,433,434,1,0,0,0,434,435,1,0,0,0,435,436,5,15,0,0,436,77,1,0,0,0,437,
  	439,5,14,0,0,438,440,3,56,28,0,439,438,1,0,0,0,439,440,1,0,0,0,440,442,
  	1,0,0,0,441,443,5,9,0,0,442,441,1,0,0,0,442,443,1,0,0,0,443,444,1,0,0,
  	0,444,445,5,15,0,0,445,79,1,0,0,0,446,458,3,118,59,0,447,458,3,106,53,
  	0,448,458,3,70,35,0,449,458,3,58,29,0,450,458,3,62,31,0,451,458,3,60,
  	30,0,452,458,3,22,11,0,453,454,5,12,0,0,454,455,3,94,47,0,455,456,5,13,
  	0,0,456,458,1,0,0,0,457,446,1,0,0,0,457,447,1,0,0,0,457,448,1,0,0,0,457,
  	449,1,0,0,0,457,450,1,0,0,0,457,451,1,0,0,0,457,452,1,0,0,0,457,453,1,
  	0,0,0,458,81,1,0,0,0,459,460,5,7,0,0,460,461,3,94,47,0,461,462,5,8,0,
  	0,462,83,1,0,0,0,463,470,3,80,40,0,464,469,3,82,41,0,465,469,3,78,39,
  	0,466,469,3,26,13,0,467,469,3,74,37,0,468,464,1,0,0,0,468,465,1,0,0,0,
  	468,466,1,0,0,0,468,467,1,0,0,0,469,472,1,0,0,0,470,468,1,0,0,0,470,471,
  	1,0,0,0,471,85,1,0,0,0,472,470,1,0,0,0,473,475,3,88,44,0,474,473,1,0,
  	0,0,475,476,1,0,0,0,476,474,1,0,0,0,476,477,1,0,0,0,477,87,1,0,0,0,478,
  	479,6,44,-1,0,479,480,3,90,45,0,480,486,1,0,0,0,481,482,10,1,0,0,482,
  	483,5,16,0,0,483,485,3,90,45,0,484,481,1,0,0,0,485,488,1,0,0,0,486,484,
  	1,0,0,0,486,487,1,0,0,0,487,89,1,0,0,0,488,486,1,0,0,0,489,490,6,45,-1,
  	0,490,491,3,84,42,0,491,497,1,0,0,0,492,493,10,1,0,0,493,494,5,17,0,0,
  	494,496,3,84,42,0,495,492,1,0,0,0,496,499,1,0,0,0,497,495,1,0,0,0,497,
  	498,1,0,0,0,498,91,1,0,0,0,499,497,1,0,0,0,500,501,5,18,0,0,501,502,3,
  	84,42,0,502,93,1,0,0,0,503,504,6,47,-1,0,504,505,3,96,48,0,505,532,1,
  	0,0,0,506,507,10,8,0,0,507,508,5,19,0,0,508,531,3,96,48,0,509,510,10,
  	7,0,0,510,511,5,20,0,0,511,531,3,96,48,0,512,513,10,6,0,0,513,514,5,21,
  	0,0,514,531,3,96,48,0,515,516,10,5,0,0,516,517,5,22,0,0,517,531,3,96,
  	48,0,518,519,10,4,0,0,519,520,5,23,0,0,520,531,3,96,48,0,521,522,10,3,
  	0,0,522,523,5,24,0,0,523,531,3,96,48,0,524,525,10,2,0,0,525,526,5,25,
  	0,0,526,531,3,96,48,0,527,528,10,1,0,0,528,529,5,26,0,0,529,531,3,96,
  	48,0,530,506,1,0,0,0,530,509,1,0,0,0,530,512,1,0,0,0,530,515,1,0,0,0,
  	530,518,1,0,0,0,530,521,1,0,0,0,530,524,1,0,0,0,530,527,1,0,0,0,531,534,
  	1,0,0,0,532,530,1,0,0,0,532,533,1,0,0,0,533,95,1,0,0,0,534,532,1,0,0,
  	0,535,536,6,48,-1,0,536,537,3,98,49,0,537,564,1,0,0,0,538,539,10,8,0,
  	0,539,540,5,14,0,0,540,563,3,98,49,0,541,542,10,7,0,0,542,543,5,15,0,
  	0,543,563,3,98,49,0,544,545,10,6,0,0,545,546,5,27,0,0,546,563,3,98,49,
  	0,547,548,10,5,0,0,548,549,5,28,0,0,549,563,3,98,49,0,550,551,10,4,0,
  	0,551,552,5,29,0,0,552,563,3,98,49,0,553,554,10,3,0,0,554,555,5,30,0,
  	0,555,563,3,98,49,0,556,557,10,2,0,0,557,558,5,31,0,0,558,563,3,98,49,
  	0,559,560,10,1,0,0,560,561,5,32,0,0,561,563,3,98,49,0,562,538,1,0,0,0,
  	562,541,1,0,0,0,562,544,1,0,0,0,562,547,1,0,0,0,562,550,1,0,0,0,562,553,
  	1,0,0,0,562,556,1,0,0,0,562,559,1,0,0,0,563,566,1,0,0,0,564,562,1,0,0,
  	0,564,565,1,0,0,0,565,97,1,0,0,0,566,564,1,0,0,0,567,568,6,49,-1,0,568,
  	569,3,100,50,0,569,584,1,0,0,0,570,571,10,4,0,0,571,572,5,33,0,0,572,
  	583,3,100,50,0,573,574,10,3,0,0,574,575,5,34,0,0,575,583,3,100,50,0,576,
  	577,10,2,0,0,577,578,5,35,0,0,578,583,3,100,50,0,579,580,10,1,0,0,580,
  	581,5,36,0,0,581,583,3,100,50,0,582,570,1,0,0,0,582,573,1,0,0,0,582,576,
  	1,0,0,0,582,579,1,0,0,0,583,586,1,0,0,0,584,582,1,0,0,0,584,585,1,0,0,
  	0,585,99,1,0,0,0,586,584,1,0,0,0,587,588,6,50,-1,0,588,589,3,102,51,0,
  	589,610,1,0,0,0,590,591,10,6,0,0,591,592,5,37,0,0,592,609,3,102,51,0,
  	593,594,10,5,0,0,594,595,5,38,0,0,595,609,3,102,51,0,596,597,10,4,0,0,
  	597,598,5,39,0,0,598,609,3,102,51,0,599,600,10,3,0,0,600,601,5,40,0,0,
  	601,609,3,102,51,0,602,603,10,2,0,0,603,604,5,45,0,0,604,609,3,108,54,
  	0,605,606,10,1,0,0,606,607,5,46,0,0,607,609,3,108,54,0,608,590,1,0,0,
  	0,608,593,1,0,0,0,608,596,1,0,0,0,608,599,1,0,0,0,608,602,1,0,0,0,608,
  	605,1,0,0,0,609,612,1,0,0,0,610,608,1,0,0,0,610,611,1,0,0,0,611,101,1,
  	0,0,0,612,610,1,0,0,0,613,627,3,104,52,0,614,615,5,41,0,0,615,627,3,104,
  	52,0,616,617,5,42,0,0,617,627,3,104,52,0,618,619,5,43,0,0,619,627,3,104,
  	52,0,620,621,3,104,52,0,621,622,5,42,0,0,622,627,1,0,0,0,623,624,3,104,
  	52,0,624,625,5,43,0,0,625,627,1,0,0,0,626,613,1,0,0,0,626,614,1,0,0,0,
  	626,616,1,0,0,0,626,618,1,0,0,0,626,620,1,0,0,0,626,623,1,0,0,0,627,103,
  	1,0,0,0,628,634,3,86,43,0,629,630,5,12,0,0,630,631,3,94,47,0,631,632,
  	5,13,0,0,632,634,1,0,0,0,633,628,1,0,0,0,633,629,1,0,0,0,634,105,1,0,
  	0,0,635,637,5,89,0,0,636,638,5,86,0,0,637,636,1,0,0,0,637,638,1,0,0,0,
  	638,650,1,0,0,0,639,641,5,90,0,0,640,642,5,86,0,0,641,640,1,0,0,0,641,
  	642,1,0,0,0,642,650,1,0,0,0,643,650,5,87,0,0,644,650,5,84,0,0,645,650,
  	5,88,0,0,646,650,5,59,0,0,647,650,5,60,0,0,648,650,5,58,0,0,649,635,1,
  	0,0,0,649,639,1,0,0,0,649,643,1,0,0,0,649,644,1,0,0,0,649,645,1,0,0,0,
  	649,646,1,0,0,0,649,647,1,0,0,0,649,648,1,0,0,0,650,107,1,0,0,0,651,652,
  	6,54,-1,0,652,655,3,110,55,0,653,654,5,7,0,0,654,656,5,8,0,0,655,653,
  	1,0,0,0,655,656,1,0,0,0,656,665,1,0,0,0,657,658,10,2,0,0,658,659,5,36,
  	0,0,659,664,3,108,54,3,660,661,10,1,0,0,661,662,5,35,0,0,662,664,3,108,
  	54,2,663,657,1,0,0,0,663,660,1,0,0,0,664,667,1,0,0,0,665,663,1,0,0,0,
  	665,666,1,0,0,0,666,109,1,0,0,0,667,665,1,0,0,0,668,677,3,112,56,0,669,
  	677,3,114,57,0,670,677,3,116,58,0,671,677,3,118,59,0,672,673,5,12,0,0,
  	673,674,3,108,54,0,674,675,5,13,0,0,675,677,1,0,0,0,676,668,1,0,0,0,676,
  	669,1,0,0,0,676,670,1,0,0,0,676,671,1,0,0,0,676,672,1,0,0,0,677,111,1,
  	0,0,0,678,679,7,1,0,0,679,113,1,0,0,0,680,685,5,71,0,0,681,682,5,14,0,
  	0,682,683,3,108,54,0,683,684,5,15,0,0,684,686,1,0,0,0,685,681,1,0,0,0,
  	685,686,1,0,0,0,686,771,1,0,0,0,687,694,5,72,0,0,688,689,5,14,0,0,689,
  	690,3,108,54,0,690,691,5,9,0,0,691,692,3,108,54,0,692,693,5,15,0,0,693,
  	695,1,0,0,0,694,688,1,0,0,0,694,695,1,0,0,0,695,771,1,0,0,0,696,697,5,
  	10,0,0,697,698,3,38,19,0,698,699,5,11,0,0,699,771,1,0,0,0,700,771,5,73,
  	0,0,701,771,5,74,0,0,702,704,5,10,0,0,703,705,3,48,24,0,704,703,1,0,0,
  	0,704,705,1,0,0,0,705,707,1,0,0,0,706,708,5,9,0,0,707,706,1,0,0,0,707,
  	708,1,0,0,0,708,709,1,0,0,0,709,771,5,11,0,0,710,715,5,75,0,0,711,712,
  	5,14,0,0,712,713,3,108,54,0,713,714,5,15,0,0,714,716,1,0,0,0,715,711,
  	1,0,0,0,715,716,1,0,0,0,716,771,1,0,0,0,717,726,5,76,0,0,718,720,5,14,
  	0,0,719,721,3,42,21,0,720,719,1,0,0,0,720,721,1,0,0,0,721,723,1,0,0,0,
  	722,724,5,9,0,0,723,722,1,0,0,0,723,724,1,0,0,0,724,725,1,0,0,0,725,727,
  	5,15,0,0,726,718,1,0,0,0,726,727,1,0,0,0,727,771,1,0,0,0,728,737,5,77,
  	0,0,729,731,5,14,0,0,730,732,3,42,21,0,731,730,1,0,0,0,731,732,1,0,0,
  	0,732,734,1,0,0,0,733,735,5,9,0,0,734,733,1,0,0,0,734,735,1,0,0,0,735,
  	736,1,0,0,0,736,738,5,15,0,0,737,729,1,0,0,0,737,738,1,0,0,0,738,771,
  	1,0,0,0,739,748,5,78,0,0,740,741,5,14,0,0,741,744,3,108,54,0,742,743,
  	5,9,0,0,743,745,5,89,0,0,744,742,1,0,0,0,744,745,1,0,0,0,745,746,1,0,
  	0,0,746,747,5,15,0,0,747,749,1,0,0,0,748,740,1,0,0,0,748,749,1,0,0,0,
  	749,771,1,0,0,0,750,768,5,79,0,0,751,752,5,14,0,0,752,764,3,108,54,0,
  	753,754,5,9,0,0,754,755,5,7,0,0,755,760,5,89,0,0,756,757,5,9,0,0,757,
  	759,5,89,0,0,758,756,1,0,0,0,759,762,1,0,0,0,760,758,1,0,0,0,760,761,
  	1,0,0,0,761,763,1,0,0,0,762,760,1,0,0,0,763,765,5,8,0,0,764,753,1,0,0,
  	0,764,765,1,0,0,0,765,766,1,0,0,0,766,767,5,15,0,0,767,769,1,0,0,0,768,
  	751,1,0,0,0,768,769,1,0,0,0,769,771,1,0,0,0,770,680,1,0,0,0,770,687,1,
  	0,0,0,770,696,1,0,0,0,770,700,1,0,0,0,770,701,1,0,0,0,770,702,1,0,0,0,
  	770,710,1,0,0,0,770,717,1,0,0,0,770,728,1,0,0,0,770,739,1,0,0,0,770,750,
  	1,0,0,0,771,115,1,0,0,0,772,773,7,2,0,0,773,117,1,0,0,0,774,775,5,85,
  	0,0,775,119,1,0,0,0,97,121,130,134,143,149,152,155,160,165,170,178,183,
  	188,194,203,206,209,216,222,225,230,234,241,249,254,266,272,291,299,307,
  	315,323,331,339,345,348,352,355,361,364,370,373,379,382,388,394,397,403,
  	406,412,415,421,424,430,433,439,442,457,468,470,476,486,497,530,532,562,
  	564,582,584,608,610,626,633,637,641,649,655,663,665,676,685,694,704,707,
  	715,720,723,726,731,734,737,744,748,760,764,768,770
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
    setState(121);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2305717664888132800) != 0) || ((((_la - 84) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 84)) & 123) != 0)) {
      setState(120);
      stmtList();
    }
    setState(123);
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
    setState(125);
    stmt();
    setState(130);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 1, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(126);
        match(OpenCMLParser::SEP);
        setState(127);
        stmt(); 
      }
      setState(132);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 1, _ctx);
    }
    setState(134);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::SEP) {
      setState(133);
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

OpenCMLParser::AssignStmtContext* OpenCMLParser::StmtContext::assignStmt() {
  return getRuleContext<OpenCMLParser::AssignStmtContext>(0);
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
    setState(143);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 3, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(136);
      letStmt();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(137);
      useStmt();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(138);
      typeStmt();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(139);
      exprStmt();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(140);
      assignStmt();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(141);
      funcDef();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(142);
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

tree::TerminalNode* OpenCMLParser::LetStmtContext::LET() {
  return getToken(OpenCMLParser::LET, 0);
}

OpenCMLParser::CarrierContext* OpenCMLParser::LetStmtContext::carrier() {
  return getRuleContext<OpenCMLParser::CarrierContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::LetStmtContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::LetStmtContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
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
    setState(165);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::LET: {
        enterOuterAlt(_localctx, 1);
        setState(145);
        match(OpenCMLParser::LET);
        setState(146);
        carrier();
        setState(149);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(147);
          match(OpenCMLParser::T__0);
          setState(148);
          typeExpr(0);
        }
        setState(155);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 2296851203121747076) != 0) || ((((_la - 84) & ~ 0x3fULL) == 0) &&
          ((1ULL << (_la - 84)) & 123) != 0)) {
          setState(152);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if (_la == OpenCMLParser::T__1) {
            setState(151);
            match(OpenCMLParser::T__1);
          }
          setState(154);
          entityExpr(0);
        }
        break;
      }

      case OpenCMLParser::T__6:
      case OpenCMLParser::T__9:
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(157);
        carrier();
        setState(160);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__0) {
          setState(158);
          match(OpenCMLParser::T__0);
          setState(159);
          typeExpr(0);
        }
        setState(162);
        match(OpenCMLParser::T__2);
        setState(163);
        entityExpr(0);
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
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::UseStmtContext::USE() {
  return getToken(OpenCMLParser::USE, 0);
}

OpenCMLParser::CarrierContext* OpenCMLParser::UseStmtContext::carrier() {
  return getRuleContext<OpenCMLParser::CarrierContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::UseStmtContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
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
    setState(178);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::USE: {
        enterOuterAlt(_localctx, 1);
        setState(167);
        match(OpenCMLParser::USE);
        setState(168);
        carrier();
        setState(170);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__1) {
          setState(169);
          match(OpenCMLParser::T__1);
        }
        setState(172);
        entityExpr(0);
        break;
      }

      case OpenCMLParser::T__6:
      case OpenCMLParser::T__9:
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(174);
        carrier();
        setState(175);
        match(OpenCMLParser::T__3);
        setState(176);
        entityExpr(0);
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
    setState(180);
    match(OpenCMLParser::TYPE);
    setState(181);
    identRef();
    setState(183);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(182);
      match(OpenCMLParser::T__1);
    }
    setState(185);
    typeExpr(0);
   
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
    setState(188);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(187);
      annotations();
    }
    setState(190);
    entityExpr(0);
   
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
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::AssignStmtContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::AssignStmtContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
}

OpenCMLParser::MemberAccessContext* OpenCMLParser::AssignStmtContext::memberAccess() {
  return getRuleContext<OpenCMLParser::MemberAccessContext>(0);
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
  enterRule(_localctx, 14, OpenCMLParser::RuleAssignStmt);
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
    identRef();
    setState(194);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(193);
      memberAccess();
    }
    setState(196);
    match(OpenCMLParser::T__1);
    setState(197);
    entityExpr(0);
   
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
    setState(199);
    match(OpenCMLParser::WITH);
    setState(200);
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
    setState(203);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(202);
      annotations();
    }
    setState(206);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::WITH) {
      setState(205);
      withDef();
    }
    setState(209);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 279223176896970752) != 0)) {
      setState(208);
      modifiers();
    }
    setState(211);
    match(OpenCMLParser::FUNC);
    setState(212);
    identRef();
    setState(213);
    parentParams();
    setState(216);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__0) {
      setState(214);
      match(OpenCMLParser::T__0);
      setState(215);
      typeExpr(0);
    }
    setState(218);
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
    setState(220);
    match(OpenCMLParser::RETURN);
    setState(222);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2296851203121747072) != 0) || ((((_la - 84) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 84)) & 123) != 0)) {
      setState(221);
      entityExpr(0);
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

OpenCMLParser::BracedStmtsContext* OpenCMLParser::LambdaExprContext::bracedStmts() {
  return getRuleContext<OpenCMLParser::BracedStmtsContext>(0);
}

OpenCMLParser::ModifiersContext* OpenCMLParser::LambdaExprContext::modifiers() {
  return getRuleContext<OpenCMLParser::ModifiersContext>(0);
}

OpenCMLParser::ParentParamsContext* OpenCMLParser::LambdaExprContext::parentParams() {
  return getRuleContext<OpenCMLParser::ParentParamsContext>(0);
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
    setState(225);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 279223176896970752) != 0)) {
      setState(224);
      modifiers();
    }
    setState(234);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__11) {
      setState(227);
      parentParams();
      setState(230);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == OpenCMLParser::T__0) {
        setState(228);
        match(OpenCMLParser::T__0);
        setState(229);
        typeExpr(0);
      }
      setState(232);
      match(OpenCMLParser::T__4);
    }
    setState(236);
    bracedStmts();
   
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
    setState(241);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 1);
        setState(238);
        identRef();
        break;
      }

      case OpenCMLParser::T__9: {
        enterOuterAlt(_localctx, 2);
        setState(239);
        bracedIdents();
        break;
      }

      case OpenCMLParser::T__6: {
        enterOuterAlt(_localctx, 3);
        setState(240);
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

OpenCMLParser::PrimEntityContext* OpenCMLParser::AnnotationContext::primEntity() {
  return getRuleContext<OpenCMLParser::PrimEntityContext>(0);
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
    setState(243);
    match(OpenCMLParser::T__5);
    setState(244);
    primEntity();
   
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
    setState(247); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(246);
      annotation();
      setState(249); 
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

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::STATIC() {
  return getTokens(OpenCMLParser::STATIC);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::STATIC(size_t i) {
  return getToken(OpenCMLParser::STATIC, i);
}

std::vector<tree::TerminalNode *> OpenCMLParser::ModifiersContext::SYNC() {
  return getTokens(OpenCMLParser::SYNC);
}

tree::TerminalNode* OpenCMLParser::ModifiersContext::SYNC(size_t i) {
  return getToken(OpenCMLParser::SYNC, i);
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
    setState(252); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(251);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 279223176896970752) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(254); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 279223176896970752) != 0));
   
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
    setState(256);
    identRef();
    setState(257);
    match(OpenCMLParser::T__0);
    setState(258);
    typeExpr(0);
   
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

std::vector<OpenCMLParser::EntityExprContext *> OpenCMLParser::KeyValuePairContext::entityExpr() {
  return getRuleContexts<OpenCMLParser::EntityExprContext>();
}

OpenCMLParser::EntityExprContext* OpenCMLParser::KeyValuePairContext::entityExpr(size_t i) {
  return getRuleContext<OpenCMLParser::EntityExprContext>(i);
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
    setState(260);
    entityExpr(0);
    setState(261);
    match(OpenCMLParser::T__0);
    setState(262);
    entityExpr(0);
   
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
    setState(264);
    identRef();
    setState(266);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__5) {
      setState(265);
      annotation();
    }
    setState(268);
    match(OpenCMLParser::T__0);
    setState(269);
    typeExpr(0);
    setState(272);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__1) {
      setState(270);
      match(OpenCMLParser::T__1);
      setState(271);
      entityExpr(0);
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
    setState(274);
    match(OpenCMLParser::T__6);
    setState(275);
    entityExpr(0);
    setState(276);
    match(OpenCMLParser::T__7);
    setState(277);
    match(OpenCMLParser::T__0);
    setState(278);
    typeExpr(0);
   
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
    setState(280);
    match(OpenCMLParser::T__6);
    setState(281);
    entityExpr(0);
    setState(282);
    match(OpenCMLParser::T__7);
    setState(283);
    match(OpenCMLParser::T__0);
    setState(284);
    entityExpr(0);
   
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
    setState(286);
    typeExpr(0);
    setState(291);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(287);
        match(OpenCMLParser::T__8);
        setState(288);
        typeExpr(0); 
      }
      setState(293);
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
    setState(294);
    identRef();
    setState(299);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(295);
        match(OpenCMLParser::T__8);
        setState(296);
        identRef(); 
      }
      setState(301);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28, _ctx);
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
    setState(302);
    entityExpr(0);
    setState(307);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(303);
        match(OpenCMLParser::T__8);
        setState(304);
        entityExpr(0); 
      }
      setState(309);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx);
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
    setState(310);
    keyTypePair();
    setState(315);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(311);
        match(OpenCMLParser::T__8);
        setState(312);
        keyTypePair(); 
      }
      setState(317);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx);
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
    setState(318);
    keyValuePair();
    setState(323);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(319);
        match(OpenCMLParser::T__8);
        setState(320);
        keyValuePair(); 
      }
      setState(325);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx);
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
    setState(326);
    keyParamPair();
    setState(331);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(327);
        match(OpenCMLParser::T__8);
        setState(328);
        keyParamPair(); 
      }
      setState(333);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx);
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
    setState(334);
    indexKVPair();
    setState(339);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(335);
        match(OpenCMLParser::T__8);
        setState(336);
        indexKVPair(); 
      }
      setState(341);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx);
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
    setState(348);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(342);
      valueList();
      setState(345);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 34, _ctx)) {
      case 1: {
        setState(343);
        match(OpenCMLParser::T__8);
        setState(344);
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
      setState(347);
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
  enterRule(_localctx, 58, OpenCMLParser::RuleBracedValues);
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
    match(OpenCMLParser::T__9);
    setState(352);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2296851203121747072) != 0) || ((((_la - 84) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 84)) & 123) != 0)) {
      setState(351);
      valueList();
    }
    setState(355);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(354);
      match(OpenCMLParser::T__8);
    }
    setState(357);
    match(OpenCMLParser::T__10);
   
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
  enterRule(_localctx, 60, OpenCMLParser::RuleBracedIndexKVPairs);
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
    match(OpenCMLParser::T__9);
    setState(361);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__6) {
      setState(360);
      indexKVPairs();
    }
    setState(364);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(363);
      match(OpenCMLParser::T__8);
    }
    setState(366);
    match(OpenCMLParser::T__10);
   
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
  enterRule(_localctx, 62, OpenCMLParser::RuleBracedPairedValues);
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
    setState(368);
    match(OpenCMLParser::T__9);
    setState(370);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2296851203121747072) != 0) || ((((_la - 84) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 84)) & 123) != 0)) {
      setState(369);
      pairedValues();
    }
    setState(373);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(372);
      match(OpenCMLParser::T__8);
    }
    setState(375);
    match(OpenCMLParser::T__10);
   
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
    setState(377);
    match(OpenCMLParser::T__9);
    setState(379);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(378);
      identList();
    }
    setState(382);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(381);
      match(OpenCMLParser::T__8);
    }
    setState(384);
    match(OpenCMLParser::T__10);
   
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
  enterRule(_localctx, 66, OpenCMLParser::RuleBracedStmts);
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
    setState(386);
    match(OpenCMLParser::T__9);
    setState(388);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2305717664888132800) != 0) || ((((_la - 84) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 84)) & 123) != 0)) {
      setState(387);
      stmtList();
    }
    setState(390);
    match(OpenCMLParser::T__10);
   
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
    setState(392);
    match(OpenCMLParser::T__6);
    setState(394);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(393);
      identList();
    }
    setState(397);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(396);
      match(OpenCMLParser::T__8);
    }
    setState(399);
    match(OpenCMLParser::T__7);
   
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
    setState(401);
    match(OpenCMLParser::T__6);
    setState(403);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2296851203121747072) != 0) || ((((_la - 84) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 84)) & 123) != 0)) {
      setState(402);
      valueList();
    }
    setState(406);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(405);
      match(OpenCMLParser::T__8);
    }
    setState(408);
    match(OpenCMLParser::T__7);
   
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
    setState(410);
    match(OpenCMLParser::T__11);
    setState(412);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(411);
      pairedParams();
    }
    setState(415);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(414);
      match(OpenCMLParser::T__8);
    }
    setState(417);
    match(OpenCMLParser::T__12);
   
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

OpenCMLParser::ArgumentListContext* OpenCMLParser::ParentValuesContext::argumentList() {
  return getRuleContext<OpenCMLParser::ArgumentListContext>(0);
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
    setState(419);
    match(OpenCMLParser::T__11);
    setState(421);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2296851203121747072) != 0) || ((((_la - 84) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 84)) & 123) != 0)) {
      setState(420);
      argumentList();
    }
    setState(424);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(423);
      match(OpenCMLParser::T__8);
    }
    setState(426);
    match(OpenCMLParser::T__12);
   
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
    setState(428);
    match(OpenCMLParser::T__13);
    setState(430);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::IDENTIFIER) {
      setState(429);
      pairedParams();
    }
    setState(433);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(432);
      match(OpenCMLParser::T__8);
    }
    setState(435);
    match(OpenCMLParser::T__14);
   
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
    setState(437);
    match(OpenCMLParser::T__13);
    setState(439);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2296851203121747072) != 0) || ((((_la - 84) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 84)) & 123) != 0)) {
      setState(438);
      argumentList();
    }
    setState(442);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == OpenCMLParser::T__8) {
      setState(441);
      match(OpenCMLParser::T__8);
    }
    setState(444);
    match(OpenCMLParser::T__14);
   
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
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::IdentRefContext* OpenCMLParser::PrimEntityContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::LiteralContext* OpenCMLParser::PrimEntityContext::literal() {
  return getRuleContext<OpenCMLParser::LiteralContext>(0);
}

OpenCMLParser::BracketValuesContext* OpenCMLParser::PrimEntityContext::bracketValues() {
  return getRuleContext<OpenCMLParser::BracketValuesContext>(0);
}

OpenCMLParser::BracedValuesContext* OpenCMLParser::PrimEntityContext::bracedValues() {
  return getRuleContext<OpenCMLParser::BracedValuesContext>(0);
}

OpenCMLParser::BracedPairedValuesContext* OpenCMLParser::PrimEntityContext::bracedPairedValues() {
  return getRuleContext<OpenCMLParser::BracedPairedValuesContext>(0);
}

OpenCMLParser::BracedIndexKVPairsContext* OpenCMLParser::PrimEntityContext::bracedIndexKVPairs() {
  return getRuleContext<OpenCMLParser::BracedIndexKVPairsContext>(0);
}

OpenCMLParser::LambdaExprContext* OpenCMLParser::PrimEntityContext::lambdaExpr() {
  return getRuleContext<OpenCMLParser::LambdaExprContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::PrimEntityContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
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
  enterRule(_localctx, 80, OpenCMLParser::RulePrimEntity);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(457);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 57, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(446);
      identRef();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(447);
      literal();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(448);
      bracketValues();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(449);
      bracedValues();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(450);
      bracedPairedValues();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(451);
      bracedIndexKVPairs();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(452);
      lambdaExpr();
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(453);
      match(OpenCMLParser::T__11);
      setState(454);
      entityExpr(0);
      setState(455);
      match(OpenCMLParser::T__12);
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
  enterRule(_localctx, 82, OpenCMLParser::RuleMemberAccess);

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
    match(OpenCMLParser::T__6);
    setState(460);
    entityExpr(0);
    setState(461);
    match(OpenCMLParser::T__7);
   
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
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimEntityContext* OpenCMLParser::EntityContext::primEntity() {
  return getRuleContext<OpenCMLParser::PrimEntityContext>(0);
}

std::vector<OpenCMLParser::MemberAccessContext *> OpenCMLParser::EntityContext::memberAccess() {
  return getRuleContexts<OpenCMLParser::MemberAccessContext>();
}

OpenCMLParser::MemberAccessContext* OpenCMLParser::EntityContext::memberAccess(size_t i) {
  return getRuleContext<OpenCMLParser::MemberAccessContext>(i);
}

std::vector<OpenCMLParser::AngledValuesContext *> OpenCMLParser::EntityContext::angledValues() {
  return getRuleContexts<OpenCMLParser::AngledValuesContext>();
}

OpenCMLParser::AngledValuesContext* OpenCMLParser::EntityContext::angledValues(size_t i) {
  return getRuleContext<OpenCMLParser::AngledValuesContext>(i);
}

std::vector<OpenCMLParser::AnnotationContext *> OpenCMLParser::EntityContext::annotation() {
  return getRuleContexts<OpenCMLParser::AnnotationContext>();
}

OpenCMLParser::AnnotationContext* OpenCMLParser::EntityContext::annotation(size_t i) {
  return getRuleContext<OpenCMLParser::AnnotationContext>(i);
}

std::vector<OpenCMLParser::ParentValuesContext *> OpenCMLParser::EntityContext::parentValues() {
  return getRuleContexts<OpenCMLParser::ParentValuesContext>();
}

OpenCMLParser::ParentValuesContext* OpenCMLParser::EntityContext::parentValues(size_t i) {
  return getRuleContext<OpenCMLParser::ParentValuesContext>(i);
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
  enterRule(_localctx, 84, OpenCMLParser::RuleEntity);

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
    setState(463);
    primEntity();
    setState(470);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 59, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(468);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case OpenCMLParser::T__6: {
            setState(464);
            memberAccess();
            break;
          }

          case OpenCMLParser::T__13: {
            setState(465);
            angledValues();
            break;
          }

          case OpenCMLParser::T__5: {
            setState(466);
            annotation();
            break;
          }

          case OpenCMLParser::T__11: {
            setState(467);
            parentValues();
            break;
          }

        default:
          throw NoViableAltException(this);
        } 
      }
      setState(472);
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

//----------------- EntityChainContext ------------------------------------------------------------------

OpenCMLParser::EntityChainContext::EntityChainContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
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
  enterRule(_localctx, 86, OpenCMLParser::RuleEntityChain);

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
    setState(474); 
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(473);
              entityLink(0);
              break;
            }

      default:
        throw NoViableAltException(this);
      }
      setState(476); 
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 60, _ctx);
    } while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER);
   
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
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::EntityCallContext* OpenCMLParser::EntityLinkContext::entityCall() {
  return getRuleContext<OpenCMLParser::EntityCallContext>(0);
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
  size_t startState = 88;
  enterRecursionRule(_localctx, 88, OpenCMLParser::RuleEntityLink, precedence);

    

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
    setState(479);
    entityCall(0);
    _ctx->stop = _input->LT(-1);
    setState(486);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EntityLinkContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEntityLink);
        setState(481);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(482);
        match(OpenCMLParser::T__15);
        setState(483);
        entityCall(0); 
      }
      setState(488);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 61, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- EntityCallContext ------------------------------------------------------------------

OpenCMLParser::EntityCallContext::EntityCallContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::EntityContext* OpenCMLParser::EntityCallContext::entity() {
  return getRuleContext<OpenCMLParser::EntityContext>(0);
}

OpenCMLParser::EntityCallContext* OpenCMLParser::EntityCallContext::entityCall() {
  return getRuleContext<OpenCMLParser::EntityCallContext>(0);
}


size_t OpenCMLParser::EntityCallContext::getRuleIndex() const {
  return OpenCMLParser::RuleEntityCall;
}


std::any OpenCMLParser::EntityCallContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEntityCall(this);
  else
    return visitor->visitChildren(this);
}


OpenCMLParser::EntityCallContext* OpenCMLParser::entityCall() {
   return entityCall(0);
}

OpenCMLParser::EntityCallContext* OpenCMLParser::entityCall(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::EntityCallContext *_localctx = _tracker.createInstance<EntityCallContext>(_ctx, parentState);
  OpenCMLParser::EntityCallContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 90;
  enterRecursionRule(_localctx, 90, OpenCMLParser::RuleEntityCall, precedence);

    

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
    setState(490);
    entity();
    _ctx->stop = _input->LT(-1);
    setState(497);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 62, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EntityCallContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEntityCall);
        setState(492);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(493);
        match(OpenCMLParser::T__16);
        setState(494);
        entity(); 
      }
      setState(499);
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

//----------------- EntitySpreadContext ------------------------------------------------------------------

OpenCMLParser::EntitySpreadContext::EntitySpreadContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::EntityContext* OpenCMLParser::EntitySpreadContext::entity() {
  return getRuleContext<OpenCMLParser::EntityContext>(0);
}


size_t OpenCMLParser::EntitySpreadContext::getRuleIndex() const {
  return OpenCMLParser::RuleEntitySpread;
}


std::any OpenCMLParser::EntitySpreadContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitEntitySpread(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::EntitySpreadContext* OpenCMLParser::entitySpread() {
  EntitySpreadContext *_localctx = _tracker.createInstance<EntitySpreadContext>(_ctx, getState());
  enterRule(_localctx, 92, OpenCMLParser::RuleEntitySpread);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(500);
    match(OpenCMLParser::T__17);
    setState(501);
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
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::RelaExprContext* OpenCMLParser::EntityExprContext::relaExpr() {
  return getRuleContext<OpenCMLParser::RelaExprContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::EntityExprContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
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
   return entityExpr(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::entityExpr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::EntityExprContext *_localctx = _tracker.createInstance<EntityExprContext>(_ctx, parentState);
  OpenCMLParser::EntityExprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 94;
  enterRecursionRule(_localctx, 94, OpenCMLParser::RuleEntityExpr, precedence);

    

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
    relaExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(532);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 64, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(530);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 63, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(506);

          if (!(precpred(_ctx, 8))) throw FailedPredicateException(this, "precpred(_ctx, 8)");
          setState(507);
          match(OpenCMLParser::T__18);
          setState(508);
          relaExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(509);

          if (!(precpred(_ctx, 7))) throw FailedPredicateException(this, "precpred(_ctx, 7)");
          setState(510);
          match(OpenCMLParser::T__19);
          setState(511);
          relaExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(512);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(513);
          match(OpenCMLParser::T__20);
          setState(514);
          relaExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(515);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(516);
          match(OpenCMLParser::T__21);
          setState(517);
          relaExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(518);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(519);
          match(OpenCMLParser::T__22);
          setState(520);
          relaExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(521);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(522);
          match(OpenCMLParser::T__23);
          setState(523);
          relaExpr(0);
          break;
        }

        case 7: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(524);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(525);
          match(OpenCMLParser::T__24);
          setState(526);
          relaExpr(0);
          break;
        }

        case 8: {
          _localctx = _tracker.createInstance<EntityExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleEntityExpr);
          setState(527);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(528);
          match(OpenCMLParser::T__25);
          setState(529);
          relaExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(534);
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

//----------------- RelaExprContext ------------------------------------------------------------------

OpenCMLParser::RelaExprContext::RelaExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
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
  size_t startState = 96;
  enterRecursionRule(_localctx, 96, OpenCMLParser::RuleRelaExpr, precedence);

    

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
    setState(536);
    addExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(564);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 66, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(562);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 65, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(538);

          if (!(precpred(_ctx, 8))) throw FailedPredicateException(this, "precpred(_ctx, 8)");
          setState(539);
          match(OpenCMLParser::T__13);
          setState(540);
          addExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(541);

          if (!(precpred(_ctx, 7))) throw FailedPredicateException(this, "precpred(_ctx, 7)");
          setState(542);
          match(OpenCMLParser::T__14);
          setState(543);
          addExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(544);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(545);
          match(OpenCMLParser::T__26);
          setState(546);
          addExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(547);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(548);
          match(OpenCMLParser::T__27);
          setState(549);
          addExpr(0);
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(550);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(551);
          match(OpenCMLParser::T__28);
          setState(552);
          addExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(553);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(554);
          match(OpenCMLParser::T__29);
          setState(555);
          addExpr(0);
          break;
        }

        case 7: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(556);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(557);
          match(OpenCMLParser::T__30);
          setState(558);
          addExpr(0);
          break;
        }

        case 8: {
          _localctx = _tracker.createInstance<RelaExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleRelaExpr);
          setState(559);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(560);
          match(OpenCMLParser::T__31);
          setState(561);
          addExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(566);
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

//----------------- AddExprContext ------------------------------------------------------------------

OpenCMLParser::AddExprContext::AddExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
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
  size_t startState = 98;
  enterRecursionRule(_localctx, 98, OpenCMLParser::RuleAddExpr, precedence);

    

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
    setState(568);
    multiExpr(0);
    _ctx->stop = _input->LT(-1);
    setState(584);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 68, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(582);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 67, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(570);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(571);
          match(OpenCMLParser::T__32);
          setState(572);
          multiExpr(0);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(573);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(574);
          match(OpenCMLParser::T__33);
          setState(575);
          multiExpr(0);
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(576);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(577);
          match(OpenCMLParser::T__34);
          setState(578);
          multiExpr(0);
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<AddExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleAddExpr);
          setState(579);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(580);
          match(OpenCMLParser::T__35);
          setState(581);
          multiExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(586);
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

//----------------- MultiExprContext ------------------------------------------------------------------

OpenCMLParser::MultiExprContext::MultiExprContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::UnaryExprContext* OpenCMLParser::MultiExprContext::unaryExpr() {
  return getRuleContext<OpenCMLParser::UnaryExprContext>(0);
}

OpenCMLParser::MultiExprContext* OpenCMLParser::MultiExprContext::multiExpr() {
  return getRuleContext<OpenCMLParser::MultiExprContext>(0);
}

tree::TerminalNode* OpenCMLParser::MultiExprContext::AS() {
  return getToken(OpenCMLParser::AS, 0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::MultiExprContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
}

tree::TerminalNode* OpenCMLParser::MultiExprContext::IS() {
  return getToken(OpenCMLParser::IS, 0);
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
  size_t startState = 100;
  enterRecursionRule(_localctx, 100, OpenCMLParser::RuleMultiExpr, precedence);

    

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
    setState(588);
    unaryExpr();
    _ctx->stop = _input->LT(-1);
    setState(610);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(608);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(590);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(591);
          match(OpenCMLParser::T__36);
          setState(592);
          unaryExpr();
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(593);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(594);
          match(OpenCMLParser::T__37);
          setState(595);
          unaryExpr();
          break;
        }

        case 3: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(596);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(597);
          match(OpenCMLParser::T__38);
          setState(598);
          unaryExpr();
          break;
        }

        case 4: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(599);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(600);
          match(OpenCMLParser::T__39);
          setState(601);
          unaryExpr();
          break;
        }

        case 5: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(602);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(603);
          match(OpenCMLParser::AS);
          setState(604);
          typeExpr(0);
          break;
        }

        case 6: {
          _localctx = _tracker.createInstance<MultiExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleMultiExpr);
          setState(605);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(606);
          match(OpenCMLParser::IS);
          setState(607);
          typeExpr(0);
          break;
        }

        default:
          break;
        } 
      }
      setState(612);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx);
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
  enterRule(_localctx, 102, OpenCMLParser::RuleUnaryExpr);

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
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 71, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(613);
      primExpr();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(614);
      match(OpenCMLParser::T__40);
      setState(615);
      primExpr();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(616);
      match(OpenCMLParser::T__41);
      setState(617);
      primExpr();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(618);
      match(OpenCMLParser::T__42);
      setState(619);
      primExpr();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(620);
      primExpr();
      setState(621);
      match(OpenCMLParser::T__41);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(623);
      primExpr();
      setState(624);
      match(OpenCMLParser::T__42);
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
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::EntityChainContext* OpenCMLParser::PrimExprContext::entityChain() {
  return getRuleContext<OpenCMLParser::EntityChainContext>(0);
}

OpenCMLParser::EntityExprContext* OpenCMLParser::PrimExprContext::entityExpr() {
  return getRuleContext<OpenCMLParser::EntityExprContext>(0);
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
  enterRule(_localctx, 104, OpenCMLParser::RulePrimExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(633);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 72, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(628);
      entityChain();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(629);
      match(OpenCMLParser::T__11);
      setState(630);
      entityExpr(0);
      setState(631);
      match(OpenCMLParser::T__12);
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
  enterRule(_localctx, 106, OpenCMLParser::RuleLiteral);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(649);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER: {
        enterOuterAlt(_localctx, 1);
        setState(635);
        match(OpenCMLParser::INTEGER);
        setState(637);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 73, _ctx)) {
        case 1: {
          setState(636);
          match(OpenCMLParser::UNIT);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::REAL: {
        enterOuterAlt(_localctx, 2);
        setState(639);
        match(OpenCMLParser::REAL);
        setState(641);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 74, _ctx)) {
        case 1: {
          setState(640);
          match(OpenCMLParser::UNIT);
          break;
        }

        default:
          break;
        }
        break;
      }

      case OpenCMLParser::STRING: {
        enterOuterAlt(_localctx, 3);
        setState(643);
        match(OpenCMLParser::STRING);
        break;
      }

      case OpenCMLParser::MULTI_STR: {
        enterOuterAlt(_localctx, 4);
        setState(644);
        match(OpenCMLParser::MULTI_STR);
        break;
      }

      case OpenCMLParser::FSTRING: {
        enterOuterAlt(_localctx, 5);
        setState(645);
        match(OpenCMLParser::FSTRING);
        break;
      }

      case OpenCMLParser::TRUE: {
        enterOuterAlt(_localctx, 6);
        setState(646);
        match(OpenCMLParser::TRUE);
        break;
      }

      case OpenCMLParser::FALSE: {
        enterOuterAlt(_localctx, 7);
        setState(647);
        match(OpenCMLParser::FALSE);
        break;
      }

      case OpenCMLParser::NULL_: {
        enterOuterAlt(_localctx, 8);
        setState(648);
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

OpenCMLParser::TypeContext* OpenCMLParser::TypeExprContext::type() {
  return getRuleContext<OpenCMLParser::TypeContext>(0);
}

std::vector<OpenCMLParser::TypeExprContext *> OpenCMLParser::TypeExprContext::typeExpr() {
  return getRuleContexts<OpenCMLParser::TypeExprContext>();
}

OpenCMLParser::TypeExprContext* OpenCMLParser::TypeExprContext::typeExpr(size_t i) {
  return getRuleContext<OpenCMLParser::TypeExprContext>(i);
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
   return typeExpr(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::typeExpr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  OpenCMLParser::TypeExprContext *_localctx = _tracker.createInstance<TypeExprContext>(_ctx, parentState);
  OpenCMLParser::TypeExprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 108;
  enterRecursionRule(_localctx, 108, OpenCMLParser::RuleTypeExpr, precedence);

    

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
    setState(652);
    type();
    setState(655);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 76, _ctx)) {
    case 1: {
      setState(653);
      match(OpenCMLParser::T__6);
      setState(654);
      match(OpenCMLParser::T__7);
      break;
    }

    default:
      break;
    }
    _ctx->stop = _input->LT(-1);
    setState(665);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 78, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(663);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 77, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<TypeExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleTypeExpr);
          setState(657);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(658);
          match(OpenCMLParser::T__35);
          setState(659);
          typeExpr(3);
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<TypeExprContext>(parentContext, parentState);
          pushNewRecursionContext(_localctx, startState, RuleTypeExpr);
          setState(660);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(661);
          match(OpenCMLParser::T__34);
          setState(662);
          typeExpr(2);
          break;
        }

        default:
          break;
        } 
      }
      setState(667);
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

//----------------- TypeContext ------------------------------------------------------------------

OpenCMLParser::TypeContext::TypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

OpenCMLParser::PrimTypeContext* OpenCMLParser::TypeContext::primType() {
  return getRuleContext<OpenCMLParser::PrimTypeContext>(0);
}

OpenCMLParser::StructTypeContext* OpenCMLParser::TypeContext::structType() {
  return getRuleContext<OpenCMLParser::StructTypeContext>(0);
}

OpenCMLParser::SpecialTypeContext* OpenCMLParser::TypeContext::specialType() {
  return getRuleContext<OpenCMLParser::SpecialTypeContext>(0);
}

OpenCMLParser::IdentRefContext* OpenCMLParser::TypeContext::identRef() {
  return getRuleContext<OpenCMLParser::IdentRefContext>(0);
}

OpenCMLParser::TypeExprContext* OpenCMLParser::TypeContext::typeExpr() {
  return getRuleContext<OpenCMLParser::TypeExprContext>(0);
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
  enterRule(_localctx, 110, OpenCMLParser::RuleType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(676);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case OpenCMLParser::INTEGER_TYPE:
      case OpenCMLParser::INTEGER32_TYPE:
      case OpenCMLParser::INTEGER64_TYPE:
      case OpenCMLParser::REAL_TYPE:
      case OpenCMLParser::FLOAT_TYPE:
      case OpenCMLParser::DOUBLE_TYPE:
      case OpenCMLParser::NUMBER_TYPE:
      case OpenCMLParser::STRING_TYPE:
      case OpenCMLParser::BOOL_TYPE:
      case OpenCMLParser::CHAR_TYPE: {
        enterOuterAlt(_localctx, 1);
        setState(668);
        primType();
        break;
      }

      case OpenCMLParser::T__9:
      case OpenCMLParser::SET_TYPE:
      case OpenCMLParser::MAP_TYPE:
      case OpenCMLParser::LIST_TYPE:
      case OpenCMLParser::DICT_TYPE:
      case OpenCMLParser::ARRAY_TYPE:
      case OpenCMLParser::TUPLE_TYPE:
      case OpenCMLParser::UNION_TYPE:
      case OpenCMLParser::VECTOR_TYPE:
      case OpenCMLParser::TENSOR_TYPE: {
        enterOuterAlt(_localctx, 2);
        setState(669);
        structType();
        break;
      }

      case OpenCMLParser::ANY_TYPE:
      case OpenCMLParser::VOID_TYPE:
      case OpenCMLParser::FUNCTOR_TYPE: {
        enterOuterAlt(_localctx, 3);
        setState(670);
        specialType();
        break;
      }

      case OpenCMLParser::IDENTIFIER: {
        enterOuterAlt(_localctx, 4);
        setState(671);
        identRef();
        break;
      }

      case OpenCMLParser::T__11: {
        enterOuterAlt(_localctx, 5);
        setState(672);
        match(OpenCMLParser::T__11);
        setState(673);
        typeExpr(0);
        setState(674);
        match(OpenCMLParser::T__12);
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

//----------------- PrimTypeContext ------------------------------------------------------------------

OpenCMLParser::PrimTypeContext::PrimTypeContext(ParserRuleContext *parent, size_t invokingState)
  : antlr4::RuleContextWithAltNum(parent, invokingState) {
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::INTEGER_TYPE() {
  return getToken(OpenCMLParser::INTEGER_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::INTEGER32_TYPE() {
  return getToken(OpenCMLParser::INTEGER32_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::INTEGER64_TYPE() {
  return getToken(OpenCMLParser::INTEGER64_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::REAL_TYPE() {
  return getToken(OpenCMLParser::REAL_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::FLOAT_TYPE() {
  return getToken(OpenCMLParser::FLOAT_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::DOUBLE_TYPE() {
  return getToken(OpenCMLParser::DOUBLE_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::NUMBER_TYPE() {
  return getToken(OpenCMLParser::NUMBER_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::STRING_TYPE() {
  return getToken(OpenCMLParser::STRING_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::BOOL_TYPE() {
  return getToken(OpenCMLParser::BOOL_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::PrimTypeContext::CHAR_TYPE() {
  return getToken(OpenCMLParser::CHAR_TYPE, 0);
}


size_t OpenCMLParser::PrimTypeContext::getRuleIndex() const {
  return OpenCMLParser::RulePrimType;
}


std::any OpenCMLParser::PrimTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<OpenCMLVisitor*>(visitor))
    return parserVisitor->visitPrimType(this);
  else
    return visitor->visitChildren(this);
}

OpenCMLParser::PrimTypeContext* OpenCMLParser::primType() {
  PrimTypeContext *_localctx = _tracker.createInstance<PrimTypeContext>(_ctx, getState());
  enterRule(_localctx, 112, OpenCMLParser::RulePrimType);
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
    _la = _input->LA(1);
    if (!(((((_la - 61) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 61)) & 1023) != 0))) {
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

OpenCMLParser::IndexKTPairContext* OpenCMLParser::StructTypeContext::indexKTPair() {
  return getRuleContext<OpenCMLParser::IndexKTPairContext>(0);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::LIST_TYPE() {
  return getToken(OpenCMLParser::LIST_TYPE, 0);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::DICT_TYPE() {
  return getToken(OpenCMLParser::DICT_TYPE, 0);
}

OpenCMLParser::PairedTypesContext* OpenCMLParser::StructTypeContext::pairedTypes() {
  return getRuleContext<OpenCMLParser::PairedTypesContext>(0);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::ARRAY_TYPE() {
  return getToken(OpenCMLParser::ARRAY_TYPE, 0);
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

std::vector<tree::TerminalNode *> OpenCMLParser::StructTypeContext::INTEGER() {
  return getTokens(OpenCMLParser::INTEGER);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::INTEGER(size_t i) {
  return getToken(OpenCMLParser::INTEGER, i);
}

tree::TerminalNode* OpenCMLParser::StructTypeContext::TENSOR_TYPE() {
  return getToken(OpenCMLParser::TENSOR_TYPE, 0);
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
  enterRule(_localctx, 114, OpenCMLParser::RuleStructType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(770);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 96, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(680);
      match(OpenCMLParser::SET_TYPE);
      setState(685);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 80, _ctx)) {
      case 1: {
        setState(681);
        match(OpenCMLParser::T__13);
        setState(682);
        typeExpr(0);
        setState(683);
        match(OpenCMLParser::T__14);
        break;
      }

      default:
        break;
      }
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(687);
      match(OpenCMLParser::MAP_TYPE);
      setState(694);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 81, _ctx)) {
      case 1: {
        setState(688);
        match(OpenCMLParser::T__13);
        setState(689);
        typeExpr(0);
        setState(690);
        match(OpenCMLParser::T__8);
        setState(691);
        typeExpr(0);
        setState(692);
        match(OpenCMLParser::T__14);
        break;
      }

      default:
        break;
      }
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(696);
      match(OpenCMLParser::T__9);
      setState(697);
      indexKTPair();
      setState(698);
      match(OpenCMLParser::T__10);
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(700);
      match(OpenCMLParser::LIST_TYPE);
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(701);
      match(OpenCMLParser::DICT_TYPE);
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(702);
      match(OpenCMLParser::T__9);
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
      if (_la == OpenCMLParser::T__8) {
        setState(706);
        match(OpenCMLParser::T__8);
      }
      setState(709);
      match(OpenCMLParser::T__10);
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(710);
      match(OpenCMLParser::ARRAY_TYPE);
      setState(715);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 84, _ctx)) {
      case 1: {
        setState(711);
        match(OpenCMLParser::T__13);
        setState(712);
        typeExpr(0);
        setState(713);
        match(OpenCMLParser::T__14);
        break;
      }

      default:
        break;
      }
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(717);
      match(OpenCMLParser::TUPLE_TYPE);
      setState(726);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 87, _ctx)) {
      case 1: {
        setState(718);
        match(OpenCMLParser::T__13);
        setState(720);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & -2305843009213688832) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
          ((1ULL << (_la - 64)) & 2621439) != 0)) {
          setState(719);
          typeList();
        }
        setState(723);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__8) {
          setState(722);
          match(OpenCMLParser::T__8);
        }
        setState(725);
        match(OpenCMLParser::T__14);
        break;
      }

      default:
        break;
      }
      break;
    }

    case 9: {
      enterOuterAlt(_localctx, 9);
      setState(728);
      match(OpenCMLParser::UNION_TYPE);
      setState(737);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 90, _ctx)) {
      case 1: {
        setState(729);
        match(OpenCMLParser::T__13);
        setState(731);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & -2305843009213688832) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
          ((1ULL << (_la - 64)) & 2621439) != 0)) {
          setState(730);
          typeList();
        }
        setState(734);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__8) {
          setState(733);
          match(OpenCMLParser::T__8);
        }
        setState(736);
        match(OpenCMLParser::T__14);
        break;
      }

      default:
        break;
      }
      break;
    }

    case 10: {
      enterOuterAlt(_localctx, 10);
      setState(739);
      match(OpenCMLParser::VECTOR_TYPE);
      setState(748);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 92, _ctx)) {
      case 1: {
        setState(740);
        match(OpenCMLParser::T__13);
        setState(741);
        typeExpr(0);
        setState(744);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__8) {
          setState(742);
          match(OpenCMLParser::T__8);
          setState(743);
          match(OpenCMLParser::INTEGER);
        }
        setState(746);
        match(OpenCMLParser::T__14);
        break;
      }

      default:
        break;
      }
      break;
    }

    case 11: {
      enterOuterAlt(_localctx, 11);
      setState(750);
      match(OpenCMLParser::TENSOR_TYPE);
      setState(768);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 95, _ctx)) {
      case 1: {
        setState(751);
        match(OpenCMLParser::T__13);
        setState(752);
        typeExpr(0);
        setState(764);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == OpenCMLParser::T__8) {
          setState(753);
          match(OpenCMLParser::T__8);
          setState(754);
          match(OpenCMLParser::T__6);
          setState(755);
          match(OpenCMLParser::INTEGER);
          setState(760);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == OpenCMLParser::T__8) {
            setState(756);
            match(OpenCMLParser::T__8);
            setState(757);
            match(OpenCMLParser::INTEGER);
            setState(762);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
          setState(763);
          match(OpenCMLParser::T__7);
        }
        setState(766);
        match(OpenCMLParser::T__14);
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
  enterRule(_localctx, 116, OpenCMLParser::RuleSpecialType);
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
    setState(772);
    _la = _input->LA(1);
    if (!(((((_la - 80) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 80)) & 7) != 0))) {
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
  enterRule(_localctx, 118, OpenCMLParser::RuleIdentRef);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(774);
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
    case 44: return entityLinkSempred(antlrcpp::downCast<EntityLinkContext *>(context), predicateIndex);
    case 45: return entityCallSempred(antlrcpp::downCast<EntityCallContext *>(context), predicateIndex);
    case 47: return entityExprSempred(antlrcpp::downCast<EntityExprContext *>(context), predicateIndex);
    case 48: return relaExprSempred(antlrcpp::downCast<RelaExprContext *>(context), predicateIndex);
    case 49: return addExprSempred(antlrcpp::downCast<AddExprContext *>(context), predicateIndex);
    case 50: return multiExprSempred(antlrcpp::downCast<MultiExprContext *>(context), predicateIndex);
    case 54: return typeExprSempred(antlrcpp::downCast<TypeExprContext *>(context), predicateIndex);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::entityLinkSempred(EntityLinkContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 0: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::entityCallSempred(EntityCallContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 1: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::entityExprSempred(EntityExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 2: return precpred(_ctx, 8);
    case 3: return precpred(_ctx, 7);
    case 4: return precpred(_ctx, 6);
    case 5: return precpred(_ctx, 5);
    case 6: return precpred(_ctx, 4);
    case 7: return precpred(_ctx, 3);
    case 8: return precpred(_ctx, 2);
    case 9: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::relaExprSempred(RelaExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 10: return precpred(_ctx, 8);
    case 11: return precpred(_ctx, 7);
    case 12: return precpred(_ctx, 6);
    case 13: return precpred(_ctx, 5);
    case 14: return precpred(_ctx, 4);
    case 15: return precpred(_ctx, 3);
    case 16: return precpred(_ctx, 2);
    case 17: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::addExprSempred(AddExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 18: return precpred(_ctx, 4);
    case 19: return precpred(_ctx, 3);
    case 20: return precpred(_ctx, 2);
    case 21: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::multiExprSempred(MultiExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 22: return precpred(_ctx, 6);
    case 23: return precpred(_ctx, 5);
    case 24: return precpred(_ctx, 4);
    case 25: return precpred(_ctx, 3);
    case 26: return precpred(_ctx, 2);
    case 27: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool OpenCMLParser::typeExprSempred(TypeExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 28: return precpred(_ctx, 2);
    case 29: return precpred(_ctx, 1);

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
