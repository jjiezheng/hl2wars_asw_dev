//====== Copyright � 2007-2012 Sandern Corporation, All rights reserved. ======//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "src_python.h"
#include "filesystem.h"
#include "src_python_usermessage.h"
#include "src_python_gamerules.h"
#include "src_python_entities.h"
#include "src_python_networkvar.h"
#include "gamestringpool.h"

#ifdef CLIENT_DLL
	#include "networkstringtable_clientdll.h"
	#include "src_python_materials.h"
#else
	#include "networkstringtable_gamedll.h"
#endif // CLIENT_DLL

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
	extern void	PySetLoadingBackgroundDialog( boost::python::object panel );
	extern void DestroyPyPanels();
#endif // CLIENT_DLL

// For debugging
ConVar g_debug_python( "g_debug_python", "0", FCVAR_REPLICATED );

const Color g_PythonColor( 0, 255, 0, 255 );

// The thread ID in which python is initialized
unsigned int g_hPythonThreadID;

#if defined (PY_CHECK_LOG_OVERRIDES) || defined (_DEBUG)
	ConVar py_log_overrides("py_log_overrides", "0", FCVAR_REPLICATED);
#endif

// Global main space
boost::python::object mainmodule;
boost::python::object mainnamespace;

// Global module references.
bp::object __builtin__;
bp::object types;
bp::object sys;
bp::object srcmgr;
bp::object gamemgr;
bp::object weakref;
bp::object srcbase;
bp::object _entities_misc;
bp::object _entities;
bp::object unit_helper;
bp::object _particles;
bp::object _physics;

#ifdef CLIENT_DLL
	boost::python::object _vguicontrols;
#endif // CLIENT_DLL

static CSrcPython g_SrcPythonSystem;

CSrcPython *SrcPySystem()
{
	return &g_SrcPythonSystem;
}

// Prevent python classes from initializing
bool g_bDoNotInitPythonClasses = true;

void SysAppendPath( const char *path, bool inclsubdirs = false )
{
	SrcPySystem()->SysAppendPath(path, inclsubdirs);
}

#ifdef CLIENT_DLL
extern void HookPyNetworkCls();
#endif // CLIENT_DLL

//-----------------------------------------------------------------------------
// Purpose: Required by boost to be user defined if BOOST_NO_EXCEPTIONS is defined
//			http://www.boost.org/doc/libs/1_49_0/libs/utility/throw_exception.html
//-----------------------------------------------------------------------------
namespace boost
{
	void throw_exception(std::exception const & e)
	{
		Msg("Exception\n");

		FileHandle_t fh = filesystem->Open( "log.txt", "wb" );
		if ( !fh )
		{
			DevWarning( 2, "Couldn't create %s!\n", "log.txt" );
			return;
		}

		filesystem->Write( "Exception", strlen("Exception"), fh );
		filesystem->Close(fh);
	}
}

using namespace boost::python;

// Append functions
#ifdef CLIENT_DLL
extern void AppendClientModules();
#else
extern void AppendServerModules();
#endif // CLIENT_DLL
extern void AppendSharedModules();

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CSrcPython::CSrcPython()
{
	m_bPythonRunning = false;
	m_bPythonIsFinalizing = false;

	double fStartTime = Plat_FloatTime();
	// Before the python interpreter is initialized, the modules must be appended
#ifdef CLIENT_DLL
	AppendClientModules();
#else
	AppendServerModules();
#endif // CLIENT_DLL
	AppendSharedModules();

#ifdef CLIENT_DLL
	DevMsg( "CLIENT: " );
#else
	DevMsg( "SERVER: " );
#endif // CLIENT_DLL
	DevMsg( "Added Python default modules... (%f seconds)\n", Plat_FloatTime() - fStartTime );
}

bool CSrcPython::Init( )
{
	const bool bEnabled = !CommandLine() || CommandLine()->FindParm("-disablepython") == 0;

	if( !bEnabled )
	{
	#ifdef CLIENT_DLL
		ConColorMsg( g_PythonColor, "CLIENT: " );
	#else
		ConColorMsg( g_PythonColor, "SERVER: " );
	#endif // CLIENT_DLL
		ConColorMsg( g_PythonColor, "Python is disabled.\n" );
		return true;
	}

	if( m_bPythonRunning )
		return true;

#ifndef _LINUX
	// Change working directory	
	// FIXME: On linux this causes very weird crashes	
	char moddir[_MAX_PATH];
	filesystem->RelativePathToFullPath(".", "MOD", moddir, _MAX_PATH);
	V_FixupPathName(moddir, _MAX_PATH, moddir);	
	V_SetCurrentDirectory(moddir);
#endif // _LINUX

	m_bPythonRunning = true;

	double fStartTime = Plat_FloatTime();

	// Initialize an interpreter
	Py_InitializeEx( 0 );
#ifdef CLIENT_DLL
	ConColorMsg( g_PythonColor, "CLIENT: " );
#else
	ConColorMsg( g_PythonColor, "SERVER: " );
#endif // CLIENT_DLL
	ConColorMsg( g_PythonColor, "Initialized Python... (%f seconds)\n", Plat_FloatTime() - fStartTime );
	fStartTime = Plat_FloatTime();

	// Save our thread ID
#ifdef _WIN32
	g_hPythonThreadID = GetCurrentThreadId();
#endif // _WIN32

	// get our main space
	try {
		mainmodule = import("__main__");
		mainnamespace = mainmodule.attr("__dict__");
	} catch( error_already_set & ) {
		Warning("Failed to import main namespace!\n");
		PyErr_Print();
		return false;
	}

	// Redirect print
	// TODO: Integrate this into python.
	Run( "import redirect" );

	// Import sys module
	Run( "import sys" );
	weakref = Import("weakref");
	sys = Import("sys");

	srcbase = Import("srcbase");
	__builtin__ = Import("__builtin__");

	// Set isclient and isserver globals to the right values
	try {
#ifdef CLIENT_DLL
		__builtin__.attr("isclient") = true;
#else
		__builtin__.attr("isserver") = true;
#endif // CLIENT_DLL
		__builtin__.attr("gpGlobals") = srcbase.attr("gpGlobals");
	} catch( error_already_set & ) {
		PyErr_Print();
	}

	// Add the maps directory to the modulse path
	SysAppendPath("maps");
	SysAppendPath("python//base");
	SysAppendPath("python//srclib");

	srcmgr = Import("srcmgr");
	Run( "import srcmgr" );

	// Default imports
	Run( "from srcbase import *" );
	Run( "from vmath import *" );
	
	types = Import("types");
	Run( "import sound" ); // Import _sound before _entities_misc (register converters)
	Run( "import _entities_misc" );
	_entities_misc = Import("_entities_misc");
	Run( "import _entities" );
	_entities = Import("_entities");
	unit_helper = Import("unit_helper");
	_particles = Import("_particles");
	_physics = Import("_physics");
#ifdef CLIENT_DLL
	Run( "import input" );		// Registers buttons
	_vguicontrols = Import("_vguicontrols");
#endif	// CLIENT_DLL

	//  initialize the module that manages the python side
#ifndef CLIENT_DLL
	Run( Get("_Init", "srcmgr", true) );
#else
	Run( Get("_Init", "srcmgr", true) );
#endif	// CLIENT_DLL

#ifdef CLIENT_DLL
	DevMsg( "CLIENT: " );
#else
	DevMsg( "SERVER: " );
#endif // CLIENT_DLL
	DevMsg( "Initialized Python default modules... (%f seconds)\n", Plat_FloatTime() - fStartTime );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSrcPython::Shutdown( void )
{
	if( !m_bPythonRunning )
		return;

#ifdef CLIENT_DLL
	PyShutdownProceduralMaterials();
#endif // CLIENT_DLL

	PyErr_Clear(); // Make sure it does not hold any references...
	GarbageCollect();
}

#ifdef WIN32
extern "C" {
	void PyImport_FreeDynLibraries( void );
}
#endif // WIN32

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSrcPython::ExtraShutdown( void )
{
	if( !m_bPythonRunning )
		return;

#ifdef CLIENT_DLL
	// Clear loading dialog
	PySetLoadingBackgroundDialog( bp::object() );

	// Clear python panels
	DestroyPyPanels();
#endif // CLIENT_DLL

	// Clear Python gamerules
	ClearPyGameRules();

	// Make sure these lists don't hold references
	m_deleteList.Purge();
	m_methodTickList.Purge();
	m_methodPerFrameList.Purge();

	// Clear modules
	mainmodule = bp::object();
	mainnamespace = bp::object();

	__builtin__ = bp::object();
	sys = bp::object();
	types = bp::object();
	srcmgr = bp::object();
	gamemgr = bp::object();
	weakref = bp::object();
	srcbase = bp::object();
	_entities_misc = bp::object();
	_entities = bp::object();
	unit_helper = bp::object();
	_particles = bp::object();
	_physics = bp::object();
#ifdef CLIENT_DLL
	_vguicontrols = bp::object();
#endif // CLIENT_DLL

	// Finalize
	m_bPythonIsFinalizing = true;
	PyErr_Clear(); // Make sure it does not hold any references...
	Py_Finalize();
#ifdef WIN32
	PyImport_FreeDynLibraries(); // IMPORTANT, otherwise it will crash.
#endif // WIN32
	m_bPythonIsFinalizing = false;
	m_bPythonRunning = false;

#ifdef CLIENT_DLL
	ConColorMsg( g_PythonColor, "CLIENT: " );
#else
	ConColorMsg( g_PythonColor, "SERVER: " );
#endif // CLIENT_DLL
	ConColorMsg( g_PythonColor, "Python is no longer running...\n" );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSrcPython::PostInit()
{
	if( !IsPythonRunning() )
		return;

	// Hook PyMessage
#ifdef CLIENT_DLL
	HookPyMessage();
	HookPyNetworkCls();
	HookPyNetworkVar();
#endif // CLIENT_DLL

	// Gamemgr manages all game packages
	Run( "import gamemgr" );
	gamemgr = Import("gamemgr");

	// Autorun once
	ExecuteAllScriptsInPath("python/autorun_once/");
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSrcPython::LevelInitPreEntity()
{
	m_bActive = true;

	if( !IsPythonRunning() )
		return;

#ifdef CLIENT_DLL
	char pLevelName[_MAX_PATH];
	Q_FileBase(engine->GetLevelName(), pLevelName, _MAX_PATH);
#else
	const char *pLevelName = STRING(gpGlobals->mapname);
#endif

	m_LevelName = AllocPooledString(pLevelName);

	// BEFORE creating the entities setup the network tables
#ifndef CLIENT_DLL
	SetupNetworkTables();
#endif // CLIENT_DLL

	// srcmgr level init
	Run<const char *>( Get("_LevelInitPreEntity", "srcmgr", true), pLevelName );
	
	// Send prelevelinit signal
	try {
		CallSignalNoArgs( Get("prelevelinit", "core.signals", true) );
		CallSignalNoArgs( Get("map_prelevelinit", "core.signals", true)[STRING(m_LevelName)] );
	} catch( error_already_set & ) {
		Warning("Failed to retrieve level signal:\n");
		PyErr_Print();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSrcPython::LevelInitPostEntity()
{
	if( !IsPythonRunning() )
		return;

	// srcmgr level init
	Run( Get("_LevelInitPostEntity", "srcmgr", true) );

	// Send postlevelinit signal
	try {
		CallSignalNoArgs( Get("postlevelinit", "core.signals", true) );
		CallSignalNoArgs( Get("map_postlevelinit", "core.signals", true)[STRING(m_LevelName)] );
	} catch( error_already_set & ) {
		Warning("Failed to retrieve level signal:\n");
		PyErr_Print();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSrcPython::LevelShutdownPreEntity()
{
	if( !IsPythonRunning() )
		return;

	// srcmgr level shutdown
	Run( Get("_LevelShutdownPreEntity", "srcmgr", true) );

	// Send prelevelshutdown signal
	try {
		CallSignalNoArgs( Get("prelevelshutdown", "core.signals", true) );
		CallSignalNoArgs( Get("map_prelevelshutdown", "core.signals", true)[STRING(m_LevelName)] );
	} catch( error_already_set & ) {
		Warning("Failed to retrieve level signal:\n");
		PyErr_Print();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSrcPython::LevelShutdownPostEntity()
{
	if( !IsPythonRunning() )
		return;

	// srcmgr level shutdown
	Run( Get("_LevelShutdownPostEntity", "srcmgr", true) );

	// Send postlevelshutdown signal
	try {
		CallSignalNoArgs( Get("postlevelshutdown", "core.signals", true) );
		CallSignalNoArgs( Get("map_postlevelshutdown", "core.signals", true)[STRING(m_LevelName)] );
	} catch( error_already_set & ) {
		Warning("Failed to retrieve level signal:\n");
		PyErr_Print();
	}

	// Reset all send/recv tables
	PyResetAllNetworkTables();
#ifndef CLIENT_DLL
	//SetupNetworkTablesOnHold();
#endif // CLIENT_DLL

	m_bActive = false;
}

static ConVar py_disable_update("py_disable_update", "0", FCVAR_CHEAT|FCVAR_REPLICATED);
extern "C" { void PyEval_RunThreads(); }
#ifdef CLIENT_DLL
void CSrcPython::Update( float frametime )
#else
void CSrcPython::FrameUpdatePostEntityThink( void )
#endif // CLIENT_DLL
{
	if( !IsPythonRunning() )
		return;

	PyEval_RunThreads();  // Force threads to run. TODO: Performance?

	// Update tick methods
	int i;
	for(i=m_methodTickList.Count()-1; i>=0; i--)
	{
		if( m_methodTickList[i].m_fNextTickTime < gpGlobals->curtime )
		{
			try {
				m_methodTickList[i].method();

				// Method might have removed the method already
				if( !m_methodTickList.IsValidIndex(i) )
					continue;

				// Remove tick methods that are not looped (used to call back a function after a set time)
				if( !m_methodTickList[i].m_bLooped )
				{
					m_methodTickList.Remove(i);
					continue;
				}
			} catch( error_already_set & ) {
				Warning("Unregistering tick method due the following exception (catch exception if you don't want this): \n");
				PyErr_Print();
				m_methodTickList.Remove(i);
				continue;
			}
			m_methodTickList[i].m_fNextTickTime = gpGlobals->curtime + m_methodTickList[i].m_fTickSignal;
		}	
	}

	// Update frame methods
	for(i=m_methodPerFrameList.Count()-1; i>=0; i--)
	{
		try {
			m_methodPerFrameList[i]();
		}catch( error_already_set & ) {
			Warning("Unregistering per frame method due the following exception (catch exception if you don't want this): \n");
			PyErr_Print();
			m_methodPerFrameList.Remove(i);
			continue;
		}
	}

#ifdef CLIENT_DLL
	PyUpdateProceduralMaterials();

	CleanupDelayedUpdateList();
#endif // CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
object CSrcPython::Import( const char *pModule )
{
	// Import into the main space
	try
	{
		return import(pModule);
	}
	catch(error_already_set &)
	{
#ifdef CLIENT_DLL
		DevMsg("CLIENT: ImportPyModuleIntern failed -> mod: %s\n", pModule );
#else
		DevMsg("SERVER: ImportPyModuleIntern failed -> mod: %s\n", pModule );
#endif

		PyErr_Print();
	}

	return object();
}

object CSrcPython::ImportSilent( const char *pModule )
{
	// Import into the main space
	try
	{
		return import(pModule);
	}
	catch(error_already_set &)
	{
		PyErr_Clear();
	}

	return object();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
object CSrcPython::Get( const char *pAttrName, object obj, bool bReport )
{
	try {
		return obj.attr(pAttrName);
	} catch(error_already_set &) {
		if( bReport )
			PyErr_Print();	
		else
			PyErr_Clear();
	}
	return bp::object();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
object CSrcPython::Get( const char *pAttrName, const char *pModule, bool bReport )
{
	return Get( pAttrName, Import(pModule), bReport );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSrcPython::Run( bp::object method, bool report_errors )
{
	PYRUNMETHOD( method, report_errors )
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSrcPython::Run( const char *pString, const char *pModule )
{
	// execute statement
	try
	{
		object dict = (pModule ? Import(pModule).attr("__dict__") : mainnamespace);
		exec( pString, dict, dict );
	}
	catch(error_already_set &)
	{
		PyErr_Print();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CSrcPython::ExecuteFile( const char* pScript )
{
	char char_filename[ _MAX_PATH ];
	char char_output_full_filename[ _MAX_PATH ];

	strcpy( char_filename, pScript);
	filesystem->RelativePathToFullPath(char_filename,"MOD",char_output_full_filename,sizeof(char_output_full_filename));

	const char *constcharpointer = reinterpret_cast<const char *>( char_output_full_filename );

	if (!filesystem->FileExists(constcharpointer))
	{
		Warning( "[Python] IFileSystem Cannot find the file: %s\n", constcharpointer);
		return false;
	}

	try
	{
		exec_file(constcharpointer, mainnamespace, mainnamespace );
	}
	catch(error_already_set &)
	{
#ifdef CLIENT_DLL
		DevMsg("CLIENT: ");
#else
		DevMsg("SERVER: ");
#endif // CLIENT_DLL
		DevMsg("RunPythonFile failed -> file: %s\n", pScript );
		PyErr_Print();
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSrcPython::Reload( const char *pModule )
{
	DevMsg("Reloading module %s\n", pModule);

	try
	{
		// import into the main space
		char command[MAX_PATH];
		Q_snprintf( command, sizeof( command ), "import %s", pModule );
		exec(command, mainnamespace, mainnamespace );

		// Reload
		Q_snprintf( command, sizeof( command ), "reload(%s)", pModule );
		exec(command, mainnamespace, mainnamespace );
	}
	catch(error_already_set &)
	{
		PyErr_Print();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Collect garbage
//-----------------------------------------------------------------------------
void CSrcPython::GarbageCollect( void )
{
	PyGC_Collect();
}

//-----------------------------------------------------------------------------
// Purpose: Add a path
//-----------------------------------------------------------------------------
void CSrcPython::SysAppendPath( const char* path, bool inclsubdirs )
{
	// First retrieve the append method
	object append = Get("append", Get("path", "sys", true), true);

	// Fixup path
	char char_output_full_filename[ _MAX_PATH ];
	char p_out[_MAX_PATH];
	filesystem->RelativePathToFullPath(path,"GAME",char_output_full_filename,sizeof(char_output_full_filename));
	V_FixupPathName(char_output_full_filename, _MAX_PATH, char_output_full_filename );
	V_StrSubst(char_output_full_filename, "\\", "//", p_out, _MAX_PATH ); 

	// Append
	Run<const char *>(append, p_out, true);

	// Check for sub dirs
	if( inclsubdirs )
	{
		char wildcard[MAX_PATH];
		FileFindHandle_t findHandle;
		
		Q_snprintf( wildcard, sizeof( wildcard ), "%s//*", path );
		const char *pFilename = filesystem->FindFirstEx( wildcard, "MOD", &findHandle );
		while ( pFilename != NULL )
		{

			if( Q_strncmp(pFilename, ".", 1) != 0 &&
					Q_strncmp(pFilename, "..", 2) != 0 &&
					filesystem->FindIsDirectory(findHandle) ) {
				char path2[MAX_PATH];
				Q_snprintf( path2, sizeof( path2 ), "%s//%s", path, pFilename );
				SysAppendPath(path2, inclsubdirs);
			}
			pFilename = filesystem->FindNext( findHandle );
		}
		filesystem->FindClose( findHandle );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Create a weakref using the weakref module
//-----------------------------------------------------------------------------
object CSrcPython::CreateWeakRef( object obj_ref )
{
	try
	{
		 return weakref.attr("ref")(obj_ref);
	}
	catch(error_already_set &)
	{
		PyErr_Print();
	}
	return object();
}

//-----------------------------------------------------------------------------
// Purpose: Execute all python files in a folder
//-----------------------------------------------------------------------------
void CSrcPython::ExecuteAllScriptsInPath( const char *pPath )
{
	char tempfile[MAX_PATH];
	char wildcard[MAX_PATH];

	Q_snprintf( wildcard, sizeof( wildcard ), "%s*.py", pPath );

	FileFindHandle_t findHandle;
	const char *pFilename = filesystem->FindFirstEx( wildcard, "GAME", &findHandle );
	while ( pFilename != NULL )
	{
		Q_snprintf( tempfile, sizeof( tempfile ), "%s/%s", pPath, pFilename );
		ExecuteFile(tempfile);
		pFilename = filesystem->FindNext( findHandle );
	}
	filesystem->FindClose( findHandle );
}

//-----------------------------------------------------------------------------
// Purpose: Identifier between server and client
//-----------------------------------------------------------------------------
int CSrcPython::GetModuleIndex( const char *pModule )
{
	if ( pModule )
	{
		int nIndex = g_pStringTablePyModules->FindStringIndex( pModule );
		if (nIndex != INVALID_STRING_INDEX ) 
			return nIndex;

		return g_pStringTablePyModules->AddString( CBaseEntity::IsServer(), pModule );
	}

	// This is the invalid string index
	return INVALID_STRING_INDEX;
}

const char * CSrcPython::GetModuleNameFromIndex( int nModuleIndex )
{
	if ( nModuleIndex >= 0 && nModuleIndex < g_pStringTablePyModules->GetMaxStrings() )
		return g_pStringTablePyModules->GetString( nModuleIndex );
	return "error";
}

void CSrcPython::CallSignalNoArgs( bp::object signal )
{
	try {
		srcmgr.attr("_CheckReponses")( 
			signal.attr("send_robust")( bp::object() )
		);
	} catch( error_already_set & ) {
		Warning("Failed to call signal:\n");
		PyErr_Print();
	}
}

void CSrcPython::CallSignal( bp::object signal, bp::dict kwargs )
{
	try {
		srcmgr.attr("_CallSignal")( bp::object(signal.attr("send_robust")), kwargs );
	} catch( error_already_set & ) {
		Warning("Failed to call signal:\n");
		PyErr_Print();
	}
}

#if 0
void CSrcPython::SignalCheckResponses( PyObject *pResponses )
{
	// TODO: Write a c version
	try {
		bp::object checkmethod = SrcPySystem()->Get("_CheckReponses", "core.signals", true);
		PyEval_CallObject( checkmethod.ptr(), pResponses );
	} catch( error_already_set & ) {
		Warning("SignalCheckResponses:\n");
		PyErr_Print();
	}
}
#endif // 0

//-----------------------------------------------------------------------------
// Purpose: Retrieving basic type values
//-----------------------------------------------------------------------------
int	CSrcPython::GetInt(const char *name, object obj, int default_value, bool report_error )
{
	return Get<int>(name, obj, default_value, report_error);
}

float CSrcPython::GetFloat(const char *name, object obj, float default_value, bool report_error )
{
	return Get<float>(name, obj, default_value, report_error);
}

const char *CSrcPython::GetString( const char *name, object obj, const char *default_value, bool report_error )
{
	return Get<const char *>(name, obj, default_value, report_error);
}

Vector CSrcPython::GetVector( const char *name, object obj, Vector default_value, bool report_error )
{
	return Get<Vector>(name, obj, default_value, report_error);
}

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
#ifdef CLIENT_DLL
void CSrcPython::AddToDelayedUpdateList( EHANDLE hEnt, char *name, bp::object data, bool callchanged )
{
	CSrcPython::py_delayed_data_update v;
	v.hEnt = hEnt;
	Q_snprintf(v.name, _MAX_PATH, name);
	v.data = data;
	v.callchanged = callchanged;
	py_delayed_data_update_list.AddToTail( v );
}

extern ConVar g_debug_pynetworkvar;
void CSrcPython::CleanupDelayedUpdateList()
{
	for( int i=py_delayed_data_update_list.Count()-1; i >= 0; i-- )
	{
		EHANDLE h = py_delayed_data_update_list[i].hEnt;
		if( h != NULL )
		{	
			if( g_debug_pynetworkvar.GetBool() )
			{
				Msg("#%d Cleaning up delayed PyNetworkVar update %s\n", 
					h.GetEntryIndex(),
					py_delayed_data_update_list[i].name);
			}
			h->PyUpdateNetworkVar( py_delayed_data_update_list[i].name, 
				py_delayed_data_update_list[i].data );

			if( py_delayed_data_update_list[i].callchanged )
				h->PyNetworkVarCallChangedCallback( py_delayed_data_update_list[i].name );

			py_delayed_data_update_list.Remove(i);
		}
	}
}
#endif // CLIENT_DLL

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
void CSrcPython::RegisterTickMethod( bp::object method, float ticksignal, bool looped )
{
	int i;
	for(i=0; i<m_methodTickList.Count(); i++)
	{
		if( m_methodTickList[i].method == method )
		{
			PyErr_SetString(PyExc_Exception, "Method already registered" );
			throw boost::python::error_already_set(); 
		}
	}
	py_tick_methods tickmethod;
	tickmethod.method = method;
	tickmethod.m_fTickSignal = ticksignal;
	tickmethod.m_fNextTickTime = gpGlobals->curtime + ticksignal;
	tickmethod.m_bLooped = looped;
	m_methodTickList.AddToTail(tickmethod);
}

void CSrcPython::UnregisterTickMethod( bp::object method )
{
	int i;
	for(i=0; i<m_methodTickList.Count(); i++)
	{
		if( m_methodTickList[i].method == method )
		{
			m_methodTickList.Remove(i);
			return;
		}
	}
	PyErr_SetString(PyExc_Exception, "Method not found" );
	throw boost::python::error_already_set(); 
}

boost::python::list CSrcPython::GetRegisteredTickMethods()
{
	boost::python::list methodlist;
	int i;
	for(i=0; i<m_methodTickList.Count(); i++)
	{
		methodlist.append(m_methodTickList[i].method);
	}
	return methodlist;
}

void CSrcPython::RegisterPerFrameMethod( bp::object method )
{
	int i;
	for(i=0; i<m_methodPerFrameList.Count(); i++)
	{
		if( m_methodPerFrameList[i] == method )
		{
			PyErr_SetString(PyExc_Exception, "Method already registered" );
			throw boost::python::error_already_set(); 
		}
	}
	m_methodPerFrameList.AddToTail(method);
}

void CSrcPython::UnregisterPerFrameMethod( bp::object method )
{
	int i;
	for(i=0; i<m_methodPerFrameList.Count(); i++)
	{
		if( m_methodPerFrameList[i] == method )
		{
			m_methodPerFrameList.Remove(i);
			return;
		}
	}
	PyErr_SetString(PyExc_Exception, "Method not found" );
	throw boost::python::error_already_set(); 
}

boost::python::list CSrcPython::GetRegisteredPerFrameMethods()
{
	boost::python::list methodlist;
	int i;
	for(i=0; i<m_methodPerFrameList.Count(); i++)
	{
		methodlist.append(m_methodPerFrameList[i]);
	}
	return methodlist;
}

//-----------------------------------------------------------------------------
// Commands follow here
//-----------------------------------------------------------------------------
#ifndef CLIENT_DLL
CON_COMMAND( py_runfile, "Run a python script")
#else
CON_COMMAND_F( cl_py_runfile, "Run a python script", FCVAR_CHEAT)
#endif // CLIENT_DLL
{
	if( !SrcPySystem()->IsPythonRunning() )
		return;
#ifndef CLIENT_DLL
	if( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
#endif // CLIENT_DLL
	g_SrcPythonSystem.ExecuteFile(args.ArgS());
}

#ifndef CLIENT_DLL
CON_COMMAND( py_run, "Run a string on the python interpreter")
#else
CON_COMMAND_F( cl_py_run, "Run a string on the python interpreter", FCVAR_CHEAT)
#endif // CLIENT_DLL
{
	if( !SrcPySystem()->IsPythonRunning() )
		return;
#ifndef CLIENT_DLL
	if( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
#endif // CLIENT_DLL
	g_SrcPythonSystem.Run(args.ArgS());
}

#ifndef CLIENT_DLL
CON_COMMAND( py_import, "Import a python module")
#else
CON_COMMAND_F( cl_py_import, "Import a python module", FCVAR_CHEAT)
#endif // CLIENT_DLL
{
	if( !SrcPySystem()->IsPythonRunning() )
		return;
#ifndef CLIENT_DLL
	if( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
#endif // CLIENT_DLL
	char command[MAX_PATH];
	Q_snprintf( command, sizeof( command ), "import %s", args.ArgS() );
	g_SrcPythonSystem.Run(command);
}

#ifndef CLIENT_DLL
CON_COMMAND( py_reload, "Reload a python module")
#else
CON_COMMAND_F( cl_py_reload, "Reload a python module", FCVAR_CHEAT)
#endif // CLIENT_DLL
{
	if( !SrcPySystem()->IsPythonRunning() )
		return;
#ifndef CLIENT_DLL
	if( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
#endif // CLIENT_DLL
	g_SrcPythonSystem.Reload(args.ArgS());
}

#ifdef CLIENT_DLL
#include "vgui_controls/Panel.h"


CON_COMMAND_F( test_shared_converters, "Test server converters", FCVAR_CHEAT)
{
	if( !SrcPySystem()->IsPythonRunning() )
		return;
	Msg("Testing keyvalues converter\n");
	KeyValues *pFromPython;
	KeyValues *pToPython = new KeyValues("DataC", "CName1", "CValue1");

	pFromPython = SrcPySystem()->RunT<KeyValues *, KeyValues &>
		( SrcPySystem()->Get("test_keyvalues", "test_converters", true), NULL, *pToPython );

	if( pFromPython )
		Msg("Got keyvalues from python. Name: %s, Value1: %s\n", pFromPython->GetName(), pFromPython->GetString("Name1", ""));
	else
		Msg("No data from python :(\n");

	Msg("Testing string_t converter\n");
	string_t str_t_toPython = MAKE_STRING("Hello there");
	const char *str_from_python;
	str_from_python = SrcPySystem()->RunT<const char *, string_t>
		( SrcPySystem()->Get("test_string_t", "test_converters", true), NULL, str_t_toPython );
	Msg("Return value: %s\n", str_from_python);
}

CON_COMMAND_F( test_client_converters, "Test client converters", FCVAR_CHEAT)
{
	if( !SrcPySystem()->IsPythonRunning() )
		return;
	// Panel
	vgui::Panel *pFromPython;
	vgui::Panel *pToPython = new vgui::Panel(NULL, "PanelBla");

	pFromPython = SrcPySystem()->RunT<vgui::Panel *, vgui::Panel *>
		( SrcPySystem()->Get("test_panel", "test_converters", true), NULL, pToPython );

	if( pFromPython )
		Msg("Got Panel from python\n", pFromPython->GetName() );
	else
		Msg("No data from python :(\n");
}

#endif // CLIENT_DLL
