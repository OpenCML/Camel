@echo off

set "info=[32m"
set "end=[0m"

@echo %info%Generating antlr parser for cpp...%end%
@echo Output dir is "../src/antlr".
java -jar antlr-4.13.1-complete.jar -Dlanguage=Cpp -DcontextSuperClass=antlr4::RuleContextWithAltNum OpenCML.g4 -no-listener -visitor -o "../src/antlr"
@echo %info%Task completed.%end%