/*
 *  CParser.cpp
 *  HyperC
 *
 *  Created by Uli Kusterer on 29.07.06.
 *  Copyright 2006 Uli Kusterer. All rights reserved.
 *
 */

// -----------------------------------------------------------------------------
//	Headers:
// -----------------------------------------------------------------------------

#include "CParser.h"
#include "CToken.h"
#include "CParseTree.h"
#include "CFunctionDefinitionNode.h"
#include "CCommandNode.h"
#include "CFunctionCallNode.h"
#include "CWhileLoopNode.h"
#include "CCodeBlockNode.h"
#include "CIfNode.h"
#include "CPushValueCommandNode.h"
#include "CAssignCommandNode.h"
#include "CGetParamCommandNode.h"
#include "CPrintCommandNode.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <fstream>
#include <cmath>


using namespace Carlson;


#define ADJUST_LINE_NUM(a,b)


// -----------------------------------------------------------------------------
//	Globals and static ivars:
// -----------------------------------------------------------------------------

namespace Carlson
{

// Static ivars:
int										CVariableEntry::mTempCounterSeed = 0;	// Counter we use for generating unique temp variable names.
std::map<std::string,CObjCMethodEntry>	CParser::sObjCMethodTable;				// Table of ObjC method signature -> types mappings for calling Cocoa.
std::map<std::string,CObjCMethodEntry>	CParser::sCFunctionTable;				// Table of C function name -> types mappings for calling native system calls.
std::map<std::string,CObjCMethodEntry>	CParser::sCFunctionPointerTable;		// Table of C function pointer type name -> types mappings for generating callback trampolines.
std::map<std::string,std::string>		CParser::sSynonymToTypeTable;			// Table of C type synonym name -> real name mappings.
std::map<std::string,int>				CParser::sConstantToValueTable;			// Table of C system constant name -> constant value mappings.

#pragma mark -
#pragma mark [Operator lookup table]
// LOOKUP TABLES:
// Operator token(s), precedence and instruction function name:
static TOperatorEntry	sOperators[] =
{
	{ EAndIdentifier, ELastIdentifier_Sentinel, 100, "vcy_op_and", EAndIdentifier },
	{ EOrIdentifier, ELastIdentifier_Sentinel, 100, "vcy_op_or", EOrIdentifier },
	{ ELessThanOperator, EGreaterThanOperator, 200, "vcy_cmp_ne", ENotEqualPseudoOperator },
	{ ELessThanOperator, EEqualsOperator, 200, "vcy_cmp_le", ELessThanEqualPseudoOperator },
	{ ELessThanOperator, ELastIdentifier_Sentinel, 200, "vcy_cmp_lt", ELessThanOperator },
	{ EGreaterThanOperator, EEqualsOperator, 200, "vcy_cmp_ge", EGreaterThanEqualPseudoOperator },
	{ EGreaterThanOperator, ELastIdentifier_Sentinel, 200, "vcy_cmp_gt", EGreaterThanOperator },
	{ EEqualsOperator, ELastIdentifier_Sentinel, 200, "vcy_cmp", EEqualsOperator },
	{ EIsIdentifier, ENotIdentifier, 200, "vcy_cmp_ne", ENotEqualPseudoOperator },
	{ EIsIdentifier, ELastIdentifier_Sentinel, 200, "vcy_cmp", EEqualsOperator },
	{ EAmpersandOperator, EAmpersandOperator, 300, "vcy_cat_space", EDoubleAmpersandPseudoOperator },
	{ EAmpersandOperator, ELastIdentifier_Sentinel, 300, "vcy_cat", EAmpersandOperator },
	{ EPlusOperator, ELastIdentifier_Sentinel, 500, "vcy_add", EPlusOperator },
	{ EMinusOperator, ELastIdentifier_Sentinel, 500, "vcy_sub", EMinusOperator },
	{ EMultiplyOperator, ELastIdentifier_Sentinel, 1000, "vcy_mul", EMultiplyOperator },
	{ EDivideOperator, ELastIdentifier_Sentinel, 1000, "vcy_div", EDivideOperator },
	{ EModIdentifier, ELastIdentifier_Sentinel, 1000, "vcy_mod", EModuloIdentifier },
	{ EModuloIdentifier, ELastIdentifier_Sentinel, 1000, "vcy_mod", EModuloIdentifier },
	{ EExponentOperator, ELastIdentifier_Sentinel, 1100, "vcy_pow", EExponentOperator },
	{ ELastIdentifier_Sentinel, ELastIdentifier_Sentinel, 0, "", ELastIdentifier_Sentinel }
};

static TUnaryOperatorEntry	sUnaryOperators[] =
{
	{ ENotIdentifier, "vcy_not" },
	{ EMinusOperator, "vcy_neg" },
	{ ELastIdentifier_Sentinel, "" }
};


static TGlobalPropertyEntry	sGlobalProperties[] =
{
	{ EItemDelIdentifier, "gItemDel" },
	{ EItemDelimIdentifier, "gItemDel" },
	{ EItemDelimiterIdentifier, "gItemDel" },
	{ ELastIdentifier_Sentinel, "" }
};


#pragma mark [Chunk type lookup table]
// Chunk expression start token -> Chunk type constant (as string for code generation):
static TChunkTypeEntry	sChunkTypes[] =
{
	{ ECharIdentifier, ECharsIdentifier, TChunkTypeCharacter },
	{ ECharacterIdentifier, ECharactersIdentifier, TChunkTypeCharacter },
	{ ELineIdentifier, ELinesIdentifier, TChunkTypeLine },
	{ EItemIdentifier, EItemsIdentifier, TChunkTypeItem },
	{ EWordIdentifier, EWordsIdentifier, TChunkTypeWord },
	{ ELastIdentifier_Sentinel, ELastIdentifier_Sentinel, TChunkTypeInvalid }
};

#pragma mark [Constant lookup table]
// Constant identifier and actual code to generate the value:
struct TConstantEntry	sConstants[] =
{
	{ ETrueIdentifier, new CBoolValueNode( NULL, true ) },
	{ EFalseIdentifier, new CBoolValueNode( NULL, false ) },
	{ EEmptyIdentifier, new CStringValueNode( NULL, std::string("") ) },
	{ ECommaIdentifier, new CStringValueNode( NULL, std::string(",") ) },
	{ EColonIdentifier, new CStringValueNode( NULL, std::string(":") ) },
	{ ECrIdentifier, new CStringValueNode( NULL, std::string("\r") ) },
	{ ELineFeedIdentifier, new CStringValueNode( NULL, std::string("\n") ) },
	{ ENullIdentifier, new CStringValueNode( NULL, std::string("\0") ) },
	{ EQuoteIdentifier, new CStringValueNode( NULL, std::string("\"") ) },
	{ EReturnIdentifier, new CStringValueNode( NULL, std::string("\r") ) },
	{ ENewlineIdentifier, new CStringValueNode( NULL, std::string("\n") ) },
	{ ESpaceIdentifier, new CStringValueNode( NULL, std::string(" ") ) },
	{ ETabIdentifier, new CStringValueNode( NULL, std::string("\t") ) },
	{ EPiIdentifier, new CFloatValueNode( NULL, (float) M_PI ) },
	{ ELastIdentifier_Sentinel, NULL }
};

#pragma mark [ObjC -> Variant mapping table]
// ObjC type -> variant conversion code mapping table:
struct TObjCTypeConversionEntry	sObjCToVariantMappings[] =
{
	{ "NSString*", "HyperC_VariantFromCFString( (CFStringRef)", " )", false },
	{ "CFStringRef", "HyperC_VariantFromCFString( ", " )", false },
	{ "NSNumber*", "HyperC_NSNumberToVariant(", ")", true },
	{ "CFNumberRef", "HyperC_NSNumberToVariant(", ")", true },
	{ "char*", "HyperC_VariantFromCString(", ")", false },
	{ "UInt8*", "HyperC_VariantFromCString(", ")", false },
	{ "const char*", "HyperC_VariantFromCString(", ")", false },
	{ "int", "CVariant( (int) ", " )", false },
	{ "unsigned int", "CVariant( (int) ", " )", false },
	{ "unsigned", "CVariant( (int) ", " )", false },
	{ "SInt8", "CVariant( (int) ", " )", false },
	{ "UInt8", "CVariant( (int) ", " )", false },
	{ "SInt16", "CVariant( (int) ", " )", false },
	{ "UInt16", "CVariant( (int) ", " )", false },
	{ "SInt32", "CVariant( (int) ", " )", false },
	{ "UInt32", "CVariant( (int) ", " )", false },
	{ "short", "CVariant( (int) ", " )", false },
	{ "unsigned short", "CVariant( (int) ", " )", false },
	{ "long", "CVariant( (int) ", " )", false },
	{ "unsigned long", "CVariant( (int) ", " )", false },
	//{ "SEL", "HyperC_VariantFromCFString( (CFStringRef) NSStringFromSelector( ", " ) )" },	// Doesn't work because CVariant only knows it's a "native object", but not whether it's a class, SEL or id.
	{ "SEL", "CVariant( (void*) ", " )", true },
	{ "id", "CVariant( (void*) ", " )", true },
	//{ "Class", "HyperC_VariantFromCFString( (CFStringRef) NSStringFromClass( ", " ) )" },		// Doesn't work because CVariant only knows it's a "native object", but not whether it's a class, SEL or id.
	{ "Class", "CVariant( (void*) ", " )", true },
	{ "char", "CVariant( (char) ", " )", false },
	{ "BOOL", "CVariant( (bool) ", " )", true },
	{ "bool", "CVariant( (bool) ", " )", false },
	{ "Boolean", "CVariant( (bool) ", " )", false },
	{ "NSRect", "HyperC_VariantFromCFString((CFStringRef)NSStringFromRect( ", " ))", true },
	{ "NSPoint", "HyperC_VariantFromCFString((CFStringRef)NSStringFromPoint( ", " ))", true },
	{ "NSSize", "HyperC_VariantFromCFString((CFStringRef)NSStringFromSize( ", " ))", true },
	{ "float", "CVariant( ", " )", false },
	{ "double", "CVariant( (float) ", " )" },
	{ "void", "((", "), CVariant(TVariantTypeNotSet) )", false },
	{ "", "", "", false }
};

#pragma mark [Variant -> ObjC mapping table]
// Mapping table for generating a certain ObjC type from a variant:
struct TObjCTypeConversionEntry	sVariantToObjCMappings[] =
{
	{ "NSString*", "[NSString stringWithUTF8String: (", ").GetAsString().c_str()]", true },
	{ "NSNumber*", "[NSNumber numberWithFloat: (", ").GetAsFloat()]", true },
	{ "CFStringRef", "((CFStringRef) [NSString stringWithUTF8String: (", ").GetAsString().c_str()])", true },
	{ "CFNumberRef", "((CFNumberRef) [NSNumber numberWithFloat: (", ").GetAsFloat()])", true },
	{ "char*", "(", ").GetAsString().c_str()", false },
	{ "const char*", "(", ").GetAsString().c_str()", false },
	{ "UInt8*", "(", ").GetAsString().c_str()", false },
	{ "int", "(", ").GetAsInt()", false },
	{ "unsigned int", "((unsigned int)(", ").GetAsInt())", false },
	{ "unsigned", "((unsigned)(", ").GetAsInt())", false },
	{ "short", "((short)(", ").GetAsInt())", false },
	{ "unsigned short", "((unsigned short)(", ").GetAsInt())", false },
	{ "long", "((long)(", ").GetAsInt())", false },
	{ "unsigned long", "((unsigned long)(", ").GetAsInt())", false },
	{ "UInt8", "((UInt8)(", ").GetAsInt())", false },
	{ "SInt8", "((SInt8)(", ").GetAsInt())", false },
	{ "UInt16", "((UInt16)(", ").GetAsInt())", false },
	{ "SInt16", "((SInt16)(", ").GetAsInt())", false },
	{ "UInt32", "((UInt32)(", ").GetAsInt())", false },
	{ "SInt32", "((SInt32)(", ").GetAsInt())", false },
	//{ "SEL", "NSSelectorFromString([NSString stringWithUTF8String: (", ").GetAsString().c_str()])" },	// Doesn't work because CVariant only knows it's a "native object", but not whether it's a class, SEL or id.
	{ "SEL", "((SEL)(", ").GetAsNativeObject())", true },
	{ "id", "((id)(", ").GetAsNativeObject())", true },
	//{ "Class", "NSClassFromString([NSString stringWithUTF8String: (", ").GetAsString().c_str()])" },	// Doesn't work because CVariant only knows it's a "native object", but not whether it's a class, SEL or id.
	{ "Class", "((Class)(", ").GetAsNativeObject())", true },
	{ "char", "(", ").GetAsString().c_str().at(0)", false },
	{ "BOOL", "((BOOL)(", ").GetAsBool())", true },
	{ "bool", "(", ").GetAsBool()", false },
	{ "Boolean", "(", ").GetAsBool()", false },
	{ "NSRect", "NSRectFromString( [NSString stringWithUTF8String: (", ").GetAsString().c_str()] )", true },
	{ "NSPoint", "NSPointFromString( [NSString stringWithUTF8String: (", ").GetAsString().c_str()] )", true },
	{ "NSSize", "NSSizeFromString( [NSString stringWithUTF8String: (", ").GetAsString().c_str()] )", true },
	{ "float", "(", ").GetAsFloat()", false },
	{ "double", "((double)(", ").GetAsFloat())", false },
	{ "", "", "", false }
};

#pragma mark -

void	PrintStringStream( std::stringstream& sstr )
{
	std::cout << sstr.str() << std::endl;
}


// -----------------------------------------------------------------------------
//	GetNewTempName:
//		Generate a unique name for a temp variable.
// -----------------------------------------------------------------------------

const std::string CVariableEntry::GetNewTempName()
{
	char tempName[40];
	snprintf( tempName, 40, "temp%d", mTempCounterSeed++ );
	
	return std::string( tempName );
}


// -----------------------------------------------------------------------------
//	Parse:
//		Main entrypoint. This takes a script that's been tokenised and generates
//		the proper source files.
// -----------------------------------------------------------------------------

void	CParser::Parse( const char* fname, std::deque<CToken>& tokens, CParseTree& parseTree )
{
	// -------------------------------------------------------------------------
	// First recursively parse our script for top-level constructs:
	//	(functions, commands, globals, whatever...)
	std::deque<CToken>::iterator	tokenItty = tokens.begin();
	
	mFileName = fname;
	
	while( tokenItty != tokens.end() )
	{
		ParseTopLevelConstruct( tokenItty, tokens, parseTree );
	}
}


void	CParser::ParseTopLevelConstruct( std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens, CParseTree& parseTree )
{
	if( tokenItty->IsIdentifier( ENewlineOperator ) )
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip the newline.
	}
	else if( tokenItty->IsIdentifier( EFunctionIdentifier ) )
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "function" 
		ParseFunctionDefinition( false, tokenItty, tokens, parseTree );
	}
	else if( tokenItty->IsIdentifier( EOnIdentifier ) )
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "on" 
		ParseFunctionDefinition( true, tokenItty, tokens, parseTree );
	}
	else if( tokenItty->IsIdentifier( EToIdentifier ) )
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "to" 
		ParseFunctionDefinition( true, tokenItty, tokens, parseTree );
	}
	else
	{
		std::cerr << mFileName << ":" << tokenItty->mLineNum << ": warning: Skipping " << tokenItty->GetShortDescription();
		CToken::GoNextToken( mFileName, tokenItty, tokens );	// Just skip it, whatever it may be.
		while( !tokenItty->IsIdentifier( ENewlineOperator ) )	// Now skip until the end of the line.
		{
			std::cerr << " " << tokenItty->GetShortDescription();
			CToken::GoNextToken( mFileName, tokenItty, tokens );
		}
		std::cerr << "." << std::endl;
	}
}


void	CParser::ParseFunctionDefinition( bool isCommand, std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens, CParseTree& parseTree )
{
	std::string								handlerName( tokenItty->GetIdentifierText() );
	std::string								userHandlerName( tokenItty->GetIdentifierText() );
	std::stringstream						fcnHeader;
	std::stringstream						fcnSignature;
	size_t									fcnLineNum = 0;
	
	fcnLineNum = tokenItty->mLineNum;
	
	CToken::GoNextToken( mFileName, tokenItty, tokens );

	if( mFirstHandlerName.length() == 0 )
	{
		mFirstHandlerName = handlerName;
		mFirstHandlerIsFunction = !isCommand;
	}
	
	CFunctionDefinitionNode*		currFunctionNode = NULL;
	currFunctionNode = new CFunctionDefinitionNode( &parseTree, isCommand, handlerName, fcnLineNum, parseTree.GetGlobals() );
	parseTree.AddNode( currFunctionNode );
	
	// Make built-in system variables so they get declared below like other local vars:
	currFunctionNode->AddLocalVar( "theResult", "the result", TVariantTypeEmptyString, false, false, false, false );

	int		currParamIdx = 0;
	
	while( !tokenItty->IsIdentifier( ENewlineOperator ) )
	{
		std::string	realVarName( tokenItty->GetIdentifierText() );
		std::string	varName("var_");
		varName.append( realVarName );
		CCommandNode*		theVarCopyCommand = new CGetParamCommandNode( &parseTree, tokenItty->mLineNum );
		theVarCopyCommand->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunctionNode, varName, realVarName) );
		theVarCopyCommand->AddParam( new CIntValueNode( &parseTree, currParamIdx++ ) );
		currFunctionNode->AddCommand( theVarCopyCommand );
		
		currFunctionNode->AddLocalVar( varName, realVarName, TVariantTypeEmptyString, false, true, false );	// Create param var and mark as parameter in variable list.
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		if( !tokenItty->IsIdentifier( ECommaOperator ) )
		{
			if( tokenItty->IsIdentifier( ENewlineOperator ) )
				break;
			std::stringstream		errMsg;
			errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected comma or end of line here, found "
									<< tokenItty->GetShortDescription() << ".";
			throw std::runtime_error( errMsg.str() );
		}
		CToken::GoNextToken( mFileName, tokenItty, tokens );
	}
	
	while( tokenItty->IsIdentifier( ENewlineOperator ) )
		CToken::GoNextToken( mFileName, tokenItty, tokens );

	ParseFunctionBody( userHandlerName, parseTree, currFunctionNode, tokenItty, tokens );
}


void	CParser::ParseHandlerCall( CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
									std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	std::string	handlerName;
	size_t		currLineNum = tokenItty->mLineNum;
	
	handlerName.append( tokenItty->GetIdentifierText() );
	CToken::GoNextToken( mFileName, tokenItty, tokens );

	CFunctionCallNode*	currFunctionCall = new CFunctionCallNode( &parseTree, true, handlerName, currLineNum );
	ParseParamList( ENewlineOperator, parseTree, currFunction, tokenItty, tokens, currFunctionCall );
	
	CCommandNode*			theVarAssignCommand = new CAssignCommandNode( &parseTree, currLineNum );
	theVarAssignCommand->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, "theResult", "the result") );
	theVarAssignCommand->AddParam( currFunctionCall );
	currFunction->AddCommand( theVarAssignCommand );
}


void	CParser::ParsePutStatement( CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
								std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	// Put:
	CCommandNode*			thePutCommand = NULL;
	size_t					startLine = tokenItty->mLineNum;
	
	try {
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		
		// What:
		CValueNode*	whatExpression = ParseExpression( parseTree, currFunction, tokenItty, tokens );
		
		// [into|after|before]
		if( tokenItty->IsIdentifier( EIntoIdentifier ) )
		{
			thePutCommand = new CCommandNode( &parseTree, "Put", startLine );
			thePutCommand->AddParam( whatExpression );
			CToken::GoNextToken( mFileName, tokenItty, tokens );
			
			// container:
			CValueNode*	destContainer = ParseContainer( false, false, parseTree, currFunction, tokenItty, tokens );
			thePutCommand->AddParam( destContainer );
		}
		else if( tokenItty->IsIdentifier( EAfterIdentifier ) )
		{
			thePutCommand = new CCommandNode( &parseTree, "Append", startLine );
			thePutCommand->AddParam( whatExpression );
			CToken::GoNextToken( mFileName, tokenItty, tokens );
			
			// container:
			CValueNode*	destContainer = ParseContainer( false, false, parseTree, currFunction, tokenItty, tokens );
			thePutCommand->AddParam( destContainer );
		}
		else if( tokenItty->IsIdentifier( EBeforeIdentifier ) )
		{
			thePutCommand = new CCommandNode( &parseTree, "Prepend", startLine );
			thePutCommand->AddParam( whatExpression );
			CToken::GoNextToken( mFileName, tokenItty, tokens );
			
			// container:
			CValueNode*	destContainer = ParseContainer( false, false, parseTree, currFunction, tokenItty, tokens );
			thePutCommand->AddParam( destContainer );
		}
		else
		{
			thePutCommand = new CPrintCommandNode( &parseTree, startLine );
			thePutCommand->AddParam( whatExpression );
		}
		
		currFunction->AddCommand( thePutCommand );
	}
	catch( ... )
	{
		if( thePutCommand )
			delete thePutCommand;
		
		throw;
	}
}


// This currently just compiles to a "put" command:
void	CParser::ParseSetStatement( CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
									std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	CValueNode*			propRef = NULL;
	CValueNode*			whatExpr = NULL;
	CCommandNode*		thePutCommand = new CCommandNode( &parseTree, "Put", tokenItty->mLineNum );
	try
	{
		// Set:
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		
		// property:
		if( tokenItty->mType != EIdentifierToken )
		{
			std::stringstream		errMsg;
			errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected property name here, found "
									<< tokenItty->GetShortDescription() << ".";
			throw std::runtime_error( errMsg.str() );
		}
		
		std::string			propertyName( tokenItty->GetIdentifierText() );
		TIdentifierSubtype	subType = tokenItty->mSubType;
		
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		
		if( tokenItty->mType != EIdentifierToken )
		{
			std::stringstream		errMsg;
			errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"of\" or \"to\" here, found "
									<< tokenItty->GetShortDescription() << ".";
			throw std::runtime_error( errMsg.str() );
		}
		
		if( tokenItty->IsIdentifier( EOfIdentifier ) )
		{
			throw std::runtime_error( "TODO: Object properties not yet implemented." );
		}
		else
		{
			// Find it in our list of global properties:
			int				x = 0;
			
			for( x = 0; sGlobalProperties[x].mType != ELastIdentifier_Sentinel; x++ )
			{
				if( sGlobalProperties[x].mType == subType )
				{
					propRef = new CLocalVariableRefValueNode( &parseTree, currFunction, sGlobalProperties[x].mGlobalPropertyVarName, sGlobalProperties[x].mGlobalPropertyVarName );
					break;
				}
			}
			
			if( propRef == NULL )
			{
				std::stringstream		errMsg;
				errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Unknown global property \""
										<< propertyName << "\".";
				throw std::runtime_error( errMsg.str() );
			}
			
			// to:
			if( !tokenItty->IsIdentifier( EToIdentifier ) )
			{
				std::stringstream		errMsg;
				errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"to\" here, found \""
										<< propertyName << "\".";
				throw std::runtime_error( errMsg.str() );
			}

			CToken::GoNextToken( mFileName, tokenItty, tokens );
			
			// What:
			whatExpr = ParseExpression( parseTree, currFunction, tokenItty, tokens );
			thePutCommand->AddParam( whatExpr );
			whatExpr = NULL;
			
			thePutCommand->AddParam( propRef );
			propRef = NULL;
		}
		
		currFunction->AddCommand( thePutCommand );
	}
	catch( ... )
	{
		if( propRef )
			delete propRef;
		if( whatExpr )
			delete whatExpr;
		if( thePutCommand )
			delete thePutCommand;
		
		throw;
	}

}


void	CParser::ParseGlobalStatement( bool isPublic, CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
										std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "global".
	
	std::string		globalName( "var_" );
	globalName.append( tokenItty->GetIdentifierText() );
	
	currFunction->AddLocalVar( globalName, tokenItty->GetIdentifierText(), TVariantType_INVALID, false, false, true );
	
	CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip global name.
}


void	CParser::ParseGetStatement( CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
									std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	CCommandNode*	thePutCommand = new CCommandNode( &parseTree, "Put", tokenItty->mLineNum );
	
	// We map "get" to "put <what> into it":
	CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "get".
	
	// What:
	CValueNode*	theWhatNode = ParseExpression( parseTree, currFunction, tokenItty, tokens );
	thePutCommand->AddParam( theWhatNode );
		
	// Make sure we have an "it":
	CreateVariable( "var_it", "it", false, currFunction );
	thePutCommand->AddParam( new CLocalVariableRefValueNode( &parseTree, currFunction, "var_it", "it" ) );
	
	currFunction->AddCommand( thePutCommand );
}


void	CParser::ParseReturnStatement( CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
										std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	CCommandNode*	theReturnCommand = new CCommandNode( &parseTree, "return", tokenItty->mLineNum );
	
	// Return:
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// What:
	CValueNode*	theWhatNode = ParseExpression( parseTree, currFunction, tokenItty, tokens );
	theReturnCommand->AddParam( theWhatNode );
	
	currFunction->AddCommand( theReturnCommand );
}


void	CParser::ParseAddStatement( CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
										std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	CCommandNode*	theAddCommand = new CCommandNode( &parseTree, "AddTo", tokenItty->mLineNum );
	
	// Add:
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// What:
	CValueNode*	theWhatNode = ParseExpression( parseTree, currFunction, tokenItty, tokens );
	theAddCommand->AddParam( theWhatNode );
	
	// To:
	if( !tokenItty->IsIdentifier( EToIdentifier ) )
	{
		std::stringstream		errMsg;
		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"to\" here, found "
								<< tokenItty->GetShortDescription() << ".";
		throw std::runtime_error( errMsg.str() );
	}
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// Dest:
	CValueNode*	theContainerNode = ParseContainer( false, false, parseTree, currFunction, tokenItty, tokens );
	theAddCommand->AddParam( theContainerNode );
	
	currFunction->AddCommand( theAddCommand );
}


void	CParser::ParseSubtractStatement( CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
										std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	CCommandNode*	theAddCommand = new CCommandNode( &parseTree, "SubtractFrom", tokenItty->mLineNum );
	
	// Add:
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// What:
	CValueNode*	theWhatNode = ParseExpression( parseTree, currFunction, tokenItty, tokens );
	theAddCommand->AddParam( theWhatNode );
	
	// From:
	if( !tokenItty->IsIdentifier( EFromIdentifier ) )
	{
		std::stringstream		errMsg;
		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"from\" here, found "
								<< tokenItty->GetShortDescription() << ".";
		throw std::runtime_error( errMsg.str() );
	}
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// Dest:
	CValueNode*	theContainerNode = ParseContainer( false, false, parseTree, currFunction, tokenItty, tokens );
	theAddCommand->AddParam( theContainerNode );
	
	currFunction->AddCommand( theAddCommand );
}


void	CParser::ParseMultiplyStatement( CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
										std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	CCommandNode*	theAddCommand = new CCommandNode( &parseTree, "MultiplyWith", tokenItty->mLineNum );
	
	// Multiply:
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// Dest:
	CValueNode*	theContainerNode = ParseContainer( false, false, parseTree, currFunction, tokenItty, tokens );
	theAddCommand->AddParam( theContainerNode );
	
	// With:
	if( !tokenItty->IsIdentifier( EWithIdentifier ) )
	{
		std::stringstream		errMsg;
		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"with\" here, found "
								<< tokenItty->GetShortDescription() << ".";
		throw std::runtime_error( errMsg.str() );
	}
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// What:
	CValueNode*	theWhatNode = ParseExpression( parseTree, currFunction, tokenItty, tokens );
	theAddCommand->AddParam( theWhatNode );
	
	currFunction->AddCommand( theAddCommand );
}


void	CParser::ParseDivideStatement( CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
										std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	CCommandNode*	theAddCommand = new CCommandNode( &parseTree, "DivideBy", tokenItty->mLineNum );
	
	// Divide:
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// Dest:
	CValueNode*	theContainerNode = ParseContainer( false, false, parseTree, currFunction, tokenItty, tokens );
	theAddCommand->AddParam( theContainerNode );
	
	// By:
	if( !tokenItty->IsIdentifier( EByIdentifier ) )
	{
		std::stringstream		errMsg;
		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"by\" here, found "
								<< tokenItty->GetShortDescription() << ".";
		throw std::runtime_error( errMsg.str() );
	}
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// What:
	CValueNode*	theWhatNode = ParseExpression( parseTree, currFunction, tokenItty, tokens );
	theAddCommand->AddParam( theWhatNode );
	
	currFunction->AddCommand( theAddCommand );
}


// When you enter this, "repeat for each" has already been parsed, and you should be at the chunk type token:
void	CParser::ParseRepeatForEachStatement( std::string& userHandlerName, CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
										std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	// chunk type:
	TChunkType	chunkTypeConstant = GetChunkTypeNameFromIdentifierSubtype( tokenItty->GetIdentifierSubType() );
	if( chunkTypeConstant == TChunkTypeInvalid )
	{
		std::stringstream		errMsg;
		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected chunk type identifier here, found "
								<< tokenItty->GetShortDescription() << ".";
		throw std::runtime_error( errMsg.str() );
	}
	CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip chunk type.
	
	// <varName>:
	std::string	counterVarName("var_");
	counterVarName.append( tokenItty->GetIdentifierText() );
	
	CreateVariable( counterVarName, tokenItty->GetIdentifierText(), false, currFunction );
	
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// of:
	if( !tokenItty->IsIdentifier( EOfIdentifier ) )
	{
		std::stringstream		errMsg;
		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"of\" here, found "
								<< tokenItty->GetShortDescription() << ".";
		throw std::runtime_error( errMsg.str() );
	}
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// <expression>
	size_t			currLineNum = tokenItty->mLineNum;
	CValueNode* theExpressionNode = ParseExpression( parseTree, currFunction, tokenItty, tokens );
	
	// MakeListOfAllChunksOfType( tempName, <expression> );
	std::string		tempName = CVariableEntry::GetNewTempName();
	std::string		tempCounterName = CVariableEntry::GetNewTempName();
	std::string		tempMaxCountName = CVariableEntry::GetNewTempName();
	
	CCommandNode*			theVarAssignCommand = new CCommandNode( &parseTree, "GetChunkArray", currLineNum );
	theVarAssignCommand->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempName, tempName) );
	theVarAssignCommand->AddParam( theExpressionNode );
	theVarAssignCommand->AddParam( new CIntValueNode(&parseTree, chunkTypeConstant) );
	currFunction->AddCommand( theVarAssignCommand );

	// tempCounterName = 0;
	theVarAssignCommand = new CAssignCommandNode( &parseTree, currLineNum );
	theVarAssignCommand->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempCounterName, tempCounterName) );
	theVarAssignCommand->AddParam( new CIntValueNode(&parseTree, 0) );
	currFunction->AddCommand( theVarAssignCommand );

	// tempMaxCountName = GetElementCount( tempName );
	CFunctionCallNode*	currFunctionCall = new CFunctionCallNode( false, &parseTree, "GetNumListItems",currLineNum);
	currFunctionCall->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempName, tempName) );
	theVarAssignCommand = new CAssignCommandNode( &parseTree, currLineNum );
	theVarAssignCommand->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempMaxCountName, tempMaxCountName) );
	theVarAssignCommand->AddParam( currFunctionCall );
	currFunction->AddCommand( theVarAssignCommand );
	
	// while( tempCounterName < tempMaxCountName )
	currFunctionCall = new CFunctionCallNode( false, &parseTree, "<", currLineNum );
	currFunctionCall->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempCounterName, tempCounterName) );
	currFunctionCall->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempMaxCountName, tempMaxCountName) );
	
	CWhileLoopNode*		whileLoop = new CWhileLoopNode( &parseTree, currLineNum, currFunction );
	currFunctionCall = new CFunctionCallNode( false, &parseTree, "<", currLineNum );
	currFunctionCall->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempCounterName, tempCounterName) );
	currFunctionCall->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempMaxCountName, tempMaxCountName) );
	whileLoop->SetCondition( currFunctionCall );
	currFunction->AddCommand( whileLoop );
	
	// counterVarName = GetConstElementAtIndex( tempName, tempCounterName );
	currFunctionCall = new CFunctionCallNode( false, &parseTree, "GetConstElementAtIndex", currLineNum );
	currFunctionCall->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempName, tempName) );
	currFunctionCall->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempCounterName, tempCounterName) );
	theVarAssignCommand = new CAssignCommandNode( &parseTree, currLineNum );
	theVarAssignCommand->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, counterVarName, counterVarName) );
	theVarAssignCommand->AddParam( currFunctionCall );
	whileLoop->AddCommand( theVarAssignCommand );

	while( !tokenItty->IsIdentifier( EEndIdentifier ) )
	{
		ParseOneLine( userHandlerName, parseTree, whileLoop, tokenItty, tokens );
	}
	
	// tempCounterName += 1;	-- increment loop counter.
	theVarAssignCommand = new CCommandNode( &parseTree, "+=", currLineNum );
	theVarAssignCommand->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempCounterName, tempCounterName) );
	theVarAssignCommand->AddParam( new CIntValueNode(&parseTree, 1) );
	whileLoop->AddCommand( theVarAssignCommand );
	
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	if( !tokenItty->IsIdentifier(ERepeatIdentifier) )	// end repeat
	{
		std::stringstream		errMsg;
		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"end repeat\" here, found "
								<< tokenItty->GetShortDescription() << ".";
		throw std::runtime_error( errMsg.str() );
	}
	CToken::GoNextToken( mFileName, tokenItty, tokens );
}


void	CParser::ParseRepeatStatement( std::string& userHandlerName, CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
										std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	int		conditionLineNum = tokenItty->mLineNum;
	
	// Repeat:
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	if( tokenItty->IsIdentifier( EWhileIdentifier ) || tokenItty->IsIdentifier( EUntilIdentifier ) )	// While:
	{
		bool			doUntil = (tokenItty->mSubType == EUntilIdentifier);
		
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		
		CWhileLoopNode*		whileLoop = new CWhileLoopNode( &parseTree, conditionLineNum, currFunction );
		CValueNode*			conditionNode = NULL;
		
		// Condition:
		conditionNode = ParseExpression( parseTree, currFunction, tokenItty, tokens );
		CFunctionCallNode*	funcNode = new CFunctionCallNode(  false, &parseTree, "GetAsBool", conditionLineNum );
		funcNode->AddParam( conditionNode );
		conditionNode = funcNode;

		if( doUntil )
		{
			funcNode = new CFunctionCallNode(  false, &parseTree, "!", conditionLineNum );
			funcNode->AddParam( conditionNode );
			conditionNode = funcNode;
		}
		
		whileLoop->SetCondition( conditionNode );
		
		// Commands:
		while( !tokenItty->IsIdentifier( EEndIdentifier ) )
		{
			ParseOneLine( userHandlerName, parseTree, whileLoop, tokenItty, tokens );
		}

		CToken::GoNextToken( mFileName, tokenItty, tokens );
		tokenItty->ExpectIdentifier( mFileName, ERepeatIdentifier, EEndIdentifier );
		CToken::GoNextToken( mFileName, tokenItty, tokens );
	}
	else if( tokenItty->IsIdentifier( EWithIdentifier ) )	// With:
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		
		std::string	counterVarName("var_");
		counterVarName.append( tokenItty->GetIdentifierText() );
		
		CreateVariable( counterVarName, tokenItty->GetIdentifierText(), false, currFunction );
		
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		
		// From:
		if( !tokenItty->IsIdentifier( EFromIdentifier ) && !tokenItty->IsIdentifier( EEqualsOperator ) )
		{
			std::stringstream		errMsg;
			errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"from\" or \"=\" here, found "
								<< tokenItty->GetShortDescription() << ".";
			throw std::runtime_error( errMsg.str() );
		}
		
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		
		// startNum:
		CValueNode*	startNumExpr = ParseExpression( parseTree, currFunction, tokenItty, tokens );
		
		const char*	incrementOp = "+=";
		const char*	compareOp = "<=";
		
		// [down] ?
		if( tokenItty->IsIdentifier( EDownIdentifier ) )
		{
			incrementOp = "-=";
			compareOp = ">=";
			
			CToken::GoNextToken( mFileName, tokenItty, tokens );
		}
		
		
		// To:
		tokenItty->ExpectIdentifier( mFileName, EToIdentifier );
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		
		// endNum:
		CValueNode*	endNumExpr = ParseExpression( parseTree, currFunction, tokenItty, tokens );
		std::string		tempName = CVariableEntry::GetNewTempName();
		currFunction->AddLocalVar( tempName, tempName, TVariantTypeInt );
		
		CWhileLoopNode*		whileLoop = new CWhileLoopNode( &parseTree, conditionLineNum, currFunction );
		
		// tempName = GetAsInt(startNum);
		CCommandNode*	theAssignCommand = new CAssignCommandNode( &parseTree, conditionLineNum );
		CFunctionCallNode*	theFuncCall = new CFunctionCallNode(  false, &parseTree, "GetAsInt", conditionLineNum );
		theFuncCall->AddParam( startNumExpr );
		theAssignCommand->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempName, tempName) );
		theAssignCommand->AddParam( theFuncCall );
		currFunction->AddCommand( theAssignCommand );
		
		// while( tempName <= GetAsInt(endNum) )
		CFunctionCallNode*	theComparison = new CFunctionCallNode( false, &parseTree, compareOp, conditionLineNum );
		theFuncCall = new CFunctionCallNode(  false, &parseTree, "GetAsInt", conditionLineNum );
		theFuncCall->AddParam( endNumExpr );
		theComparison->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempName, tempName) );
		theComparison->AddParam( theFuncCall );
		whileLoop->SetCondition( theComparison );
		
		// conterVar = itemName;
		theAssignCommand = new CAssignCommandNode( &parseTree, conditionLineNum );
		theAssignCommand->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, counterVarName, counterVarName) );
		theAssignCommand->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempName, tempName) );
		whileLoop->AddCommand( theAssignCommand );
		
		while( !tokenItty->IsIdentifier( EEndIdentifier ) )
		{
			ParseOneLine( userHandlerName, parseTree, whileLoop, tokenItty, tokens );
		}
		
		// tempName += 1;
		theAssignCommand = new CCommandNode( &parseTree, incrementOp, tokenItty->mLineNum );
		theAssignCommand->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempName, tempName) );
		theAssignCommand->AddParam( new CIntValueNode(&parseTree, 1) );
		whileLoop->AddCommand( theAssignCommand );	// TODO: Need to dispose this on exceptions above.
		
		currFunction->AddCommand( whileLoop );
		
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		tokenItty->ExpectIdentifier( mFileName, ERepeatIdentifier, EEndIdentifier );
		CToken::GoNextToken( mFileName, tokenItty, tokens );
	}
	else
	{
		// [for] ?
		if( tokenItty->IsIdentifier( EForIdentifier ) )
		{
			CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "for".
			if( tokenItty->IsIdentifier( EEachIdentifier ) )
			{
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "each".
				ParseRepeatForEachStatement( userHandlerName, parseTree,
											currFunction, tokenItty, tokens );
				return;
			}
		}
		
		// countNum:
		CValueNode*		countExpression = ParseExpression( parseTree, currFunction, tokenItty, tokens );
				
		// [times] ?
		if( tokenItty->IsIdentifier( ETimesIdentifier ) )
			CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "times".
		
		std::string			tempName = CVariableEntry::GetNewTempName();
		CWhileLoopNode*		whileLoop = new CWhileLoopNode( &parseTree, conditionLineNum, currFunction );
		
		// tempName = 0;
		CCommandNode*	theAssignCommand = new CAssignCommandNode( &parseTree, conditionLineNum );
		theAssignCommand->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempName, tempName) );
		theAssignCommand->AddParam( new CIntValueNode(&parseTree, 0) );
		currFunction->AddCommand( theAssignCommand );
		
		// while( tempName < GetAsInt(countExpression) )
		CFunctionCallNode*	theComparison = new CFunctionCallNode( false, &parseTree, "<", conditionLineNum );
		CFunctionCallNode*	theFuncCall = new CFunctionCallNode( false, &parseTree, "GetAsInt", conditionLineNum );
		theFuncCall->AddParam( countExpression );
		theComparison->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempName, tempName) );
		theComparison->AddParam( theFuncCall );
		whileLoop->SetCondition( theComparison );

		while( !tokenItty->IsIdentifier( EEndIdentifier ) )
		{
			ParseOneLine( userHandlerName, parseTree, whileLoop, tokenItty, tokens );
		}
		
		// tempName += 1;
		theAssignCommand = new CCommandNode( &parseTree, "+=", tokenItty->mLineNum );
		theAssignCommand->AddParam( new CLocalVariableRefValueNode(&parseTree, currFunction, tempName, tempName) );
		theAssignCommand->AddParam( new CIntValueNode(&parseTree, 1) );
		whileLoop->AddCommand( theAssignCommand );
		currFunction->AddCommand( whileLoop );	// TODO: Need to dispose this on exceptions above.

		CToken::GoNextToken( mFileName, tokenItty, tokens );
		tokenItty->ExpectIdentifier( mFileName, ERepeatIdentifier, EEndIdentifier );
		CToken::GoNextToken( mFileName, tokenItty, tokens );
	}
}


void	CParser::ParseIfStatement( std::string& userHandlerName, CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
										std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	std::string		tempName = CVariableEntry::GetNewTempName();
	int				conditionLineNum = tokenItty->mLineNum;
	CIfNode*		ifNode = new CIfNode( &parseTree, conditionLineNum, currFunction );
	
	// If:
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// Condition:
	CValueNode*			condition = ParseExpression( parseTree, currFunction, tokenItty, tokens );
	CFunctionCallNode*	fcall = new CFunctionCallNode( false, &parseTree, "GetAsBool", conditionLineNum );
	fcall->AddParam( condition );
	ifNode->SetCondition( condition );
	
	while( tokenItty->IsIdentifier(ENewlineOperator) )
		CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// Then:
	tokenItty->ExpectIdentifier( mFileName, EThenIdentifier );
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	bool	needEndIf = true;
	
	if( tokenItty->IsIdentifier( ENewlineOperator ) )
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		// Commands:
		while( !tokenItty->IsIdentifier( EEndIdentifier ) && !tokenItty->IsIdentifier( EElseIdentifier ) )
		{
			ParseOneLine( userHandlerName, parseTree, ifNode, tokenItty, tokens );
		}
	}
	else
	{
		ParseOneLine( userHandlerName, parseTree, ifNode, tokenItty, tokens, true );
		needEndIf = false;
	}
	
	while( tokenItty->IsIdentifier(ENewlineOperator) )
		CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// Else:
	if( tokenItty->IsIdentifier( EElseIdentifier ) )	// It's an "else"! Parse another block!
	{
		CCodeBlockNode*		elseNode = ifNode->CreateElseBlock( tokenItty->mLineNum );
		
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		
		if( tokenItty->IsIdentifier(ENewlineOperator) )	// Followed by a newline! Multi-line if!
		{
			CToken::GoNextToken( mFileName, tokenItty, tokens );
			while( !tokenItty->IsIdentifier( EEndIdentifier ) )
			{
				ParseOneLine( userHandlerName, parseTree, elseNode, tokenItty, tokens );
			}
			needEndIf = true;
		}
		else
		{
			ParseOneLine( userHandlerName, parseTree, elseNode, tokenItty, tokens, true );	// Don't swallow return.
			needEndIf = false;
		}
	}
	
	// End If:
	if( needEndIf && tokenItty->IsIdentifier( EEndIdentifier ) )
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		if( !tokenItty->IsIdentifier(EIfIdentifier) )
		{
			std::stringstream		errMsg;
			errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"end if\" here, found "
									<< tokenItty->GetShortDescription() << ".";
			throw std::runtime_error( errMsg.str() );
		}
		CToken::GoNextToken( mFileName, tokenItty, tokens );
	}
	
	currFunction->AddCommand( ifNode );	// TODO: delete ifNode on exceptions above, and condition before it's added to ifNode etc.
}


CValueNode*	CParser::ParseArrayItem( CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
								std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// itemNumber:
	CValueNode*	theIndex = ParseExpression( parseTree, currFunction, tokenItty, tokens );
	
	// of:
	tokenItty->ExpectIdentifier( mFileName, EOfIdentifier );
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	// container:
	size_t				containerLineNum = tokenItty->mLineNum;
	CValueNode*			theTarget = ParseContainer( false, true, parseTree, currFunction, tokenItty, tokens );
	CFunctionCallNode*	fcall = new CFunctionCallNode( &parseTree, true, "GetItemOfListWithKey", containerLineNum );
	fcall->AddParam( theTarget );
	fcall->AddParam( theIndex );
	
	return fcall;	// TODO: delete stuff on exceptions before this.
}


CValueNode*	CParser::ParseContainer( bool asPointer, bool initWithName, CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
								std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	// Try to find chunk type that matches:
	TChunkType	typeConstant = GetChunkTypeNameFromIdentifierSubtype( tokenItty->mSubType );
	
	if( typeConstant != TChunkTypeInvalid )
	{
		return ParseChunkExpression( typeConstant, parseTree, currFunction, tokenItty, tokens );
	}

	// Otherwise try to parse a variable:
	if( tokenItty->IsIdentifier( ETheIdentifier ) )
		CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	std::string		realVarName( tokenItty->GetIdentifierText() );
	std::string		varName( "var_" );
	if( tokenItty->IsIdentifier( EResultIdentifier ) )
	{
		varName.assign( "theResult" );
		CreateVariable( varName, realVarName, initWithName, currFunction );
	}
	else if( tokenItty->IsIdentifier( EItemDelimiterIdentifier ) || tokenItty->IsIdentifier( EItemDelIdentifier )
			|| tokenItty->IsIdentifier( EItemDelimIdentifier ) )
	{
		varName.assign( "gItemDel" );
		realVarName.assign( "itemDelimiter" );
		CreateVariable( varName, realVarName, initWithName, currFunction, true );
	}
	else
	{
		varName.append( realVarName );
		CreateVariable( varName, realVarName, initWithName, currFunction );
	}
	
//	theFunctionBody << (asPointer? "&" : "") << varName;
	
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	return new CLocalVariableRefValueNode( &parseTree, currFunction, varName, realVarName );
}


void	CParser::CreateVariable( const std::string& varName, const std::string& realVarName, bool initWithName,
									CCodeBlockNodeBase* currFunction, bool isGlobal )
{
	std::map<std::string,CVariableEntry>::iterator	theContainerItty;
	std::map<std::string,CVariableEntry>*			varMap;
	
	if( isGlobal )
		varMap = &currFunction->GetGlobals();
	else
		varMap = &currFunction->GetLocals();
	theContainerItty = varMap->find( varName );

	if( theContainerItty == varMap->end() )	// No var of that name yet?
		(*varMap)[varName] = CVariableEntry( realVarName, TVariantType_INVALID, initWithName );	// Add one to variable list.

}


void	CParser::ParseOneLine( std::string& userHandlerName, CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
								std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens,
								bool dontSwallowReturn )
{
	while( tokenItty->IsIdentifier(ENewlineOperator) )
		CToken::GoNextToken( mFileName, tokenItty, tokens );
	
	if( tokenItty->mType == EIdentifierToken && tokenItty->mSubType == ELastIdentifier_Sentinel )	// Unknown identifier.
		ParseHandlerCall( parseTree, currFunction, tokenItty, tokens );
	else if( tokenItty->IsIdentifier(EPutIdentifier) )		// put command.
		ParsePutStatement( parseTree, currFunction, tokenItty, tokens );
	else if( tokenItty->IsIdentifier(EDeleteIdentifier) )	// delete command.
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		
		CValueNode*	theContainer = ParseContainer( false, false, parseTree, currFunction, tokenItty, tokens );
		CFunctionCallNode*	theFCall = new CFunctionCallNode( &parseTree, true, "Delete", tokenItty->mLineNum );
		theFCall->AddParam( theContainer );
		currFunction->AddCommand( theFCall );
	}
	else if( tokenItty->IsIdentifier(EReturnIdentifier) )
		ParseReturnStatement( parseTree, currFunction, tokenItty, tokens );
	else if( tokenItty->IsIdentifier(EExitIdentifier) )
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "exit".
		if( tokenItty->IsIdentifier(ERepeatIdentifier) )
		{
			CCommandNode*	theExitRepeatCommand = new CCommandNode( &parseTree, "ExitRepeat", tokenItty->mLineNum );
			currFunction->AddCommand( theExitRepeatCommand );
			CToken::GoNextToken( mFileName, tokenItty, tokens );
		}
		else if( tokenItty->GetIdentifierText().compare(userHandlerName) == 0 )
		{
			CCommandNode*	theReturnCommand = new CCommandNode( &parseTree, "return", tokenItty->mLineNum );
			currFunction->AddCommand( theReturnCommand );
			theReturnCommand->AddParam( new CStringValueNode(&parseTree, "") );
			CToken::GoNextToken( mFileName, tokenItty, tokens );
		}
		else
		{
			std::stringstream errMsg;
			errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"exit repeat\" or \"exit " << userHandlerName << "\", found "
					<< tokenItty->GetShortDescription() << ".";
			throw std::runtime_error( errMsg.str() );
		}
	}
	else if( tokenItty->IsIdentifier(ENextIdentifier) )
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "next".
		if( tokenItty->IsIdentifier(ERepeatIdentifier) )
		{
			CCommandNode*	theNextRepeatCommand = new CCommandNode( &parseTree, "NextRepeat", tokenItty->mLineNum );
			currFunction->AddCommand( theNextRepeatCommand );
			CToken::GoNextToken( mFileName, tokenItty, tokens );
		}
		else
		{
			std::stringstream errMsg;
			errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"next repeat\", found "
					<< tokenItty->GetShortDescription() << ".";
			throw std::runtime_error( errMsg.str() );
		}
	}
	else if( tokenItty->IsIdentifier(ERepeatIdentifier) )
		ParseRepeatStatement( userHandlerName, parseTree, currFunction, tokenItty, tokens );
	else if( tokenItty->IsIdentifier(EIfIdentifier) )
		ParseIfStatement( userHandlerName, parseTree, currFunction, tokenItty, tokens );
	else if( tokenItty->IsIdentifier(EAddIdentifier) )
		ParseAddStatement( parseTree, currFunction, tokenItty, tokens );
	else if( tokenItty->IsIdentifier(ESubtractIdentifier) )
		ParseSubtractStatement( parseTree, currFunction, tokenItty, tokens );
	else if( tokenItty->IsIdentifier(EMultiplyIdentifier) )
		ParseMultiplyStatement( parseTree, currFunction, tokenItty, tokens );
	else if( tokenItty->IsIdentifier(EDivideIdentifier) )
		ParseDivideStatement( parseTree, currFunction, tokenItty, tokens );
	else if( tokenItty->IsIdentifier(EGetIdentifier) )
		ParseGetStatement( parseTree, currFunction, tokenItty, tokens );
	else if( tokenItty->IsIdentifier(ESetIdentifier) )
		ParseSetStatement( parseTree, currFunction, tokenItty, tokens );
	else if( tokenItty->IsIdentifier(EGlobalIdentifier) )
	{
		std::stringstream errMsg;
		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: We can't do globals yet, only private globals.";
		throw std::runtime_error( errMsg.str() );
		//ParseGlobalStatement( false, currFunction, tokenItty, tokens );
	}
	else if( tokenItty->IsIdentifier(EPrivateIdentifier) )
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		if( !tokenItty->IsIdentifier(EGlobalIdentifier) )
		{
			std::stringstream errMsg;
			errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"global\" after \"private\", found "
					<< tokenItty->GetShortDescription() << ".";
			throw std::runtime_error( errMsg.str() );
		}
		ParseGlobalStatement( false, parseTree, currFunction, tokenItty, tokens );
	}
	else if( tokenItty->IsIdentifier(EPublicIdentifier) )
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );
		if( !tokenItty->IsIdentifier(EGlobalIdentifier) )
		{
			std::stringstream errMsg;
			errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"global\" after \"public\", found "
					<< tokenItty->GetShortDescription() << ".";
			throw std::runtime_error( errMsg.str() );
		}
		ParseGlobalStatement( true, parseTree, currFunction, tokenItty, tokens );
	}
	else
	{
		std::stringstream errMsg;
		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected command name or \"end " << userHandlerName << "\", found "
				<< tokenItty->GetShortDescription() << ".";
		throw std::runtime_error( errMsg.str() );
	}
	
	// End this line:
	if( !dontSwallowReturn )
	{
		if( !tokenItty->IsIdentifier(ENewlineOperator) )
		{
			std::stringstream errMsg;
			errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected end of line, found "
					<< tokenItty->GetShortDescription() << ".";
			throw std::runtime_error( errMsg.str() );
		}
			
		while( tokenItty->IsIdentifier(ENewlineOperator) )
			CToken::GoNextToken( mFileName, tokenItty, tokens );
	}
}


void	CParser::ParseFunctionBody( std::string& userHandlerName,
									CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
									std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	while( !tokenItty->IsIdentifier( EEndIdentifier ) )	// Sub-constructs will swallow their own "end XXX" instructions, so we can exit the loop. Either it's our "end", or it's unbalanced.
	{
		ParseOneLine( userHandlerName, parseTree, currFunction, tokenItty, tokens );
	}
	
	CToken::GoNextToken( mFileName, tokenItty, tokens );
	if( tokenItty->GetIdentifierText().compare(userHandlerName) != 0 )
	{
		std::stringstream		errMsg;
		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"end " << userHandlerName << "\" here, found "
								<< tokenItty->GetShortDescription() << ".";
		throw std::runtime_error( errMsg.str() );
	}
	CToken::GoNextToken( mFileName, tokenItty, tokens );
}


// Parse a list of expressions separated by commas as a list value for passing to
//	a handler as a parameter list:
void	CParser::ParseParamList( TIdentifierSubtype identifierToEndOn,
								CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
								std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens,
								CFunctionCallNode* inFCallToAddTo )
{
	while( !tokenItty->IsIdentifier( identifierToEndOn ) )
	{
		CValueNode*		paramExpression = ParseExpression( parseTree, currFunction, tokenItty, tokens );
		inFCallToAddTo->AddParam( paramExpression );
		
		if( !tokenItty->IsIdentifier( ECommaOperator ) )
		{
			if( tokenItty->IsIdentifier( identifierToEndOn ) )
				break;	// Exit loop.
			std::stringstream		errMsg;
			errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected comma here, found "
									<< tokenItty->GetShortDescription() << ".";
			throw std::runtime_error( errMsg.str() );
		}
		CToken::GoNextToken( mFileName, tokenItty, tokens );
	}
}


TIdentifierSubtype	CParser::ParseOperator( std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens, int *outPrecedence, const char* *outOpName )
{
	if( tokenItty->mType != EIdentifierToken )
		return ELastIdentifier_Sentinel;
	
	int		x = 0;
	for( ; sOperators[x].mType != ELastIdentifier_Sentinel; x++ )
	{
		if( tokenItty->IsIdentifier( sOperators[x].mType ) )
		{
			CToken::GoNextToken( mFileName, tokenItty, tokens );
			
			// Is single-token operator and matches?
			if( sOperators[x].mSecondType == ELastIdentifier_Sentinel )
			{
				*outPrecedence = sOperators[x].mPrecedence;
				*outOpName = sOperators[x].mOperationName;
				
				return sOperators[x].mTypeToReturn;
			}
			else if( tokenItty->IsIdentifier(sOperators[x].mSecondType) )
			{
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Swallow second operator token, too.
				*outPrecedence = sOperators[x].mPrecedence;
				*outOpName = sOperators[x].mOperationName;
				
				return sOperators[x].mTypeToReturn;
			}
			else
				CToken::GoPrevToken( mFileName, tokenItty, tokens );	// Backtrack so we don't accidentally swallow the token following this operator.
		}
	}
	
	return ELastIdentifier_Sentinel;
}


// -----------------------------------------------------------------------------
//	CollapseExpressionStack ():
//		Take the passed lists of terms and operators and go over them from
//		the right end, generating a function call for the rightmost operator/two
//		terms combination and than pushing that call back on the tree for use
//		as the rightmost argument of the next operator.
// -----------------------------------------------------------------------------

CValueNode*	CParser::CollapseExpressionStack( CParseTree& parseTree, std::deque<CValueNode*> &terms, std::deque<const char*> &operators )
{
	CValueNode*		operandA = NULL;
	const char*		opName = NULL;

	while( terms.size() > 1 )	// More than 1 operand? Process stuff on stack.
	{
		CValueNode*			operandB = NULL;
		
		opName = operators.back();
		operators.pop_back();
		
		operandB = terms.back();
		terms.pop_back();
		
		operandA = terms.back();
		terms.pop_back();
		
		CFunctionCallNode*	currOperation = new CFunctionCallNode( false, &parseTree, opName, operandA->GetLineNum() );
		currOperation->AddParam( operandA );
		currOperation->AddParam( operandB );
		
		terms.push_back( currOperation );
	}
	
	operandA = terms.back();
	terms.pop_back();
	
	return operandA;
}


// -----------------------------------------------------------------------------
//	ParseExpression ():
//		Parse an expression from the given token stream, adding any variables
//		and commands needed to the given function. This uses a stack to collect
//		all terms and operators, and collapses subexpressions whenever the
//		operator precedence goes down.
// -----------------------------------------------------------------------------

CValueNode*	CParser::ParseExpression( CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
										std::deque<CToken>::iterator& tokenItty,
										std::deque<CToken>& tokens )
{
	std::deque<CValueNode*>	terms;
	std::deque<const char*>	operators;
	CValueNode*				currArg;
	TIdentifierSubtype		currOpType = ELastIdentifier_Sentinel;
	int						currPrecedence = 0,
							prevPrecedence = 0;
	const char*				opName = NULL;
	
	currArg = ParseTerm( parseTree, currFunction, tokenItty, tokens );
	terms.push_back( currArg );
	currArg = NULL;
	
	while( (currOpType = ParseOperator( tokenItty, tokens, &currPrecedence, &opName )) != ELastIdentifier_Sentinel )
	{
		if( prevPrecedence > currPrecedence )
		{
			CValueNode*	collapsedOp = CollapseExpressionStack( parseTree, terms, operators );
			terms.push_back( collapsedOp );
		}

		currArg = ParseTerm( parseTree, currFunction, tokenItty, tokens );
		terms.push_back( currArg );
		currArg = NULL;
		
		operators.push_back( opName );
		
		prevPrecedence = currPrecedence;
	}
	
	return CollapseExpressionStack( parseTree, terms, operators );
}


void	CParser::LoadNativeHeaders()
{
//	static bool		objCHeadersLoaded = false;
//	
//	if( !objCHeadersLoaded )
//	{
//		// System headers (automatically built):
//		std::string			fwkheaderspath(mSupportFolderPath);
//		fwkheaderspath.append("/frameworkheaders.hhc");
//		LoadNativeHeadersFromFile( fwkheaderspath.c_str() );
//		
//		/* Headers for our built-in functions:
//		fwkheaderspath.assign(mSupportFolderPath);
//		fwkheaderspath.append("/builtinheaders.hhc");
//		LoadNativeHeadersFromFile( fwkheaderspath.c_str() );*/
//		
//		objCHeadersLoaded = true;
//	}
}

void	CParser::LoadNativeHeadersFromFile( const char* filepath )
{
//	std::ifstream		headerFile(filepath);
//	char				theCh = 0;
//	std::string			headerPath;		
//	std::string			frameworkPath;
//	
//	while( theCh != std::ifstream::traits_type::eof() )
//	{
//		theCh = headerFile.get();
//		//std::cout << theCh << std::endl;
//		
//		if( theCh == std::ifstream::traits_type::eof() )
//			break;
//		
//		switch( theCh )
//		{
//			case '#':	// comment.
//			case '*':	// class name.
//			case '<':	// protocol class/category implements.
//			case '(':	// category name.
//			case ':':	// superclass.
//				while( (theCh = headerFile.get()) != std::ifstream::traits_type::eof() && theCh != '\n' )
//					;	// Do nothing, just swallow characters on this line.
//				break;
//			
//			case '\n':	// Empty line? Just skip.
//				break;
//			
//			case 'F':
//				frameworkPath.clear();
//				while( (theCh = headerFile.get()) != std::ifstream::traits_type::eof() && theCh != '\n' )
//					frameworkPath.append( 1, theCh );
//				break;
//
//			case 'H':
//				headerPath.clear();
//				while( (theCh = headerFile.get()) != std::ifstream::traits_type::eof() && theCh != '\n' )
//					headerPath.append( 1, theCh );
//				break;
//			
//			case '~':
//			{
//				std::string		typeStr;
//				std::string		synonymousStr;
//				bool			hadComma = false;
//				
//				while( (theCh = headerFile.get()) != std::ifstream::traits_type::eof() && theCh != '\n' )
//				{
//					if( theCh == ',' )
//						hadComma = true;
//					else if( hadComma )
//						typeStr.append( 1, theCh );
//					else
//						synonymousStr.append( 1, theCh );
//				}
//				sSynonymToTypeTable[synonymousStr] = typeStr;
//				break;
//			}
//			
//			case 'e':
//			{
//				std::string		constantStr;
//				std::string		synonymousStr;
//				bool			hadComma = false;
//				
//				while( (theCh = headerFile.get()) != std::ifstream::traits_type::eof() && theCh != '\n' )
//				{
//					if( theCh == ',' )
//						hadComma = true;
//					else if( hadComma )
//						constantStr.append( 1, theCh );
//					else
//						synonymousStr.append( 1, theCh );
//				}
//				sConstantToValueTable[constantStr] = synonymousStr;
//				break;
//			}
//			
//			case '=':
//			case '-':
//			case '+':
//			case '&':
//			{
//				std::string		typesLine;
//				std::string		selectorStr;
//				char			nextSwitchChar = ',';
//				bool			isFunction = (theCh == '=');
//				bool			isFunctionPtr = (theCh == '&');
//				
//				while( (theCh = headerFile.get()) != std::ifstream::traits_type::eof() && theCh != '\n' )
//				{
//					switch( nextSwitchChar )
//					{
//						case ',':
//							if( nextSwitchChar == theCh )	// Found comma? We're finished getting selector name. Rest of line goes in types.
//								nextSwitchChar = '\n';
//							else
//								selectorStr.append( 1, theCh );
//							break;
//						
//						case '\n':
//							typesLine.append( 1, theCh );
//							break;
//					}
//				}
//				
//				if( isFunction )
//					sCFunctionTable[selectorStr] = CObjCMethodEntry( headerPath, frameworkPath, typesLine );
//				else if( isFunctionPtr )
//					sCFunctionPointerTable[selectorStr] = CObjCMethodEntry( headerPath, frameworkPath, typesLine );
//				else
//					sObjCMethodTable[selectorStr] = CObjCMethodEntry( headerPath, frameworkPath, typesLine );
//				//std::cout << selectorStr << " = " << typesLine << std::endl;
//				break;
//			}
//			
//			default:	// unknown.
//				std::cout << "warning: Ignoring unknown data of type \"" << theCh << "\" in framework headers:" << std::endl;
//				while( (theCh = headerFile.get()) != std::ifstream::traits_type::eof() && theCh != '\n' )
//					std::cout << theCh;	// Print characters on this line.
//				std::cout << std::endl;
//				break;
//		}
//	}
}


// This parses an *editable* chunk expression that is a CVariant. This is a
//	little more complex and dangerous, as it simply points to the target value.
//	If you can, use the more efficient call for constant (i.e. non-changeable)
//	chunk expressions.

CValueNode*	CParser::ParseChunkExpression( TChunkType typeConstant, CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
											std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "char" or "item" or whatever chunk type token this was.
	
	std::stringstream		valueStr;
	std::stringstream		startOffs;
	std::stringstream		endOffs;
	bool					hadTo = false;
	
	// Start offset:
	CValueNode*	startOffsObj = ParseExpression( parseTree, currFunction, tokenItty, tokens );
	CValueNode*	endOffsObj = NULL;
	
	int		lineNum = tokenItty->mLineNum;
	
	// (Optional) end offset:
	if( tokenItty->IsIdentifier( EToIdentifier ) || tokenItty->IsIdentifier( EThroughIdentifier )
		|| tokenItty->IsIdentifier( EThruIdentifier ) )
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "to"/"through"/"thru".
		
		endOffsObj = ParseExpression( parseTree, currFunction, tokenItty, tokens );
		hadTo = true;
	}
	
	// Target value:
	if( !tokenItty->IsIdentifier( EOfIdentifier ) )
	{
		std::stringstream		errMsg;
		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected ";
		if( !hadTo )
			errMsg << "\"to\" or ";
		errMsg << "\"of\" here, found " << tokenItty->GetShortDescription() << ".";
		throw std::runtime_error( errMsg.str() );
	}
	CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "of".
	
	CValueNode*	targetValObj = ParseTerm( parseTree, currFunction, tokenItty, tokens );
	
	// Now output code:
	CFunctionCallNode*	currOperation = new CFunctionCallNode( &parseTree, true, std::string("MakeChunk"), lineNum );
	currOperation->AddParam( new CIntValueNode( &parseTree, typeConstant ) );
	currOperation->AddParam( startOffsObj );
	currOperation->AddParam( hadTo ? endOffsObj : startOffsObj );
	currOperation->AddParam( targetValObj );
	currOperation->AddParam( targetValObj );
	
	return currOperation;
}


// This parses an *un-editable* chunk expression that can only be read. This
//	pretty much just fetches the chunk value right then and there.

CValueNode*	CParser::ParseConstantChunkExpression( TChunkType typeConstant, CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
										std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "char" or "item" or whatever chunk type token this was.
	
	bool					hadTo = false;
	CValueNode*				endOffsObj = NULL;
	
	// Start offset:
	CValueNode*	startOffsObj = ParseExpression( parseTree, currFunction, tokenItty, tokens );
	int			lineNum = tokenItty->mLineNum;
	
	// (Optional) end offset:
	if( tokenItty->IsIdentifier( EToIdentifier ) || tokenItty->IsIdentifier( EThroughIdentifier )
		|| tokenItty->IsIdentifier( EThruIdentifier ) )
	{
		CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "to"/"through"/"thru".
		
		endOffsObj = ParseExpression( parseTree, currFunction, tokenItty, tokens );
		hadTo = true;
	}
	
	// Target value:
	if( !tokenItty->IsIdentifier( EOfIdentifier ) )
	{
		std::stringstream		errMsg;
		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected ";
		if( !hadTo )
			errMsg << "\"to\" or ";
		errMsg << "\"of\" here, found " << tokenItty->GetShortDescription() << ".";
		throw std::runtime_error( errMsg.str() );
	}
	CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "of".
	
	CValueNode*	targetValObj = ParseTerm( parseTree, currFunction, tokenItty, tokens );
	
	CFunctionCallNode*	currOperation = new CFunctionCallNode( &parseTree, true, std::string("MakeChunkConst"), lineNum );
	currOperation->AddParam( new CIntValueNode( &parseTree, typeConstant ) );
	currOperation->AddParam( startOffsObj );
	currOperation->AddParam( hadTo ? endOffsObj : startOffsObj );
	currOperation->AddParam( targetValObj );

	return currOperation;
}


CValueNode*	CParser::ParseObjCMethodCall( CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
										std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	// We parse either a class name or an expression that evaluates to an object
	// as type "native object", followed by parameters with labels. We build the
	// method name from that and look up that method in our table of system calls.
	//
	// Then we generate conversion code between the parameter variants and our
	// actual param types, as well as for the return type.
	//
	// Also: We set the flag mUsesObjCCall to true and have code that checks it
	// and includes/links the library for ObjC support elsewhere.
	
//	std::stringstream		targetCode;
//
//	CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip open bracket.
//	
//	mUsesObjCCall = true;
//	
//	if( tokenItty->IsIdentifier(ELastIdentifier_Sentinel) )	// No reserved word identifier?
//	{
//		std::string				className( tokenItty->GetOriginalIdentifierText() );
//		std::string				varName( "var_" );
//		varName.append( ToLowerString( className ) );
//		std::map<std::string,CVariableEntry>::iterator	theContainerItty = theLocals.find( varName );
//
//		if( theContainerItty == theLocals.end() )	// No variable of that name? Must be ObjC class name:
//		{
//			targetCode << className;
//			CToken::GoNextToken( mFileName, tokenItty, tokens );	// Move past target token.
//		}
//		else	// Otherwise get it out of the expression:
//		{
//			std::string		objPrefix, objSuffix, objItselfDummy;
//			GenerateVariantToObjCTypeCode( "id", objPrefix, objSuffix, objItselfDummy );
//			
//			targetCode << objPrefix;
//			ParseExpression( parseTree, targetCode, tokenItty, tokens );
//			targetCode << objSuffix;
//		}
//	}
//	else
//	{
//		std::string		objPrefix, objSuffix, objItselfDummy;
//		GenerateVariantToObjCTypeCode( "id", objPrefix, objSuffix, objItselfDummy );
//		
//		targetCode << objPrefix;
//		ParseExpression( parseTree, targetCode, tokenItty, tokens );
//		targetCode << objSuffix;
//	}
//	
//	if( tokenItty->mType != EIdentifierToken )
//	{
//		std::stringstream		errMsg;
//		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected an identifier as a method name here, found "
//								<< tokenItty->GetShortDescription() << ".";
//		throw std::runtime_error( errMsg.str() );
//	}
//	
//	int						numParams = 0;
//	std::stringstream		methodName;
//	methodName << tokenItty->GetOriginalIdentifierText();
//	
//	CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip method name.
//
//	std::stringstream		paramsCode;	// temp we compose our params in.
//	std::stringstream		currLabel;	// temp we compose our param labels in.
//	std::deque<std::string>	params;		
//	std::deque<std::string>	paramLabels;		
//
//	if( tokenItty->IsIdentifier(EColonOperator) )	// Takes params.
//	{
//		methodName << ":";
//		paramLabels.push_back( methodName.str() );
//		
//		CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip colon.
//		ParseExpression( parseTree, paramsCode, tokenItty, tokens );	// Read 1st param that immediately follows method name.
//		
//		params.push_back( paramsCode.str() );	// Add to params list.
//		paramsCode.str( std::string() );		// Clear temp so we can compose next param.
//		numParams++;
//		
//		while( !tokenItty->IsIdentifier(ECloseSquareBracketOperator) )
//		{
//			if( tokenItty->mType != EIdentifierToken )
//			{
//				std::stringstream		errMsg;
//				errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected an identifier as a parameter label here, found "
//										<< tokenItty->GetShortDescription() << ".";
//				throw std::runtime_error( errMsg.str() );
//			}
//			
//			currLabel << tokenItty->GetOriginalIdentifierText() << ":";
//			paramLabels.push_back( currLabel.str() );
//			methodName << currLabel.str();
//			currLabel.str( std::string() );		// clear label temp again so we can compose next param.
//			CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip identifier label.
//			
//			if( !tokenItty->IsIdentifier( EColonOperator ) )
//			{
//				std::stringstream		errMsg;
//				errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected colon after parameter label here, found "
//										<< tokenItty->GetShortDescription() << ".";
//				throw std::runtime_error( errMsg.str() );
//			}
//			
//			CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip colon after label.
//			
//			ParseExpression( theLocals, paramsCode, tokenItty, tokens );	// Read param value.
//
//			params.push_back( paramsCode.str() );	// Add to params list.
//			paramsCode.str( std::string() );		// Clear temp so we can compose next param.
//			numParams++;
//		}
//	}
//	
//	CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip close bracket (ECloseSquareBracketOperator).
//
//	// Get data types for this method's params and return value:
//	std::map<std::string,CObjCMethodEntry>::iterator	foundTypes = sObjCMethodTable.find( methodName.str() );
//	if( foundTypes == sObjCMethodTable.end() )
//	{
//		std::stringstream		errMsg;
//		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Couldn't find definition of Objective C method "
//								<< methodName.str() << ".";
//		throw std::runtime_error( errMsg.str() );
//	}
//	
//	// Make sure we include the needed headers:
//	std::deque<std::string>::iterator	needItty;
//	bool								haveHeader = false;
//	std::string							hdrName( foundTypes->second.mHeaderName );
//	for( needItty = mHeadersNeeded.begin(); needItty != mHeadersNeeded.end() && !haveHeader; needItty++ )
//		haveHeader = needItty->compare( hdrName ) == 0;
//	if( !haveHeader )
//		mHeadersNeeded.push_back( hdrName );
//	
//	// Make sure we link to the needed frameworks:
//	haveHeader = false;
//	hdrName.assign( foundTypes->second.mFrameworkName );
//	for( needItty = mFrameworksNeeded.begin(); needItty != mFrameworksNeeded.end() && !haveHeader; needItty++ )
//		haveHeader = needItty->compare( hdrName ) == 0;
//	if( !haveHeader )
//		mFrameworksNeeded.push_back( hdrName );
//	
//	// Build an array of the types:
//	std::deque<std::string> typesList;
//	FillArrayWithComponentsSeparatedBy( foundTypes->second.mMethodSignature.c_str(), ',', typesList );
//	
//	// Now generate actual code for a Cocoa method call:
//	std::deque<std::string>::iterator	typeItty = typesList.begin();
//	std::string							retValPrefix, retValSuffix;
//	GenerateObjCTypeToVariantCode( *typeItty, retValPrefix, retValSuffix );
//	theFunctionBody << retValPrefix;
//	typeItty++;
//	theFunctionBody << "[" << targetCode.str();
//	if( numParams == 0 )
//		theFunctionBody << " " << methodName.str();
//	else
//	{
//		std::deque<std::string>::iterator	valItty = params.begin();
//		std::deque<std::string>::iterator	labelItty = paramLabels.begin();
//		
//		for( ; valItty != params.end(); valItty++, labelItty++, typeItty++ )
//		{
//			std::string	paramPrefix, paramSuffix, paramItself( *valItty );
//			GenerateVariantToObjCTypeCode( *typeItty, paramPrefix, paramSuffix, paramItself );
//			theFunctionBody << " " << (*labelItty) << paramPrefix << paramItself << paramSuffix;
//		}
//	}
//	theFunctionBody << "]" << retValSuffix;

	return NULL;
}


void	CParser::GenerateObjCTypeToVariantCode( std::string type, std::string &prefix, std::string &suffix )
{
//	// Find out whether this type is a synonym for another -- in that case look for a mapping for the other:
//	const char*	typeStr = NULL;
//	std::map<std::string,std::string>::iterator foundSyn = sSynonymToTypeTable.find(type);
//	if( foundSyn != sSynonymToTypeTable.end() )
//		typeStr = foundSyn->second.c_str();
//	else
//		typeStr = type.c_str();
//	
//	// Find a mapping entry for this type:
//	int		x;
//	for( x = 0; sObjCToVariantMappings[x].mType[0] != '\0'; x++ )
//	{
//		if( strcmp(sObjCToVariantMappings[x].mType,typeStr) == 0 )
//		{
//			prefix.assign( sObjCToVariantMappings[x].mPrefix );
//			suffix.assign( sObjCToVariantMappings[x].mSuffix );
//			
//			if( sObjCToVariantMappings[x].mUsesObjC )
//				mUsesObjCCall = true;
//			return;
//		}
//	}
//	
//	// None found?
//	if( type.at( type.length()-1 ) == '*' )				// If it's some kind of pointer, return the raw pointer as an opaque "object":
//	{
//		prefix.assign( "CVariant( (void*) " );
//		suffix.assign( ")" );
//	}
//	else if( type.rfind("Ref") == type.length() -3 )	// If it's some kind of opaque reference, return the raw pointer as an opaque "object":
//	{
//		std::cout << ":0: warning: Function receives unknown type \"" << type << "\", treating it as a void*." << std::endl;	// Warn user in case this type wasn't a MacOS-style Ref.
//		prefix.assign( "CVariant( (void*) " );
//		suffix.assign( ")" );
//	}
//	else if( type.rfind("Ptr") == type.length() -3 )	// If it's some kind of data pointer, return the raw pointer as an opaque "object":
//	{
//		std::cout << ":0: warning: Function receives unknown type \"" << type << "\", treating it as a void*." << std::endl;	// Warn user in case this type wasn't a MacOS-style Ptr.
//		prefix.assign( "CVariant( (void*) " );
//		suffix.assign( ")" );
//	}
//	else if( type.rfind("Handle") == type.length() -6 )	// If it's some kind of data handle, return the raw pointer as an opaque "object":
//	{
//		std::cout << ":0: warning: Function receives unknown type \"" << type << "\", treating it as a void*." << std::endl;	// Warn user in case this type wasn't a MacOS-style Handle.
//		prefix.assign( "CVariant( (void*) " );
//		suffix.assign( ")" );
//	}
//	else	// Otherwise, fail:
//	{
//		std::stringstream		errMsg;
//		errMsg << mFileName << ":0: error: Don't know what to do with data of type \""
//								<< type << "\".";
//		throw std::runtime_error( errMsg.str() );
//	}
}


void	CParser::GenerateVariantToObjCTypeCode( std::string type, std::string &prefix, std::string &suffix, std::string& ioValue )
{
//	// Find out whether this type is a synonym for another -- in that case look for a mapping for the other:
//	const char*	typeStr = type.c_str();
//	std::map<std::string,std::string>::iterator foundSyn;
//	while( true )	// Loop until we find the actual base type in case the synonym is for another synonym.
//	{
//		foundSyn = sSynonymToTypeTable.find(typeStr);
//		if( foundSyn == sSynonymToTypeTable.end() )
//			break;
//		typeStr = foundSyn->second.c_str();
//	}
//	
//	int		x;
//	for( x = 0; sVariantToObjCMappings[x].mType[0] != '\0'; x++ )
//	{
//		if( strcmp(sVariantToObjCMappings[x].mType,typeStr) == 0 )
//		{
//			if( foundSyn != sSynonymToTypeTable.end() )	// Had a synonym? Typecast, just in case it wasn't an exact match (like our case of treating enums as ints).
//			{
//				prefix.assign( "(" );
//				prefix.append( type );
//				prefix.append( ") " );
//				prefix.append( sVariantToObjCMappings[x].mPrefix );
//			}
//			else
//				prefix.assign( sVariantToObjCMappings[x].mPrefix );
//			suffix.assign( sVariantToObjCMappings[x].mSuffix );
//			
//			if( sVariantToObjCMappings[x].mUsesObjC )
//				mUsesObjCCall = true;
//			return;
//		}
//	}
//	
//	// None found?
//	
//	// First, try ProcPtr:
//	std::map<std::string,CObjCMethodEntry>::iterator	foundProcPtr;
//	foundProcPtr = sCFunctionPointerTable.find( type );
//	if( foundProcPtr != sCFunctionPointerTable.end() )
//	{
//		const char*	handlerIDPrefix = "CVariant( (void*) ";
//		const char*	handlerIDSuffix = " )";
//		std::string	handlerName( ioValue );
//		if( handlerName.find(handlerIDPrefix) != 0 )
//		{
//			std::stringstream		errMsg;
//			errMsg << mFileName << ":0: error: expected a handler ID here, but what I got can't be made into a \""
//									<< type << "\".";
//			throw std::runtime_error( errMsg.str() );
//		}
//		handlerName.erase(0,strlen(handlerIDPrefix));
//		size_t	expectedPos = handlerName.length() -strlen(handlerIDSuffix);
//		if( handlerName.rfind(handlerIDSuffix) != expectedPos )
//		{
//			std::stringstream		errMsg;
//			errMsg << mFileName << ":0: error: expected a handler ID here, but what I got can't be made into a \""
//									<< type << "\".";
//			throw std::runtime_error( errMsg.str() );
//		}
//		handlerName.erase(expectedPos,strlen(handlerIDSuffix));
//		
//		// Add a trampoline function that does parameter/result conversion to the header:
//		CreateHandlerTrampolineForFunction( handlerName, type, foundProcPtr->second.mMethodSignature.c_str(), mHeaderString, ioValue );
//		
//		prefix.assign("");
//		suffix.assign("");
//	}
//	else if( type.at( type.length()-1 ) == '*' )		// If it's some kind of pointer, store the raw pointer as an opaque "object":
//	{
//		prefix.assign( "((" );
//		prefix.append( type );
//		prefix.append( ")(" );
//		suffix.assign( ").GetAsNativeObject())" );
//	}
//	else if( type.rfind("Ref") == type.length() -3 )	// If it's some kind of opaque reference, return the raw pointer as an opaque "object":
//	{
//		std::cout << ":0: warning: Function returns unknown type \"" << type << "\", treating it as a void*." << std::endl;	// Warn user in case this type wasn't a MacOS-style Ref.
//		prefix.assign( "((" );
//		prefix.append( type );
//		prefix.append( ")(" );
//		suffix.assign( ").GetAsNativeObject())" );
//	}
//	else if( type.rfind("Ptr") == type.length() -3 )	// If it's some kind of data pointer, return the raw pointer as an opaque "object":
//	{
//		std::cout << ":0: warning: Function returns unknown type \"" << type << "\", treating it as a void*." << std::endl;	// Warn user in case this type wasn't a MacOS-style Ptr.
//		prefix.assign( "((" );
//		prefix.append( type );
//		prefix.append( ")(" );
//		suffix.assign( ").GetAsNativeObject())" );
//	}
//	else if( type.rfind("Handle") == type.length() -6 )	// If it's some kind of data handle, return the raw pointer as an opaque "object":
//	{
//		std::cout << ":0: warning: Function returns unknown type \"" << type << "\", treating it as a void*." << std::endl;	// Warn user in case this type wasn't a MacOS-style Handle.
//		prefix.assign( "((" );
//		prefix.append( type );
//		prefix.append( ")(" );
//		suffix.assign( ").GetAsNativeObject())" );
//	}
//	else	// Otherwise, fail:
//	{
//		std::stringstream		errMsg;
//		errMsg << mFileName << ":0: error: Don't know how to convert to data of type \""
//								<< type << "\".";
//		throw std::runtime_error( errMsg.str() );
//	}
}


TChunkType	CParser::GetChunkTypeNameFromIdentifierSubtype( TIdentifierSubtype identifierToCheck )
{
	// Try to find chunk type that matches:
	TChunkType	foundType = TChunkTypeInvalid;
	int			x = 0;
	
	for( x = 0; sChunkTypes[x].mType != ELastIdentifier_Sentinel; x++ )
	{
		if( identifierToCheck == sChunkTypes[x].mType || identifierToCheck == sChunkTypes[x].mPluralType )
			foundType = sChunkTypes[x].mChunkTypeConstant;
	}
	
	return foundType;
}


CValueNode*	CParser::ParseNativeFunctionCallStartingAtParams( std::string& methodName, CObjCMethodEntry& methodInfo,
							CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
							std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
//	int						numParams = 0;
//	std::stringstream		paramsCode;	// temp we compose our params in.
//	std::deque<std::string>	params;		
//
//	while( !tokenItty->IsIdentifier(ECloseBracketOperator) )
//	{
//		ParseExpression( parseTree, paramsCode, tokenItty, tokens );	// Read param value.
//
//		params.push_back( paramsCode.str() );	// Add to params list.
//		paramsCode.str( std::string() );		// Clear temp so we can compose next param.
//		numParams++;
//		
//		if( !tokenItty->IsIdentifier(ECommaOperator) )
//			break;
//		
//		CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip comma.
//	}
//	
//	if( !tokenItty->IsIdentifier(ECloseBracketOperator) )
//	{
//		std::stringstream		errMsg;
//		errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected closing bracket after parameter list, found "
//					<< tokenItty->GetShortDescription() << ".";
//		throw std::runtime_error( errMsg.str() );
//	}
//	
//	CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip close bracket (ECloseSquareBracketOperator).
//
//	// Make sure we include the needed headers:
//	std::deque<std::string>::iterator	needItty;
//	bool								haveHeader = false;
//	std::string							hdrName( methodInfo.mHeaderName );
//	for( needItty = mHeadersNeeded.begin(); needItty != mHeadersNeeded.end() && !haveHeader; needItty++ )
//		haveHeader = needItty->compare( hdrName ) == 0;
//	if( !haveHeader )
//		mHeadersNeeded.push_back( hdrName );
//	
//	// Make sure we link to the needed frameworks:
//	haveHeader = false;
//	hdrName.assign( methodInfo.mFrameworkName );
//	for( needItty = mFrameworksNeeded.begin(); needItty != mFrameworksNeeded.end() && !haveHeader; needItty++ )
//		haveHeader = needItty->compare( hdrName ) == 0;
//	if( !haveHeader )
//		mFrameworksNeeded.push_back( hdrName );
//	
//	// Get data types for this method's params and return value. Build an array of the types:
//	std::deque<std::string> typesList;
//	
//	FillArrayWithComponentsSeparatedBy( methodInfo.mMethodSignature.c_str(), ',', typesList );
//	
//	// Now generate actual code for a native function call:
//	std::deque<std::string>::iterator	typeItty = typesList.begin();
//	std::string							retValPrefix, retValSuffix;
//	GenerateObjCTypeToVariantCode( *typeItty, retValPrefix, retValSuffix );
//	theFunctionBody << retValPrefix;
//	typeItty++;
//	theFunctionBody << methodName << "( ";
//	std::deque<std::string>::iterator	valItty = params.begin();
//	bool								isFirst = true;
//	
//	for( ; valItty != params.end(); valItty++, typeItty++ )
//	{
//		if( isFirst )
//			isFirst = false;
//		else
//			theFunctionBody << ", ";
//		
//		std::string	paramPrefix, paramSuffix, paramItself(*valItty);
//		GenerateVariantToObjCTypeCode( *typeItty, paramPrefix, paramSuffix, paramItself );
//		theFunctionBody << paramPrefix << paramItself << paramSuffix;
//	}
//	theFunctionBody << " )" << retValSuffix;
	
	return NULL;
}


CValueNode*	CParser::ParseTerm( CParseTree& parseTree, CCodeBlockNodeBase* currFunction,
								std::deque<CToken>::iterator& tokenItty, std::deque<CToken>& tokens )
{
	CValueNode*	theTerm = NULL;
	
	switch( tokenItty->mType )
	{
		case EStringToken:
		{
			theTerm = new CStringValueNode( &parseTree, tokenItty->mStringValue );
			CToken::GoNextToken( mFileName, tokenItty, tokens );
			break;
		}

		case ENumberToken:	// Any number (integer). We fake floats by parsing an integer/period-operator/integer sequence.
		{
			int					theNumber = tokenItty->mNumberValue;
			
			CToken::GoNextToken( mFileName, tokenItty, tokens );
			
			if( tokenItty->IsIdentifier( EPeriodOperator ) )	// Integer followed by period? Could be a float!
			{
				CToken::GoNextToken( mFileName, tokenItty, tokens );
				if( tokenItty->mType == ENumberToken )	// Is a float!
				{
					std::stringstream	numStr;
					numStr << theNumber << "." << tokenItty->mNumberValue;
					char*				endPtr = NULL;
					double				theNum = strtod( numStr.str().c_str(), &endPtr );
					
					theTerm = new CFloatValueNode( &parseTree, theNum );
					
					CToken::GoNextToken( mFileName, tokenItty, tokens );
				}
				else	// Backtrack, that period was something else:
				{
					CToken::GoPrevToken( mFileName, tokenItty, tokens );
					theTerm = new CIntValueNode( &parseTree, theNumber );
				}
			}
			else
				theTerm = new CIntValueNode( &parseTree, theNumber );
			break;
		}

		case EIdentifierToken:	// Function call?
			if( tokenItty->mSubType == ELastIdentifier_Sentinel )	// Any user-defined identifier.
			{
				std::string	handlerName( tokenItty->GetIdentifierText() );
				std::string	realHandlerName( tokenItty->GetOriginalIdentifierText() );
				size_t		callLineNum = tokenItty->mLineNum;
				
				CToken::GoNextToken( mFileName, tokenItty, tokens );
				
				if( tokenItty->IsIdentifier(EOpenBracketOperator) )	// Yes! Function call!
				{
					CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip opening bracket.
					
					std::map<std::string,CObjCMethodEntry>::iterator funcItty = sCFunctionTable.find( realHandlerName );
					if( funcItty == sCFunctionTable.end() )	// No native function of that name? Call function handler:
					{
						CFunctionCallNode*	fcall = new CFunctionCallNode( false, &parseTree, handlerName, callLineNum );
						theTerm = fcall;
						ParseParamList( ECloseBracketOperator, parseTree, currFunction, tokenItty, tokens, fcall );
						
						CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip closing bracket.
					}
					else	// Native call!
						theTerm = ParseNativeFunctionCallStartingAtParams( realHandlerName, funcItty->second, parseTree, currFunction, tokenItty, tokens );
				}
				else	// No. 
				{
					std::map<std::string,int>::iterator		sysConstItty = sConstantToValueTable.find( realHandlerName );
					if( sysConstItty == sConstantToValueTable.end() )	// Not a system constant either? Guess it was a variable name:
					{
						CToken::GoPrevToken( mFileName, tokenItty, tokens );	// Rewind past token that wasn't a bracket.
						
						theTerm = ParseContainer( false, true, parseTree, currFunction, tokenItty, tokens );
					}
					else
						theTerm = new CIntValueNode( &parseTree, sysConstItty->second );
				}
				
				break;	// Exit our switch.
			}
			else if( tokenItty->mSubType == EEndIdentifier )
			{
				return NULL;
			}
			else if( tokenItty->mSubType == EEntryIdentifier )
			{	
				theTerm = ParseArrayItem( parseTree, currFunction, tokenItty, tokens );
				break;
			}
			else if( tokenItty->mSubType == EIdIdentifier )	// "id"?
			{
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "id".
				
				// OF:
				if( !tokenItty->IsIdentifier(EOfIdentifier) )
				{
					std::stringstream		errMsg;
					errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"of\" here, found "
											<< tokenItty->GetShortDescription() << ".";
					throw std::runtime_error( errMsg.str() );
				}
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "of".
				
				std::string		hdlName;
				if( tokenItty->IsIdentifier(EFunctionIdentifier) )
				{
					hdlName.assign("fun_");
					CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "function".
					if( tokenItty->IsIdentifier(EHandlerIdentifier) )
						CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "handler".
				}
				else if( tokenItty->IsIdentifier(EMessageIdentifier) )
				{
					hdlName.assign("hdl_");
					CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "message".
					if( !tokenItty->IsIdentifier(EHandlerIdentifier) )
					{
						std::stringstream		errMsg;
						errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"function handler\" or \"message handler\" here, found "
												<< tokenItty->GetShortDescription() << ".";
						throw std::runtime_error( errMsg.str() );
					}
					CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "handler".
				}
				else
				{
					hdlName.assign("hdl_");
					if( !tokenItty->IsIdentifier(EHandlerIdentifier) )
					{
						std::stringstream		errMsg;
						errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"function handler\" or \"message handler\" here, found "
												<< tokenItty->GetShortDescription() << ".";
						throw std::runtime_error( errMsg.str() );
					}
					CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "handler".
				}
				
				hdlName.append( tokenItty->GetIdentifierText() );
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "handler".
				
				// Now that we know whether it's a function or a handler, store a pointer to it:
				theTerm = new CFunctionCallNode( false, &parseTree, "vcy_fcn_addr", tokenItty->mLineNum );
				break;
			}
			else if( tokenItty->mSubType == ENumberIdentifier || tokenItty->mSubType == ENumIdentifier )		// The identifier "number", i.e. the actual word.
			{
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "number".
				
				// OF:
				if( !tokenItty->IsIdentifier(EOfIdentifier) )
				{
					std::stringstream		errMsg;
					errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"of\" here, found "
											<< tokenItty->GetShortDescription() << ".";
					throw std::runtime_error( errMsg.str() );
				}
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "of".
				
				// Chunk type:
				TChunkType	typeConstant = GetChunkTypeNameFromIdentifierSubtype( tokenItty->GetIdentifierSubType() );
				if( typeConstant == TChunkTypeInvalid )
				{
					std::stringstream		errMsg;
					errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected a chunk type like \"character\", \"item\", \"word\" or \"line\" here, found "
											<< tokenItty->GetShortDescription() << ".";
					throw std::runtime_error( errMsg.str() );
				}
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "items" etc.
				
				// OF:
				if( !tokenItty->IsIdentifier(EOfIdentifier) )
				{
					std::stringstream		errMsg;
					errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected \"of\" here, found "
											<< tokenItty->GetShortDescription() << ".";
					throw std::runtime_error( errMsg.str() );
				}
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "of".
				
				// VALUE:
				CFunctionCallNode*	fcall = new CFunctionCallNode( false, &parseTree, "vcy_chunk_count", tokenItty->mLineNum );
				CValueNode*			valueObj = ParseTerm( parseTree, currFunction, tokenItty, tokens );
				
				fcall->AddParam( new CIntValueNode( &parseTree, typeConstant ) );
				fcall->AddParam( valueObj );
				
				theTerm = fcall;
				break;
			}
			else if( tokenItty->mSubType == EOpenBracketOperator )
			{
				CToken::GoNextToken( mFileName, tokenItty, tokens );
				
				theTerm = ParseExpression( parseTree, currFunction, tokenItty, tokens );
				
				if( !tokenItty->IsIdentifier(ECloseBracketOperator) )
				{
					std::stringstream		errMsg;
					errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected closing bracket here, found "
											<< tokenItty->GetShortDescription() << ".";
					throw std::runtime_error( errMsg.str() );
				}
				CToken::GoNextToken( mFileName, tokenItty, tokens );
				break;
			}
			else if( tokenItty->mSubType == ETheIdentifier )
			{
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "the".
				if( tokenItty->IsIdentifier( EParamCountIdentifier ) )
				{
					CLocalVariableRefValueNode*	paramsNode = new CLocalVariableRefValueNode( &parseTree, currFunction, "paramList", "paramList" );
					CFunctionCallNode*			countFunction = new CFunctionCallNode( false, &parseTree, "vcy_list_count", tokenItty->mLineNum );
					countFunction->AddParam( paramsNode );
					theTerm = countFunction;
					
					CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "paramCount".
				}
				else if( tokenItty->IsIdentifier( ELongIdentifier ) || tokenItty->IsIdentifier( EShortIdentifier )
						|| tokenItty->IsIdentifier( EAbbrIdentifier ) || tokenItty->IsIdentifier( EAbbrevIdentifier )
						|| tokenItty->IsIdentifier( EAbbreviatedIdentifier ) )
				{
					std::string			paramListTemp( CVariableEntry::GetNewTempName() );
					CreateVariable( paramListTemp, paramListTemp, false, currFunction );
					std::string			lengthQualifier( tokenItty->GetIdentifierText() );
					
					CFunctionCallNode*	makeListCall = new CFunctionCallNode( &parseTree, true, "vcy_list_assign_items", tokenItty->mLineNum );
					makeListCall->AddParam( new CLocalVariableRefValueNode( &parseTree, currFunction, paramListTemp, paramListTemp ) );
					makeListCall->AddParam( new CIntValueNode(&parseTree, 1) );
					makeListCall->AddParam( new CStringValueNode( &parseTree, lengthQualifier ) );
					
					CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip long|short|abbreviated identifier.
					std::stringstream	funName;
					funName << "fun_" << tokenItty->GetIdentifierText();
					
					CFunctionCallNode*	theFuncCall = new CFunctionCallNode( false, &parseTree, funName.str(), tokenItty->mLineNum );
					theFuncCall->AddParam( makeListCall );
					
					CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip function name identifier.
					
					theTerm = theFuncCall;
				}
				else
				{
					CToken::GoPrevToken( mFileName, tokenItty, tokens );	// Backtrack so ParseContainer sees "the", too.
					theTerm = ParseContainer( false, true, parseTree, currFunction, tokenItty, tokens );
				}
				break;
			}
			else if( tokenItty->mSubType == EParamCountIdentifier )
			{
				bool		hadBrackets = false;
				size_t		lineNum = tokenItty->mLineNum;
				
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "paramCount".
				
				if( tokenItty->IsIdentifier( EOpenBracketOperator ) )
				{
					CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip opening bracket.
					if( tokenItty->IsIdentifier( ECloseBracketOperator ) )
					{
						CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip closing bracket.
						hadBrackets = true;
					}
				}
					
				if( !hadBrackets )
				{
					std::stringstream		errMsg;
					errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: expected \"(\" and \")\" after function name, found "
											<< tokenItty->GetShortDescription() << ".";
					throw std::runtime_error( errMsg.str() );
				}
				
				CLocalVariableRefValueNode*	paramsNode = new CLocalVariableRefValueNode( &parseTree, currFunction, "paramList", "paramList" );
				CFunctionCallNode*			countFunction = new CFunctionCallNode( false, &parseTree, "vcy_list_count", lineNum );
				countFunction->AddParam( paramsNode );
				theTerm = countFunction;
				break;
			}
			else if( tokenItty->mSubType == EParamIdentifier )
			{
				size_t		lineNum = tokenItty->mLineNum;
				
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "param".
				
				if( !tokenItty->IsIdentifier( EOpenBracketOperator ) )	// Parse open bracket.
				{
					std::stringstream		errMsg;
					errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: excpected \"(\" after function name, found "
											<< tokenItty->GetShortDescription() << ".";
					throw std::runtime_error( errMsg.str() );
				}
				
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip opening bracket.
				
				CLocalVariableRefValueNode*	paramListVar = new CLocalVariableRefValueNode( &parseTree, currFunction, "paramList", "paramList" );
				CFunctionCallNode*			fcall = new CFunctionCallNode( false, &parseTree, "vcy_list_get", lineNum );
				
				fcall->AddParam( paramListVar );
				fcall->AddParam( ParseExpression( parseTree, currFunction, tokenItty, tokens ) );
				
				if( !tokenItty->IsIdentifier( ECloseBracketOperator ) )	// Parse close bracket.
				{
					std::stringstream		errMsg;
					errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: excpected \"(\" after function name, found "
											<< tokenItty->GetShortDescription() << ".";
					throw std::runtime_error( errMsg.str() );
				}
				
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip closing bracket.
				
				theTerm = fcall;
				break;
			}
			else if( tokenItty->mSubType == EParameterIdentifier )
			{
				size_t		lineNum = tokenItty->mLineNum;
				
				CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip "parameter".
				
				CLocalVariableRefValueNode*	paramListVar = new CLocalVariableRefValueNode( &parseTree, currFunction, "paramList", "paramList" );
				CFunctionCallNode*			fcall = new CFunctionCallNode( false, &parseTree, "vcy_list_get", lineNum );
				
				fcall->AddParam( paramListVar );
				fcall->AddParam( ParseExpression( parseTree, currFunction, tokenItty, tokens ) );

				theTerm = fcall;
				break;
			}
			else if( tokenItty->mSubType == EResultIdentifier || tokenItty->mSubType == ETheIdentifier )
			{	
				theTerm = ParseContainer( false, true, parseTree, currFunction, tokenItty, tokens );
				break;
			}
			else if( tokenItty->mSubType == EOpenSquareBracketOperator )
			{	
				theTerm = ParseObjCMethodCall( parseTree, currFunction, tokenItty, tokens );
				break;
			}
			else
			{
				// Try to find chunk type that matches:
				TChunkType typeConstant = GetChunkTypeNameFromIdentifierSubtype( tokenItty->mSubType );
				if( typeConstant != TChunkTypeInvalid )
				{
					theTerm = ParseConstantChunkExpression( typeConstant, parseTree, currFunction, tokenItty, tokens );
					break;
				}

				// Now try constant:
				CValueNode*	constantValue = NULL;
				int			x = 0;
				
				for( x = 0; sConstants[x].mType != ELastIdentifier_Sentinel; x++ )
				{
					if( tokenItty->mSubType == sConstants[x].mType )
					{
						constantValue = sConstants[x].mValue;
						break;
					}
				}
				
				if( constantValue )	// Found constant of that name!
				{
					theTerm = constantValue->Copy();
					CToken::GoNextToken( mFileName, tokenItty, tokens );
					break;
				}

				// Try to find unary operator that matches:
				const char*	operatorCommandName = NULL;
				
				for( x = 0; sUnaryOperators[x].mType != ELastIdentifier_Sentinel; x++ )
				{
					if( tokenItty->mSubType == sUnaryOperators[x].mType )
						operatorCommandName = sUnaryOperators[x].mOperatorCommandName;
				}
				
				if( operatorCommandName != NULL )
				{
					size_t	lineNum = tokenItty->mLineNum;
					CToken::GoNextToken( mFileName, tokenItty, tokens );	// Skip operator token.
					
					CFunctionCallNode*	opFCall = new CFunctionCallNode( false, &parseTree, operatorCommandName, lineNum );
					opFCall->AddParam( ParseTerm( parseTree, currFunction, tokenItty, tokens ) );
					theTerm = opFCall;
					break;
				}
			}
			// else fall through to error.
		
		default:
		{
			std::stringstream		errMsg;
			errMsg << mFileName << ":" << tokenItty->mLineNum << ": error: Expected a term here, found "
									<< tokenItty->GetShortDescription() << ".";
			throw std::runtime_error( errMsg.str() );
			break;
		}
	}
	
	return theTerm;
}


// Take a list in a string delimited by a single character (e.g. comma) and fill
//	a deque with the various items:
void	CParser::FillArrayWithComponentsSeparatedBy( const char* typesStr, char delimiter, std::deque<std::string> &destTypesList )
{
	std::string				tempType;
	int						x = 0;
	
	for( x = 0; typesStr[x] != 0; x++ )
	{
		if( typesStr[x] == delimiter )
		{
			destTypesList.push_back(tempType);
			tempType.clear();
		}
		else
			tempType.append(1,typesStr[x]);
	}
	
	if( tempType.length() > 0 )
	{
		destTypesList.push_back(tempType);
		tempType.clear();
	}
}


// Create a "trampoline" function that can be handed to a system API as a callback
//	and calls a particular handler with the converted parameters:
void	CParser::CreateHandlerTrampolineForFunction( const std::string &handlerName, const std::string& procPtrName,
														const char* typesStr,
														std::stringstream& theCode, std::string &outTrampolineName )
{
	// Build an array of the types:
	std::deque<std::string> typesList;
	
	FillArrayWithComponentsSeparatedBy( typesStr, ',', typesList );
	
	outTrampolineName.assign("Trampoline_");
	outTrampolineName.append(procPtrName);
	outTrampolineName.append("_");
	outTrampolineName.append(handlerName);
	
	// Generate method name and param signature:
	theCode << "#ifndef GUARD_" << outTrampolineName << std::endl;
	theCode << "#define GUARD_" << outTrampolineName << "\t1" << std::endl;
	theCode << "const CVariant\t" << handlerName << "( CVariant& paramList );" << std::endl;
	theCode << typesList[0] << "\t" << outTrampolineName << "( ";
	std::deque<std::string>::iterator itty = typesList.begin();
	int			x = 1;
	bool		isFirst = true;
	itty++;
	while( itty != typesList.end() )
	{
		if( isFirst )
			isFirst = false;
		else
			theCode << ", ";
		theCode << *itty << " param" << x++;
		
		itty++;
	}
	theCode << " )" << std::endl << "{" << std::endl;
	theCode << "\tCVariant	temp1( TVariantTypeList );" << std::endl;
	
	// Generate translation code that calls our handler:
	itty = typesList.begin();
	
	theCode << "\t";
	if( !itty->compare( "void" ) == 0 )
		theCode << "CVariant\tresult = ";
	theCode << handlerName << "( temp1.MakeList()";
	
		// Do each param:
	itty++;
	isFirst = true;
	x = 1;
	while( itty != typesList.end() )
	{
		std::string		parPrefix, parSuffix;
		theCode << ".Append( ";
		
		GenerateObjCTypeToVariantCode( *itty, parPrefix, parSuffix );
		theCode << parPrefix << "param" << x++ << parSuffix << " )";
		
		itty++;
	}

	theCode << " );" << std::endl;
	
	// Return value:
	if( typesList[0].compare("void") != 0 )
	{
		std::string		resultPrefix, resultSuffix, resultItself("result");
		theCode << "\treturn ";
		GenerateVariantToObjCTypeCode( typesList[0], resultPrefix, resultSuffix, resultItself );
		theCode << resultPrefix << resultItself << resultSuffix << ";" << std::endl;
	}
	
	theCode << "}" << std::endl
			<< "#endif /*GUARD_" << outTrampolineName << "*/" << std::endl; 
}

} // Namespace Carlson.
