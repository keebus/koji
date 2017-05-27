solution "koji"
	language "C"
	configurations { "Debug", "Release" }
	platforms { "x32", "x64" }
	location "build"
	targetdir "bin"
	debugdir "bin"

	configuration "Debug"
		flags "Symbols"
	
	configuration "Release"
		optimize "Speed"

	configuration "vs*"
		defines "_CRT_SECURE_NO_WARNINGS"

	project "libkoji"
		targetname "koji"
		kind "StaticLib"
		files { "src/**.*" }
		removefiles { "src/kmain.c", "src/ktests.c" }

	project "kojitests"
		kind "ConsoleApp"
		files { "src/ktests.c" }
		links "libkoji"

	project "kojic"
		kind "ConsoleApp"
		files { "src/kmain.c", "./**.kj" }
		debugargs "../sample/helloworld.kj"
		links "libkoji"
