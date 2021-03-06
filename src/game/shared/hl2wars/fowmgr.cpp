//====== Copyright � 2007-2012 Sandern Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "fowmgr.h"
#include "unit_base_shared.h"
#include "tier0/vprof.h"
#include "utlbuffer.h"
#include <algorithm>

#if defined( CLIENT_DLL )
	#include "hl2wars/c_hl2wars_player.h"
	#include "materialsystem/ITexture.h"
	#include "materialsystem/imaterialvar.h"
	#include "renderparm.h"
	#include "tex_fogofwar.h"
#else
	#include "hl2wars/hl2wars_player.h"
#endif

#include <string.h>
#include "wars_mapboundary.h"

#include <filesystem.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Increment this to force rebuilding all heightmaps
#define	 HEIGHTMAP_VERSION_NUMBER	2

// #define FOW_USE_PROCTEX

static CFogOfWarMgr s_FogOfWarMgr; // singleton

CFogOfWarMgr* FogOfWarMgr() { return &s_FogOfWarMgr; }

ConVar sv_fogofwar( "sv_fogofwar", "1", FCVAR_CHEAT | FCVAR_REPLICATED | FCVAR_NOTIFY, "Disables/enables the fog of war." );
ConVar sv_fogofwar_shadowcast( "sv_fogofwar_shadowcast", "1", FCVAR_CHEAT | FCVAR_REPLICATED, "Use shadow casting when computing the fog of war (i.e. height differences block the line of sight." );
ConVar g_debug_fogofwar( "g_debug_fogofwar", "0", FCVAR_CHEAT | FCVAR_REPLICATED );

ConVar sv_fogofwar_tilesize( "sv_fogofwar_tilesize", "64", FCVAR_CHEAT | FCVAR_REPLICATED, "Tile size of the fog of war. Lower values result in a more detailed fog of war, but at more expense. Must be a power of 2." );

// Split update rate on the server and client
// Client will have an higher update rate to make it visually look better
#ifdef CLIENT_DLL
	ConVar cl_fogofwar_updaterate( "cl_fogofwar_updaterate", "0.1", 0, "Rate at which the fog of war visuals update." );
	#define FOW_UPDATERATE cl_fogofwar_updaterate.GetFloat()

#ifdef FOW_USE_PROCTEX
	ConVar cl_fogofwar_convergerate( "cl_fogofwar_convergerate", "0.033", FCVAR_ARCHIVE, "Converge/texture generation rate" );
	ConVar cl_fogofwar_noconverge( "cl_fogofwar_noconverge", "0", FCVAR_ARCHIVE, "Dont converge the fog of war visuals. Directly change instead." );
	ConVar cl_fogofwar_convergespeed_in( "cl_fogofwar_convergespeed_in", "250", 0, "Speed per second at which the fog of war converges to hidden (from 0 to 255)." );
	ConVar cl_fogofwar_convergespeed_out( "cl_fogofwar_convergespeed_out", "250", 0, "Speed per second at which the fog of war converges to visible (from 255 to 0)." );
#endif // FOW_USE_PROCTEX
	ConVar cl_fogofwar_notextureupdate( "cl_fogofwar_notextureupdate", "0", FCVAR_CHEAT, "Debug command" );

	ConVar mat_fow_converge_ratein("mat_fow_converge_ratein", "1.0");
	ConVar mat_fow_converge_rateout("mat_fow_converge_rateout", "1.0");
	ConVar mat_fow_blur("mat_fow_blur", "1");
	ConVar mat_fow_blur_factor("mat_fow_blur_factor", "0.2");

	ConVar fow_shadowcast_debug("fow_shadowcast_debug", "0", FCVAR_CHEAT);
	ConVar fow_debug_draw_max("fow_debug_draw_max", "-1", FCVAR_CHEAT);
#else
	ConVar sv_fogofwar_updaterate( "sv_fogofwar_updaterate", "0.2", FCVAR_GAMEDLL, "Rate at which the fog of war logic updates." );
	#define FOW_UPDATERATE sv_fogofwar_updaterate.GetFloat()
#endif

//-----------------------------------------------------------------------------
// FOW Entity list. Keeps track of fow entities per player/owner
//-----------------------------------------------------------------------------
void FOWAddEntity( FOWListInfo **pHead, int ownernumber, CBaseEntity *pEnt )
{
	if( EHANDLE(pEnt) == NULL )
	{
		Warning("FOWAddEntity: tried to add an uninitialzed entity to the fog of war list.\n");
		return;
	}

	// Add to the unit list
	FOWListInfo *pFOWList = *pHead;
	while( pFOWList )
	{
		// Found
		if( pFOWList->m_iOwnerNumber == ownernumber )
		{
			pFOWList->m_EntityList.AddToTail(pEnt);
			return;
		}
		pFOWList = pFOWList->m_pNext;
	}

	// Not found, create new one
	pFOWList = new FOWListInfo();
	if( !pFOWList )
		return;

	pFOWList->m_iOwnerNumber = ownernumber;
	pFOWList->m_EntityList.AddToTail(pEnt);
	pFOWList->m_pNext = *pHead;
	*pHead = pFOWList;
}

void FOWRemoveEntity( FOWListInfo **pHead, int ownernumber, CBaseEntity *pEnt )
{
	FOWListInfo *pFOWList = *pHead;
	while( pFOWList )
	{
		// Found
		if( pFOWList->m_iOwnerNumber == ownernumber )
		{
			pFOWList->m_EntityList.FindAndRemove(pEnt);
			return;
		}
		pFOWList = pFOWList->m_pNext;
	}
	Assert(0);
	Warning("FOWRemoveEntity: Couldn't find entity #%d %s\n", pEnt->entindex(), pEnt->GetClassname());
}

FOWListInfo *FOWFindList( FOWListInfo *pHead, int ownernumber )
{
	FOWListInfo *pFOWList = pHead;
	while( pFOWList )
	{
		if( pFOWList->m_iOwnerNumber == ownernumber )
		{
			return pFOWList;
		}
		pFOWList = pFOWList->m_pNext;
	}
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CFogOfWarMgr::CFogOfWarMgr()
{
	m_fNeedsUpdate = 0;
	m_bActive = false;
	m_pFogUpdaterListHead = NULL;
	m_bWasFogofwarOn = true;
	m_nGridSize = -1;
	m_nTileSize = -1;
	m_bHeightMapLoaded = false;

#ifdef CLIENT_DLL
	m_pTextureRegen = new CFOWTextureRegen();
	m_bRenderingFOW = false;
#endif // CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CFogOfWarMgr::~CFogOfWarMgr()
{
#ifdef CLIENT_DLL
	delete m_pTextureRegen;
#endif // CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CFogOfWarMgr::Init()
{
	AllocateFogOfWar();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFogOfWarMgr::Shutdown()
{
	DeallocateFogOfWar();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFogOfWarMgr::LevelInitPreEntity()
{
#ifdef CLIENT_DLL
#if 0
	// Read fog of war color from map resource file
	ConVarRef mat_fogofwar_r("mat_fogofwar_r");
	ConVarRef mat_fogofwar_g("mat_fogofwar_g");
	ConVarRef mat_fogofwar_b("mat_fogofwar_b");
	ConVarRef mat_fogofwar_a("mat_fogofwar_a");

	char buf[MAX_PATH];
	V_StripExtension( engine->GetLevelName(), buf, MAX_PATH );
	Q_snprintf( buf, sizeof( buf ), "%s.res", buf );

	KeyValues *pMapData = new KeyValues("MapData");
	if( pMapData->LoadFromFile( filesystem, buf ) )
	{
		mat_fogofwar_r.SetValue( pMapData->GetString("fow_r", mat_fogofwar_r.GetDefault()) );
		mat_fogofwar_g.SetValue( pMapData->GetString("fow_g", mat_fogofwar_g.GetDefault()) );
		mat_fogofwar_b.SetValue( pMapData->GetString("fow_b", mat_fogofwar_b.GetDefault()) );
		mat_fogofwar_a.SetValue( pMapData->GetString("fow_a", mat_fogofwar_a.GetDefault()) );

		pMapData->deleteThis();
		pMapData = NULL;
	}
	else
	{
		mat_fogofwar_r.SetValue( mat_fogofwar_r.GetDefault() );
		mat_fogofwar_g.SetValue( mat_fogofwar_g.GetDefault() );
		mat_fogofwar_b.SetValue( mat_fogofwar_b.GetDefault() );
		mat_fogofwar_a.SetValue( mat_fogofwar_a.GetDefault() );
	}
#endif //0
#else
	for( int i = 0; i < MAX_PLAYERS; i++ )
		ResetToKnown( i );
#endif // CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFogOfWarMgr::LevelInitPostEntity()
{
	m_bActive = true;
	m_fNeedsUpdate = 0.0f;
	m_fNextConvergeUpdate = 0.0f;

	LoadHeightMap();
	if( !m_bHeightMapLoaded )
	{
		CalculateHeightMap();
		SaveHeightMap();
	}

	if( sv_fogofwar.GetBool() )
	{
		m_bWasFogofwarOn = true;
		ClearFogOfWarTo( FOWHIDDEN_MASK );
	}
	else
	{
		m_bWasFogofwarOn = false;
		ClearFogOfWarTo( FOWCLEAR_MASK );
	}

#ifdef CLIENT_DLL
	//UpdateTexture( false );
#endif // CLIENT_DLL

	UpdateVisibility();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CFogOfWarMgr::CanCalculateHeightMap()
{
	return GetMapBoundaryList() != NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Calculates a height map for the fog of war
//-----------------------------------------------------------------------------
void CFogOfWarMgr::CalculateHeightMap()
{
	Assert( m_TileHeights.Count() == m_nGridSize * m_nGridSize );
	if( m_TileHeights.Count() !=  m_nGridSize * m_nGridSize )
	{
		Warning("CalculateHeightMap: invalid tileheights size\n");
		return;
	}

	float z = -MAX_COORD_FLOAT;
	if( CanCalculateHeightMap() )
	{
		double fStartTime = Plat_FloatTime();

		for( CBaseFuncMapBoundary *pEnt = GetMapBoundaryList(); pEnt != NULL; pEnt = pEnt->m_pNext )
		{
			Vector mins, maxs;
			pEnt->GetMapBoundary( mins, maxs );

			z = MAX(z, maxs.z);
		}

		float tilez;
		Vector start, end;
		trace_t tr;
		for( int x = 0; x < m_nGridSize; x++ )
		{
			for( int y = 0; y < m_nGridSize; y++ )
			{
				tilez = 0.0f;

				start = ComputeWorldPosition( x, y );
				start.z = z - 16.0f;
				end = start + Vector( 0, 0, -1 ) * MAX_TRACE_LENGTH;
				//UTIL_TraceHull( start, end, -Vector(m_nTileSize, m_nTileSize, 8.0f), Vector(m_nTileSize, m_nTileSize, 8.0f),
				//	MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );

				// Use four trace lines and average the result
				UTIL_TraceLine( start, end, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
				tilez += tr.endpos.z;

				start.x += m_nTileSize;
				end.x += m_nTileSize;
				UTIL_TraceLine( start, end, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
				tilez += tr.endpos.z;

				start.y += m_nTileSize;
				end.y += m_nTileSize;
				UTIL_TraceLine( start, end, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
				tilez += tr.endpos.z;

				start.x -= m_nTileSize;
				end.x -= m_nTileSize;
				UTIL_TraceLine( start, end, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
				tilez += tr.endpos.z;

				//Msg("start: %f %f %f, end: %f %f %f, x: %d, y: %d, end: %f\n", x, y, tr.endpos.z);
				m_TileHeights[FOWINDEX(x, y)] = tilez / 4.0f;
			}
		}

		Msg("CFogOfWarMgr: Generated height map in %f seconds\n", Plat_FloatTime() - fStartTime);
	}
	else
	{
		Warning("CFogOfWarMgr: No map boundaries, fog of war will have no height differences!\n");
		for( int x = 0; x < m_nGridSize; x++ )
		{
			for( int y = 0; y < m_nGridSize; y++ )
			{
				m_TileHeights[FOWINDEX(x, y)] = -MAX_COORD_FLOAT;
			}
		}
	}

	m_bHeightMapLoaded = true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFogOfWarMgr::SaveHeightMap()
{
#ifndef CLIENT_DLL
	// Get filename
	char szNrpFilename[MAX_PATH];
	Q_strncpy( szNrpFilename, "maps/" ,sizeof(szNrpFilename));
	Q_strncat( szNrpFilename, STRING( gpGlobals->mapname ), sizeof(szNrpFilename), COPY_ALL_CHARACTERS );
	Q_strncat( szNrpFilename, "mapheightfielddata.bin", sizeof( szNrpFilename ), COPY_ALL_CHARACTERS );
	Msg("Saving height map to %s\n", szNrpFilename);

	CUtlBuffer buf;

	// ---------------------------
	// Save the version number
	// ---------------------------
	buf.PutInt(HEIGHTMAP_VERSION_NUMBER);
	buf.PutInt(gpGlobals->mapversion);

	// -------------------------------
	// Store grid size
	// -------------------------------
	buf.PutInt( m_nGridSize );

	// -------------------------------
	// Store if we have a height map
	// -------------------------------
	bool bHasHeightMap = CanCalculateHeightMap();
	buf.PutChar( (char)bHasHeightMap );

	if( bHasHeightMap )
	{
		// ---------------------------
		// Save heightmap
		// ---------------------------
		int i;
		for ( i = 0; i < m_nGridSize*m_nGridSize; i++)
		{
			buf.PutFloat( m_TileHeights[i] );
		}
	}

	// -------------------------------
	// Write the file out
	// -------------------------------
	FileHandle_t fh = filesystem->Open( szNrpFilename, "wb" );
	if ( !fh )
	{
		DevWarning( 2, "Couldn't create %s!\n", szNrpFilename );
		return;
	}

	filesystem->Write( buf.Base(), buf.TellPut(), fh );
	filesystem->Close(fh);

#endif // CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: Loads height map from file. Creates and saves one if not present or changed.
//-----------------------------------------------------------------------------
void CFogOfWarMgr::LoadHeightMap()
{
	m_bHeightMapLoaded = false;

	// Get filename
	char szNrpFilename[MAX_PATH];
#ifdef CLIENT_DLL
	V_StripExtension( engine->GetLevelName(), szNrpFilename, MAX_PATH );
#else
	Q_strncpy( szNrpFilename, "maps/" ,sizeof(szNrpFilename));
	Q_strncat( szNrpFilename, STRING( gpGlobals->mapname ), sizeof(szNrpFilename), COPY_ALL_CHARACTERS );
#endif // CLIENT_DLL
	Q_strncat( szNrpFilename, "mapheightfielddata.bin", sizeof( szNrpFilename ), COPY_ALL_CHARACTERS );
	double fStartTime = Plat_FloatTime();

	// Read file
	CUtlBuffer buf;
	if ( !filesystem->ReadFile( szNrpFilename, "game", buf ) )
	{
		DevMsg( "Height map %s does not exists\n", szNrpFilename );
		return;
	}

	// ---------------------------
	// Check the version number
	// ---------------------------
	if ( buf.GetChar() == 'V' && buf.GetChar() == 'e' && buf.GetChar() == 'r' )
	{
		DevMsg( "Height map %s is out of date\n", szNrpFilename );
		return;
	}

	buf.SeekGet( CUtlBuffer::SEEK_HEAD, 0 );

	int version = buf.GetInt();
	if ( version != HEIGHTMAP_VERSION_NUMBER)
	{
		DevMsg( "Height map %s is out of date\n", szNrpFilename );
		return;
	}

	
#ifndef CLIENT_DLL // FIXME
	int mapversion = buf.GetInt();
	if ( mapversion != gpGlobals->mapversion )
	{
		DevMsg( "Height map %s is out of date (map version changed)\n", szNrpFilename );
		return;
	}
#else
	buf.GetInt();
#endif // CLIENT_DLL

	// ---------------------------
	// Check grid size
	// ---------------------------
	int gridsize = buf.GetInt();
	if( gridsize != m_nGridSize )
	{
		DevMsg( "Height map %s is out of date (grid size changed %d -> %d)\n", szNrpFilename, gridsize, m_nGridSize );
		return;
	}

	// ---------------------------
	// Check if we have a height map
	// ---------------------------
	int i;
	bool bHasHeightMap = (bool)buf.GetChar();

	// ---------------------------
	// Load heightmap
	// ---------------------------
	if( bHasHeightMap )
	{
		for ( i = 0; i < gridsize*gridsize; i++)
			m_TileHeights[i] = buf.GetFloat();
	}
	else
	{
		Q_memset( m_TileHeights.Base(), 0, gridsize*gridsize*sizeof(float) );
	}

	DevMsg("CFogOfWarMgr: Loaded height map in %f seconds\n", Plat_FloatTime() - fStartTime);
	m_bHeightMapLoaded = true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFogOfWarMgr::ModifyHeightAtTile( int x, int y, float fHeight )
{
	int idx = FOWINDEX(x, y);
	if( m_TileHeights.IsValidIndex( idx ) )
		m_TileHeights[idx] = fHeight;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFogOfWarMgr::ModifyHeightAtPoint( const Vector &vPoint, float fHeight )
{
	int x, y;
	ComputeFOWPosition( vPoint, x, y );
	ModifyHeightAtTile( x, y, fHeight );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFogOfWarMgr::ModifyHeightAtExtent( const Vector &vMins, const Vector &vMaxs, float fHeight )
{
	int x1, y1, x2, y2;
	ComputeFOWPosition( vMins, x1, y1 );
	ComputeFOWPosition( vMaxs, x2, y2 );

	for( int i = x1; i < x2+1; i++ )
	{
		for( int j = y1; j < y2+1; j++ )
		{
			ModifyHeightAtTile( i, j, fHeight );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CFogOfWarMgr::GetHeightAtTile( int x, int y )
{
	int idx = FOWINDEX(x, y);
	if( m_TileHeights.IsValidIndex( idx ) )
		return m_TileHeights[idx];
	return 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CFogOfWarMgr::GetHeightAtPoint( const Vector &vPoint )
{
	int x, y;
	ComputeFOWPosition( vPoint, x, y );
	return GetHeightAtTile( x, y );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFogOfWarMgr::LevelShutdownPostEntity()
{
	m_bActive = false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CFogOfWarMgr::NeedsUpdate()
{
	if( m_fNeedsUpdate < gpGlobals->curtime )
		return true;

	return false;
}

#ifndef CLIENT_DLL
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFogOfWarMgr::PreClientUpdate()
{
	if( !m_bActive )
		return;
	UpdateShared();
}
#endif // CLIENT_DLL

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFogOfWarMgr::AllocateFogOfWar()
{
	VPROF_BUDGET( "CFogOfWarMgr::AllocateFogOfWar", VPROF_BUDGETGROUP_FOGOFWAR );

	if( ( sv_fogofwar_tilesize.GetInt() & (sv_fogofwar_tilesize.GetInt() - 1) ) != 0 )
	{
		Warning("Invalid fog of war tile size. Resetting to default\n");
		sv_fogofwar_tilesize.Revert();
	}

	if( sv_fogofwar_tilesize.GetInt() < 0 )
	{
		Warning("Invalid fog of war tile size value. Resetting to default\n");
		sv_fogofwar_tilesize.Revert();
	}

	if( sv_fogofwar_tilesize.GetInt() == m_nTileSize )
		return;

	Msg("Allocating Fog of War with tilesize %d\n", sv_fogofwar_tilesize.GetInt());

	DeallocateFogOfWar();

	m_nTileSize = sv_fogofwar_tilesize.GetInt();
	m_nGridSize = FOW_WORLDSIZE / m_nTileSize;

	m_FogOfWar.SetCount( m_nGridSize*m_nGridSize );
	m_TileHeights.SetCount( m_nGridSize*m_nGridSize );

	if( m_bActive )
		CalculateHeightMap();

#ifdef CLIENT_DLL
#if FOW_USE_PROCTEX
	m_FogOfWarTextureData.SetCount( m_nGridSize*m_nGridSize );

	// IMPORTANT: Use TEXTUREFLAGS_SINGLECOPY in case you want to be able to regenerate only a part of the texture (i.e. specifiy
	//				a sub rect when calling ->Download()).
#ifdef HL2WARS_ASW_DLL
	m_RenderBuffer.InitProceduralTexture( "__rt_fow", TEXTURE_GROUP_CLIENT_EFFECTS, m_nGridSize, m_nGridSize, IMAGE_FORMAT_I8, 
		TEXTUREFLAGS_PROCEDURAL|TEXTUREFLAGS_NOLOD|TEXTUREFLAGS_NOMIP|TEXTUREFLAGS_SINGLECOPY|
		TEXTUREFLAGS_TRILINEAR|TEXTUREFLAGS_PRE_SRGB|TEXTUREFLAGS_NODEPTHBUFFER|TEXTUREFLAGS_PWL_CORRECTED );

	CMatRenderContextPtr pRenderContext( g_pMaterialSystem );
	pRenderContext->SetVectorRenderingParameter( VECTOR_RENDERPARM_GLOBAL_FOW_MINS, Vector(MIN_COORD_FLOAT, MIN_COORD_FLOAT, MIN_COORD_FLOAT) );
	pRenderContext->SetVectorRenderingParameter( VECTOR_RENDERPARM_GLOBAL_FOW_MAXS, Vector(MAX_COORD_FLOAT, MAX_COORD_FLOAT, MAX_COORD_FLOAT) );
#else
	m_RenderBuffer.InitProceduralTexture( "__rt_fow", TEXTURE_GROUP_CLIENT_EFFECTS, m_nGridSize, m_nGridSize, IMAGE_FORMAT_I8, 
		TEXTUREFLAGS_PROCEDURAL|TEXTUREFLAGS_NOLOD|TEXTUREFLAGS_NOMIP|TEXTUREFLAGS_SINGLECOPY|
		TEXTUREFLAGS_TRILINEAR|TEXTUREFLAGS_NODEPTHBUFFER );
#endif // HL2WARS_ASW_DLL
	m_RenderBuffer->SetTextureRegenerator( m_pTextureRegen );
#else
#endif // 0

	// Must have one reference to a material using the fog of war texture (otherwise ->Download does not take effect).
	m_FOWMaterial.Init("fow/fow", TEXTURE_GROUP_CLIENT_EFFECTS);
	m_FOWImMaterial.Init("fow/fow_im", TEXTURE_GROUP_CLIENT_EFFECTS);
	m_FOWBlurMaterial.Init("fow/fow_blur", TEXTURE_GROUP_CLIENT_EFFECTS);

	static bool bFirstLoad = true;
	if( !bFirstLoad )
		materials->ReloadMaterials();
	bFirstLoad = false;
#endif // CLIENT_DLL
}

void CFogOfWarMgr::DeallocateFogOfWar()
{
	VPROF_BUDGET( "CFogOfWarMgr::DeallocateFogOfWar", VPROF_BUDGETGROUP_FOGOFWAR );

	m_FogOfWar.Purge();
	m_TileHeights.Purge();

#ifdef CLIENT_DLL
#if FOW_USE_PROCTEX
	m_FogOfWarTextureData.Purge();

	if( m_RenderBuffer.IsValid() )
	{
		m_RenderBuffer->SetTextureRegenerator(NULL);
		m_RenderBuffer.Shutdown();
	}
#endif // 0

	if( m_FOWMaterial.IsValid() )
	{
		m_FOWMaterial.Shutdown();
	}

	if( m_FOWImMaterial.IsValid() )
	{
		m_FOWImMaterial.Shutdown();
	}
#endif // CLIENT_DLL

	m_nGridSize = -1;
	m_nTileSize = -1;
}

#ifdef CLIENT_DLL
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#define FOW_RT_SIZE 1024
#define FOW_RT_SIZE_LOW 512
#define FOW_RT_SIZE_VERY_LOW 256

int CFogOfWarMgr::CalculateRenderTargetSize( void )
{
	int iSize = (ScreenHeight() < 1024 || ScreenWidth() < 1024) ? FOW_RT_SIZE_LOW : FOW_RT_SIZE;
	return (ScreenHeight() < 512 || ScreenWidth() < 512) ? FOW_RT_SIZE_VERY_LOW : iSize;
}

void CFogOfWarMgr::InitRenderTargets( void )
{
#ifndef FOW_USE_PROCTEX
	int iSize = CalculateRenderTargetSize();

	unsigned int fowFlags =	TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_RENDERTARGET;
	ImageFormat fmt = IMAGE_FORMAT_RGBA8888;

	m_RenderBuffer.Init( materials->CreateNamedRenderTargetTextureEx2(
		"_rt_fog_of_war",
		iSize, iSize,
		RT_SIZE_NO_CHANGE,
		fmt,
		MATERIAL_RT_DEPTH_NONE,
		fowFlags, 0 ) );
	m_RenderBufferBlur.Init( materials->CreateNamedRenderTargetTextureEx2(
		"__rt_fow_blur",
		iSize, iSize,
		RT_SIZE_NO_CHANGE,
		fmt,
		MATERIAL_RT_DEPTH_NONE,
		fowFlags, 0 ) );
	m_RenderBufferIM.Init( materials->CreateNamedRenderTargetTextureEx2(
		"__rt_fow_im",
		iSize, iSize,
		RT_SIZE_NO_CHANGE,
		fmt,
		MATERIAL_RT_DEPTH_NONE,
		fowFlags, 0 ) );

#endif // FOW_USE_PROCTEX
}

void CFogOfWarMgr::ShutdownRenderTargets( void )
{
#ifndef FOW_USE_PROCTEX
	m_RenderBuffer.Shutdown();
	m_RenderBufferBlur.Shutdown();
	m_RenderBufferIM.Shutdown();
#endif // FOW_USE_PROCTEX
}

void CFogOfWarMgr::OnResolutionChanged()
{
#ifndef FOW_USE_PROCTEX
	// FIXME: End result seems incorrect if the resolution is lower than the render target
	//		  For now, we will reallocate the render target
	int iSize = CalculateRenderTargetSize();

	if( m_RenderBuffer.IsValid() )
	{
		if( m_RenderBuffer->GetActualWidth() == iSize )
			return; // Nothing to do
	}

	materials->ReEnableRenderTargetAllocation_IRealizeIfICallThisAllTexturesWillBeUnloadedAndLoadTimeWillSufferHorribly();
	materials->BeginRenderTargetAllocation();
	ShutdownRenderTargets();
	InitRenderTargets();
	materials->EndRenderTargetAllocation();
#endif // FOW_USE_PROCTEX
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFogOfWarMgr::Update( float frametime )
{
	if( !m_bActive )
		return;

	VPROF_BUDGET( "CFogOfWarMgr::Update", VPROF_BUDGETGROUP_FOGOFWAR );

	UpdateShared();

#ifndef FOW_USE_PROCTEX
	if( cl_fogofwar_notextureupdate.GetBool() == false )
		RenderFogOfWar( frametime );
#else
	if( sv_fogofwar.GetBool() && cl_fogofwar_noconverge.GetBool() == false )
	{
		if( m_fNextConvergeUpdate < gpGlobals->curtime )
		{
			UpdateTexture( true, cl_fogofwar_convergerate.GetFloat() );
			m_fNextConvergeUpdate = gpGlobals->curtime + cl_fogofwar_convergerate.GetFloat();
		}
	}
#endif // FOW_USE_PROCTEX
}

#if FOW_USE_PROCTEX
//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFogOfWarMgr::UpdateTexture( bool bConverge, float fTime )
{
	VPROF_BUDGET( "CFogOfWarMgr::UpdateTexture", VPROF_BUDGETGROUP_FOGOFWAR );
	
	if( cl_fogofwar_notextureupdate.GetBool() )
		return;

	if( bConverge )
	{
		// Update target fow values on the client (to make it look visually good)
		char deltaIn = MIN(255,MAX(1, cl_fogofwar_convergespeed_in.GetFloat() * fTime)); 
		char deltaOut = MIN(255,MAX(1, cl_fogofwar_convergespeed_out.GetFloat() * fTime)); 

		int x, y, idx;
		int iMinX = m_nGridSize + 1;
		int iMaxX = -1;
		int iMinY = m_nGridSize + 1;
		int iMaxY = -1;

		FOWSIZE_TYPE *fowData = m_FogOfWar.Base();
		FOWSIZE_TYPE *imageData = m_FogOfWarTextureData.Base();

		for( x = 0; x < m_nGridSize; x++ )
		{
			for( y = 0; y < m_nGridSize; y++ )
			{
				idx = FOWINDEX(x, y);
				if( fowData[idx] > imageData[idx] )
				{
					imageData[idx] = MIN( FOWCLEAR_MASK, imageData[idx] + deltaOut );
					iMinX = MIN( iMinX, x );
					iMaxX = MAX( iMaxX, x );
					iMinY = MIN( iMinY, y );
					iMaxY = MAX( iMaxY, y );
				}
				else if( fowData[idx] < imageData[idx] )
				{
					imageData[idx] = MAX( FOWHIDDEN_MASK, imageData[idx] - deltaIn );
					iMinX = MIN( iMinX, x );
					iMaxX = MAX( iMaxX, x );
					iMinY = MIN( iMinY, y );
					iMaxY = MAX( iMaxY, y );
				}
			}
		}

		// Regenerate the fow texture
		if( iMaxX != -1 && m_RenderBuffer.IsValid() )
		{
			VPROF_BUDGET( "CFogOfWarMgr::DownloadCall", "ITextureDownloadIsSlow" );

			Rect_t rect;
			rect.x = iMinX;
			rect.y = iMinY;
			rect.width = iMaxX - iMinX + 1;
			rect.height = iMaxY - iMinY + 1;

			g_bFOWUpdateType = TEXFOW_INDIRECT;
			m_RenderBuffer->Download( &rect );
			g_bFOWUpdateType = TEXFOW_NONE;
		}
	}
	else
	{
		if( m_RenderBuffer.IsValid() )
		{
			VPROF_BUDGET( "CFogOfWarMgr::DownloadCall", "ITextureDownloadIsSlow" );

			g_bFOWUpdateType = TEXFOW_DIRECT;
			m_RenderBuffer->Download();
			g_bFOWUpdateType = TEXFOW_NONE;
		}
	}
	
}
#endif

static inline bool SetMaterialVarFloat( IMaterial* pMat, const char* pVarName, float flValue )
{
	Assert( pMat != NULL );
	Assert( pVarName != NULL );
	if ( pMat == NULL || pVarName == NULL )
	{
		return false;
	}

	bool bFound = false;
	IMaterialVar* pVar = pMat->FindVar( pVarName, &bFound );
	if ( bFound )
	{
		pVar->SetFloatValue( flValue );
	}

	return bFound;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFogOfWarMgr::RenderFogOfWar( float frametime )
{
	CMatRenderContextPtr pRenderContext( materials );

	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_GLOBAL_FOW_RATEIN, frametime * mat_fow_converge_ratein.GetFloat() );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_GLOBAL_FOW_RATEOUT, frametime * mat_fow_converge_rateout.GetFloat() );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_GLOBAL_FOW_TILESIZE, m_nTileSize / (float)FOW_WORLDSIZE );
	
	int iFOWRenderSize = m_RenderBuffer->GetActualWidth();

	// Setup view
	CViewSetup setup;
	setup.x = setup.y = 0;
	setup.width = iFOWRenderSize;
	setup.height = iFOWRenderSize;
	setup.m_bOrtho = false;
	setup.m_flAspectRatio = 1;
	setup.fov = 90;
	setup.zFar = 9999;
	setup.zNear = 10;

	int nSrcWidth = m_RenderBufferIM->GetActualWidth();
	int nSrcHeight = m_RenderBufferIM->GetActualHeight();

	bool bFOWBlur = mat_fow_blur.GetBool();

	if( bFOWBlur )
	{
		//////////////////////////////////////
		// Additional blur using 3x3 gaussian
		//////////////////////////////////////

		// Setup view and render target
		render->Push2DView( setup, 0, m_RenderBufferBlur, m_Frustum );

		pRenderContext->PushRenderTargetAndViewport();

		pRenderContext->SetRenderTarget( m_RenderBufferBlur );

		float fBlurFactor = mat_fow_blur_factor.GetFloat();
		SetMaterialVarFloat( m_FOWBlurMaterial, "$c0_x", fBlurFactor / (float)m_RenderBufferBlur->GetActualWidth() );
		SetMaterialVarFloat( m_FOWBlurMaterial, "$c0_y", fBlurFactor / (float)m_RenderBufferBlur->GetActualHeight() );
		SetMaterialVarFloat( m_FOWBlurMaterial, "$c1_x", -fBlurFactor / (float)m_RenderBufferBlur->GetActualWidth() );
		SetMaterialVarFloat( m_FOWBlurMaterial, "$c1_y", fBlurFactor / (float)m_RenderBufferBlur->GetActualHeight() );

		pRenderContext->DrawScreenSpaceRectangle(
			m_FOWBlurMaterial, 0, 0, nSrcWidth, nSrcHeight,
			0, 0, m_RenderBufferBlur->GetActualWidth()-1, m_RenderBufferBlur->GetActualHeight()-1,
			m_RenderBufferBlur->GetActualWidth(), m_RenderBufferBlur->GetActualHeight() );

		pRenderContext->PopRenderTargetAndViewport();

		render->PopView( m_Frustum );
	}

	// Setup view and render target
	render->Push2DView( setup, 0, m_RenderBuffer, m_Frustum );

	pRenderContext->PushRenderTargetAndViewport();

	pRenderContext->SetRenderTarget( m_RenderBuffer );

	if( !bFOWBlur )
	{
		pRenderContext->CopyRenderTargetToTextureEx( m_RenderBufferBlur, 0, NULL, NULL );
	}

	// Render
	if( sv_fogofwar.GetBool() )
	{
		pRenderContext->DrawScreenSpaceRectangle(
			m_FOWImMaterial, 0, 0, nSrcWidth, nSrcHeight, // Mat, destx, desty, width, height
			0, 0, m_RenderBuffer->GetActualWidth()-1, m_RenderBuffer->GetActualHeight()-1, // srcx0, srcy0, srcx1, srcy1
			m_RenderBuffer->GetActualWidth(), m_RenderBuffer->GetActualHeight() ); // srcw, srch
	}
	else
	{
		pRenderContext->ClearColor4ub( 255, 255, 255, 255 ); // Make everything visible
		pRenderContext->ClearBuffers( true, false );
	}

	pRenderContext->PopRenderTargetAndViewport();

	render->PopView( m_Frustum );

	pRenderContext.SafeRelease();
}

//-----------------------------------------------------------------------------
// Purpose: Start rendering the fow state to a render target
//-----------------------------------------------------------------------------
void CFogOfWarMgr::BeginRenderFow( bool bStartShrouded )
{
	if( !m_RenderBufferIM || !m_RenderBufferIM.IsValid() ) 
	{
		Msg("RenderFogOfWar: No render buffer\b");
		return;
	}

	FOWSIZE_TYPE *fowData = m_FogOfWar.Base();

	if( !fowData )
	{
		Msg("RenderFogOfWar: No fow data\n");
		return;
	}

	int iFOWRenderSize = m_RenderBuffer->GetActualWidth();

	// Setup view, settings and render target
	CViewSetup setup;
	setup.x = setup.y = 0;
	setup.width = iFOWRenderSize;
	setup.height = iFOWRenderSize;
	setup.m_bOrtho = false;
	setup.m_flAspectRatio = 1;
	setup.fov = 90;
	setup.zFar = 9999;
	setup.zNear = 10;

	render->Push2DView( setup, 0, m_RenderBufferIM, m_Frustum );

	CMatRenderContextPtr pRenderContext( materials );

	pRenderContext->SetVectorRenderingParameter( VECTOR_RENDERPARM_GLOBAL_FOW_MINS, Vector(MIN_COORD_FLOAT, MIN_COORD_FLOAT, MIN_COORD_FLOAT) );
	pRenderContext->SetVectorRenderingParameter( VECTOR_RENDERPARM_GLOBAL_FOW_MAXS, Vector(MAX_COORD_FLOAT, MAX_COORD_FLOAT, MAX_COORD_FLOAT) );

	pRenderContext->PushRenderTargetAndViewport();

	pRenderContext->SetRenderTarget( m_RenderBufferIM );
	pRenderContext->Viewport(0,0,m_RenderBufferIM->GetActualWidth(), m_RenderBufferIM->GetActualHeight());

	// Render fog of war
	if( bStartShrouded )
		pRenderContext->ClearColor4ub( 0, 0, 0, 0 ); // Default to shrouded
	else
		pRenderContext->ClearColor4ub( 255, 255, 255, 255 ); // Cleared

	pRenderContext->ClearBuffers( true, false );

	pRenderContext.SafeRelease();

	m_bRenderingFOW = true;
}

//-----------------------------------------------------------------------------
// Purpose: Render a single unit to the fow render target
//-----------------------------------------------------------------------------
void CFogOfWarMgr::RenderFow( CUtlVector< FowPos_t > &EndPos, int x, int y )
{
	if( !m_bRenderingFOW )
		return;

	int iFOWRenderSize = m_RenderBuffer->GetActualWidth();

	CMatRenderContextPtr pRenderContext( materials );

	//IMaterial *black_mat = materials->FindMaterial( "Tools/toolsblack", TEXTURE_GROUP_OTHER, true );
	IMaterial *white_mat = materials->FindMaterial( "vgui/white", TEXTURE_GROUP_OTHER, true );

	pRenderContext->Bind( white_mat );
	IMesh* pMesh = pRenderContext->GetDynamicMesh( true );

	CMeshBuilder meshBuilder;

	int nDebug = fow_shadowcast_debug.GetInt();

	if( nDebug == 0 )
	{
		meshBuilder.Begin( pMesh, MATERIAL_POLYGON, EndPos.Count() );

		for( int i = 0; i < EndPos.Count(); i++ )
		{
			x = EndPos[i].x * (iFOWRenderSize / (float)m_nGridSize);
			y = EndPos[i].y * (iFOWRenderSize / (float)m_nGridSize);

			meshBuilder.Position3f( x, y, 0.0f );
			meshBuilder.TexCoord2f( 0, 0.0f, 1.0f );
			meshBuilder.Color4ub( 255, 255, 255, 255 );
			meshBuilder.AdvanceVertex();
		}

		meshBuilder.End();
		pMesh->Draw();
	}
	else if( nDebug == 1 )
	{
		for( int i = 0; i < EndPos.Count(); i++ )
		{
			x = EndPos[i].x;
			y = EndPos[i].y;

			pRenderContext->DrawScreenSpaceRectangle( white_mat, x, y, 1, 1,
				0, 0, m_RenderBufferIM->GetActualWidth(), m_RenderBufferIM->GetActualHeight(),
				m_RenderBufferIM->GetActualWidth(), m_RenderBufferIM->GetActualHeight() );
		}
	}
	else if( nDebug == 2 )
	{
		FOWSIZE_TYPE *fowData = m_FogOfWar.Base();
		int idx;
		for( int i = x - 20; i < x +  20; i++ )
		{
			for( int j = y - 20; j < y +  20; j++ )
			{
				idx = FOWINDEX(i, j);

				if( fowData[idx] == 255 )
				{
					pRenderContext->DrawScreenSpaceRectangle( white_mat, i, j, 1, 1,
						0, 0, m_RenderBufferIM->GetActualWidth(), m_RenderBufferIM->GetActualHeight(),
						m_RenderBufferIM->GetActualWidth(), m_RenderBufferIM->GetActualHeight() );
				}
			}
		}
	}
	else if( nDebug == 3 )
	{
		int c = fow_debug_draw_max.GetInt() > 0 ? fow_debug_draw_max.GetInt() : EndPos.Count();
		c = MIN( EndPos.Count(), c );

		meshBuilder.Begin( pMesh, MATERIAL_LINE_STRIP, c );

		for( int i = 0; i < c; i++ )
		{
			x = EndPos[i].x;
			y = EndPos[i].y;

			meshBuilder.Position3f( x, y, 0.0f );
			meshBuilder.TexCoord2f( 0, 0.0f, 0.0f );
			meshBuilder.Color4ub( 0, 255, 0, 255 );
			meshBuilder.AdvanceVertex();
		}

		meshBuilder.End();
		pMesh->Draw();
	}

	pRenderContext.SafeRelease();
}

//-----------------------------------------------------------------------------
// Purpose: Finalize rendering the fow state
//-----------------------------------------------------------------------------
void CFogOfWarMgr::EndRenderFow()
{
	if( !m_bRenderingFOW )
		return;

	CMatRenderContextPtr pRenderContext( materials );

	pRenderContext->PopRenderTargetAndViewport();

	pRenderContext.SafeRelease();

	render->PopView( m_Frustum );

	m_bRenderingFOW = false;
}

//-----------------------------------------------------------------------------
// Purpose: Clears all
//-----------------------------------------------------------------------------
void CFogOfWarMgr::RenderFowClear()
{
	BeginRenderFow( false );
	EndRenderFow();
}
#endif // CLIENT_DLL

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFogOfWarMgr::UpdateShared()
{
	VPROF_BUDGET( "CFogOfWarMgr::UpdateShared", VPROF_BUDGETGROUP_FOGOFWAR );

	if( sv_fogofwar_tilesize.GetInt() != m_nTileSize )
	{
		if( m_nTileSize != -1 ) Msg("Fog of war size changed, reallocating arrays...\n");
		AllocateFogOfWar();
	}

	if( !NeedsUpdate() )
		return;

	CBaseEntity *pEnt;
	int i;

	m_fNeedsUpdate = gpGlobals->curtime + FOW_UPDATERATE;

#ifdef CLIENT_DLL
	C_HL2WarsPlayer *pPlayer = C_HL2WarsPlayer::GetLocalHL2WarsPlayer();
	if( !pPlayer )
		return;
#endif

	// look if fog of war is disabled, and if we need to clear the fog then
	if( !sv_fogofwar.GetBool() 
#ifdef CLIENT_DLL
		|| (pPlayer->GetTeamNumber() == TEAM_SPECTATOR && pPlayer->GetOwnerNumber() == 0) 
#endif // CLIENT_DLL
		)
	{
		if( m_bWasFogofwarOn )
		{
			// Clear the fog of war, update visibility and regenerate the texture
			ClearFogOfWarTo( FOWCLEAR_MASK );

			UpdateVisibility();
#ifdef FOW_USE_PROCTEX
#ifdef CLIENT_DLL
			UpdateTexture( false );
#endif // CLIENT_DLL
#endif // FOW_USE_PROCTEX
			m_bWasFogofwarOn = false;
		}

#ifdef CLIENT_DLL
		RenderFowClear();
#endif // CLIENT_DLL
		return;
	}

	m_bWasFogofwarOn = true;

	// Generate fog at the old positions of the units
	// Memset will do the same. Might be a bit slower when there are hardly units.
	// But we can avoid switching between lists this way and having to lookup the relationship table.
	ClearFogOfWarTo( FOWHIDDEN_MASK );

	// Update positions
	for( FOWListInfo *pFOWList=m_pFogUpdaterListHead; pFOWList; pFOWList=pFOWList->m_pNext )
	{
		for( i=0; i<pFOWList->m_EntityList.Count(); i++ )
		{
			pEnt =  pFOWList->m_EntityList.Element(i);
			if( !pEnt )
				continue;

			const Vector &origin = pEnt->GetAbsOrigin();
			pEnt->m_iFOWOldPosX = pEnt->m_iFOWPosX;
			pEnt->m_iFOWOldPosY = pEnt->m_iFOWPosY;
			ComputeFOWPosition( origin, pEnt->m_iFOWPosX, pEnt->m_iFOWPosY );
		}
	}

#ifdef CLIENT_DLL
	BeginRenderFow();
#endif // CLIENT_DLL

	// Remove the fog at the new positions
#ifndef CLIENT_DLL
	for( FOWListInfo *pFOWList=m_pFogUpdaterListHead; pFOWList; pFOWList=pFOWList->m_pNext )
	{
		if( pFOWList->m_iOwnerNumber < 0 || 
			pFOWList->m_iOwnerNumber >= FOWMAXPLAYERS )
			continue;

		ClearNewPositions(pFOWList, pFOWList->m_iOwnerNumber);
	}
#else
	for( FOWListInfo *pFOWList=m_pFogUpdaterListHead; pFOWList; pFOWList=pFOWList->m_pNext )
	{
		if( pFOWList->m_iOwnerNumber < 0 || 
			pFOWList->m_iOwnerNumber >= FOWMAXPLAYERS )
			continue;

		if( pFOWList->m_iOwnerNumber != pPlayer->GetOwnerNumber() && g_playerrelationships[pPlayer->GetOwnerNumber()][pFOWList->m_iOwnerNumber] != D_LI )
			continue;

		ClearNewPositions(pFOWList, pPlayer->GetOwnerNumber());
	}
#endif // CLIENT_DLL

#ifdef CLIENT_DLL
	EndRenderFow();
#endif // CLIENT_DLL

	UpdateVisibility();

#ifdef CLIENT_DLL
#ifdef FOW_USE_PROCTEX
	if( cl_fogofwar_noconverge.GetBool() )
	{
		UpdateTexture( false );
	}
#endif // CLIENT_DLL
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFogOfWarMgr::ClearFogOfWarTo( FOWSIZE_TYPE state )
{
	m_FogOfWar.FillWithValue( state );
}

//-----------------------------------------------------------------------------
// Purpose: Iterates all entities (units) of a player/owner and clears the 
//			fog of war in a radius around them.
//-----------------------------------------------------------------------------
void CFogOfWarMgr::ClearNewPositions( FOWListInfo *pFOWList, int iOwner, bool bClear )
{
	int i, radius;
	float view_distance;
	CBaseEntity *pEnt;

	FOWSIZE_TYPE visMask = CalculatePlayerVisibilityMask( iOwner );

	// Clear units
	for( i=0; i<pFOWList->m_EntityList.Count(); i++ )
	{
		pEnt =  pFOWList->m_EntityList.Element(i);
		if( !pEnt )
			continue;

		view_distance = pEnt->GetViewDistance();

		radius = view_distance / m_nTileSize;

		//if( pEnt->m_iFOWPosX-radius < 0 || pEnt->m_iFOWPosX+radius >= m_iSizeFow || 
		//		pEnt->m_iFOWPosY-radius < 0 || pEnt->m_iFOWPosY+radius >= m_iSizeFow )
		//	continue;

		if( sv_fogofwar_shadowcast.GetBool() )
			DoShadowCasting( pEnt, radius, visMask );
		else
			UpdateFogOfWarState( pEnt->m_iFOWPosX, pEnt->m_iFOWPosY, radius, visMask, bClear ? FOWCLEAR_MASK : FOWHIDDEN_MASK);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Calculates visiblity mask for the specified owner/player (i.e. can see allies).
//-----------------------------------------------------------------------------
FOWSIZE_TYPE CFogOfWarMgr::CalculatePlayerVisibilityMask( int iOwner )
{
	int k;
	FOWSIZE_TYPE iVisibilityMask = 0;
	for(k=0; k<FOWMAXPLAYERS; k++)
	{
		if( k != iOwner && g_playerrelationships[iOwner][k] != D_LI )
			continue;
		iVisibilityMask |= ( 1 << k );
	}
	return iVisibilityMask;
}

//-----------------------------------------------------------------------------
// Purpose: Set the state of a row in the fog of war
//-----------------------------------------------------------------------------
void CFogOfWarMgr::FillLine( int x1, int x2, int y, FOWSIZE_TYPE state, FOWSIZE_TYPE clearmask )
{
	std::fill( m_FogOfWar.Base()+FOWINDEX(x1, y), m_FogOfWar.Base()+FOWINDEX(x2+1, y), clearmask );
}

//-----------------------------------------------------------------------------
// Purpose: Clears the fog of war at the given coordinates and radius
//-----------------------------------------------------------------------------
void CFogOfWarMgr::UpdateFogOfWarState( int x0, int y0, int radius, FOWSIZE_TYPE state, FOWSIZE_TYPE clearmask )
{
	// Midpoint circle algorithm
	int f = 1 - radius;
	int ddF_x = 1;
	int ddF_y = -2 * radius;
	int x = 0;
	int y = radius;

	FillLine( x0 - radius, x0 + radius, y0, state, clearmask );

	while( x < y )
	{
		// ddF_x == 2 * x + 1;
		// ddF_y == -2 * y;
		// f == x*x + y*y - radius*radius + 2*x - y + 1;
		if( f >= 0 )
		{
			y -= 1;
			ddF_y += 2;
			f += ddF_y;
		}
		x += 1;
		ddF_x += 2;
		f += ddF_x;

		FillLine( x0 - x, x0 + x, y0 + y, state, clearmask );
		FillLine( x0 - x, x0 + x, y0 - y, state, clearmask );
		FillLine( x0 - y, x0 + y, y0 + x, state, clearmask );
		FillLine( x0 - y, x0 + y, y0 - x, state, clearmask );
	}
}

//-----------------------------------------------------------------------------
// Purpose: See DoShadowCasting.
//-----------------------------------------------------------------------------
void CFogOfWarMgr::ShadowCast( int cx, int cy, int row, float start, float end,
								int radius, int xx, int xy, int yx, int yy, FOWSIZE_TYPE mask, float eyez, CUtlVector< FowPos_t > &EndPos )
{
	if( start < end )
		return;

	bool blocked;
	int j, dx, dy, radius_squared, X, Y;
	float l_slope, r_slope, new_start;

	new_start = start;
	radius_squared = radius*radius;
	for( j = row; j < radius+1; j++ )
	{
		dx = -j-1; dy = -j;
		blocked = false;
		while( dx <= 0 )
		{
			dx += 1;
			// Translate the dx, dy coordinates into map coordinates:
			X = cx + dx * xx + dy * xy;
			Y = cy + dx * yx + dy * yy;
			// l_slope and r_slope store the slopes of the left and right
			// extremities of the square we're considering:
			l_slope = ((float)dx-0.5f)/((float)dy+0.5f);
			r_slope = ((float)dx+0.5f)/((float)dy-0.5f);
			if( start < r_slope )
				continue;
			else if( end > l_slope )
				break;
			else
			{
				// Our light beam is touching this square; light it:
				int sqrdist =  dx*dx + dy*dy;
				if( sqrdist < radius_squared )
				{
#ifdef CLIENT_DLL
					m_FogOfWar[FOWINDEX(X, Y)] = FOWCLEAR_MASK;
#else
					m_FogOfWar[FOWINDEX(X, Y)] |= mask;
#endif // CLIENT_DLL
				}
				else
				{
					int sqrdist2 =  dx*dx + (dy+1)*(dy+1);
					if( sqrdist2 < radius_squared )
					{
						EndPos.AddToTail();
						EndPos.Tail().x = X;
						EndPos.Tail().y = Y;
					}
				}

				if( blocked )
				{
					// we're scanning a row of blocked squares:
					if( m_TileHeights[FOWINDEX(X, Y)] > eyez )
					{
						new_start = r_slope;
						continue;
					}
					else
					{
						blocked = false;
						start = new_start;
					}
				}
				else
				{
					if( m_TileHeights[FOWINDEX(X, Y)] > eyez && j < radius )
					{
						EndPos.AddToTail();
						EndPos.Tail().x = X;
						EndPos.Tail().y = Y;

                        // This is a blocking square, start a child scan:
                        blocked = true;
                        ShadowCast(cx, cy, j+1, start, l_slope,
                                         radius, xx, xy, yx, yy, mask, eyez, EndPos );
                        new_start = r_slope;
					}
				}
			}
		}
		// Row is scanned; do next row unless last square was blocked:
		if( blocked )
		{
			break;
		}
	}
}

const static int ShadowCastMult[4][8] = {
	{1,  0,  0,  1, -1,  0,  0, -1},
	{0,  1,  1,  0,  0, -1, -1,  0},
	{0,  1, -1,  0,  0, -1,  1,  0},
	{1,  0,  0, -1, -1,  0,  0,  1},
};

ConVar fow_test_printendpos("fow_test_printendpos", "0", FCVAR_CHEAT|FCVAR_REPLICATED);
ConVar fow_test_singleoct("fow_test_singleoct", "-1", FCVAR_CHEAT|FCVAR_REPLICATED);

//-----------------------------------------------------------------------------
// Purpose: Sort clockwise
//-----------------------------------------------------------------------------
static FowPos_t s_FowCenterSortPoint;
int FowPointSort( FowPos_t const *a, FowPos_t const *b )
{
    double angle1 = atan2((float)(a->y - s_FowCenterSortPoint.y), (float)(a->x - s_FowCenterSortPoint.x));
    double angle2 = atan2((float)(b->y - s_FowCenterSortPoint.y), (float)(b->x - s_FowCenterSortPoint.x));

	if (angle1 < angle2) return -1;
	else if (angle2 < angle1) return 1;

    return 0;
}

//-----------------------------------------------------------------------------
// Purpose: Performs shadow casting.
// Based on: http://roguebasin.roguelikedevelopment.org/index.php?title=FOV_using_recursive_shadowcasting
//-----------------------------------------------------------------------------
void CFogOfWarMgr::DoShadowCasting( CBaseEntity *pEnt, int radius, FOWSIZE_TYPE mask )
{
	// Own tile is always lit
	int X = pEnt->m_iFOWPosX;
	int Y = pEnt->m_iFOWPosY;
#ifdef CLIENT_DLL
					m_FogOfWar[FOWINDEX(X, Y)] = FOWCLEAR_MASK;
#else
					m_FogOfWar[FOWINDEX(X, Y)] |= mask;
#endif // CLIENT_DLL

	CUtlVector< FowPos_t > EndPos;


	if( fow_test_singleoct.GetInt() < 0 || fow_test_singleoct.GetInt() > 7  )
	{
		int oct;
		for( oct = 0; oct < 8; oct++ )
		{
			ShadowCast( X, Y, 1, 1.0f, 0.0f, radius,
					ShadowCastMult[0][oct], ShadowCastMult[1][oct],
					ShadowCastMult[2][oct], ShadowCastMult[3][oct], mask, pEnt->EyePosition().z + 16.0f, EndPos );
		}
	}
	else
	{
		int oct = fow_test_singleoct.GetInt();
		ShadowCast( X, Y, 1, 1.0f, 0.0f, radius,
				ShadowCastMult[0][oct], ShadowCastMult[1][oct],
				ShadowCastMult[2][oct], ShadowCastMult[3][oct], mask, pEnt->EyePosition().z + 16.0f, EndPos );
	}

#ifdef CLIENT_DLL
#if 1
	s_FowCenterSortPoint.x = X;
	s_FowCenterSortPoint.y = Y;
#else
	s_FowCenterSortPoint.x = 0;
	s_FowCenterSortPoint.y = 0;
	for( int i = 0; i < EndPos.Count(); i++ )
	{
		s_FowCenterSortPoint.x += EndPos[i].x;
		s_FowCenterSortPoint.y += EndPos[i].y;
	}
	s_FowCenterSortPoint.x /= EndPos.Count();
	s_FowCenterSortPoint.y /= EndPos.Count();
#endif // 0
	EndPos.Sort( FowPointSort );
	RenderFow( EndPos, X, Y );

	if( fow_test_printendpos.GetBool() )
	{
		Msg("Printing end pos (%d count). Sort center: %d %d\n", EndPos.Count(), s_FowCenterSortPoint.x, s_FowCenterSortPoint.y );
		for( int i = 0; i < EndPos.Count(); i++ )
			Msg( "%d: %d %d\n", i, EndPos[i].x, EndPos[i].y );
		fow_test_printendpos.SetValue( false );
	}
#endif // CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: Updates the state of entities affected by the fog of war.
//-----------------------------------------------------------------------------
void CFogOfWarMgr::UpdateVisibility()
{
	int i;
	CBaseEntity *pEnt;
	Vector origin;
	bool infow;

#ifdef CLIENT_DLL
	C_HL2WarsPlayer *pPlayer = C_HL2WarsPlayer::GetLocalHL2WarsPlayer();
	if( !pPlayer )
		return;
#endif // CLIENT_DLL

	// Update all entities that are affected by the fog of war
	for(i=0; i<m_FogEntities.Count(); i++)
	{
		pEnt = m_FogEntities[i];
		if( !pEnt )
			continue;

		// The position of entities that don't update the fog of war are updated here.
		// Also update if pEnt->m_iFOWPosX is -1, which means it is not initialized yet.
		// Updater entity types might arrive here when we call UpdateVisibility
		// from somewhere else than UpdateShared.
		if( (pEnt->GetFOWFlags() & FOWFLAG_UPDATER) == 0 || pEnt->m_iFOWPosX == -1 )
		{
			const Vector &origin = pEnt->GetAbsOrigin();
			pEnt->m_iFOWOldPosX = pEnt->m_iFOWPosX;
			pEnt->m_iFOWOldPosY = pEnt->m_iFOWPosY;
			ComputeFOWPosition( origin, pEnt->m_iFOWPosX, pEnt->m_iFOWPosY );
		}

		Assert( pEnt->m_iFOWPosX >= 0 || pEnt->m_iFOWPosX < m_nGridSize );
		Assert( pEnt->m_iFOWPosY >= 0 || pEnt->m_iFOWPosY < m_nGridSize );

		// Don't care if the fog of war is on
		// Just dispatch update transmit/update visiblity, which should make the entities visible
		if( !sv_fogofwar.GetBool() 
#ifdef CLIENT_DLL
			|| (pPlayer->GetTeamNumber() == TEAM_SPECTATOR && pPlayer->GetOwnerNumber() == 0) 
#endif // CLIENT_DLL
			)
		{
#ifdef CLIENT_DLL
			pEnt->SetInFOW(false);
			pEnt->UpdateVisibility();
#else
			memset( pEnt->m_bInFOW, false, FOWMAXPLAYERS*sizeof(bool) );
			pEnt->DispatchUpdateTransmitState();
#endif // CLIENT_DLL
			continue;
		}

		// Detect changes. On the client notify the visibility changed, since we go in or out the fog of war.
		// On the server update the transmit state, because some entities won't send any info when in the fow.
#ifdef CLIENT_DLL
		// TODO: Fading in entities could probably look pretty cool
#if 0
		// Change render mode + alpha
		byte bTargetRenderA = 255 - m_FogOfWarTextureData[FOWINDEX(pEnt->m_iFOWPosX, pEnt->m_iFOWPosY)];
		if( bTargetRenderA != pEnt->GetRenderAlpha() )
		{
			pEnt->SetRenderMode( kRenderTransTexture );
			pEnt->SetRenderAlpha( 255 - m_FogOfWarTextureData[FOWINDEX(pEnt->m_iFOWPosX, pEnt->m_iFOWPosY)] );
		}
		else if( pEnt->GetRenderMode() != kRenderNormal )
		{
			pEnt->SetRenderMode( kRenderNormal );
			pEnt->SetRenderAlpha( 255 );
		}
#endif // 0

#if 0
		infow = m_FogOfWarTextureData[FOWINDEX(pEnt->m_iFOWPosX, pEnt->m_iFOWPosY)] < 10;
#else
		infow = m_FogOfWar[FOWINDEX(pEnt->m_iFOWPosX, pEnt->m_iFOWPosY)] < 10;
#endif // 0
		if( infow != pEnt->IsInFOW() )
		{
			pEnt->SetInFOW( infow );
			pEnt->UpdateVisibility();
		}

		if( (pEnt->GetFOWFlags() & FOWFLAG_HIDDEN) != 0 )
		{
			if( infow )
			{
				if( !pEnt->IsDormant() )
				{
					pEnt->SetDormant(true);
				}
			}
			else
			{
				if( pEnt->IsDormant() && pEnt->GetLastShouldTransmitState() == SHOULDTRANSMIT_START )
				{
					pEnt->SetDormant(false);
				}
			}
		}
#else
		bool bNeedsUpdateTransmitState = false;
		for( int j=0; j<FOWMAXPLAYERS; j++ )
		{
			infow = (m_FogOfWar[FOWINDEX(pEnt->m_iFOWPosX, pEnt->m_iFOWPosY)] & (1 << j)) == 0;
			if( infow != pEnt->IsInFOW(j) )
			{
				if( g_debug_fogofwar.GetBool() )
					Msg("#%d Ent %s changed to fow status %d for owner %d (tile: %d %d)\n", pEnt->entindex(), pEnt->GetClassname(), infow, j, pEnt->m_iFOWPosX, pEnt->m_iFOWPosY);
				pEnt->m_bInFOW[j] = infow;
				bNeedsUpdateTransmitState = true;
			}
		}
		if( bNeedsUpdateTransmitState )
			pEnt->DispatchUpdateTransmitState();
#endif // CLIENT_DLL
	}
}

//-----------------------------------------------------------------------------
// Purpose: Adds an entity which can clear the fog of war for a player/owner.
//-----------------------------------------------------------------------------
void CFogOfWarMgr::AddFogUpdater(int owner, CBaseEntity *pEnt )
{ 
	if( !pEnt )
		return;
	const Vector &origin = pEnt->GetAbsOrigin();
	if( m_nGridSize != -1 )
	{
		ComputeFOWPosition( origin, pEnt->m_iFOWPosX, pEnt->m_iFOWPosY );
	}
	else
	{
		pEnt->m_iFOWPosX = pEnt->m_iFOWPosY = -1;
	}
	FOWAddEntity(&m_pFogUpdaterListHead, owner, pEnt); 
}

//-----------------------------------------------------------------------------
// Purpose: Removes an entity which can clear the fog of war for a player/owner.
//-----------------------------------------------------------------------------
void CFogOfWarMgr::RemoveFogUpdater(int owner, CBaseEntity *pEnt ) 
{ 
	if( !pEnt ) 
		return;

	int radius;
	radius = pEnt->GetViewDistance() / m_nTileSize;

	// NOTE: Fog of war array is always reset completely, so no need to hide the fog

	FOWRemoveEntity(&m_pFogUpdaterListHead, owner, pEnt); 
}

//-----------------------------------------------------------------------------
// Purpose: Adds an entity which is affected by the fog of war (not visible/transmitted)
//-----------------------------------------------------------------------------
void CFogOfWarMgr::AddFogEntity(CBaseEntity *pEnt)
{
	if( m_FogEntities.Find(pEnt) == -1 )
	{
		m_FogEntities.AddToTail(pEnt);
		pEnt->m_iFOWPosX = pEnt->m_iFOWPosY = -1;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Removes an entity which is affected by the fog of war (not visible/transmitted)
//-----------------------------------------------------------------------------
void CFogOfWarMgr::RemoveFogEntity(CBaseEntity *pEnt )
{
	m_FogEntities.FindAndRemove(pEnt);
}

#if 0
//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFogOfWarMgr::AddSpecialUpdater(  Vector origin, float radius, float delay, float lifetime, int owner )
{
	fogofwar_t data;
	data.m_iX = (origin.x + (FOW_WORLDSIZE / 2)) / (float)m_nTileSize;
	data.m_iY = (origin.y + (FOW_WORLDSIZE / 2)) / (float)m_nTileSize;
	data.m_iRadius = radius / m_nTileSize;
	data.m_iOwnerNumber = owner;
	data.m_fDelay = gpGlobals->curtime + delay;
	data.m_fLifeTime = gpGlobals->curtime + lifetime;
	AddSpecialUpdater(data);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFogOfWarMgr::AddSpecialUpdater( fogofwar_t data )
{
	m_SpecialUpdateList.AddToTail(data);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFogOfWarMgr::UpdateSpecialUpdateList()
{
	int i;
	for(i=0; i < m_SpecialUpdateList.Count(); i++ )
	{
		if( m_SpecialUpdateList.Element(i).m_fDelay > gpGlobals->curtime )
			continue;

		if( m_SpecialUpdateList.Element(i).m_fLifeTime >= 0.0 && m_SpecialUpdateList.Element(i).m_fLifeTime < gpGlobals->curtime )
		{
			if( m_SpecialUpdateList.Element(i).m_iOwnerNumber >= 0 &&
					m_SpecialUpdateList.Element(i).m_iOwnerNumber < MAX_PLAYERS )
			{
				UpdateFOWAt( m_SpecialUpdateList.Element(i).m_iOwnerNumber, m_SpecialUpdateList.Element(i).m_iX, 
					m_SpecialUpdateList.Element(i).m_iY, m_SpecialUpdateList.Element(i).m_iRadius, true );
			}
			m_SpecialUpdateList.Remove(i);
			continue;
		}
		if( m_SpecialUpdateList.Element(i).m_iOwnerNumber >= 0 &&
			m_SpecialUpdateList.Element(i).m_iOwnerNumber < MAX_PLAYERS )
		{
			UpdateFOWAt( m_SpecialUpdateList.Element(i).m_iOwnerNumber, m_SpecialUpdateList.Element(i).m_iX, 
			m_SpecialUpdateList.Element(i).m_iY, m_SpecialUpdateList.Element(i).m_iRadius, false );
		}
	}
}
#endif // 0

//-----------------------------------------------------------------------------
// Purpose: Lists all entities updating the/or being affected by the fog of war.
//-----------------------------------------------------------------------------
void CFogOfWarMgr::DebugPrintEntities()
{
	CBaseEntity *pEnt;
	int i;

	Msg("Fog of war Updaters:\n");
	for( FOWListInfo *pFOWList=m_pFogUpdaterListHead; pFOWList; pFOWList=pFOWList->m_pNext )
	{
		for( i=0; i<pFOWList->m_EntityList.Count(); i++ )
		{
			pEnt =  pFOWList->m_EntityList.Element(i);
			if( !pEnt )
				continue;

			Msg("\t#%d - classname: %s - owner: %d - flags: %d - fowpos: %d,%d\n", 
				pEnt->entindex(), pEnt->GetClassname(), pEnt->GetOwnerNumber(), 
				pEnt->GetFOWFlags(), pEnt->m_iFOWPosX, pEnt->m_iFOWPosY );
		}
	}

	Msg("Fog of war affected entities:\n");
	for(i=0; i<m_FogEntities.Count(); i++)
	{
		pEnt = m_FogEntities[i];
		if( !pEnt )
			continue;

		Msg("\t#%d - classname: %s - owner: %d - flags: %d - fowpos: %d,%d\n", 
			pEnt->entindex(), pEnt->GetClassname(), pEnt->GetOwnerNumber(), 
			pEnt->GetFOWFlags(), pEnt->m_iFOWPosX, pEnt->m_iFOWPosY );
	}
}

#ifdef CLIENT_DLL
CON_COMMAND_F( fow_print_cliententities, "", FCVAR_CHEAT)
#else
CON_COMMAND_F( fow_print_serverentities, "", FCVAR_CHEAT)
#endif // CLIENT_DLL
{
	FogOfWarMgr()->DebugPrintEntities();	
}