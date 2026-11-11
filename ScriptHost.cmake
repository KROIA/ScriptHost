## description: Qt library that lets users and programs script a C++ application in JavaScript: the app defines the callable methods, scripts run sandboxed on their own threads, several at once, with timers, events (keys, mouse, app events), extensions, checks, limits, dry run and call stepping. The engine needs only Qt Core + Qml; the optional ScriptHostWidgets library adds the editor, debugger, scripts window and a canvas scripts draw on (shapes, highlights, simple games).

function(dep LIBRARY_MACRO_NAME SHARED_LIB STATIC_LIB STATIC_PROFILE_LIB INCLUDE_PATHS)
    # Define the git repository and tag to download from
    set(LIB_NAME ScriptHost)
    set(LIB_MACRO_NAME SH_LIBRARY_AVAILABLE)
    set(GIT_REPO https://github.com/KROIA/ScriptHost.git)
    set(GIT_TAG main)
	set(NO_EXAMPLES True)	
	set(NO_UNITTESTS True)
	set(ADDITIONAL_INCLUDE_PATHS )
	# Two libraries: the engine (ScriptHost, Qt Core + Qml) and the widgets
	# (ScriptHostWidgets, + Gui and Widgets). Both are linked; set
	# SCRIPTHOST_ENGINE_ONLY to ON before this file is included for the engine
	# alone, e.g. in a program without windows.
	if(NOT SCRIPTHOST_ENGINE_ONLY)
		set(ADDITIONAL_SHARED_LIB_DEPENDENCIES ScriptHostWidgets_shared)
		set(ADDITIONAL_STATIC_LIB_DEPENDENCIES ScriptHostWidgets_static)
		set(ADDITIONAL_STATIC_PROFILE_LIB_DEPENDENCIES
			"$<IF:$<TARGET_EXISTS:ScriptHostWidgets_static_profile>,ScriptHostWidgets_static_profile,ScriptHostWidgets_static>")
	endif()

	downloadStandardLibrary()
endfunction()

dep(DEPENDENCY_NAME_MACRO 
    DEPENDENCIES_FOR_SHARED_LIB 
    DEPENDENCIES_FOR_STATIC_LIB 
    DEPENDENCIES_FOR_STATIC_PROFILE_LIB 
    DEPENDENCIES_INCLUDE_PATHS)