
solution "koji"
	language "C"
	location "build"
	targetdir "bin"
	configurations { "Debug", "Release" }
	warnings  "Extra"
	
	configuration "gmake"
		buildoptions { "--std=c99", "-pedantic-errors" }

	filter "Debug"
		flags     "Symbols"
		optimize  "Off"

	filter "Optimized"
		flags     "Symbols"
		optimize  "Speed"

	filter "Release"
		flags     "LinkTimeOptimization"
		optimize  "Speed"

	project "kojic"
		kind "ConsoleApp"
		files { "src/**.h", "src/**.c" }
