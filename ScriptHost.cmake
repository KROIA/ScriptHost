## description: Qt library that lets users and programs script a C++ application in JavaScript: the app defines the callable methods, scripts run sandboxed on their own thread, with checks, limits, dry run, call stepping and a built-in editor.

function(dep LIBRARY_MACRO_NAME SHARED_LIB STATIC_LIB STATIC_PROFILE_LIB INCLUDE_PATHS)
    # Define the git repository and tag to download from
    set(LIB_NAME ScriptHost)
    set(LIB_MACRO_NAME SH_LIBRARY_AVAILABLE)
    set(GIT_REPO https://github.com/KROIA/ScriptHost.git)
    set(GIT_TAG main)
	set(NO_EXAMPLES True)	
	set(NO_UNITTESTS True)
	set(ADDITIONAL_INCLUDE_PATHS )

	downloadStandardLibrary()
endfunction()

dep(DEPENDENCY_NAME_MACRO 
    DEPENDENCIES_FOR_SHARED_LIB 
    DEPENDENCIES_FOR_STATIC_LIB 
    DEPENDENCIES_FOR_STATIC_PROFILE_LIB 
    DEPENDENCIES_INCLUDE_PATHS)