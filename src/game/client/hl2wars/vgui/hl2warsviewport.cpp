//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Client DLL VGUI2 Viewport
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"

#pragma warning( disable : 4800  )  // disable forcing int to bool performance warning

// VGUI panel includes
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include <KeyValues.h>
#include <vgui/Cursor.h>
#include <vgui/IScheme.h>
#include <vgui/IVGUI.h>
#include <vgui/ILocalize.h>
#include <vgui/VGUI.h>
#include <vgui/IInput.h>

// client dll/engine defines
#include "hud.h"
#include <voice_status.h>

// viewport definitions
#include <baseviewport.h>
#include "hl2warsViewport.h"
#include "hl2wars_scoreboard.h"

#include "vguicenterprint.h"
#include "text_message.h"
#include "c_hl2wars_player.h"
#include "in_buttons.h"
#include "imouse.h"
#include "hl2wars_shareddefs.h"
#include "wars_mapboundary.h"
#include "iunit.h"

#ifndef DISABLE_PYTHON
	#include "src_python.h"
#endif // DISABLE_PYTHON

extern ConVar cl_leveloverviewmarker;

#if 0
CON_COMMAND_F( spec_help, "Show spectator help screen", FCVAR_CLIENTCMD_CAN_EXECUTE)
{
	if ( gViewPortInterface )
		gViewPortInterface->ShowPanel( PANEL_INFO, true );
}

CON_COMMAND_F( spec_menu, "Activates spectator menu", FCVAR_CLIENTCMD_CAN_EXECUTE)
{
	bool bShowIt = true;

	C_HL2WarsPlayer *pPlayer = C_HL2WarsPlayer::GetLocalHL2WarsPlayer();

	if ( pPlayer && !pPlayer->IsObserver() )
		return;

	if ( args.ArgC() == 2 )
	{
		 bShowIt = atoi( args[ 1 ] ) == 1;
	}
	
	if ( gViewPortInterface )
		gViewPortInterface->ShowPanel( PANEL_SPECMENU, bShowIt );
}

CON_COMMAND_F( togglescores, "Toggles score panel", FCVAR_CLIENTCMD_CAN_EXECUTE)
{
	if ( !gViewPortInterface )
		return;
	
	IViewPortPanel *scoreboard = gViewPortInterface->FindPanelByName( PANEL_SCOREBOARD );

	if ( !scoreboard )
		return;

	if ( scoreboard->IsVisible() )
	{
		gViewPortInterface->ShowPanel( scoreboard, false );
		GetClientVoiceMgr()->StopSquelchMode();
	}
	else
	{
		gViewPortInterface->ShowPanel( scoreboard, true );
	}
}
#endif // 0

void DiffSelection( CUtlVector< EHANDLE > &newselection, CUtlVector< EHANDLE > &oldselection, CUtlVector< EHANDLE > &newunits, CUtlVector< EHANDLE > &removedunits )
{
	// TODO: Check performance for large selections
	int i;
	CBaseEntity *pUnit;
	for( i = 0; i < newselection.Count(); i++ )
	{
		pUnit = newselection[i];
		if( oldselection.HasElement( pUnit ) )
			continue;
		newunits.AddToTail( pUnit );
	}

	for( i = 0; i < oldselection.Count(); i++ )
	{
		pUnit = oldselection[i];
		if( newselection.HasElement( pUnit ) )
			continue;
		removedunits.AddToTail( pUnit );
	}
}

HL2WarsViewport::HL2WarsViewport() : CBaseViewport()
{
	MakePopup( false );

	SetKeyBoardInputEnabled( false );
	SetMouseInputEnabled( false );

	m_nMouseButtons = 0;
	m_bDrawingSelectBox = false;
	m_bMiddleMouseActive = false;
}

HL2WarsViewport::~HL2WarsViewport()
{
	if ( input()->GetAppModalSurface() == GetVPanel() )
	{
		vgui::input()->ReleaseAppModalSurface();
	}
}

void HL2WarsViewport::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

	GetHud().InitColors( pScheme );

	SetPaintBackgroundEnabled( false );

	// dc_arrow is translated in Panel::InternalSetCursor, so it works everywhere properly.
	m_iDefaultMouseCursor = dc_arrow; //vgui::surface()->CreateCursorFromFile( "resource/arrows/default_cursor.cur" );

	SetCursor( m_iDefaultMouseCursor );
}


IViewPortPanel* HL2WarsViewport::CreatePanelByName(const char *szPanelName)
{
	IViewPortPanel* newpanel = NULL;

	if ( Q_strcmp( PANEL_SCOREBOARD, szPanelName) == 0 )
	{
		newpanel = new CHL2WarsScoreboard( this );
	}
	else
	{
		// create a generic base panel, don't add twice
		newpanel = BaseClass::CreatePanelByName( szPanelName );
	}

	return newpanel; 
}

void HL2WarsViewport::CreateDefaultPanels( void )
{
	AddNewPanel( CreatePanelByName( PANEL_SCOREBOARD ), "PANEL_SCOREBOARD" );
	AddNewPanel( CreatePanelByName( PANEL_INFO ), "PANEL_INFO" );
	//AddNewPanel( CreatePanelByName( PANEL_SPECGUI ), "PANEL_SPECGUI" );
	//AddNewPanel( CreatePanelByName( PANEL_SPECMENU ), "PANEL_SPECMENU" );
	AddNewPanel( CreatePanelByName( PANEL_NAV_PROGRESS ), "PANEL_NAV_PROGRESS" );
	// AddNewPanel( CreatePanelByName( PANEL_TEAM ), "PANEL_TEAM" );
	// AddNewPanel( CreatePanelByName( PANEL_CLASS ), "PANEL_CLASS" );
	// AddNewPanel( CreatePanelByName( PANEL_BUY ), "PANEL_BUY" );
}

int HL2WarsViewport::GetDeathMessageStartHeight( void )
{
	int x = YRES(2);

	IViewPortPanel *spectator = FindPanelByName( PANEL_SPECGUI );

	//TODO: Link to actual height of spectator bar
	if ( spectator && spectator->IsVisible() )
	{
		x += YRES(52);
	}

	return x;
}

#include <windows.h>

void HL2WarsViewport::OnThink()
{
	BaseClass::OnThink();

	// Super lame fix to ensure this panel is always behind all other panels
	surface()->MovePopupToBack(GetVPanel());

	/*
	
	bool bHadFocus = HasFocus();
	surface()->MovePopupToBack(GetVPanel());
	if( bHadFocus )
	{
		Msg("Restoring focus viewport\n");
		RequestFocus();
		surface()->SetTopLevelFocus( GetVPanel() );
	}
	*/

	/*
	bool bIsTopMost = ipanel()->IsTopmostPopup(GetVPanel());
	surface()->MovePopupToBack(GetVPanel());
	if( bIsTopMost )
	{
		Msg("Restoring focus viewport\n");
		ipanel()->SetTopmostPopup( GetVPanel(), true );
	}
	*/

	//if( vgui::input()->GetMouseOver() == GetVPanel() )
	{
		UpdateCursor();
		//Msg("Updating cursor %f, %d == %d\n", gpGlobals->curtime, vgui::input()->GetMouseOver(), GetVPanel());
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void HL2WarsViewport::GetPointInScreen( Vector2D *point, Vector *world )
{
	int x, y;
	GetVectorInScreenSpace( *world, x, y);

	point->x = x;
	point->y = y;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void HL2WarsViewport::DrawMapBounderies()
{
	for( CBaseFuncMapBoundary *pEnt = GetMapBoundaryList(); pEnt != NULL; pEnt = pEnt->m_pNext )
	{
		Vector point1, point2, point3, point4;
		Vector2D point2d1, point2d2, point2d3, point2d4; 
		Vector mins, maxs;
		pEnt->GetMapBoundary( mins, maxs );

		point1[2] = point2[2] = point3[2] = point4[2] = (pEnt->GetAbsOrigin())[2];

		point1[0] = mins[0];
		point1[1] = mins[1];
		point2[0] = mins[0];
		point2[1] = maxs[1];
		point3[0] = maxs[0];
		point3[1] = maxs[1];
		point4[0] = maxs[0];
		point4[1] = mins[1];

		GetPointInScreen( &point2d1, &point1 );
		GetPointInScreen( &point2d2, &point2 );
		GetPointInScreen( &point2d3, &point3 );
		GetPointInScreen( &point2d4, &point4 );

		surface()->DrawSetColor( 0,0,255,200 );

		surface()->DrawLine( point2d1.x, point2d1.y, point2d2.x, point2d2.y );
		surface()->DrawLine( point2d2.x, point2d2.y, point2d3.x, point2d3.y );
		surface()->DrawLine( point2d3.x, point2d3.y, point2d4.x, point2d4.y );
		surface()->DrawLine( point2d4.x, point2d4.y, point2d1.x, point2d1.y );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void HL2WarsViewport::Paint()
{
	BaseClass::Paint();

	// Draw extra stuff
	if ( cl_leveloverviewmarker.GetInt() > 0 )
	{
		int size = cl_leveloverviewmarker.GetInt();

		// draw a circle
		int radius = (int)( (float)size / 2.0f );
		vgui::surface()->DrawSetColor( 0, 255, 0, 255 );
		vgui::surface()->DrawOutlinedCircle( radius, radius, radius, 32 );

		// draw a square in the circle ( this is our playfield )
		int length = sqrt( pow( (double)radius, 2 ) + pow( (double)radius, 2 ) );
		int pos = (int)( (size - length) / 2.0f );

		vgui::surface()->DrawSetColor( 0, 255, 255, 255 );
		vgui::surface()->DrawOutlinedRect( pos, pos, length + pos, length + pos );

		// make life some easier by drawing the map bounderies
		DrawMapBounderies();
	}

	// get local player
	C_HL2WarsPlayer* pPlayer = C_HL2WarsPlayer::GetLocalHL2WarsPlayer();
	if( !pPlayer || (!pPlayer->IsStrategicModeOn() && !(pPlayer->m_nButtons & IN_SPEED) ) )
		return;

#ifndef DISABLE_PYTHON
	if( pPlayer->GetSingleActiveAbility().ptr() != Py_None ) 
	{
		SrcPySystem()->Run( SrcPySystem()->Get("Paint", pPlayer->GetSingleActiveAbility()) );
	}
	else if( pPlayer->GetMouseCapture() != NULL )
#else
	if( pPlayer->GetMouseCapture() != NULL )
#endif // #endif // DISABLE_PYTHON
	{
		pPlayer->GetMouseCapture()->GetIMouse()->OnHoverPaint();
	}
	else if( pPlayer->GetMouseData().m_hEnt.Get() && pPlayer->GetMouseData().m_hEnt.Get()->GetIMouse() )	 
	{ 
		DrawSelectBox();

		if( !m_bDrawingSelectBox )
		{
			// If there is an entity with a mouse interface under our pointer call Paint
			pPlayer->GetMouseData().m_hEnt.Get()->GetIMouse()->OnHoverPaint();
		}
	}
	else
	{
		DrawSelectBox();
	}
}

extern ConVar cl_mouse_selectionbox_threshold;
void HL2WarsViewport::DrawSelectBox()
{
	C_HL2WarsPlayer* pPlayer = C_HL2WarsPlayer::GetLocalHL2WarsPlayer();

	if( !pPlayer->IsLeftPressed() )
	{
		ClearSelectionBox();
		return;
	}

	// Must be higher than the threshold
	const MouseTraceData_t &leftpressed =  pPlayer->GetMouseDataLeftPressed();
	const MouseTraceData_t &curdata =  pPlayer->GetMouseData();
	if( abs( leftpressed.m_iX - curdata.m_iX ) <= XRES( cl_mouse_selectionbox_threshold.GetInt() ) ||
		abs( leftpressed.m_iY - curdata.m_iY ) <= YRES( cl_mouse_selectionbox_threshold.GetInt() ) )
	{
		ClearSelectionBox();
		return;
	}

	// Draw the selection box
	short xmin, ymin, xmax, ymax;
	xmin = MIN( leftpressed.m_iX, curdata.m_iX );
	xmax = MAX( leftpressed.m_iX, curdata.m_iX );
	ymin = MIN( leftpressed.m_iY, curdata.m_iY );
	ymax = MAX( leftpressed.m_iY, curdata.m_iY );

	vgui::surface()->DrawSetColor( Color( 0, 0, 0, 115 ) );
	vgui::surface()->DrawOutlinedRect( xmin, ymin, xmax, ymax );
	vgui::surface()->DrawSetColor( Color( 75, 75, 75, 90 ) );
	vgui::surface()->DrawFilledRect( xmin, ymin, xmax, ymax ); 

	UpdateSelectionBox( xmin, ymin, xmax, ymax );
}

void HL2WarsViewport::UpdateSelectionBox( int iXMin, int iYMin, int iXMax, int iYMax )
{
	int i;
	CUtlVector< EHANDLE > newselection;
	CUtlVector< EHANDLE > newunits;
	CUtlVector< EHANDLE > removedunits;
	CBaseEntity *pUnit;

	C_HL2WarsPlayer* pPlayer = C_HL2WarsPlayer::GetLocalHL2WarsPlayer();

	pPlayer->GetBoxSelection( iXMin, iYMin, iXMax, iYMax, newselection );
	DiffSelection( newselection, m_InSelectionBox, newunits, removedunits );

	for( i = 0; i < newunits.Count(); i++ )
	{
		pUnit = newunits[i];
		if( !pUnit || !pUnit->GetIUnit() )
			continue;
		pUnit->GetIUnit()->OnInSelectionBox();
	}

	for( i = 0; i < removedunits.Count(); i++ )
	{
		pUnit = removedunits[i];
		if( !pUnit || !pUnit->GetIUnit() )
			continue;
		pUnit->GetIUnit()->OnOutSelectionBox();
	}

	m_InSelectionBox = newselection;
	m_bDrawingSelectBox = true;
}

void HL2WarsViewport::ClearSelectionBox()
{
	if( !m_bDrawingSelectBox )
		return;

	CBaseEntity *pUnit;
	int i;
	for( i = 0; i < m_InSelectionBox.Count(); i++ )
	{
		pUnit = m_InSelectionBox[i];
		if( !pUnit || !pUnit->GetIUnit() )
			continue;
		pUnit->GetIUnit()->OnOutSelectionBox();
	}

	m_InSelectionBox.RemoveAll();
	m_bDrawingSelectBox = false;
}

void HL2WarsViewport::UpdateCursor()
{
	if( m_bMiddleMouseActive )
	{
		SetCursor( dc_blank );
		return;
	}

	// get local player
	C_HL2WarsPlayer* pPlayer = C_HL2WarsPlayer::GetLocalHL2WarsPlayer();
	if( !pPlayer )
		return;

#ifndef DISABLE_PYTHON
	// Active abilities are override the cursor as first
	if( pPlayer->GetSingleActiveAbility().ptr() != Py_None )
	{
		SetCursor( SrcPySystem()->RunT<unsigned long>( SrcPySystem()->Get("GetCursor", pPlayer->GetSingleActiveAbility()), m_iDefaultMouseCursor ) );
		return;
	}
#endif // DISABLE_PYTHON

	// If there is an entity with a mouse interface under our pointer it overrides our cursor
	if( pPlayer->GetMouseData().m_hEnt.Get() && pPlayer->GetMouseData().m_hEnt.Get()->GetIMouse() )
	{
		SetCursor( pPlayer->GetMouseData().m_hEnt.Get()->GetIMouse()->GetCursor() );
		return;
	}

	// Update cursors
	SetCursor( m_iDefaultMouseCursor );
}

// mouse listeners for the panel
void HL2WarsViewport::OnCursorMoved(int x, int y)
{
	m_iMouseMoveX = x;
	m_iMouseMoveY = y;
}

void HL2WarsViewport::OnCursorEntered()
{
}

void HL2WarsViewport::OnCursorExited()
{
}

//-----------------------------------------------------------------------------
// Purpose:  Sends the mouse clicks to the server side.
//-----------------------------------------------------------------------------
void HL2WarsViewport::OnMousePressed( MouseCode code )
{
	C_HL2WarsPlayer *pPlayer = C_HL2WarsPlayer::GetLocalHL2WarsPlayer();
	if( !pPlayer )
		return;

	switch( code ) {
		case MOUSE_LEFT:
			pPlayer->OnLeftMouseButtonPressedInternal( pPlayer->GetMouseData() );
			m_nMouseButtons |= IN_MOUSELEFT;
			break;
		case MOUSE_RIGHT:
			pPlayer->OnRightMouseButtonPressedInternal( pPlayer->GetMouseData() );
			m_nMouseButtons |= IN_MOUSERIGHT;
			break;
		case MOUSE_MIDDLE:
			m_bMiddleMouseActive = true;
			break;
	}	

	// Make sure the released is called to this panel
	vgui::input()->SetMouseCaptureEx(GetVPanel(), code);
}

//-----------------------------------------------------------------------------
// Purpose:  Sends the mouse clicks to the server side.
//-----------------------------------------------------------------------------
void HL2WarsViewport::OnMouseDoublePressed(MouseCode code)
{
	C_HL2WarsPlayer *pPlayer = C_HL2WarsPlayer::GetLocalHL2WarsPlayer();
	if( !pPlayer )
		return;

	switch( code ) {
		case MOUSE_LEFT:
			pPlayer->OnLeftMouseButtonDoublePressedInternal( pPlayer->GetMouseData() );
			m_nMouseButtons |= IN_MOUSELEFTDOUBLE;
			break;
		case MOUSE_RIGHT:
			pPlayer->OnRightMouseButtonDoublePressedInternal( pPlayer->GetMouseData() );
			m_nMouseButtons |= IN_MOUSERIGHTDOUBLE;
			break;
		case MOUSE_MIDDLE:
			m_bMiddleMouseActive = true;
			break;
	}
}

void HL2WarsViewport::OnMouseTriplePressed(MouseCode code)
{
}

//-----------------------------------------------------------------------------
// Purpose:  Sends the mouse clicks to the server side.
//-----------------------------------------------------------------------------
void HL2WarsViewport::OnMouseReleased(MouseCode code)
{
	C_HL2WarsPlayer *pPlayer = C_HL2WarsPlayer::GetLocalHL2WarsPlayer();
	if( !pPlayer )
		return;

	// clear
	switch( code ) {
		case MOUSE_LEFT:
			pPlayer->OnLeftMouseButtonReleasedInternal( pPlayer->GetMouseData() );
			m_nMouseButtons &= ~(IN_MOUSELEFT|IN_MOUSELEFTDOUBLE);
			break;
		case MOUSE_RIGHT:
			pPlayer->OnRightMouseButtonReleasedInternal( pPlayer->GetMouseData() );
			m_nMouseButtons &= ~(IN_MOUSERIGHT|IN_MOUSERIGHTDOUBLE);
			break;
		case MOUSE_MIDDLE:
			m_bMiddleMouseActive = false;
			break;
	}

	// Stop mouse capture
	vgui::input()->SetMouseCapture(NULL);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void HL2WarsViewport::OnMouseWheeled( int delta )
{
	BaseClass::OnMouseWheeled(delta);

	C_HL2WarsPlayer *pPlayer = C_HL2WarsPlayer::GetLocalHL2WarsPlayer();
	if(!pPlayer)
		return;

	pPlayer->SetScrollTimeOut(delta < 0);
}