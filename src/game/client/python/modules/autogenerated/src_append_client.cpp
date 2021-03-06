//=============================================================================//
// This file is automatically generated. CHANGES WILL BE LOST.
//=============================================================================//

#include "cbase.h"
#include "src_python.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace boost::python;

// The init method is in one of the generated files declared
#ifdef _WIN32
extern "C" __declspec(dllexport) void initisteam();
extern "C" __declspec(dllexport) void init_input();
extern "C" __declspec(dllexport) void init_vgui();
extern "C" __declspec(dllexport) void init_vguicontrols();
extern "C" __declspec(dllexport) void init_awesomium();
#else
extern "C"  void initisteam();
extern "C"  void init_input();
extern "C"  void init_vgui();
extern "C"  void init_vguicontrols();
extern "C"  void init_awesomium();
#endif

// The append function
void AppendClientModules()
{
	APPEND_MODULE(isteam)
	APPEND_MODULE(_input)
	APPEND_MODULE(_vgui)
	APPEND_MODULE(_vguicontrols)
	APPEND_MODULE(_awesomium)
}

