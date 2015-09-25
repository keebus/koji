solution "koji"
	language "C"
	configurations { "Debug", "Optimized", "Release" }
	platforms { "x32", "x64" }
	location "build"
	targetdir "."
	debugdir "."
	includedirs "."
	flags     "MultiProcessorCompile"
	warnings  "Extra"
	includedirs "include"

	configuration "vs*"
		buildoptions { "/wd4201", "/wd4204" }
	
	filter "Debug"
		flags     "Symbols"
		defines   "KOJI_DEBUG"
		optimize  "Off"

	filter "Optimized"
		flags     "Symbols"
		defines   "KOJI_DEBUG"
		optimize  "Speed"

	filter "Release"
		flags     "LinkTimeOptimization"
		optimize  "Speed"

	project "koji"
		kind "ConsoleApp"
		files { "koji.h", "koji.c", "main.c" }
		filter { "Debug"  } targetname "kojid"
		filter { "Optimized" } targetname "kojio"
		filter { "Release" } targetname "koji"
