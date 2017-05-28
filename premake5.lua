solution "koji"
	language "C"
	configurations { "Debug", "Release" }
	platforms { "x32", "x64" }
	location "build"
	targetdir "bin"
	debugdir "bin"

	configuration "Debug"
		symbols "On"
	
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
		files { "src/ktests.c", "tests/**.kj" }
		links "libkoji"

	project "kojic"
		kind "ConsoleApp"
		files { "src/kmain.c", "./**.kj" }
		links "libkoji"

newaction {
	trigger = "embed",
	description = "Generates the amalgamate file koji.c for better performance and simpler distribution.",

	execute = function()

		local headers = {
			"kplatform.h",
			"kerror.h",
			"kio.h",
			"kvalue.h",
			"kclass.h",
			"ktable.h", 
			"kstring.h",
			"klexer.h",
			"kbytecode.h",
			"kcompiler.h",
			"kvm.h",
		}

		local kojic = ""

		-- Append the koji public interface.
		-- out:write(io.open("src/koji.h"):read("*all"))

		-- Write out all the headers
		for k, v in pairs(headers) do
			kojic = kojic .. io.open("src/"..v):read("*all")
		end

		for k,v in pairs(os.matchfiles("src/*.c")) do
			if v ~= "src/kmain.c"  then
				kojic = kojic .. io.open(v):read("*all")
			end
		end

		kojic = kojic:gsub("/%*%s+%*%s+koji.-%*/%s*", "\n") -- remove comments
		kojic = kojic:gsub("#include \".-\"%s*", "") -- remove local includes
		kojic = kojic:gsub("#pragma once", "") -- remove pragma once

		local file = io.open("koji.c", "w")
		file:write(io.open("src/koji.h"):read("*all"))		
		file:write("\n\n\n/*** PUBLIC INTERFACE END ***/\n\n")
		file:write("#define KOJI_AMALGAMATE /* make koji internal fns priate to koji.c */")
		file:write(kojic)

		print("Embedded file koji.c generated.")
	end,
}