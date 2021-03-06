//====== Copyright � 2007-2012 Sandern Corporation, All rights reserved. ======//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "c_hl2wars_player.h"

#include "engine/IVDebugOverlay.h"

#include "iinput.h"
#include "in_buttons.h"
#include "filesystem.h"

#include "gamestringpool.h"
#include "unit_base_shared.h"
#include "hl2wars_util_shared.h"
#include "wars_mapboundary.h"
#include "hl2wars_in_main.h"

#ifndef DISABLE_PYTHON
	#include "src_python.h"
#endif // DISABLE_PYTHON

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#if defined( CHL2WarsPlayer )
	#undef CHL2WarsPlayer
#endif

extern void Cmd_CAM_ToFirstPerson(void);
extern void Cmd_CAM_ToThirdPerson(void);

// Some move settings
extern ConVar cl_strategic_cam_scrolltimeout;
extern ConVar cl_strategic_cam_scrollspeed;
extern ConVar cl_strategic_cam_speedscale;

ConVar cl_strategic_directmovetimeout( "cl_strategic_directmovetimeout", "0.25", FCVAR_ARCHIVE, "The amount of time direct move to a position keeps going on" );

// CSDKPlayerShared Data Tables
//=============================
BEGIN_RECV_TABLE_NOBASE( C_HL2WarsPlayer, DT_HL2WarsLocalPlayerExclusive )
	RecvPropString(  RECVINFO( m_NetworkedFactionName ) ),
	RecvPropEHandle		( RECVINFO( m_hControlledUnit ) ),
	RecvPropVector( RECVINFO_NAME( m_vecNetworkOrigin, m_vecOrigin ) ),
END_RECV_TABLE()

BEGIN_RECV_TABLE_NOBASE( C_HL2WarsPlayer, DT_HL2WarsNonLocalPlayerExclusive )
	RecvPropVector( RECVINFO( m_vMouseAim ) ),
	RecvPropVector( RECVINFO_NAME( m_vecNetworkOrigin, m_vecOrigin ) ),
END_RECV_TABLE()

// main table
IMPLEMENT_CLIENTCLASS_DT( C_HL2WarsPlayer, DT_HL2WarsPlayer, CHL2WarsPlayer )
	RecvPropDataTable( "hl2warslocaldata", 0, 0, &REFERENCE_RECV_TABLE(DT_HL2WarsLocalPlayerExclusive) ),
	RecvPropDataTable( "hl2warsnonlocaldata", 0, 0, &REFERENCE_RECV_TABLE(DT_HL2WarsNonLocalPlayerExclusive) ),
END_RECV_TABLE()

// ------------------------------------------------------------------------------------------ //
// Prediction tables.
// ------------------------------------------------------------------------------------------ //
BEGIN_PREDICTION_DATA( C_HL2WarsPlayer )
END_PREDICTION_DATA()

// This class is exposed in python and networkable
IMPLEMENT_PYCLIENTCLASS( C_HL2WarsPlayer, PN_HL2WARSPLAYER )

LINK_ENTITY_TO_CLASS( player, C_HL2WarsPlayer );



C_HL2WarsPlayer::C_HL2WarsPlayer() : m_bOldIsStrategicModeOn(false)
{
	m_NetworkedFactionName[0] = 0;
	m_fCurHeight = -1;
}


C_HL2WarsPlayer::~C_HL2WarsPlayer()
{

}

C_HL2WarsPlayer* C_HL2WarsPlayer::GetLocalHL2WarsPlayer(int nSlot)
{
	return ToHL2WarsPlayer( C_BasePlayer::GetLocalPlayer(nSlot) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::Spawn()
{
	BaseClass::Spawn( );

	// Hook spawn to a signal
#ifndef DISABLE_PYTHON
	// Setup dict for sending a signal
	bp::dict kwargs;
	kwargs["sender"] = bp::object();
	kwargs["client"] = GetPyHandle();
	bp::object signal = SrcPySystem()->Get( "clientspawned", "core.signals", true );
	SrcPySystem()->CallSignal( signal, kwargs );
#endif // DISABLE_PYTHON
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::ClientThink( void )
{
	BaseClass::PreThink();

	UpdateSelection();
	CleanupGroups();

	// Follow entity if set
	if( m_hCamFollowEntity )
		SnapCameraTo( m_hCamFollowEntity->GetAbsOrigin() );
	else if( m_CamFollowEntities.Count() )
		SnapCameraTo( CamCalculateGroupOrigin() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool C_HL2WarsPlayer::ShouldRegenerateOriginFromCellBits() const
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::ThirdPersonSwitch( bool bThirdperson )
{
	BaseClass::ThirdPersonSwitch(bThirdperson);

	if( GetControlledUnit() )
	{
		GetControlledUnit()->UpdateVisibility();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Input handling
//-----------------------------------------------------------------------------
bool C_HL2WarsPlayer::CreateMove( float flInputSampleTime, CUserCmd *pCmd )
{
	// Don't really know a good place for this, so will put it here.
	// In sp no prediction is run, so update the buttons here
	if( gpGlobals->maxClients == 1 )
	{
		UpdateButtonState( pCmd->buttons );
	}

#ifndef DISABLE_PYTHON
	// If we have an active ability, it overrides our mouse actions
	CUtlVector<bp::object> activeAbilities;
	activeAbilities = m_vecActiveAbilities;
	for(int i=0; i< activeAbilities.Count(); i++)
	{
		SrcPySystem()->Run( SrcPySystem()->Get("_update", activeAbilities[i]) );	
	}
#endif // DISABLE_PYTHON

	// Calculate the camera offset
	if( input->CAM_IsThirdPerson() )
	{
		Vector offs, dir;
		input->CAM_GetCameraOffset(offs);
		QAngle viewangle(offs[0], offs[1], 0.0f);
		AngleVectors( viewangle, &dir );
		pCmd->m_vCameraOffset = -dir*offs[2];
	}
	else
	{
		pCmd->m_vCameraOffset = vec3_origin;
	}
	SetCameraOffset( pCmd->m_vCameraOffset );

	if( IsStrategicModeOn() )
	{
		// If we pressed the movement keys and are following entities: release the cam
		if( pCmd->forwardmove != 0 || pCmd->sidemove != 0 )
			CamFollowRelease();

		// Directly move to a position. To be used with the minimap
		if( m_bDirectMoveActive )
		{	
			pCmd->vecmovetoposition = m_vDirectMove;
			pCmd->directmove = true;
			if( m_fDirectMoveTimeOut < gpGlobals->curtime )
				StopDirectMove();
			if( m_bDisableDirectMove )
			{
				m_bDisableDirectMove = false;
				m_bDirectMoveActive = false;
			}
		}
	}

	// Add mouse aim here, so we know for sure it is add even when not active
	// This way we can spawn entities at the right spot when typing in the console
	// Otherwise the mouse aim is zero'ed out on the server
	pCmd->m_vMouseAim = GetMouseAim();

	return BaseClass::CreateMove( flInputSampleTime, pCmd );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::PostDataUpdate( DataUpdateType_t updateType )
{
	BaseClass::PostDataUpdate(updateType);

	// Calculate mouse data for non local players
	if( IsLocalPlayer() == false )
	{
		UpdateMouseData( m_vMouseAim );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : updateType - 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::OnDataChanged( DataUpdateType_t updateType )
{
	if ( updateType == DATA_UPDATE_CREATED )
	{
		// We want to think every frame.
		SetNextClientThink( CLIENT_THINK_ALWAYS );
	}

	if( m_bOldIsStrategicModeOn != IsStrategicModeOn() ) {
		m_bOldIsStrategicModeOn = IsStrategicModeOn();

		if( IsStrategicModeOn () ) {
			Cmd_CAM_ToThirdPerson();
			//input->CAM_ToThirdPerson();
			//ThirdPersonSwitch(true);
		}
		else {
			Cmd_CAM_ToFirstPerson();
			SetForceShowMouse( false ); // reset for next time
		}
	}
	if( m_hOldControlledUnit != m_hControlledUnit )
	{
#ifndef DISABLE_PYTHON
		// Setup dict for sending a signal
		bp::dict kwargs;
		kwargs["sender"] = bp::object();
		kwargs["player"] = GetPyHandle();
#endif // DISABLE_PYTHON

		if( m_hOldControlledUnit )
		{
			m_hOldControlledUnit->GetIUnit()->OnUserLeftControl( this );
			m_hOldControlledUnit->UpdateVisibility();
			Cmd_CAM_ToThirdPerson();

			// Hide viewmodels
			for ( int i = 0 ; i < MAX_VIEWMODELS; i++ )
			{
				CBaseViewModel *vm = GetViewModel( i );
				if ( !vm )
					continue;

				vm->SetWeaponModel( NULL, NULL );
			}

#ifndef DISABLE_PYTHON
			kwargs["unit"] = m_hOldControlledUnit->GetPyHandle();
			bp::object signal = SrcPySystem()->Get( "playerleftcontrolunit", "core.signals", true );
			SrcPySystem()->CallSignal( signal, kwargs );
#endif // DISABLE_PYTHON
		}
		if( m_hControlledUnit )
		{
			m_hControlledUnit->GetIUnit()->OnUserControl( this );
			Cmd_CAM_ToThirdPerson();

			CUnitBase *pUnit = m_hControlledUnit->MyUnitPointer();
			if( pUnit->GetActiveWeapon() )
			{
				//pUnit->GetActiveWeapon()->SetViewModel();
				pUnit->GetActiveWeapon()->Deploy();
			}

#ifndef DISABLE_PYTHON
			kwargs["unit"] = m_hControlledUnit->GetPyHandle();
			bp::object signal = SrcPySystem()->Get( "playercontrolunit", "core.signals", true );
			SrcPySystem()->CallSignal( signal, kwargs );
#endif // DISABLE_PYTHON
		}
		m_hOldControlledUnit = m_hControlledUnit;
	}

	// Check if the player's faction changed ( Might want to add a string table )
	if( m_FactionName == NULL_STRING || Q_strncmp( STRING(m_FactionName), m_NetworkedFactionName, MAX_PATH ) != 0 ) 
	{
		ChangeFaction( m_NetworkedFactionName );
	}

	BaseClass::OnDataChanged( updateType );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::UpdateOnRemove( void )
{
	ClearSelection(); // Ensure selection changed signal is send
	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::SetScrollTimeOut(bool forward)
{
	/*
	m_bScrollForward = forward;
	m_fScrollTimeOut = gpGlobals->curtime + cl_strategic_cam_scrolltimeout.GetFloat();
	m_bScrolling = true;
*/
	CHL2WarsInput *pWarsInput = dynamic_cast<CHL2WarsInput *>( input );
	if( pWarsInput )
		pWarsInput->SetScrollTimeOut( forward );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::SetDirectMove( const Vector &vPos )
{
	m_bDirectMoveActive = true;
	m_vDirectMove = vPos;
	m_fDirectMoveTimeOut = gpGlobals->curtime + cl_strategic_directmovetimeout.GetFloat();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::StopDirectMove()
{
	m_bDisableDirectMove = true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::CamFollowEntity( CBaseEntity *pEnt )
{
	if( !pEnt )
	{
		CamFollowRelease();
		return;
	}

	m_CamFollowEntities.RemoveAll();
	m_hCamFollowEntity = pEnt;
	SnapCameraTo( m_hCamFollowEntity->GetAbsOrigin() );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::CamFollowGroup( const CUtlVector< EHANDLE > &m_Entities )
{
	if( m_Entities.Count() == 0 )
	{
		CamFollowRelease();
		return;
	}

	m_hCamFollowEntity = NULL;
	m_CamFollowEntities = m_Entities;
	SnapCameraTo( CamCalculateGroupOrigin() );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::CamFollowRelease()
{
	m_hCamFollowEntity = NULL;
	m_CamFollowEntities.RemoveAll();
	m_bDirectMoveActive = false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
Vector C_HL2WarsPlayer::CamCalculateGroupOrigin()
{
	Vector vOrigin = vec3_origin;

	for( int i = m_CamFollowEntities.Count()-1; i >= 0; i-- )
	{
		if( m_CamFollowEntities[i] == NULL )
		{
			m_CamFollowEntities.Remove( i );
			continue;
		}

		vOrigin += m_CamFollowEntities[i]->GetAbsOrigin();
	}
	vOrigin /= m_CamFollowEntities.Count();
	return vOrigin;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_BaseCombatWeapon *C_HL2WarsPlayer::GetActiveWeapon( void ) const
{
	const C_UnitBase *pUnit = dynamic_cast<C_UnitBase *>(m_hControlledUnit.Get());
	if ( !pUnit )
	{
		return BaseClass::GetActiveWeapon();
	}

	return pUnit->GetActiveWeapon();
}

C_BaseCombatWeapon *C_HL2WarsPlayer::GetWeapon( int i ) const
{
	const C_UnitBase *pUnit = dynamic_cast<C_UnitBase *>(m_hControlledUnit.Get());
	if ( !pUnit )
	{
		return BaseClass::GetWeapon( i );
	}

	return pUnit->GetWeapon( i );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::GetBoxSelection( int iXMin, int iYMin, int iXMax, int iYMax,  CUtlVector< EHANDLE > &selection )
{
	UnitListInfo *pUnitList;
	CUnitBase *pUnit;
	int iX, iY, i;
	bool bInScreen;
#ifndef DISABLE_PYTHON
	boost::python::list pytargetselection;
#endif // DISABLE_PYTHON
	CUtlVector< EHANDLE > targetselection;

	// See which units we will select
	pUnitList = GetUnitListForOwnernumber(GetOwnerNumber());
	if( !pUnitList )
		return;
	for( pUnit=pUnitList->m_pHead; pUnit; pUnit=pUnit->GetNext() )
	{
		if( !pUnit->IsAlive() )
			continue;
		bInScreen = GetVectorInScreenSpace(pUnit->GetAbsOrigin(), iX, iY);
		if( bInScreen && iX >= iXMin && iY >= iYMin && iX <= iXMax && iY <= iYMax )
			targetselection.AddToTail(pUnit);
	}

	// For each unit see if it wants to be selected in this group
#ifndef DISABLE_PYTHON
	pytargetselection = UtlVectorToListByValue< EHANDLE >(targetselection);
#endif // DISABLE_PYTHON
	for(i=0; i<targetselection.Count(); i++)
	{
#ifndef DISABLE_PYTHON
		if( targetselection[i]->GetIUnit()->IsSelectableByPlayer(this, pytargetselection) )
#endif // DISABLE_PYTHON
		{
			selection.AddToTail(targetselection[i]);
		}
	}

}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::SelectBox( int iXMin, int iYMin, int iXMax, int iYMax )
{
	int i;

	//Msg("Select box xmin: %d, ymin: %d, xmax: %d, ymax: %d\n", iXMin, iYMin, iXMax, iYMax);
	if( !g_pUnitListHead )
	{
		ClearSelection();
		engine->ServerCmd("player_clearselection");
		return;
	}

	CUtlVector< EHANDLE > finalselection;
	GetBoxSelection( iXMin, iYMin, iXMax, iYMax, finalselection ); 

	if( cl_selection_noclear.GetBool() && finalselection.Count() == 0 )
	{
		return;
	}

	if( (m_nButtons & IN_SPEED) == 0 ) {
		ClearSelection( false ); // Do not trigger on selection changed, since we do that below too already.
		engine->ServerCmd("player_clearselection");
	}



	// Make selection
	for(i=0; i<finalselection.Count(); i++)
	{
		finalselection[i]->GetIUnit()->Select(this, false);
		engine->ServerCmd( VarArgs("player_addunit %ld", EncodeEntity(finalselection[i].Get())) );
	}
	
	ScheduleSelectionChangedSignal();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::SelectAllUnitsOfTypeInScreen(const char *pUnitType)
{
	UnitListInfo *pUnitList;
	CUnitBase *pUnit;
	int iX, iY;
	bool bInScreen;

	if( !g_pUnitListHead )
		return;

	pUnitList = GetUnitListForOwnernumber(GetOwnerNumber());
	if( !pUnitList )
	{
		ClearSelection();
		engine->ServerCmd("player_clearselection");
		return;
	}

	ClearSelection( false ); // Do not trigger on selection changed, since we do that below too already.
	engine->ServerCmd("player_clearselection");

	for( pUnit=pUnitList->m_pHead; pUnit; pUnit=pUnit->GetNext() )
	{
		if( !pUnit->IsAlive() || Q_stricmp( pUnitType, pUnit->GetUnitType() ) )
			continue;

		bInScreen = GetVectorInScreenSpace(pUnit->GetLocalOrigin(), iX, iY);
		if( !bInScreen || iX < 0 || iX >= ScreenWidth() || iY < 0 || iY >= ScreenHeight() ) {
			continue;
		}

		AddUnit(pUnit, false);
		engine->ServerCmd( VarArgs("player_addunit %ld", EncodeEntity(pUnit)) );
	}

	ScheduleSelectionChangedSignal();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::SimulateOrderUnits( Vector &vStart, Vector &vEnd, CBaseEntity *pHitEnt )
{
	MouseTraceData_t mousedata;
	mousedata.m_vStartPos = vStart;
	mousedata.m_vEndPos = vEnd;
	mousedata.m_vWorldOnlyEndPos = vEnd;
	mousedata.m_hEnt = pHitEnt;

	m_MouseData = mousedata;
	m_MouseDataRightPressed = mousedata;
	m_MouseDataRightReleased = mousedata;
			
	CBaseEntity *pUnit;
	for( int i = 0; i < CountUnits(); i++ )
	{
		pUnit = GetUnit(i);
		if( !pUnit )
			continue;
		pUnit->GetIUnit()->Order(this);
	}

	engine->ServerCmd( VarArgs("player_orderunits %f %f %f %f %f %f %lu", vStart.x, vStart.y, vStart.z, vEnd.x, vEnd.y, vEnd.z, pHitEnt ? pHitEnt->index : -1) );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2WarsPlayer::MinimapClick( Vector &vStart, Vector &vEnd, CBaseEntity *pHitEnt )
{
	MouseTraceData_t mousedata;
	mousedata.m_vStartPos = vStart;
	mousedata.m_vEndPos = vEnd;
	mousedata.m_vWorldOnlyEndPos = vEnd;
	mousedata.m_hEnt = pHitEnt;

	m_MouseData = mousedata;
	m_MouseDataRightPressed = mousedata;
	m_MouseDataRightReleased = mousedata;
	m_MouseDataLeftPressed = mousedata;
	m_MouseDataLeftReleased = mousedata;

	CUtlVector<bp::object> activeAbilities;
	activeAbilities = m_vecActiveAbilities;
	for(int i=0; i< activeAbilities.Count(); i++)
	{
		SrcPySystem()->RunT<bool, MouseTraceData_t>( SrcPySystem()->Get("OnMinimapClick", activeAbilities[i]), false, mousedata );
	}

	engine->ServerCmd( VarArgs("minimap_lm %f %f %f %f %f %f %lu", vStart.x, vStart.y, vStart.z, vEnd.x, vEnd.y, vEnd.z, pHitEnt ? pHitEnt->index : -1) );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *C_HL2WarsPlayer::GetSelectedUnitType( void )
{
	return STRING(m_pSelectedUnitType);
}

void C_HL2WarsPlayer::SetSelectedUnitType(const char *pUnitType)
{
	m_pSelectedUnitType = AllocPooledString(pUnitType);
	UpdateSelectedUnitType();
}

void C_HL2WarsPlayer::GetSelectedUnitTypeRange(int &iMin, int &iMax)
{
	iMin = m_iSelectedUnitTypeMin;
	iMax = m_iSelectedUnitTypeMax;
}

void C_HL2WarsPlayer::UpdateSelectedUnitType( void )
{
	// reset
	m_iSelectedUnitTypeMin = -1;
	m_iSelectedUnitTypeMax = -1;

	if( !m_pSelectedUnitType )
		return;

	int i;
	CBaseEntity *pUnit;
	for(i=0; i<CountUnits(); i++)
	{
		 pUnit = GetUnit(i);
		 if( !pUnit )
			 continue;

		if( !Q_stricmp( m_pSelectedUnitType, pUnit->GetIUnit()->GetUnitType() ) )
		{
			if( m_iSelectedUnitTypeMin == -1 )
			{
				m_iSelectedUnitTypeMin = i;
				m_iSelectedUnitTypeMax = i+1;
			}
			else
			{
				m_iSelectedUnitTypeMax = i+1;
			}
		}
		else
		{
			if( m_iSelectedUnitTypeMax != -1 )
				break; // Done, since the selection array is ordered on unittype
		}
	}

	if( m_iSelectedUnitTypeMin == -1 )
		m_pSelectedUnitType = NULL;
}
