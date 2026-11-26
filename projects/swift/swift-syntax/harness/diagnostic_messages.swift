// Generated from SwiftParserDiagnostics/{ParserDiagnosticMessages,MissingNodesError,MissingTokenError,LexerDiagnosticMessages}.swift:
// every static parser message, and a reader for the public properties of each concrete message type.
import SwiftDiagnostics
import SwiftParserDiagnostics
import SwiftSyntax

// every static parser error / fix-it / note message (generated from SwiftParserDiagnostics)
let allStaticParserErrors: [StaticParserError] = [.allStatementsInSwitchMustBeCoveredByCase, .legacyEmptyProtocolComposition, .legacyProtocolComposition, .legacyProtocolCompositionSingle, .ambiguousOptionalComposition, .expectedLeftBraceOrIfAfterElseDidYouMeanIf, .defaultArgumentInTupleType, .curriedParameterLists, .asyncInExpression, .doKeywordSpecifiesCatchRegion, .caseLabelOutsideOfSwitch, .oldStyleArrayType, .fileDefaultRejectsAttributes, .associatedTypeCannotUsePack, .associatedTypeCannotHaveGenericParameters, .caseOutsideOfSwitchOrEnum, .classConstraintCanOnlyBeUsedInProtocol, .consecutiveDeclarationsOnSameLine, .consecutiveStatementsOnSameLine, .cStyleForLoop, .defaultCannotBeUsedWithWhere, .defaultOutsideOfSwitch, .deinitCannotThrow, .editorPlaceholderInSourceFile, .escapedNewlineAtLastLineOfMultiLineStringLiteralNotAllowed, .expectedColonClass, .expectedExpressionAfterTry, .expectedAssignmentInsteadOfComparisonOperator, .expectedCommaInWhereClause, .expectedLeftBraceOrIfAfterElse, .expectedSequenceExpressionInForEachLoop, .extraTokensAtTheEndOfSourceLocationDirective, .extraTokensFollowingConditionalCompilationDirective, .extraRightBracket, .forbiddenExtendedEscapingString, .forbiddenInterpolatedString, .genericParamCantBeUsedInEnumCaseDecl, .initializerInPattern, .initializerCannotHaveName, .invalidFlagAfterPrecedenceGroupAssignment, .invalidPrecedenceGroupAssociativity, .letCaseMisspelled, .joinConditionsUsingComma, .joinPlatformsUsingComma, .maximumNestingLevelOverflow, .misplacedAttributeInVarDecl, .missingColonAndExprInTernaryExpr, .missingColonInTernaryExpr, .missingConformanceRequirement, .misspelledAsync, .misspelledThrows, .missingFixityInOperatorDeclaration, .multiLineStringLiteralMustBeginOnNewLine, .multiLineStringLiteralMustHaveClosingDelimiterOnNewLine, .operatorShouldBeDeclaredWithoutBody, .singleQuoteStringLiteral, .standaloneSemicolonStatement, .stringLiteralAtSign, .subscriptsCannotHaveNames, .submoduleCannotBeImportedUsingModuleSelector, .tooManyClosingPoundDelimiters, .tooFewClosingPoundDelimiters, .tooManyRawStringDelimitersToStartInterpolation, .tryMustBePlacedOnReturnedExpr, .tryMustBePlacedOnThrownExpr, .tryMustBePlacedOnThenExpr, .tryOnInitialValueExpression, .typeParameterPackEllipsis, .unexpectedPoundElseSpaceIf, .unexpectedSemicolon, .versionComparisonNotNeeded]
let allStaticParserFixIts: [StaticParserFixIt] = [.changeIndentationToMatchClosingDelimiter, .insertSemicolon, .insertAttributeArguments, .insertBackslash, .insertNewline, .insertWhitespace, .writeInlineArrayType, .joinIdentifiers, .joinIdentifiersWithCamelCase, .removeBackslash, .removeExtraneousDelimiters, .insertExtraClosingPounds, .replaceLegacyProtocolComposition, .parenthesizeComposition, .removeExtraneousWhitespace, .removeOperatorBody, .replaceCurlyQuoteByNormalQuote, .replaceNonBreakingSpaceBySpace, .wrapInBackticks, .convertToExtendedRegexLiteral]
let allStaticParserNotes: [StaticParserNote] = [.shouldMatchIndentationOfClosingQuote]

/// Read the public properties of a diagnostic's concrete message type.
func inspectMessage(_ m: Any) {
    if let x = m as? StaticParserError {  _ = x.message }
    if let x = m as? AsyncMustPrecedeThrows { _ = x.asyncKeywords; _ = x.throwsKeyword; _ = x.message }
    if let x = m as? AvailabilityConditionAsExpression { _ = x.availabilityToken; _ = x.negatedAvailabilityToken; _ = x.message }
    if let x = m as? AvailabilityConditionInExpression { _ = x.availabilityCondition; _ = x.message }
    if let x = m as? CannotParseVersionTuple { _ = x.versionTuple; _ = x.message }
    if let x = m as? DeclarationNotPermittedInContext { _ = x.missingDecl; _ = x.invalidDecl; _ = x.message }
    if let x = m as? DeinitializerSignatureError { _ = x.name; _ = x.params; _ = x.returnClause; _ = x.message }
    if let x = m as? DuplicateEffectSpecifiers { _ = x.correctSpecifier; _ = x.unexpectedSpecifier; _ = x.message }
    if let x = m as? EffectsSpecifierAfterArrow { _ = x.effectsSpecifiersAfterArrow; _ = x.message }
    if let x = m as? ExtraneousCodeAtTopLevel { _ = x.extraneousCode; _ = x.message }
    if let x = m as? ExtraneousWhitespace { _ = x.tokenWithWhitespace; _ = x.message }
    if let x = m as? IdentifierNotAllowedInOperatorName { _ = x.identifier; _ = x.message }
    if let x = m as? IfConfigDeclNotAllowedInContext { _ = x.context; _ = x.message }
    if let x = m as? InvalidFloatLiteralMissingLeadingZero { _ = x.decimalDigits; _ = x.message }
    if let x = m as? InvalidIdentifierError { _ = x.invalidIdentifier; _ = x.missingIdentifier; _ = x.message }
    if let x = m as? InvalidIndentationInMultiLineStringLiteralError { _ = x.kind; _ = x.lines; _ = x.message }
    if let x = m as? MissingAttributeArgument { _ = x.attributeName; _ = x.message }
    if let x = m as? MissingBothStringQuotesOfStringSegments { _ = x.stringSegments; _ = x.message }
    if let x = m as? MissingConditionInStatement {  _ = x.message }
    if let x = m as? MissingExpressionInStatement {  _ = x.message }
    if let x = m as? NegatedAvailabilityCondition { _ = x.availabilityCondition; _ = x.negatedAvailabilityKeyword; _ = x.message }
    if let x = m as? OpaqueMidComposition { _ = x.keyword; _ = x.message }
    if let x = m as? SpaceSeparatedIdentifiersError { _ = x.firstToken; _ = x.additionalTokens; _ = x.message }
    if let x = m as? SpecifierOnParameterName { _ = x.misplacedSpecifiers; _ = x.message }
    if let x = m as? TokensNotAllowedInOperatorName { _ = x.tokens; _ = x.message }
    if let x = m as? TrailingVersionAreIgnored { _ = x.major; _ = x.components; _ = x.message }
    if let x = m as? TryCannotBeUsed { _ = x.nextToken; _ = x.message }
    if let x = m as? UnexpectedNodesError { _ = x.unexpectedNodes; _ = x.message }
    if let x = m as? UnknownDifferentiabilityKind { _ = x.kind; _ = x.message }
    if let x = m as? UnknownDirectiveError { _ = x.unexpected; _ = x.message }
    if let x = m as? UnknownParameterError { _ = x.parameter; _ = x.validParameters; _ = x.message }
    if let x = m as? EffectSpecifierDeclaredHere {  _ = x.message }
    if let x = m as? StaticParserNote {  _ = x.message }
    if let x = m as? StaticParserFixIt {  _ = x.message }
    if let x = m as? InsertFixIt { _ = x.tokenToBeInserted; _ = x.message }
    if let x = m as? MoveToBeginningOfCompositionFixIt { _ = x.keyword; _ = x.message }
    if let x = m as? MoveTokensAfterFixIt { _ = x.movedTokens; _ = x.after; _ = x.message }
    if let x = m as? MoveTokensInFrontOfTypeFixIt { _ = x.movedTokens; _ = x.message }
    if let x = m as? RemoveRedundantFixIt { _ = x.removeTokens; _ = x.message }
    if let x = m as? RemoveNodesFixIt { _ = x.nodesToRemove; _ = x.message }
    if let x = m as? ReplaceTokensFixIt { _ = x.replaceTokens; _ = x.replacements; _ = x.message }
    if let x = m as? MoveNodesInFrontOfFixIt<Syntax> { _ = x.movedNodes; _ = x.inFrontOf; _ = x.message }
    if let x = m as? MoveNodesInFrontOfFixIt<UnexpectedNodesSyntax> { _ = x.movedNodes; _ = x.inFrontOf; _ = x.message }
    if let x = m as? MissingNodesError { _ = x.missingNodes; _ = x.message }
    if let x = m as? MatchingOpeningTokenNote {  _ = x.message }
    if let x = m as? InsertTokenFixIt { _ = x.missingNodes; _ = x.message }
    if let x = m as? InvalidFloatingPointExponentDigit { _ = x.kind; _ = x.message }
    if let x = m as? InvalidDigitInIntegerLiteral { _ = x.kind; _ = x.message }
    if let x = m as? ErrorToWarningDowngrade { _ = x.error; _ = x.message }
}
