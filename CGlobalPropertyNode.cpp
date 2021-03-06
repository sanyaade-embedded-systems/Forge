/*
 *  CGlobalPropertyNode.cpp
 *  HyperCompiler
 *
 *  Created by Uli Kusterer on 12.05.07.
 *  Copyright 2007 M. Uli Kusterer. All rights reserved.
 *
 */

#include "CGlobalPropertyNode.h"
#include "CParseTree.h"
#include "CCodeBlock.h"
#include "LEOInstructions.h"


namespace Carlson
{

void	CGlobalPropertyNode::DebugPrint( std::ostream& destStream, size_t indentLevel )
{
	INDENT_PREPARE(indentLevel);
	
	destStream << indentChars << "Global Property \"" << gInstructionNames[mGetterInstructionID] << "\"" << std::endl
				<< indentChars << "{" << std::endl;
	
	std::vector<CValueNode*>::iterator itty;
	
	for( itty = mParams.begin(); itty != mParams.end(); itty++ )
	{
		(*itty)->DebugPrint( destStream, indentLevel +1 );
	}
	
	destStream << indentChars << "}" << std::endl;
}


void	CGlobalPropertyNode::AddParam( CValueNode* val )
{
	mParams.push_back( val );
	mParseTree->NodeWasAdded(val);
}


void	CGlobalPropertyNode::Simplify()
{
	std::vector<CValueNode*>::iterator itty;
	
	for( itty = mParams.begin(); itty != mParams.end(); itty++ )
		(*itty)->Simplify();
}


void	CGlobalPropertyNode::GenerateCode( CCodeBlock* inCodeBlock )
{
	std::vector<CValueNode*>::iterator itty;
	
	// Push all params on stack:
	for( itty = mParams.begin(); itty != mParams.end(); itty++ )
		(*itty)->GenerateCode( inCodeBlock );
	
	inCodeBlock->GenerateOperatorInstruction( mGetterInstructionID );
}


void	CGlobalPropertyNode::GenerateSetterCode( CCodeBlock* inCodeBlock, CValueNode* newValueNode )
{
	std::vector<CValueNode*>::iterator itty;
	
	// Push all params on stack:
	for( itty = mParams.begin(); itty != mParams.end(); itty++ )
		(*itty)->GenerateCode( inCodeBlock );
	
	newValueNode->GenerateCode( inCodeBlock );
	
	inCodeBlock->GenerateOperatorInstruction( mSetterInstructionID );
}

} // namespace Carlson
