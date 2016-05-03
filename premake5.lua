
solution "koji"
	language "C"
	location "build"
	targetdir "bin"
	configurations { "Debug", "Release" }
	
	project "kojic"
		kind "ConsoleApp"
		files { "src/**.h", "src/**.c" }
