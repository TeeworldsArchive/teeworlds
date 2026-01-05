SDL = {
	basepath = PathDir(ModuleFilename()),

	OptFind = function (name, required)
		local check = function(option, settings)
			option.value = false
			option.use_pkgconfig = false
			option.lib_path = nil
			
			if ExecuteSilent("pkg-config --exists sdl3") == 0 then
				option.value = true
				option.use_pkgconfig = true
			end
		end
		
		local apply = function(option, settings)
			if option.use_pkgconfig == true then
				settings.cc.flags:Add("`pkg-config --cflags sdl3`")
				settings.link.flags:Add("`pkg-config --libs sdl3`")
			end
		end
		
		local save = function(option, output)
			output:option(option, "value")
			output:option(option, "use_pkgconfig")
		end
		
		local display = function(option)
			if option.value == true then
				if option.use_pkgconfig == true then return "using pkg-config" end
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
