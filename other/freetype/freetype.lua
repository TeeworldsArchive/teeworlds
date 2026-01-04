FreeType = {
	basepath = PathDir(ModuleFilename()),

	OptFind = function (name, required)
		local check = function(option, settings)
			option.value = false
			option.use_pkgconfig = false
			option.use_ftconfig = false
			option.lib_path = nil

			if ExecuteSilent("pkg-config --exists freetype2") == 0 then
				option.value = true
				option.use_pkgconfig = true
			elseif ExecuteSilent("freetype-config") == 0 and ExecuteSilent("freetype-config --cflags") == 0 then
				option.value = true
				option.use_ftconfig = true
			end
		end

		local apply = function(option, settings)
			if option.use_pkgconfig == true then
				settings.cc.flags:Add("`pkg-config --cflags freetype2`")
				settings.link.flags:Add("`pkg-config --libs freetype2`")
			elseif option.use_ftconfig == true then
				settings.cc.flags:Add("`freetype-config --cflags`")
				settings.link.flags:Add("`freetype-config --libs`")
			end
		end

		local save = function(option, output)
			output:option(option, "value")
			output:option(option, "use_pkgconfig")
			output:option(option, "use_ftconfig")
		end

		local display = function(option)
			if option.value == true then
				if option.use_pkgconfig == true then return "using pkg-config" end
				if option.use_ftconfig == true then return "using freetype-config" end
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
