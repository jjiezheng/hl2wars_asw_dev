//====== Copyright � 2007-2012 Sandern Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef SRC_PYTHON_MATCHMAKING_H
#define SRC_PYTHON_MATCHMAKING_H
#ifdef _WIN32
#pragma once
#endif

class KeyValues;

// Entry point to create session
void PyMKCreateSession( KeyValues *pSettings );

// Entry point to match into a session
void PyMKMatchSession( KeyValues *pSettings );

class PyMatchSession
{
public:
	static KeyValues * GetSessionSystemData();
	static KeyValues * GetSessionSettings();
	static void UpdateSessionSettings( KeyValues *pSettings );
	static void Command( KeyValues *pCommand );
};

#endif // SRC_PYTHON_MATCHMAKING_H
