from generate_mods_helper import GenerateModuleSemiShared

from pygccxml.declarations import matchers

class VProf(GenerateModuleSemiShared):
    module_name = 'vprof'
    
    files = [
        'vprof.h'
    ]

    def Parse(self, mb):
        # Exclude everything, then add what we need
        # Otherwise we get very big source code and dll's
        mb.decls().exclude() 
        
        mb.class_('CVProfile').include()
        
        mb.mem_funs('GetRoot').exclude()
        mb.mem_funs('FindNode').exclude()
        mb.mem_funs('GetBudgetGroupColor').exclude()
        mb.mem_funs('RegisterNumBudgetGroupsChangedCallBack').exclude()
        mb.mem_funs('FindOrCreateCounter').exclude()
        mb.mem_funs('GetCounterNameAndValue').exclude()
        mb.vars('m_pNumBudgetGroupsChangedCallBack').exclude()
        mb.vars('m_pName').exclude()
        
        mb.vars('g_VProfCurrentProfile').include()
        mb.vars('g_VProfCurrentProfile').rename('vprofcurrentprofilee')
        
        # Remove any protected function 
        mb.calldefs( matchers.access_type_matcher_t( 'protected' ) ).exclude()    