//====== Copyright � 2007-2012 Sandern Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "c_unit_base.h"
#include "unit_base_shared.h"
#include "gamestringpool.h"
#include "model_types.h"
#include "cdll_bounded_cvars.h"

#include <vgui_controls/Controls.h>
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>

#include "c_hl2wars_player.h"
#include "hl2wars_util_shared.h"
#include "iinput.h"

#include "unit_baseanimstate.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar	cl_wars_smooth		( "cl_wars_smooth", "1", 0, "Smooth unit's render origin after prediction errors" );
static ConVar	cl_wars_smoothtime	( 
	"cl_wars_smoothtime", 
	"0.1", 
	0, 
	"Smooth unit's render origin after prediction error over this many seconds",
	true, 0.01,	// min/max is 0.01/2.0
	true, 2.0
	 );

//-----------------------------------------------------------------------------
// Purpose: Recv proxies
//-----------------------------------------------------------------------------
void RecvProxy_Unit_LocalVelocityX( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	CUnitBase *pUnit = (CUnitBase *) pStruct;

	Assert( pUnit );

	float flNewVel_x = pData->m_Value.m_Float;
	
	Vector vecVelocity = pUnit->GetLocalVelocity();	

	if( vecVelocity.x != flNewVel_x )	// Should this use an epsilon check?
	{
		//if (vecVelocity.x > 30.0f)
		//{
			
		//}		
		vecVelocity.x = flNewVel_x;
		pUnit->SetLocalVelocity( vecVelocity );
	}
}

void RecvProxy_Unit_LocalVelocityY( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	CUnitBase *pUnit = (CUnitBase *) pStruct;

	Assert( pUnit );

	float flNewVel_y = pData->m_Value.m_Float;

	Vector vecVelocity = pUnit->GetLocalVelocity();

	if( vecVelocity.y != flNewVel_y )
	{
		vecVelocity.y = flNewVel_y;
		pUnit->SetLocalVelocity( vecVelocity );
	}
}

void RecvProxy_Unit_LocalVelocityZ( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	CUnitBase *pUnit = (CUnitBase *) pStruct;
	
	Assert( pUnit );

	float flNewVel_z = pData->m_Value.m_Float;

	Vector vecVelocity = pUnit->GetLocalVelocity();

	if( vecVelocity.z != flNewVel_z )
	{
		vecVelocity.z = flNewVel_z;
		pUnit->SetLocalVelocity( vecVelocity );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Recv tables
//-----------------------------------------------------------------------------
void RecvProxy_LocalVelocityX( const CRecvProxyData *pData, void *pStruct, void *pOut );
void RecvProxy_LocalVelocityY( const CRecvProxyData *pData, void *pStruct, void *pOut );
void RecvProxy_LocalVelocityZ( const CRecvProxyData *pData, void *pStruct, void *pOut );

BEGIN_RECV_TABLE_NOBASE( CUnitBase, DT_CommanderExclusive )
	// Hi res origin and angle
	RecvPropVector( RECVINFO_NAME( m_vecNetworkOrigin, m_vecOrigin ) ),
	RecvPropFloat( RECVINFO_NAME( m_angNetworkAngles[0], m_angRotation[0] ) ),
	RecvPropFloat( RECVINFO_NAME( m_angNetworkAngles[1], m_angRotation[1] ) ),
	RecvPropFloat( RECVINFO_NAME( m_angNetworkAngles[2], m_angRotation[2] ) ),

	// Only received by the commander
	RecvPropEHandle		( RECVINFO( m_hGroundEntity ) ),
	RecvPropVector		( RECVINFO( m_vecBaseVelocity ) ),
	RecvPropFloat		( RECVINFO(m_vecVelocity[0]), 0, RecvProxy_Unit_LocalVelocityX ),
	RecvPropFloat		( RECVINFO(m_vecVelocity[1]), 0, RecvProxy_Unit_LocalVelocityY ),
	RecvPropFloat		( RECVINFO(m_vecVelocity[2]), 0, RecvProxy_Unit_LocalVelocityZ ),
	RecvPropFloat		( RECVINFO(m_vecViewOffset[0]) ),
	RecvPropFloat		( RECVINFO(m_vecViewOffset[1]) ),
	RecvPropFloat		( RECVINFO(m_vecViewOffset[2]) ),
END_RECV_TABLE()

BEGIN_RECV_TABLE_NOBASE( CUnitBase, DT_NormalExclusive )
	RecvPropVectorXY( RECVINFO_NAME( m_vecNetworkOrigin, m_vecOrigin ), 0, C_BaseEntity::RecvProxy_CellOriginXY ),
	RecvPropFloat( RECVINFO_NAME( m_vecNetworkOrigin[2], m_vecOrigin[2] ), 0, C_BaseEntity::RecvProxy_CellOriginZ ),

	RecvPropFloat( RECVINFO_NAME( m_angNetworkAngles[0], m_angRotation[0] ) ),
	RecvPropFloat( RECVINFO_NAME( m_angNetworkAngles[1], m_angRotation[1] ) ),
	RecvPropFloat( RECVINFO_NAME( m_angNetworkAngles[2], m_angRotation[2] ) ),
END_RECV_TABLE()

IMPLEMENT_NETWORKCLASS_ALIASED( UnitBase, DT_UnitBase )

BEGIN_NETWORK_TABLE( CUnitBase, DT_UnitBase )
	RecvPropString(  RECVINFO( m_NetworkedUnitType ) ),

	RecvPropInt		(RECVINFO(m_iHealth)),
	RecvPropInt		(RECVINFO(m_iMaxHealth)),
	RecvPropInt		(RECVINFO(m_fFlags)),
	RecvPropInt		(RECVINFO( m_takedamage)),
	RecvPropInt		(RECVINFO( m_lifeState)),

	RecvPropEHandle		( RECVINFO( m_hSquadUnit ) ),
	RecvPropEHandle		( RECVINFO( m_hCommander ) ),
	RecvPropEHandle		( RECVINFO( m_hEnemy ) ),

	RecvPropBool( RECVINFO( m_bCrouching ) ),
	RecvPropBool( RECVINFO( m_bClimbing ) ),

	RecvPropInt		(RECVINFO(m_iEnergy)),
	RecvPropInt		(RECVINFO(m_iMaxEnergy)),

	RecvPropDataTable( "commanderdata", 0, 0, &REFERENCE_RECV_TABLE(DT_CommanderExclusive) ),
	RecvPropDataTable( "normaldata", 0, 0, &REFERENCE_RECV_TABLE(DT_NormalExclusive) ),
END_RECV_TABLE()


BEGIN_PREDICTION_DATA( CUnitBase )
	//DEFINE_PRED_FIELD( m_vecVelocity, FIELD_FLOAT, FTYPEDESC_OVERRIDE | FTYPEDESC_PRIVATE | FTYPEDESC_NOERRORCHECK ),
	DEFINE_PRED_FIELD( m_flCycle, FIELD_FLOAT, FTYPEDESC_OVERRIDE | FTYPEDESC_PRIVATE | FTYPEDESC_NOERRORCHECK ),
	//DEFINE_PRED_FIELD( m_flAnimTime, FIELD_FLOAT, FTYPEDESC_OVERRIDE | FTYPEDESC_PRIVATE | FTYPEDESC_NOERRORCHECK ),	
	DEFINE_PRED_FIELD( m_nSequence, FIELD_INTEGER, FTYPEDESC_OVERRIDE | FTYPEDESC_PRIVATE | FTYPEDESC_NOERRORCHECK ),	
	DEFINE_PRED_FIELD( m_nNewSequenceParity, FIELD_INTEGER, FTYPEDESC_OVERRIDE | FTYPEDESC_PRIVATE | FTYPEDESC_NOERRORCHECK ),
	DEFINE_PRED_FIELD( m_nResetEventsParity, FIELD_INTEGER, FTYPEDESC_OVERRIDE | FTYPEDESC_PRIVATE | FTYPEDESC_NOERRORCHECK ),
	DEFINE_PRED_FIELD( m_hGroundEntity, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD_TOL( m_vecBaseVelocity, FIELD_VECTOR, FTYPEDESC_INSENDTABLE, 0.05 ),
END_PREDICTION_DATA()

//-----------------------------------------------------------------------------
// Purpose: Default on hover paints the health bar
//-----------------------------------------------------------------------------
void CUnitBase::OnHoverPaint()
{
	//DrawHealthBar( this );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : updateType - 
//-----------------------------------------------------------------------------
void CUnitBase::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	// Check if the player's faction changed ( Might want to add a string table )
	if( m_UnitType == NULL_STRING || Q_strncmp( STRING(m_UnitType), m_NetworkedUnitType, MAX_PATH ) != 0 ) 
	{
		const char *pOldUnitType = STRING(m_UnitType);
		m_UnitType = AllocPooledString(m_NetworkedUnitType);
		OnUnitTypeChanged(pOldUnitType);
	}

	// Check change commander
	if( m_hOldCommander != m_hCommander )
	{
		UpdateVisibility();
		m_hOldCommander = m_hCommander;
	}

	// Check change active weapon
	if( m_hOldActiveWeapon != GetActiveWeapon() )
	{
		OnActiveWeaponChanged();
		m_hOldActiveWeapon = GetActiveWeapon();
	}
}

int CUnitBase::DrawModel( int flags, const RenderableInstance_t &instance )
{
	if( m_bIsBlinking )
	{
		flags |= STUDIO_ITEM_BLINK;
		if( m_fBlinkTimeOut < gpGlobals->curtime )
			m_bIsBlinking = false;
	}

	return BaseClass::DrawModel( flags, instance );
}

#if 0
bool CUnitBase::SetupBones( matrix3x4a_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime )
{
	// Skip if not in screen. SetupBones is really expensive and we shouldn't need it if not in screen.
	// Note: Don't do this if pBoneToWorldOut is not NULL, since some functions like ragdolls expect output.
	C_HL2WarsPlayer *pPlayer = C_HL2WarsPlayer::GetLocalHL2WarsPlayer();
	if ( !pBoneToWorldOut && pPlayer && !pPlayer->GetControlledUnit() )
	{
		int iX, iY;
		bool bResult = GetVectorInScreenSpace( GetAbsOrigin(), iX, iY );
		if ( !bResult || iX < -320 || iX > ScreenWidth()+320 || iY < -320 || iY > ScreenHeight() + 320 )
			return false;
	}
	return BaseClass::SetupBones( pBoneToWorldOut, nMaxBones, boneMask, currentTime );
}
#endif // 0

void CUnitBase::Blink( float blink_time )
{
	m_bIsBlinking = true;
	m_fBlinkTimeOut = gpGlobals->curtime + blink_time;
}

bool CUnitBase::ShouldDraw( void )
{
	if( GetCommander() && GetCommander() == C_HL2WarsPlayer::GetLocalHL2WarsPlayer() )
	{
		if( !input->CAM_IsThirdPerson() )
			return false;
	}
	return BaseClass::ShouldDraw();
}

void CUnitBase::UpdateClientSideAnimation()
{
	// Yaw and Pitch are updated in UserCmd if the unit has a commander
	if( !GetCommander() )
	{
		if( GetActiveWeapon() )
		{
			AimGun();
		}
		else
		{
			m_fEyePitch = EyeAngles()[PITCH];
			m_fEyeYaw = EyeAngles()[YAW];
		}
	}

	if( GetSequence() != -1 )
		FrameAdvance(gpGlobals->frametime);

	if( m_pAnimState )
	{
		m_pAnimState->Update(m_fEyeYaw, m_fEyePitch);
	}

	if( GetSequence() != -1 )
		OnLatchInterpolatedVariables((1<<0));
}

void CUnitBase::InitPredictable( C_BasePlayer *pOwner )
{
	SetLocalVelocity(vec3_origin);
	BaseClass::InitPredictable( pOwner );
}

void CUnitBase::PostDataUpdate( DataUpdateType_t updateType )
{
	bool bPredict = ShouldPredict();
	if ( bPredict )
	{
		SetSimulatedEveryTick( true );
	}
	else
	{
		SetSimulatedEveryTick( false );

		// estimate velocity for non local players
		float flTimeDelta = m_flSimulationTime - m_flOldSimulationTime;
		if ( flTimeDelta > 0  && !IsEffectActive(EF_NOINTERP) )
		{
			Vector newVelo = (GetNetworkOrigin() - GetOldOrigin()  ) / flTimeDelta;
			SetAbsVelocity( newVelo);
		}
	}

	// if player has switched into this marine, set it to be prediction eligible
	if (bPredict)
	{
		// C_BaseEntity assumes we're networking the entity's angles, so pretend that it
		// networked the same value we already have.
		//SetNetworkAngles( GetLocalAngles() );
		SetPredictionEligible( true );
	}
	else
	{
		SetPredictionEligible( false );
	}

	BaseClass::PostDataUpdate( updateType );

	if ( GetPredictable() && !bPredict )
	{
		MDLCACHE_CRITICAL_SECTION();
		ShutdownPredictable();
	}
}

void CUnitBase::NotePredictionError( const Vector &vDelta )
{
	Vector vOldDelta;

	GetPredictionErrorSmoothingVector( vOldDelta );

	// sum all errors within smoothing time
	m_vecPredictionError = vDelta + vOldDelta;

	// remember when last error happened
	m_flPredictionErrorTime = gpGlobals->curtime;
 
	ResetLatched(); 
}

void CUnitBase::GetPredictionErrorSmoothingVector( Vector &vOffset )
{
	if ( engine->IsPlayingDemo() || !cl_wars_smooth.GetInt() || !cl_predict->GetBool() )
	{
		vOffset.Init();
		return;
	}

	float errorAmount = ( gpGlobals->curtime - m_flPredictionErrorTime ) / cl_wars_smoothtime.GetFloat();

	if ( errorAmount >= 1.0f )
	{
		vOffset.Init();
		return;
	}
	
	errorAmount = 1.0f - errorAmount;

	vOffset = m_vecPredictionError * errorAmount;
}

C_BasePlayer *CUnitBase::GetPredictionOwner()
{
	return GetCommander();
}

bool CUnitBase::ShouldPredict( void )
{
	if (C_BasePlayer::IsLocalPlayer(GetCommander()))
	{
		FOR_EACH_VALID_SPLITSCREEN_PLAYER( hh )
		{
			ACTIVE_SPLITSCREEN_PLAYER_GUARD( hh );

			C_HL2WarsPlayer* player = C_HL2WarsPlayer::GetLocalHL2WarsPlayer();
			if (player && player->GetControlledUnit() == this)
			{
				return true;
			}
		}
	}
	return false;
}

void CUnitBase::EstimateAbsVelocity( Vector& vel )
{
	// FIXME: Unit velocity doesn't seems correct
	if( ShouldPredict() )
	{
		vel = GetAbsVelocity();
		return;
	}
	return BaseClass::EstimateAbsVelocity(vel);
}

#if 0
// units require extra interpolation time due to think rate
ConVar cl_unit_extra_interp( "cl_unit_extra_interp", "0.0", FCVAR_NONE, "Extra interpolation for units." );

float CUnitBase::GetInterpolationAmount( int flags )
{
	return BaseClass::GetInterpolationAmount( flags ) + cl_unit_extra_interp.GetFloat();
}
#endif // 0 