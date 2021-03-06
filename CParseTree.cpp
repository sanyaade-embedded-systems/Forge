/*
 *  CParseTree.cpp
 *  HyperCompiler
 *
 *  Created by Uli Kusterer on 10.05.07.
 *  Copyright 2007 M. Uli Kusterer. All rights reserved.
 *
 */

#include "CParseTree.h"

namespace Carlson
{

CParseTree::~CParseTree()
{
	std::deque<CNode*>::iterator itty;
	
	for( itty = mNodes.begin(); itty != mNodes.end(); itty++ )
	{
		delete *itty;
		*itty = NULL;
	}
}


void	CParseTree::Simplify()
{
	std::deque<CNode*>::iterator itty;
	
	for( itty = mNodes.begin(); itty != mNodes.end(); itty++ )
	{
		(*itty)->Simplify();
	}
}


void	CParseTree::GenerateCode( CCodeBlock* inCodeBlock )
{
	std::deque<CNode*>::iterator itty;
	
	for( itty = mNodes.begin(); itty != mNodes.end(); itty++ )
	{
		(*itty)->GenerateCode( inCodeBlock );
	}
}


void	CParseTree::DebugPrint( std::ostream& destStream, size_t indentLevel )
{
	INDENT_PREPARE(indentLevel);
	
	std::deque<CNode*>::iterator itty;
	
	for( itty = mNodes.begin(); itty != mNodes.end(); itty++ )
	{
		(*itty)->DebugPrint( destStream, indentLevel );
	}
}


} // namespace Carlson
