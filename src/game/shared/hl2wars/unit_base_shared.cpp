//====== Copyright � 2007-2012 Sandern Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "unit_base_shared.h"
#include "gamestringpool.h"
#include "hl2wars_shareddefs.h"
#include "fowmgr.h"
#include "animation.h"

#ifdef CLIENT_DLL
	#include "c_hl2wars_player.h"
	#include "c_wars_weapon.h"
#else
	#include "hl2wars_player.h"
	#include "wars_weapon.h"
#endif // CLIENT_DLL

#include "ammodef.h"
#include "takedamageinfo.h"
#include "shot_manipulator.h"
#include "ai_debug_shared.h"
#include "collisionutils.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern void	SpawnBlood(Vector vecSpot, const Vector &vecDir, int bloodColor, float flDamage);

ConVar unit_cheaphitboxtest("unit_cheaphitboxtest", "1", FCVAR_CHEAT|FCVAR_REPLICATED, "Enables/disables testing against hitboxes of an unit, regardless of whether they have hitboxes");
ConVar unit_cheapshotsimulation("unit_cheapshotsimulation", "1", FCVAR_CHEAT|FCVAR_REPLICATED, "Enables/disables cheap shooting.");

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

CUnit_Manager g_Unit_Manager;

//-------------------------------------

CUnit_Manager::CUnit_Manager()
{
	m_Units.EnsureCapacity( MAX_UNITS );
}

//-------------------------------------

CUnitBase **CUnit_Manager::AccessUnits()
{
	if (m_Units.Count())
		return &m_Units[0];
	return NULL;
}

//-------------------------------------

int CUnit_Manager::NumUnits()
{
	return m_Units.Count();
}

//-------------------------------------

void CUnit_Manager::AddUnit( CUnitBase *pUnit )
{
	m_Units.AddToTail( pUnit );
}

//-------------------------------------

void CUnit_Manager::RemoveUnit( CUnitBase *pUnit )
{
	int i = m_Units.Find( pUnit );

	if ( i != -1 )
		m_Units.FastRemove( i );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
UnitListInfo *g_pUnitListHead = NULL;

UnitListInfo *GetUnitListForOwnernumber(int iOwnerNumber)
{
	UnitListInfo *pUnitList;
	for( pUnitList=g_pUnitListHead; pUnitList; pUnitList=pUnitList->m_pNext )
	{
		if( pUnitList->m_OwnerNumber == iOwnerNumber )
			return pUnitList;
	}
	return NULL;
}

#ifndef DISABLE_PYTHON
void MapUnits( boost::python::object method )
{
	CUnitBase *pUnit;
	UnitListInfo *pUnitList;
	for( pUnitList=g_pUnitListHead; pUnitList; pUnitList=pUnitList->m_pNext )
	{
		// For each unit
		for( pUnit=pUnitList->m_pHead; pUnit; pUnit=pUnit->GetNext() )
		{
			method( pUnit->GetPyInstance() );
		}
	}
}
#endif // DISABLE_PYTHON

//-----------------------------------------------------------------------------
Disposition_t g_playerrelationships[MAX_PLAYERS][MAX_PLAYERS];

void SetPlayerRelationShip(int p1, int p2, Disposition_t rel)
{
	if( p1 < 0 || p1 >= MAX_PLAYERS || p2 < 0 || p2 >= MAX_PLAYERS )
		return;
	g_playerrelationships[p1][p2] = rel;
}

Disposition_t GetPlayerRelationShip(int p1, int p2)
{
	if( p1 < 0 || p1 >= MAX_PLAYERS || p2 < 0 || p2 >= MAX_PLAYERS )
		return D_ER;
	return g_playerrelationships[p1][p2];
}

// This class is exposed in python and networkable
#ifdef CLIENT_DLL
	IMPLEMENT_PYCLIENTCLASS( C_UnitBase, PN_UNITBASE )
#else
	IMPLEMENT_PYSERVERCLASS( CUnitBase, PN_UNITBASE );
#endif // CLIENT_DLL

//-----------------------------------------------------------------------------
// Purpose: Constructor
// Input  :
// Output :
//-----------------------------------------------------------------------------
CUnitBase::CUnitBase() : m_bUseCheapShotSimulation(true), m_fAccuracy(1.0f), m_bCanBeSeen(true)
{
	SetAllowNavIgnore(true);

#ifndef CLIENT_DLL
	DensityMap()->SetType( DENSITY_GAUSSIAN );

	// Default navigator/pathfind values
	m_fDeathDrop = 600.0f;
	m_fSaveDrop = 300.0f;
	m_fMaxClimbHeight = 0.0f; // Indicates not capable of climbing
	m_fTestRouteStartHeight = 100.0f;
	m_fLastRangeAttackLOSTime = -1;
	m_iAttackLOSMask = MASK_BLOCKLOS_AND_NPCS|CONTENTS_IGNORE_NODRAW_OPAQUE;

	// Default unit type
	SetUnitType("unit_unknown");

	m_bFOWFilterFriendly = true;
#else
	SetPredictionEligible( true );
#endif // CLIENT_DLL

	AddToUnitList();
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
// Input  :
// Output :
//-----------------------------------------------------------------------------
CUnitBase::~CUnitBase()
{
	RemoveFromUnitList();
}

void CUnitBase::UpdateOnRemove( void )
{
	RemoveFromUnitList();

	int i;
	for( i=0; i < m_SelectedByPlayers.Count(); i++ )
	{
		if( m_SelectedByPlayers[i] )
			m_SelectedByPlayers[i]->RemoveUnit( this );
	}

	BaseClass::UpdateOnRemove();
}

void CUnitBase::OnChangeOwnerNumberInternal( int old_owner_number )
{
	BaseClass::OnChangeOwnerNumberInternal(old_owner_number);

	Assert(m_pUnitList); 
	if( m_pUnitList )
	{
		RemoveFromUnitList();
		AddToUnitList();
	}
}

void CUnitBase::AddToUnitList()
{
	if( IsMarkedForDeletion() )
		return;

	g_Unit_Manager.AddUnit(this);

	// Add to the unit list
	UnitListInfo *pUnitList = g_pUnitListHead;
	while( pUnitList )
	{
		// Found
		if( pUnitList->m_OwnerNumber == GetOwnerNumber() )
		{
			if( pUnitList->m_pHead )
				pUnitList->m_pHead->m_pPrev = this;
			m_pNext = pUnitList->m_pHead;
			pUnitList->m_pHead = this;
			m_pUnitList = pUnitList;
			return;
		}
		pUnitList = pUnitList->m_pNext;
	}

	// Not found, create new one
	pUnitList = new UnitListInfo;
	if( !pUnitList )
		return;

	pUnitList->m_OwnerNumber = GetOwnerNumber();
	pUnitList->m_pHead = this;
	m_pUnitList = pUnitList;
	
	pUnitList->m_pNext = g_pUnitListHead;
	g_pUnitListHead = pUnitList;
}

void CUnitBase::RemoveFromUnitList()
{
	g_Unit_Manager.RemoveUnit(this);

	if( !m_pUnitList )
		return;

	// Unlink myself
	if( m_pUnitList && m_pUnitList->m_pHead == this )
		m_pUnitList->m_pHead = m_pNext;
	if( m_pNext )
		m_pNext->m_pPrev = m_pPrev;
	if( m_pPrev )
		m_pPrev->m_pNext = m_pNext;
	m_pUnitList = NULL;
	m_pNext = NULL;
	m_pPrev = NULL;
}

extern ConVar sv_gravity;
void CUnitBase::PhysicsSimulate( void )
{
#ifdef CLIENT_DLL
	if (ShouldPredict())
	{
		m_nSimulationTick = gpGlobals->tickcount;
		return;
	}
#else
	//NDebugOverlay::Box( GetAbsOrigin(), Vector(-16, -16, -16), Vector(16, 24, 16), 0, 255, 0, 255, 0.1f);
#endif // CLIENT_DLL

	if( GetMoveType() != MOVETYPE_WALK )
	{
		BaseClass::PhysicsSimulate();
		return;
	}

	// Run all but the base think function
	PhysicsRunThink( THINK_FIRE_ALL_BUT_BASE );
	PhysicsRunThink( THINK_FIRE_BASE_ONLY );
}

//-----------------------------------------------------------------------------
// Set the contents types that are solid by default to all Units
//-----------------------------------------------------------------------------
unsigned int CUnitBase::PhysicsSolidMaskForEntity( void ) const 
{ 
	return MASK_NPCSOLID;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CUnitBase::Select( CHL2WarsPlayer *pPlayer, bool bTriggerSel )
{
	if( !pPlayer )
		return;
	pPlayer->AddUnit(this, bTriggerSel);
}

void CUnitBase::OnSelected( CHL2WarsPlayer *pPlayer )
{
	m_SelectedByPlayers.AddToTail( pPlayer );
}

void CUnitBase::OnDeSelected( CHL2WarsPlayer *pPlayer )
{
	m_SelectedByPlayers.FindAndRemove( pPlayer );
}

void CUnitBase::OnUnitTypeChanged( const char *old_unit_type )
{
	int i;
	for( i = 0; i < m_SelectedByPlayers.Count(); i++ )
	{
		if( !m_SelectedByPlayers[i] )
			continue;
		m_SelectedByPlayers[i]->ScheduleSelectionChangedSignal();
	}
}

//=========================================================
// SetEyePosition
//
// queries the units's model for $eyeposition and copies
// that vector to the npc's m_vDefaultEyeOffset and m_vecViewOffset
//
//=========================================================
void CUnitBase::SetDefaultEyeOffset( Vector *pCustomOfset )
{
	if( pCustomOfset )
	{
		m_vDefaultEyeOffset = *pCustomOfset;
	}
	else if  ( GetModelPtr() )
	{
		GetEyePosition( GetModelPtr(), m_vDefaultEyeOffset );

		if ( m_vDefaultEyeOffset == vec3_origin )
		{
			//if ( Classify() != CLASS_NONE )
			{
				DevMsg( "WARNING: %s(%s) has no eye offset in .qc!\n", GetClassname(), STRING(GetModelName()) );
			}
			VectorAdd( WorldAlignMins(), WorldAlignMaxs(), m_vDefaultEyeOffset );
			m_vDefaultEyeOffset *= 0.75;
		}
	}
	else
		m_vDefaultEyeOffset = vec3_origin;

	// Clamp to values in dt
	m_vDefaultEyeOffset.x = MAX(MIN(m_vDefaultEyeOffset.x, 256.0f), -256.0);
	m_vDefaultEyeOffset.y = MAX(MIN(m_vDefaultEyeOffset.y, 256.0f), -256.0);
	m_vDefaultEyeOffset.z = MAX(MIN(m_vDefaultEyeOffset.z, 1024.0f), -1.0f);

#ifndef CLIENT_DLL
	SetViewOffset( m_vDefaultEyeOffset );
#endif // CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: Relationships
//-----------------------------------------------------------------------------
Disposition_t CUnitBase::IRelationType ( CBaseEntity *pTarget )
{
	if ( pTarget )
	{
		// First check for specific relationship with this edict
		for (int i=0;i<m_Relationship.Count();i++) 
		{
			if (pTarget == m_Relationship[i].entity) 
			{
				return m_Relationship[i].disposition;
			}
		}

		// Global relationships between teams
		if(GetOwnerNumber() < 0 || GetOwnerNumber() >= MAX_PLAYERS || 
				pTarget->GetOwnerNumber() < 0 || pTarget->GetOwnerNumber() >= MAX_PLAYERS)
			return D_NU;
		return g_playerrelationships[GetOwnerNumber()][pTarget->GetOwnerNumber()];
	}
	return D_ER;
}

int CUnitBase::IRelationPriority( CBaseEntity *pTarget )
{
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: Add or Change a entity relationship for this entity
// Input  :
// Output :
//-----------------------------------------------------------------------------
void CUnitBase::AddEntityRelationship ( CBaseEntity* pEntity, Disposition_t disposition, int priority )
{
	// First check to see if a relationship has already been declared for this entity
	// If so, update it with the new relationship
	for (int i=m_Relationship.Count()-1;i >= 0;i--) 
	{
		if (m_Relationship[i].entity == pEntity) 
		{
			m_Relationship[i].disposition	= disposition;
			if ( priority != DEF_RELATIONSHIP_PRIORITY )
				m_Relationship[i].priority	= priority;
			return;
		}
	}

	int index = m_Relationship.AddToTail();
	// Add the new class relationship to our relationship table
	m_Relationship[index].entity		= pEntity;
	m_Relationship[index].disposition	= disposition;
	m_Relationship[index].priority		= ( priority != DEF_RELATIONSHIP_PRIORITY ) ? priority : 0;
}

//-----------------------------------------------------------------------------
// Purpose: Removes an entity relationship from our list
// Input  : *pEntity - Entity with whom the relationship should be ended
// Output : True is entity was removed, false if it was not found
//-----------------------------------------------------------------------------
bool CUnitBase::RemoveEntityRelationship( CBaseEntity *pEntity )
{
	// Find the entity in our list, if it exists
	for ( int i = m_Relationship.Count()-1; i >= 0; i-- ) 
	{
		if ( m_Relationship[i].entity == pEntity )
		{
			// Done, remove it
			m_Relationship.Remove( i );
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : const char
//-----------------------------------------------------------------------------
const char *CUnitBase::GetTracerType( void )
{
	if ( GetActiveWeapon() )
	{
		return GetActiveWeapon()->GetTracerType();
	}

	return BaseClass::GetTracerType();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &vecTracerSrc - 
//			&tr - 
//			iTracerType - 
//-----------------------------------------------------------------------------
void CUnitBase::MakeTracer( const Vector &vecTracerSrc, const trace_t &tr, int iTracerType )
{
	CWarsWeapon *pWeapon = dynamic_cast<CWarsWeapon *>( GetActiveWeapon() );
	if ( pWeapon )
	{
		pWeapon->MakeTracer( vecTracerSrc, tr, iTracerType );
		return;
	}

	BaseClass::MakeTracer( vecTracerSrc, tr, iTracerType );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &tr - 
//			nDamageType - 
//-----------------------------------------------------------------------------
void CUnitBase::DoImpactEffect( trace_t &tr, int nDamageType )
{
	if ( GetActiveWeapon() != NULL )
	{
		GetActiveWeapon()->DoImpactEffect( tr, nDamageType );
		return;
	}

	BaseClass::DoImpactEffect( tr, nDamageType );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CUnitBase::TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr )
{
	Vector vecOrigin = ptr->endpos - vecDir * 4;

	if ( m_takedamage )
	{
		AddMultiDamage( info, this );

		// Must always be called from the client to save data
#ifdef CLIENT_DLL
		int blood = BloodColor();
		if ( blood != DONT_BLEED )
		{
			SpawnBlood( vecOrigin, vecDir, blood, info.GetDamage() );// a little surface blood.
			TraceBleed( info.GetDamage(), vecDir, ptr, info.GetDamageType() );
		}
#endif // CLIENT_DLL
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CUnitBase::DoMuzzleFlash()
{
	if( GetCommander() )
	{
		// Muzzleflash viewmodels
		GetCommander()->DoMuzzleFlash();
	}

	BaseClass::DoMuzzleFlash();
}

//-----------------------------------------------------------------------------
// Purpose: Weapons ignore other weapons when LOS tracing
//-----------------------------------------------------------------------------
class CWarsBulletsFilter : public CTraceFilterSimpleList
{
	DECLARE_CLASS( CWarsBulletsFilter, CTraceFilterSimpleList );
public:
	CWarsBulletsFilter( CUnitBase *pUnit, int collisionGroup ) : CTraceFilterSimpleList( collisionGroup ), m_pUnit(pUnit)
	{

	}

	virtual bool ShouldHitEntity( IHandleEntity *pServerEntity, int contentsMask )
	{
		CBaseEntity *pEntity = EntityFromEntityHandle( pServerEntity );
		if ( !pEntity )
			return false;

		if( m_pUnit->IRelationType(pEntity) != D_HT && m_pUnit->GetCommander() == NULL )
			return false;

		return BaseClass::ShouldHitEntity( pServerEntity, contentsMask );
	}
private:
	CUnitBase *m_pUnit;
};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CUnitBase::FireBullets( const FireBulletsInfo_t &info )
{
	static int	tracerCount;
	trace_t		tr;
	CAmmoDef*	pAmmoDef	= GetAmmoDef();
	int			nDamageType	= pAmmoDef->DamageType(info.m_iAmmoType);
	//int			nAmmoFlags	= pAmmoDef->Flags(info.m_iAmmoType);
	int iNumShots;
	float flActualDamage;

	// the default attacker is ourselves
	CBaseEntity *pAttacker = info.m_pAttacker ? info.m_pAttacker : this;

	ClearMultiDamage();
	g_MultiDamage.SetDamageType( nDamageType | DMG_NEVERGIB );

	Vector vecDir;
	Vector vecEnd;

	// Adjust spread to accuracy
	Vector vecSpread( info.m_vecSpread );
	//vecSpread.x = sin( ( (asin( info.m_vecSpread.x ) * 2.0f) * m_fAccuracy ) / 2.0f );
	//vecSpread.y = sin( ( (asin( info.m_vecSpread.y ) * 2.0f) * m_fAccuracy ) / 2.0f );
	//vecSpread.z = sin( ( (asin( info.m_vecSpread.z ) * 2.0f) * m_fAccuracy ) / 2.0f );

	// Skip multiple entities when tracing
	CWarsBulletsFilter traceFilter( this, COLLISION_GROUP_NONE );
	traceFilter.SetPassEntity( this ); // Standard pass entity for THIS so that it can be easily removed from the list after passing through a portal
	traceFilter.AddEntityToIgnore( info.m_pAdditionalIgnoreEnt );

	CShotManipulator Manipulator( info.m_vecDirShooting );
	if( m_bUseCheapShotSimulation && unit_cheapshotsimulation.GetBool() )
	{
		iNumShots = 1;
		flActualDamage = info.m_flDamage * info.m_iShots;
		if ( flActualDamage == 0.0 )
		{
			flActualDamage = g_pGameRules->GetAmmoDamage( pAttacker, tr.m_pEnt, info.m_iAmmoType ) * info.m_iShots;
		}
	}
	else
	{
		iNumShots = info.m_iShots;
		flActualDamage = info.m_flDamage;
		if ( flActualDamage == 0.0 )
		{
			flActualDamage = g_pGameRules->GetAmmoDamage( pAttacker, tr.m_pEnt, info.m_iAmmoType );
		}
	}
	flActualDamage *= m_fAccuracy; // Pretty much a damage modifier

	for (int iShot = 0; iShot < iNumShots; iShot++)
	{
		//vecDir = info.m_vecDirShooting;
		vecDir = Manipulator.ApplySpread( vecSpread );

		vecEnd = info.m_vecSrc + vecDir * info.m_flDistance;

		AI_TraceLine(info.m_vecSrc, vecEnd, MASK_SHOT, &traceFilter, &tr);

		//Msg("Firing bullets. Ent: %d, IsSelf? %d, My own? %d, World? %d, Fraction: %f\n", tr.m_pEnt, tr.m_pEnt == this, 
		//	tr.m_pEnt->GetOwnerNumber() == GetOwnerNumber(), tr.m_pEnt->IsWorld(), tr.fraction);
		//NDebugOverlay::Line(info.m_vecSrc, vecEnd, 255, 0, 0, 255, 0.1f);
		//NDebugOverlay::Line(info.m_vecSrc, tr.endpos, 0, 255, 0, 255, 0.1f);

		// Make sure given a valid bullet type
		if (info.m_iAmmoType == -1)
		{
			DevMsg("ERROR: Undefined ammo type!\n");
			return;
		}

		Vector vecTracerDest = tr.endpos;

		// do damage, paint decals
		if (tr.fraction != 1.0)
		{
			CTakeDamageInfo dmgInfo( pAttacker, pAttacker, flActualDamage, nDamageType );
			CalculateBulletDamageForce( &dmgInfo, info.m_iAmmoType, vecDir, tr.endpos );
			dmgInfo.ScaleDamageForce( info.m_flDamageForceScale );
			dmgInfo.SetAmmoType( info.m_iAmmoType );

			(dynamic_cast<CBaseEntity *>(tr.m_pEnt))->DispatchTraceAttack( dmgInfo, vecDir, &tr );

			// Effects only, FireBullets should be called on the client.
			// Dispatching on the server generates far too many events/data!
#ifdef CLIENT_DLL 
			DoImpactEffect( tr, nDamageType );

			Vector vecTracerSrc = vec3_origin;
			ComputeTracerStartPosition( info.m_vecSrc, &vecTracerSrc );

			trace_t Tracer;
			Tracer = tr;
			Tracer.endpos = vecTracerDest;

			MakeTracer( vecTracerSrc, Tracer, pAmmoDef->TracerType(info.m_iAmmoType) );
#endif // CLIENT_DLL
		}
	}

#ifdef GAME_DLL
	ApplyMultiDamage();
#endif // GAME_DLL
}

//-----------------------------------------------------------------------------
// Purpose: Do not test against hit boxes, but against the bounding box.
//			Much cheaper and we don't really need hitboxes for hl2wars.
//-----------------------------------------------------------------------------
bool CUnitBase::TestHitboxes( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
#if 1
	if( unit_cheaphitboxtest.GetBool() == false )
		return BaseClass::TestHitboxes( ray, fContentsMask, tr );

	CStudioHdr *pStudioHdr = GetModelPtr( );
	if (!pStudioHdr)
		return false;

	Ray_t ray2 = ray;
	Vector start = GetAbsOrigin() - ray.m_Start;
	ray2.Init(start, start+ray.m_Delta);
	IntersectRayWithBox(ray2, WorldAlignMins(), WorldAlignMaxs(), 0.0f, &tr);

	if ( tr.DidHit() )
	{
		tr.surface.name = "**studio**";
		tr.surface.flags = SURF_HITBOX;
		tr.surface.surfaceProps = VPhysicsGetObject() ? VPhysicsGetObject()->GetMaterialIndex() : 0;
		//NDebugOverlay::SweptBox(ray.m_Start, tr.endpos, -Vector(32, 32, 32), Vector(32, 32, 32), GetAbsAngles(), 255, 0, 0, 255, 1.0f);
		return true;
	}
	return false;
#else
	return BaseClass::TestHitboxes( ray, fContentsMask, tr );
#endif // 0
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *CUnitBase::GetUnitType()
{
	return STRING(m_UnitType);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CUnitBase::AimGun()
{
	if( GetEnemy() )
	{
		Vector vecShootOrigin = Weapon_ShootPosition();
		Vector vecShootDir = GetShootEnemyDir( vecShootOrigin, false );
		SetAim(vecShootDir);
	}
	else
	{
		RelaxAim();
	}
}

void CUnitBase::RelaxAim()
{
	float curPitch = m_fEyePitch;

	// dampen existing aim
	float newPitch = AngleNormalize( ApproachAngle(0, curPitch, 3 ));
	//float newYaw = AngleNormalize( ApproachAngle(GetAbsAngles().y, curYaw, 2) )

	m_fEyePitch = newPitch;
	m_fEyeYaw = GetAbsAngles().y; // newYaw
}

void CUnitBase::SetAim( Vector &vAimDir )
{
	QAngle angDir = QAngle();
	VectorAngles(vAimDir, angDir);

    // Just set pitch to enemy direction and yaw to our move directin
	m_fEyePitch = angDir.x;
    m_fEyeYaw = angDir.y; //GetAbsAngles().y;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
Vector CUnitBase::GetShootEnemyDir( Vector &shootOrigin, bool noisy )
{
	CBaseEntity *pEnemy = GetEnemy();

	if( !pEnemy ) 
	{
        Vector forward;
        AngleVectors( GetLocalAngles(), &forward );
		return forward;
	}

    const Vector &vecEnemy = pEnemy->GetAbsOrigin();

	Vector vecEnemyOffset;
	CUnitBase *pUnit = pEnemy->MyUnitPointer();
    if( pUnit )
        vecEnemyOffset = pUnit->BodyTarget( shootOrigin, noisy ) - vecEnemy;
    else
        vecEnemyOffset = vec3_origin;
        
    Vector retval = vecEnemyOffset + vecEnemy - shootOrigin;
    VectorNormalize( retval );
    return retval;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
Vector CUnitBase::BodyTarget( const Vector &posSrc, bool bNoisy ) 
{ 
	Vector low = WorldSpaceCenter() - ( WorldSpaceCenter() - GetAbsOrigin() ) * .25;
	Vector high = EyePosition();
	Vector delta = high - low;
	Vector result;
	if ( bNoisy )
	{
		// bell curve
		float rand1 = random->RandomFloat( 0.0, 0.5 );
		float rand2 = random->RandomFloat( 0.0, 0.5 );
		result = low + delta * rand1 + delta * rand2;
	}
	else
		result = low + delta * 0.5; 

	return result;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHL2WarsPlayer* CUnitBase::GetCommander() const
{
	return dynamic_cast<CHL2WarsPlayer*>(m_hCommander.Get());
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CUnitBase::OnUserControl( CHL2WarsPlayer *pPlayer )
{
#ifdef CLIENT_DLL
	if( GetActiveWeapon() )
		GetActiveWeapon()->SetViewModel();
#endif // CLIENT_DLL
}