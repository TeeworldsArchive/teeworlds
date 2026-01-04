SDL = {
	basepath = PathDir(ModuleFilename()),

	OptFind = function (name, required)
		local check = function(option, settings)
			option.value = false
			option.use_pkgconfig = false
			option.use_sdlconfig = false
			option.lib_path = nil
			
			if ExecuteSilent("pkg-config --exists sdl2") == 0 then
				option.value = true
				option.use_pkgconfig = true
			elseif ExecuteSilent("sdl2-config") == 0 and ExecuteSilent("sdl2-config --cflags") == 0 then
				option.value = true
				option.use_sdlconfig = true
			end
		end
		
		local apply = function(option, settings)
			if option.use_pkgconfig == true then
				settings.cc.flags:Add("`pkg-config --cflags sdl2`")
				settings.link.flags:Add("`pkg-config --libs sdl2`")
			elseif option.use_sdlconfig == true then
				settings.cc.flags:Add("`sdl2-config --cflags`")
				settings.link.flags:Add("`sdl2-config --libs`")
			end
		end
		
		local save = function(option, output)
			output:option(option, "value")
			output:option(option, "use_pkgconfig")
			output:option(option, "use_sdlconfig")
		end
		
		local display = function(option)
			if option.value == true then
				if option.use_pkgconfig == true then return "using pkg-config" end
				if option.use_sdlconfig == true then return "using sdl2-config" end
				return "using unknown method"
			else
				if option.required then
					return "not found (required)"
				else
					return "not found (optional)"
				end
			end
		end
		
		local o = MakeOption(name, 0, check, save, display)
		o.Apply = apply
		o.include_path = nil
		o.lib_path = nil
		o.required = required
		return o
	end
}
