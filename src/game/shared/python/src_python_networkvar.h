//====== Copyright � 2007-2012 Sandern Corporation, All rights reserved. ======//
//
// Purpose: 
//
//=============================================================================//

#ifndef SRC_PYTHON_NETWORKVAR_H
#define SRC_PYTHON_NETWORKVAR_H

#ifdef _WIN32
#pragma once
#endif

#include <boost/python.hpp>

namespace bp = boost::python;

#ifndef CLIENT_DLL
#define PYNETVAR_MAX_NAME 260
class CPythonNetworkVarBase
{
public:
	CPythonNetworkVarBase( bp::object ent, const char *name, bool changedcallback=false );
	~CPythonNetworkVarBase();

	void NetworkStateChanged( void );
	virtual void NetworkVarsUpdateClient( CBaseEntity *pEnt, int iClient );

public:
	// This bit vector contains the players who don't have the most up to date data
	CBitVec<ABSOLUTE_PLAYER_LIMIT> m_PlayerUpdateBits;

protected:
	char m_Name[PYNETVAR_MAX_NAME];
	bool m_bChangedCallback;

	bp::object m_wrefEnt;

};

class CPythonNetworkVar : CPythonNetworkVarBase
{
public:
	CPythonNetworkVar( bp::object self, const char *name, bp::object data = bp::object(), 
		bool initstatechanged=false, bool changedcallback=false );

	void NetworkVarsUpdateClient( CBaseEntity *pEnt, int iClient );

	void Set( bp::object data );
	bp::object Get( void );

private:
	bp::object m_dataInternal;
};

class CPythonNetworkArray : CPythonNetworkVarBase
{
public:
	CPythonNetworkArray( bp::object self, const char *name, bp::list data = bp::list(), 
		bool initstatechanged=false, bool changedcallback=false );

	void NetworkVarsUpdateClient( CBaseEntity *pEnt, int iClient );

	void SetItem( int idx, bp::object data );
	bp::object GetItem( int idx );

	void Set( bp::list data );

private:
	bp::list m_dataInternal;
};

class CPythonNetworkDict : CPythonNetworkVarBase
{
public:
	CPythonNetworkDict( bp::object self, const char *name, bp::dict data = bp::dict(), 
		bool initstatechanged=false, bool changedcallback=false );

	void NetworkVarsUpdateClient( CBaseEntity *pEnt, int iClient );

	void SetItem( bp::object key, bp::object data );
	bp::object GetItem(  bp::object key );

	void Set( bp::dict data );

private:
	bp::dict m_dataInternal;
};

void PyNetworkVarsUpdateClient( CBaseEntity *pEnt, int iEdict );

#else
	void HookPyNetworkVar();
#endif // CLIENT_DLL

#endif // SRC_PYTHON_NETWORKVAR_H