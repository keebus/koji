solution "koji"
	language "C"
	configurations { "Debug", "Release" }
	platforms { "x32", "x64" }

	project "kojic"
		kind "ConsoleApp"
		files { "src/**.*", "./**.kj" }
		debugargs "sample/helloworld.kj"