// A reduced copy of the expression levels, for the proof case: the generator reads the production
// comments of the chain from `dataExpr` to `annoExpr`, and the case needs the same shape, not the
// real levels.

/// dataExpr : waitExpr | ctrlExpr
/// waitExpr : 'wait'? assignExpr
/// ctrlExpr : 'if' logicalOrExpr 'then' blockExpr ('else' blockExpr)?
/// assignExpr : logicalOrExpr (('=') logicalOrExpr)?
/// logicalOrExpr : logicalAndExpr ('||' logicalAndExpr)*
/// logicalAndExpr : equalityExpr ('&&' equalityExpr)*
/// equalityExpr : relationalExpr (('==' | '!=') relationalExpr)*
/// relationalExpr : additiveExpr (('<' | '>') additiveExpr)*
/// additiveExpr : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
/// multiplicativeExpr : powerExpr (('*' | '/') powerExpr)*
/// powerExpr : nullableExpr ('^' powerExpr)?
/// nullableExpr : unaryExpr (('??' | '!!') dataExpr)?
/// unaryExpr : linkExpr (('as' | 'is') typeExpr)?
/// linkExpr : negExpr ('->' compExpr)*
/// negExpr : ('-' | '!') negPower | compExpr
/// negPower : ('-' | '!') negPower | compExpr ('^' negPower)?
/// compExpr : annoExpr ('..' annoExpr)*
/// annoExpr : accessExpr (indices | parentArgues | '!')*

void ParserImpl::parseExpr() {}