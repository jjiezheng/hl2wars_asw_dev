//====== Copyright � 2007-2012 Sandern Corporation, All rights reserved. ======//
//
// Purpose: Any entity that implements this interface is controlable by the player
//
// $NoKeywords: $
//=============================================================================//

#ifndef IUNIT_H
#define IUNIT_H

#ifdef _WIN32
#pragma once
#endif

#include "imouse.h"

#ifndef DISABLE_PYTHON
#include <boost/python.hpp>
#endif // DISABLE_PYTHON

class CHL2WarsPlayer;

abstract_class IUnit : public IMouse
{
public:
	virtual IUnit *GetIUnit() { return this; }

	// Type
	virtual const char *GetUnitType()														= 0;
#ifndef CLIENT_DLL
	virtual void		SetUnitType( const char *unit_type )								= 0;
#endif 

	// Selection
#ifndef DISABLE_PYTHON
	virtual bool IsSelectableByPlayer( CHL2WarsPlayer *pPlayer, boost::python::object targetselection = boost::python::object() ) = 0;
#endif // DISABLE_PYTHON
	virtual void Select( CHL2WarsPlayer *pPlayer, bool bTriggerOnSel = true )				= 0;
	virtual void OnSelected( CHL2WarsPlayer *pPlayer )										= 0;
	virtual void OnDeSelected( CHL2WarsPlayer *pPlayer )									= 0;
#ifdef CLIENT_DLL
	virtual void OnInSelectionBox( void )													= 0;
	virtual void OnOutSelectionBox( void )													= 0;
#endif // CLIENT_DLL
	virtual int GetSelectionPriority( void )												= 0;
	virtual int GetAttackPriority( void )													= 0;

	// Action
	virtual void Order( CHL2WarsPlayer *pPlayer )											= 0;

	// Squads
	virtual  CBaseEntity *GetSquad()														= 0;
#ifndef CLIENT_DLL
	virtual void SetSquad( CBaseEntity *pUnit )												= 0;
#endif // CLIENT_DLL

	// Unit is player controlled and player wants to move
	// Translate the usercmd into the unit movement
	virtual void UserCmd( CUserCmd *pCmd )													= 0;
	virtual void OnUserControl( CHL2WarsPlayer *pPlayer )									= 0;
	virtual void OnUserLeftControl( CHL2WarsPlayer *pPlayer )								= 0;
	virtual bool CanUserControl( CHL2WarsPlayer *pPlayer )									= 0;
	virtual void OnButtonsChanged( int buttonsMask, int buttonsChanged )					= 0;

#ifdef CLIENT_DLL
	virtual bool SelectSlot( int slot )														= 0;
#else
	virtual bool ClientCommand( const CCommand &args )										= 0;
#endif // CLIENT_DLL

#ifndef CLIENT_DLL
	virtual void SetCommander(CHL2WarsPlayer *player)										= 0;
#endif // CLIENT_DLL
	virtual CHL2WarsPlayer* GetCommander() const											= 0;
};

#endif // IUNIT_H
