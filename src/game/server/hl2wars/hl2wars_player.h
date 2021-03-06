//====== Copyright � 2007-2012 Sandern Corporation, All rights reserved. ======//
//
// Purpose:		Player for HL2Wars Game
//
// $NoKeywords: $
//=============================================================================//

#ifndef HL2WARS_PLAYER_H
#define HL2WARS_PLAYER_H
#pragma once

//#include "basemultiplayerplayer.h"
#include "player.h"
#include "server_class.h"

#include "hl2wars_player_shared.h"

class CUnitBase;

//=============================================================================
// >> HL2Wars Game player
//=============================================================================
//class CHL2WarsPlayer : public CBaseMultiplayerPlayer
class CHL2WarsPlayer : public CBasePlayer
{
public:
	//DECLARE_CLASS( CHL2WarsPlayer, CBaseMultiplayerPlayer );
	DECLARE_CLASS( CHL2WarsPlayer, CBasePlayer );
	DECLARE_SERVERCLASS();
	DECLARE_PREDICTABLE();
	DECLARE_DATADESC();
	DECLARE_PYSERVERCLASS( CHL2WarsPlayer );

	CHL2WarsPlayer();
	~CHL2WarsPlayer();

	static CHL2WarsPlayer *			CreatePlayer( const char *className, edict_t *ed );

	virtual void					UpdateOnRemove();
	virtual void					Precache();
	virtual void					Spawn();
	virtual int						UpdateTransmitState( void );

	virtual bool					ClientCommand( const CCommand &args );

	virtual void					PreThink( void );

	virtual Vector					EyePosition(void);
	virtual void					CreateViewModel( int viewmodelindex = 0 );

	virtual void					SetAnimation( PLAYER_ANIM playerAnim ) {} // eat

	inline int						GetHudHiddenBits() { return m_Local.m_iHideHUD; }						
	inline void						AddHudHiddenBits( int bits ) { m_Local.m_iHideHUD |= bits; } 
	inline void						RemoveHudHiddenBits( int bits ) { m_Local.m_iHideHUD &= ~bits; } 

	void							SetStrategicMode( bool state );

	virtual const char *			GetDefaultFaction( void ) { return "rebels"; }

	// Shared functions
	//virtual const Vector		GetPlayerMins( void ) const;
	//virtual const Vector		GetPlayerMaxs( void ) const;

	bool							IsStrategicModeOn() const { return GetMoveType() == MOVETYPE_STRATEGIC; }
	const Vector &					GetMouseAim() { return m_vMouseAim; }
	const MouseTraceData_t &		GetMouseData() const { return m_MouseData; }
	virtual void					UpdateMouseData( Vector &vMouseAim );
	virtual void					UpdateButtonState( int nUserCmdButtonMask );

	const MouseTraceData_t &		GetMouseDataLeftPressed() const { return m_MouseDataLeftPressed; }
	const MouseTraceData_t &		GetMouseDataLeftDoublePressed() const { return m_MouseDataLeftDoublePressed; }
	const MouseTraceData_t &		GetMouseDataLeftReleased() const { return m_MouseDataLeftReleased; }
	const MouseTraceData_t &		GetMouseDataRightPressed() const { return m_MouseDataRightPressed; }
	const MouseTraceData_t &		GetMouseDataRightDoublePressed() const { return m_MouseDataRightDoublePressed; }
	const MouseTraceData_t &		GetMouseDataRightReleased() const { return m_MouseDataRightReleased; }

	bool							IsLeftPressed();
	bool							IsLeftDoublePressed();
	bool							WasLeftDoublePressed();
	bool							IsRightPressed();
	bool							IsRightDoublePressed();
	bool							WasRightDoublePressed();

	void							ClearMouse();
	void							SetMouseCapture( CBaseEntity *pEntity );
	CBaseEntity *					GetMouseCapture();

	inline const char *				GetFaction();
	virtual void					ChangeFaction( const char *faction );

	// Camera settings
	virtual void					SnapCameraTo( const Vector &vPos ); // Positions camera correctly, uses SetAbsOrigin
	void							SetCameraOffset( Vector &offs );
	const Vector &					GetCameraOffset() const;
	void							UpdateCameraSettings();
	float							GetCamSpeed() { return m_fCamSpeed; }
	float							GetCamAcceleration() { return m_fCamAcceleration; }
	float							GetCamStopSpeed() { return m_fCamStopSpeed; }
	float							GetCamFriction() { return m_fCamFriction; }

	virtual void					CalculateHeight( const Vector &vPosition );
	float							GetCamHeight() { return m_fCurHeight; }
	float							GetCamMaxHeight() { return m_fMaxHeight; }
	const Vector &					GetCamGroundPos() { return m_vCamGroundPos; }

	// selection management
	void							UpdateSelection( void );
#ifndef DISABLE_PYTHON
	boost::python::list				GetSelection( int rangemin = 0, int rangemax = -1 );
#endif // DISABLE_PYTHON
	CBaseEntity*					GetUnit( int idx );
	void							AddUnit( CBaseEntity *pUnit, bool bTriggerOnSel=true );
	int								FindUnit( CBaseEntity *pUnit );
	void							RemoveUnit( int idx, bool bTriggerOnSel=true );
	void							RemoveUnit( CBaseEntity *pUnit, bool bTriggerOnSel=true );
	int								CountUnits();
	void							ClearSelection( bool bTriggerOnSel=true );
	void							OnSelectionChanged();
	void							OrderUnits();
	void							ScheduleSelectionChangedSignal();

	// Group management
	void							ClearGroup( int group);
	void							AddToGroup( int group, CBaseEntity *pUnit );
	void							MakeCurrentSelectionGroup( int group, bool bClearGroup );
	void							SelectGroup( int group );
	int								GetGroupNumber( CBaseEntity *pUnit );
	int								CountGroup( int group );
	CBaseEntity *					GetGroupUnit( int group, int index );
	void							CleanupGroups( void );
	const CUtlVector< EHANDLE > &	GetGroup( int group );

#ifndef DISABLE_PYTHON
	// Abilities
	void							AddActiveAbility( bp::object ability );
	void							RemoveActiveAbility( bp::object ability );
	bool							IsActiveAbility( bp::object ability );
	void							ClearActiveAbilities();
	void							SetSingleActiveAbility( bp::object ability );
	bp::object						GetSingleActiveAbility();
#endif // DISABLE_PYTHON

	void							SetControlledUnit( CBaseEntity *pUnit );
	CBaseEntity *					GetControlledUnit();

	// Mouse button checking
	void							UpdateControl( void );
	virtual void					OnLeftMouseButtonPressed( const MouseTraceData_t &data );
	virtual void					OnLeftMouseButtonDoublePressed( const MouseTraceData_t &data );
	virtual void					OnLeftMouseButtonReleased( const MouseTraceData_t &data );
	virtual void					OnRightMouseButtonPressed( const MouseTraceData_t &data );
	virtual void					OnRightMouseButtonDoublePressed( const MouseTraceData_t &data );
	virtual void					OnRightMouseButtonReleased( const MouseTraceData_t &data );	

	// Team Handling
	//virtual void			ChangeTeam( int iTeamNum ) { ChangeTeam(iTeamNum,false, false); }
	//virtual void			ChangeTeam( int iTeamNum, bool bAutoTeam, bool bSilent );

	virtual bool			ModeWantsSpectatorGUI( int iMode ) { return false; }

	// Owner number
	virtual void			OnChangeOwnerNumber( int old_owner_number );

	// 
	virtual void			VPhysicsShadowUpdate( IPhysicsObject *pPhysics );

private:
    void					OnLeftMouseButtonPressedInternal( const MouseTraceData_t &data );
	void					OnLeftMouseButtonDoublePressedInternal( const MouseTraceData_t &data );
	void					OnLeftMouseButtonReleasedInternal( const MouseTraceData_t &data );
	void					OnRightMouseButtonPressedInternal( const MouseTraceData_t &data );
	void					OnRightMouseButtonDoublePressedInternal( const MouseTraceData_t &data );
	void					OnRightMouseButtonReleasedInternal( const MouseTraceData_t &data );	

private:
	// Mouse data
	CNetworkVector( m_vMouseAim );
	MouseTraceData_t	m_MouseData;
	Vector				m_vCameraOffset;
	int					m_nMouseButtonsPressed;

	MouseTraceData_t	m_MouseDataLeftPressed;
	MouseTraceData_t	m_MouseDataLeftReleased;
	MouseTraceData_t	m_MouseDataRightPressed;
	MouseTraceData_t	m_MouseDataRightReleased;
	MouseTraceData_t	m_MouseDataLeftDoublePressed;
	MouseTraceData_t	m_MouseDataRightDoublePressed;
	bool				m_bIsLeftPressed;
	bool				m_bIsLeftDoublePressed;
	bool				m_bWasLeftDoublePressed;
	bool				m_bIsRightPressed;
	bool				m_bIsRightDoublePressed;
	bool				m_bWasRightDoublePressed;
	bool				m_bIsMouseCleared;

	EHANDLE				m_hMouseCaptureEntity;

	// Player data
	string_t						m_FactionName;
	CNetworkString(	m_NetworkedFactionName, MAX_PATH );

	// Camera setttings
	float m_fCamSpeed;
	float m_fCamAcceleration;
	float m_fCamStopSpeed;
	float m_fCamFriction;
	float m_fCurHeight, m_fMaxHeight;
	Vector m_vCamGroundPos;

	// Selection data
	CUtlVector< EHANDLE >		m_hSelectedUnits;
	bool				m_bSelectionChangedSignalScheduled;

	// Group data
	unitgroup_t m_Groups[PLAYER_MAX_GROUPS];
	float m_fLastSelectGroupTime;
	int m_iLastSelectedGroup;

	// Ability
#ifndef DISABLE_PYTHON
	CUtlVector<bp::object> m_vecActiveAbilities;
#endif // DISABLE_PYTHON
	CNetworkHandle( CBaseEntity, m_hControlledUnit );	
};

// Inlines
inline bool CHL2WarsPlayer::IsLeftPressed() 
{ 
	return m_bIsLeftPressed; 
}

inline bool CHL2WarsPlayer::IsLeftDoublePressed() 
{ 
	return m_bIsLeftDoublePressed; 
}

inline bool CHL2WarsPlayer::WasLeftDoublePressed() 
{ 
	return m_bWasLeftDoublePressed; 
}

inline bool CHL2WarsPlayer::IsRightPressed()
{ 
	return m_bIsRightPressed; 
}

inline bool CHL2WarsPlayer::IsRightDoublePressed() 
{ 
	return m_bIsRightDoublePressed; 
}

inline bool CHL2WarsPlayer::WasRightDoublePressed() 
{ 
	return m_bWasRightDoublePressed; 
}

inline const char *	CHL2WarsPlayer::GetFaction()
{
	return STRING(m_FactionName);
}

inline CHL2WarsPlayer *ToHL2WarsPlayer( CBaseEntity *pEntity )
{
	if ( !pEntity || !pEntity->IsPlayer() )
		return NULL;

#ifdef _DEBUG
	Assert( dynamic_cast<CHL2WarsPlayer*>( pEntity ) != 0 );
#endif
	return static_cast< CHL2WarsPlayer* >( pEntity );
}

#endif	// HL2WARS_PLAYER_H
