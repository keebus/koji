solution "koji"
	language "C"
	configurations { "Debug", "Release" }
	platforms { "x32", "x64" }
	location "build"

	configuration "Debug"
		symbols "On"
	
	configuration "Release"
		optimize "Speed"

	project "kojic"
		kind "ConsoleApp"
		files { "src/**.*", "./**.kj" }
		debugargs "sample/helloworld.kj"