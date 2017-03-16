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

	project "koji"
		kind "StaticLib"
		files { "src/**.*", "./**.kj" }
		removefiles { "src/kj_main.c", "src/kj_tests.c" }

	project "kojic"
		kind "ConsoleApp"
		files { "src/kj_main.c", "./**.kj" }
		debugargs "../sample/helloworld.kj"
		links "koji"

	project "tests"
		kind "ConsoleApp"
		files { "src/kj_tests.c" }
		links "koji"