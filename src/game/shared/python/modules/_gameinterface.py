from generate_mods_helper import GenerateModuleSemiShared, registered_modules
from src_helper import *
import settings

from pygccxml import declarations 
from pyplusplus.module_builder import call_policies
from pyplusplus import function_transformers as FT
from pyplusplus import code_creators

class GameInterface(GenerateModuleSemiShared):
    module_name = '_gameinterface'
    
    if settings.ASW_CODE_BASE:
        client_files = [
            'videocfg/videocfg.h',

            'cbase.h',
            'gamerules.h',
            'multiplay_gamerules.h',
            'teamplay_gamerules.h',
            'hl2wars_gamerules.h',
            'src_python_gamerules.h',
            'c_recipientfilter.h',
            'tier0/icommandline.h',
        ]
        
        server_files = [
            'cbase.h',
            'mathlib/vmatrix.h', 
            'utlvector.h', 
            'shareddefs.h',
            'util.h',
            'iservernetworkable.h',
            #'enginecallback.h',
            'recipientfilter.h',
            'src_python_usermessage.h',
            'hl2wars_gameinterface.h',
            'mapentities.h',
            'tier0/icommandline.h',
        ]
    else:
        client_files = [
            'wchartypes.h',
            'shake.h',
            'cbase.h',
            'gamerules.h',
            'multiplay_gamerules.h',
            'teamplay_gamerules.h',
            'hl2wars_gamerules.h',
            'src_python_gamerules.h',
            'c_recipientfilter.h',
        ]
    
        server_files = [
            'cbase.h',
            'mathlib/vmatrix.h', 
            'utlvector.h', 
            'shareddefs.h',
            'util.h',
            'iservernetworkable.h',
            'recipientfilter.h',
            'src_python_usermessage.h',
            'hl2wars_gameinterface.h',
            'mapentities.h',
        ]
    
    files = [
        'convar.h',
        'igameevents.h',
        'irecipientfilter.h',
        'src_python_gameinterface.h',
        'cdll_int.h',
        'wars_mount_system.h',
    ]
    
    def GetFiles(self):
        if self.isClient:
            return self.client_files + self.files 
        return self.server_files + self.files 

    def Parse(self, mb):
        # Exclude everything by default
        mb.decls().exclude() 
        
        # Sys path append
        mb.add_declaration_code( "extern void SysAppendPath(const char *path, bool inclsubdirs = false);\n" )
        mb.add_registration_code( 'bp::def( "SysAppendPath", SysAppendPath, bp::arg("path"), bp::arg("inclsubdirs") );' )
        
        # Linux model_t fix ( correct? )
        mb.add_declaration_code( '#ifdef _LINUX\r\n' + \
                             'typedef struct model_t {};\r\n' + \
                             '#endif // _LINUX\r\n'
                           )
                           
        # Filesystem functions
        mb.free_function('PyRemoveFile').include()
        mb.free_function('PyRemoveFile').rename('RemoveFile')
        mb.free_function('PyRemoveDirectory').include()
        mb.free_function('PyRemoveDirectory').rename('RemoveDirectory')
        
        # Engine functions
        mb.free_function('GetLevelName').include()    
        
        # Downloadables
        if self.isServer:
            mb.free_function('AddToDownloadables').include()
        
        # Time
        mb.free_function('Plat_FloatTime').include()
        mb.free_function('Plat_MSTime').include()
        
        # Precache functions
        mb.free_function('PrecacheMaterial').include()
        
        # ConVar wrapper
        cls = mb.class_('PyConVar')
        cls.include()
        cls.rename('ConVar')
        cls.mem_funs().virtuality = 'not virtual'
        cls.mem_fun('Shutdown').exclude()
        mb.free_function('PyShutdownConVar').include()
        
        # Don't want to include ConVar, so add methods manually...
        # Can't this be done automatically in py++?
        cls.add_registration_code(
            '''def( 
            "AddFlags"
            , (void ( ::ConVar::* )( int ) )( &::ConVar::AddFlags )
            , ( bp::arg("flags") ) )''')
        cls.add_registration_code(
            '''def( 
            "GetBool"
            , (bool ( ::ConVar::* )(  ) const)( &::ConVar::GetBool ) )''')
        cls.add_registration_code(
            '''def( 
            "GetDefault"
            , (char const * ( ::ConVar::* )(  ) const)( &::ConVar::GetDefault ) )''')
        cls.add_registration_code(
            '''def( 
            "GetFloat"
            , (float ( ::ConVar::* )(  ) const)( &::ConVar::GetFloat ) )''')
        cls.add_registration_code(
            '''def( 
            "GetHelpText"
            , (char const * ( ::ConVar::* )(  ) const)( &::ConVar::GetHelpText ) )''')
        cls.add_registration_code(
            '''def( 
            "GetInt"
            , (int ( ::ConVar::* )(  ) const)( &::ConVar::GetInt ) )''')
        cls.add_registration_code(
            '''def( 
            "GetMax"
            , (bool ( ::ConVar::* )( float & ) const)( &::ConVar::GetMax )
            , ( bp::arg("maxVal") ) )''')
        cls.add_registration_code(
            '''def( 
            "GetMin"
            , (bool ( ::ConVar::* )( float & ) const)( &::ConVar::GetMin )
            , ( bp::arg("minVal") ) )''')
        cls.add_registration_code(
            '''def( 
            "GetName"
            , (char const * ( ::ConVar::* )(  ) const)( &::ConVar::GetName ) )''')
        cls.add_registration_code(
            '''def( 
            "GetString"
            , (char const * ( ::ConVar::* )(  ) const)( &::ConVar::GetString ) )''')
        cls.add_registration_code(
            '''def( 
            "IsCommand"
            , (bool ( ::ConVar::* )(  ) const)( &::ConVar::IsCommand ) )''')
        cls.add_registration_code(
            '''def( 
            "IsFlagSet"
            , (bool ( ::ConVar::* )( int ) const)( &::ConVar::IsFlagSet )
            , ( bp::arg("flag") ) )''')
        cls.add_registration_code(
            '''def( 
            "IsRegistered"
            , (bool ( ::ConVar::* )(  ) const)( &::ConVar::IsRegistered ) )''')
        cls.add_registration_code(
            '''def( 
            "Revert"
            , (void ( ::ConVar::* )(  ) )( &::ConVar::Revert ) )''')

        # ConVarRef
        mb.class_('ConVarRef').include()
        mb.mem_funs('GetLinkedConVar').exclude()
        
        # CCommand
        cls = mb.class_('CCommand')
        cls.include()
        cls.mem_funs('Tokenize').exclude()
        cls.mem_funs('ArgV').exclude()
        cls.mem_funs('DefaultBreakSet').exclude()
        
        cls.add_registration_code(
         '''def( 
            "__len__"
            , (int ( ::CCommand::* )(  ) const)( &::CCommand::ArgC ) )''')
        
        # PyConCommand
        cls = mb.class_('PyConCommand')
        cls.include()
        cls.rename('ConCommand')
        cls.var('m_pyCommandCallback').exclude()         # Must be excluded, or else things get broken without errors/warnings!
        cls.mem_funs('Dispatch').exclude()               # Must be excluded, or else things get broken without errors/warnings!
        cls.mem_funs('AutoCompleteSuggest').exclude()
        
        #mb.mem_funs('GetName').include()
        # Virtuality fuxors ConCommand. 
        cls.mem_funs().virtuality = 'not virtual' 
        
        # Sending messages
        if self.isServer:
            mb.free_functions('PySendUserMessage').include()
            mb.free_functions('PySendUserMessage').rename('SendUserMessage')
            
        # filters
        mb.class_('IRecipientFilter').include()
        mb.class_('IRecipientFilter').mem_funs().virtuality = 'not virtual' 
        if self.isServer:
            mb.class_('CRecipientFilter').include()
            mb.class_('CRecipientFilter').mem_funs().virtuality = 'not virtual' 
        else:
            mb.class_('C_RecipientFilter').include()
            mb.class_('C_RecipientFilter').mem_funs().virtuality = 'not virtual' 
            
            mb.class_('CLocalPlayerFilter').include()
            #mb.class_('CLocalPlayerFilter').mem_funs().virtuality = 'not virtual' 
            
        # Shared filters
        mb.class_('CSingleUserRecipientFilter').include()
        mb.class_('CBroadcastRecipientFilter').include()
        mb.class_('CReliableBroadcastRecipientFilter').include()
        mb.class_('CPASFilter').include()
        mb.class_('CPASAttenuationFilter').include()
        mb.class_('CPVSFilter').include()
            
        # Gameevents
        mb.class_('PyGameEventListener').include()
        mb.class_('PyGameEventListener').rename('GameEventListener')
        mb.class_('PyGameEventListener').mem_fun('PyFireGameEvent').rename('FireGameEvent')
        mb.class_('PyGameEventListener').add_registration_code('def( "ListenForGameEvent", (void ( ::PyGameEventListener::* )( char const * ) )( &::PyGameEventListener::ListenForGameEvent ), bp::arg("name") ) ')
        mb.class_('PyGameEventListener').add_registration_code('def( "StopListeningForAllEvents", (void ( ::PyGameEventListener::* )() )( &::PyGameEventListener::StopListeningForAllEvents ) ) ')
        
        mb.class_('PyGameEvent').include()
        mb.class_('PyGameEvent').rename('GameEvent')
        mb.class_('PyGameEvent').mem_fun('Init').exclude()
        mb.class_('PyGameEvent').mem_fun('GetEvent').exclude()
        
        if self.isServer:
            mb.free_function('PyFireGameEvent').include()
            mb.free_function('PyFireGameEvent').rename('FireGameEvent')
        else:
            mb.free_function('PyFireGameEventClientSide').include()
            mb.free_function('PyFireGameEventClientSide').rename('FireGameEventClientSide')
                
        # Player info
        mb.class_('py_player_info_s').include()
        mb.class_('py_player_info_s').rename('PlayerInfo')
        
        # Get map header
        mb.free_function('PyGetMapHeader').include()
        mb.free_function('PyGetMapHeader').rename('GetMapHeader')
        mb.class_('BSPHeader_t').include()
        mb.class_('lump_t').include()
        mb.mem_funs('GetBaseMap').exclude()
        #mb.mem_funs('DataMapAccess').include()
        #mb.mem_funs('DataMapInit').include()
        mb.vars('m_DataMap').exclude()
        
        # Content mounting
        mb.free_function('PyMountSteamContent').include()
        mb.free_function('PyMountSteamContent').rename('MountSteamContent')
        mb.free_function('PyAddSearchPath').include()
        mb.free_function('PyAddSearchPath').rename('AddSearchPath')
        mb.free_function('PyRemoveSearchPath').include()
        mb.free_function('PyRemoveSearchPath').rename('RemoveSearchPath')
        mb.free_function('PyGetSearchPath').include()
        mb.free_function('PyGetSearchPath').rename('GetSearchPath')
        mb.free_function('HasApp').include()
        mb.free_function('GetAppStatus').include()
        
        mb.enum('SearchPathAdd_t').include()
        mb.enum('FilesystemMountRetval_t').include()
        mb.enum('MountApps').include()
        mb.enum('AppStatus').include()
        
        # GameSystem
        mb.class_('CBaseGameSystem').include()
        mb.class_('CBaseGameSystemPerFrame').include()
        mb.class_('CAutoGameSystem').include()
        mb.class_('CAutoGameSystemPerFrame').include()
        mb.class_('CAutoGameSystem').rename('AutoGameSystem')
        mb.class_('CAutoGameSystemPerFrame').rename('AutoGameSystemPerFrame')
        mb.mem_funs('IsPerFrame').virtuality = 'not virtual' 
        mb.mem_funs('SafeRemoveIfDesired').virtuality = 'not virtual' 
        
        # Engine
        if self.isServer:
            cls = mb.class_('PyVEngineServer')
            cls.rename('VEngineServer')
            cls.include()
        else:
            cls = mb.class_('PyVEngineClient')
            cls.rename('VEngineClient')
            cls.include()
            cls.mem_funs('LoadModel').call_policies = call_policies.return_value_policy( call_policies.return_by_value ) 
        mb.add_registration_code( "bp::scope().attr( \"engine\" ) = boost::ref(pyengine);" )   

        if self.isServer:
            # CSteamID
            # On the client this is defined in a seperate module isteam, but server only needs this one
            mb.class_('CSteamID').include()
            mb.class_('CSteamID').mem_funs('Render').exclude()
            
            mb.class_('CSteamID').calldefs( lambda decl: HasArgType(decl, 'char') ).exclude()
            mb.class_('CSteamID').mem_funs('SetFromString').exclude()      # No definition...
            mb.class_('CSteamID').mem_funs('SetFromSteam2String').exclude()      # No definition...
            mb.class_('CSteamID').mem_funs('BValidExternalSteamID').exclude()      # No definition...
            
            mb.enum('EResult').include()
            mb.enum('EDenyReason').include()
            mb.enum('EUniverse').include()
            mb.enum('EAccountType').include()
            mb.enum('ESteamUserStatType').include()
            mb.enum('EChatEntryType').include()
            mb.enum('EChatRoomEnterResponse').include()
        
        # Command line
        if settings.ASW_CODE_BASE:
            cls = mb.class_('ICommandLine')
            cls.include()
            
            mb.free_function('CommandLine').include()
            mb.free_function('CommandLine').call_policies = call_policies.return_value_policy( call_policies.reference_existing_object )
        
        # Accessing model info
        cls = mb.class_('PyVModelInfo')
        cls.rename('VModelInfo')
        cls.include()
        cls.mem_funs('GetModel').call_policies = call_policies.return_value_policy( call_policies.return_by_value ) 
        cls.mem_funs('FindOrLoadModel').call_policies = call_policies.return_value_policy( call_policies.return_by_value ) 
        mb.add_registration_code( "bp::scope().attr( \"modelinfo\" ) = boost::ref(pymodelinfo);" )   
        
        if self.isServer:
            mb.free_function('PyMapEntity_ParseAllEntities').include()
            mb.free_function('PyMapEntity_ParseAllEntities').rename('MapEntity_ParseAllEntities')
            
            cls = mb.class_('IMapEntityFilter')
            cls.include()
            cls.mem_fun('CreateNextEntity').call_policies = call_policies.return_value_policy( call_policies.return_by_value ) 
            
            mb.class_('CMapEntityRef').include()
            mb.free_function('PyGetMapEntityRef').include()
            mb.free_function('PyGetMapEntityRef').rename('GetMapEntityRef')
            mb.free_function('PyGetMapEntityRefIteratorHead').include()
            mb.free_function('PyGetMapEntityRefIteratorHead').rename('GetMapEntityRefIteratorHead')
            mb.free_function('PyGetMapEntityRefIteratorNext').include()
            mb.free_function('PyGetMapEntityRefIteratorNext').rename('GetMapEntityRefIteratorNext')
            
            # Edicts
            cls = mb.class_('edict_t')
            cls.include()
            cls.mem_fun('GetCollideable').exclude()
            
        # model_t
        cls = mb.class_('wrap_model_t')
        cls.include()
        cls.rename('model_t')
        cls.calldefs('wrap_model_t').exclude()
        cls.no_init = True
        cls.vars('pModel').exclude()

        mb.add_registration_code( "ptr_model_t_to_wrap_model_t();" )
        mb.add_registration_code( "const_ptr_model_t_to_wrap_model_t();" )
        mb.add_registration_code( "wrap_model_t_to_model_t();" )
        
        # LUMPS
        mb.add_registration_code( "bp::scope().attr( \"LUMP_ENTITIES\" ) = (int)LUMP_ENTITIES;" )
        
        # Defines
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_NONE\" ) = (int)FCVAR_NONE;" )
        
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_UNREGISTERED\" ) = (int)FCVAR_UNREGISTERED;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_DEVELOPMENTONLY\" ) = (int)FCVAR_DEVELOPMENTONLY;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_GAMEDLL\" ) = (int)FCVAR_GAMEDLL;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_CLIENTDLL\" ) = (int)FCVAR_CLIENTDLL;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_HIDDEN\" ) = (int)FCVAR_HIDDEN;" )
        
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_PROTECTED\" ) = (int)FCVAR_PROTECTED;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_SPONLY\" ) = (int)FCVAR_SPONLY;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_ARCHIVE\" ) = (int)FCVAR_ARCHIVE;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_NOTIFY\" ) = (int)FCVAR_NOTIFY;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_USERINFO\" ) = (int)FCVAR_USERINFO;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_CHEAT\" ) = (int)FCVAR_CHEAT;" )
        
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_PRINTABLEONLY\" ) = (int)FCVAR_PRINTABLEONLY;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_UNLOGGED\" ) = (int)FCVAR_UNLOGGED;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_NEVER_AS_STRING\" ) = (int)FCVAR_NEVER_AS_STRING;" )
        
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_REPLICATED\" ) = (int)FCVAR_REPLICATED;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_DEMO\" ) = (int)FCVAR_DEMO;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_DONTRECORD\" ) = (int)FCVAR_DONTRECORD;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_NOT_CONNECTED\" ) = (int)FCVAR_NOT_CONNECTED;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_ARCHIVE_XBOX\" ) = (int)FCVAR_ARCHIVE_XBOX;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_SERVER_CAN_EXECUTE\" ) = (int)FCVAR_SERVER_CAN_EXECUTE;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_SERVER_CANNOT_QUERY\" ) = (int)FCVAR_SERVER_CANNOT_QUERY;" )
        mb.add_registration_code( "bp::scope().attr( \"FCVAR_CLIENTCMD_CAN_EXECUTE\" ) = (int)FCVAR_CLIENTCMD_CAN_EXECUTE;" )
        
        # Excludes
        if self.isServer:
            mb.mem_funs( lambda decl: HasArgType(decl, 'CTeam') ).exclude()
            
    def AddAdditionalCode(self, mb):
        header = code_creators.include_t( 'src_python_gameinterface_converters.h' )
        mb.code_creator.adopt_include(header)
        super(GameInterface, self).AddAdditionalCode(mb)
        