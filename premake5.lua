solution "koji"
	language "C"
	configurations { "Debug", "Optimized", "Release" }
	platforms { "x32", "x64" }
	targetdir "bin"
	location "build"
	defines "_CRT_SECURE_NO_WARNINGS"

	configuration "Debug"
		optimize "Off"
		symbols "On"
		targetsuffix "d"

	configuration "Optimized"
		optimize "On"
		symbols "On"
		targetsuffix "o"

	configuration "Release"
		optimize "On"
		symbols "Off"

	project "koji"
		kind "StaticLib"
		files { "**.h", "**.c" }
		removefiles { "ktests.c", "kmain.c" }
    
	project "kojitests"
		kind "ConsoleApp"
		files { "ktests.c" }
		links "koji"
