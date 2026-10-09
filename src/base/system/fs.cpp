/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#define _DEFAULT_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <base/system/base.h>
#include <base/system/debug.h>
#include <base/system/fs.h>
#include <base/system/io.h>
#include <base/system/string.h>
#include <sys/stat.h>
#if defined(CONF_FAMILY_UNIX)

#include <dirent.h>
#include <unistd.h>

#elif defined(CONF_FAMILY_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <direct.h>
#else
#error NOT IMPLEMENTED
#endif

#if defined(CONF_FAMILY_WINDOWS)
static inline time_t filetime_to_unixtime(LPFILETIME filetime)
{
	time_t t;
	ULARGE_INTEGER li;
	li.LowPart = filetime->dwLowDateTime;
	li.HighPart = filetime->dwHighDateTime;

	li.QuadPart /= 10000000; // 100ns to 1s
	li.QuadPart -= 11644473600LL; // Windows epoch is in the past

	t = li.QuadPart;
	return t == (time_t) li.QuadPart ? t : (time_t) -1;
}
#endif

void fs_listdir(const char *dir, FS_LISTDIR_CALLBACK cb, int type, void *user)
{
#if defined(CONF_FAMILY_WINDOWS)
	WIN32_FIND_DATAW finddata;
	HANDLE handle;
	char buffer[IO_MAX_PATH_LENGTH];
	char buffer2[IO_MAX_PATH_LENGTH];
	WCHAR wBuffer[IO_MAX_PATH_LENGTH];
	int length;

	str_format(buffer, sizeof(buffer), "%s/*", dir);
	MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wBuffer, sizeof(wBuffer) / sizeof(WCHAR));

	handle = FindFirstFileW(wBuffer, &finddata);
	if(handle == INVALID_HANDLE_VALUE)
		return;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	/* add all the entries */
	do
	{
		WideCharToMultiByte(CP_UTF8, 0, finddata.cFileName, -1, buffer2, sizeof(buffer2), NULL, NULL);
		str_copy(buffer + length, buffer2, (int) sizeof(buffer) - length);
		if(cb(buffer2, (finddata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, type, user))
			break;
	} while(FindNextFileW(handle, &finddata));

	FindClose(handle);
#else
	struct dirent *entry;
	char buffer[IO_MAX_PATH_LENGTH];
	int length;
	DIR *d = opendir(dir);

	if(!d)
		return;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	while((entry = readdir(d)) != NULL)
	{
		str_copy(buffer + length, entry->d_name, (int) sizeof(buffer) - length);
		if(cb(entry->d_name, (entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) ? fs_is_dir(buffer) : entry->d_type == DT_DIR, type, user))
			break;
	}

	/* close the directory and return */
	closedir(d);
#endif
}

void fs_listdir_fileinfo(const char *dir, FS_LISTDIR_CALLBACK_FILEINFO cb, int type, void *user)
{
#if defined(CONF_FAMILY_WINDOWS)
	WIN32_FIND_DATAW finddata;
	HANDLE handle;
	char buffer[IO_MAX_PATH_LENGTH];
	char buffer2[IO_MAX_PATH_LENGTH];
	WCHAR wBuffer[IO_MAX_PATH_LENGTH];
	int length;

	str_format(buffer, sizeof(buffer), "%s/*", dir);
	MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wBuffer, sizeof(wBuffer) / sizeof(WCHAR));

	handle = FindFirstFileW(wBuffer, &finddata);
	if(handle == INVALID_HANDLE_VALUE)
		return;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	/* add all the entries */
	do
	{
		CFsFileInfo info;
		WideCharToMultiByte(CP_UTF8, 0, finddata.cFileName, -1, buffer2, sizeof(buffer2), NULL, NULL);
		str_copy(buffer + length, buffer2, (int) sizeof(buffer) - length);

		info.m_pName = buffer2;
		info.m_TimeCreated = filetime_to_unixtime(&finddata.ftCreationTime);
		info.m_TimeModified = filetime_to_unixtime(&finddata.ftLastWriteTime);

		if(cb(&info, (finddata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, type, user))
			break;
	} while(FindNextFileW(handle, &finddata));

	FindClose(handle);
#else
	struct dirent *entry;
	time_t created = -1, modified = -1;
	char buffer[IO_MAX_PATH_LENGTH];
	int length;
	DIR *d = opendir(dir);

	if(!d)
		return;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	while((entry = readdir(d)) != NULL)
	{
		CFsFileInfo info;

		str_copy(buffer + length, entry->d_name, (int) sizeof(buffer) - length);
		fs_file_time(buffer, &created, &modified);

		info.m_pName = entry->d_name;
		info.m_TimeCreated = created;
		info.m_TimeModified = modified;

		if(cb(&info, (entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) ? fs_is_dir(buffer) : entry->d_type == DT_DIR, type, user))
			break;
	}

	/* close the directory and return */
	closedir(d);
#endif
}

int fs_storage_path(const char *appname, char *path, int max)
{
#if defined(CONF_FAMILY_WINDOWS)
	WCHAR *home = _wgetenv(L"APPDATA");
	char buffer[IO_MAX_PATH_LENGTH];
	if(!home)
		return -1;
	WideCharToMultiByte(CP_UTF8, 0, home, -1, buffer, sizeof(buffer), NULL, NULL);
	str_format(path, max, "%s/%s", buffer, appname);
	return 0;
#else
	char *home = getenv("HOME");
	int i;
	char *xdgdatahome = getenv("XDG_DATA_HOME");

	if(!home)
		return -1;

#if defined(CONF_PLATFORM_MACOS)
	str_format(path, max, "%s/Library/Application Support/%s", home, appname);
	return 0;
#endif

	char xdgpath[IO_MAX_PATH_LENGTH];
	dbg_assert(max <= IO_MAX_PATH_LENGTH, "max too large for xdgpath");

	/* old folder location */
	str_format(path, max, "%s/.%s", home, appname);
	for(i = str_length(home) + 2; path[i]; i++)
		path[i] = tolower(path[i]);

	if(!xdgdatahome)
	{
		/* use default location */
		str_format(xdgpath, max, "%s/.local/share/%s", home, appname);
		for(i = str_length(home) + 14; xdgpath[i]; i++)
			xdgpath[i] = tolower(xdgpath[i]);
	}
	else
	{
		str_format(xdgpath, max, "%s/%s", xdgdatahome, appname);
		for(i = str_length(xdgdatahome) + 1; xdgpath[i]; i++)
			xdgpath[i] = tolower(xdgpath[i]);
	}

	/* check for old location / backward compatibility */
	if(fs_is_dir(path))
	{
		/* use old folder path */
		/* for backward compatibility */
		return 0;
	}

	str_format(path, max, "%s", xdgpath);

	return 0;
#endif
}

int fs_makedir(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	WCHAR wBuffer[IO_MAX_PATH_LENGTH];
	MultiByteToWideChar(CP_UTF8, 0, path, -1, wBuffer, sizeof(wBuffer) / sizeof(WCHAR));
	if(_wmkdir(wBuffer) == 0)
		return 0;
	if(errno == EEXIST)
		return 0;
	return -1;
#else
	if(mkdir(path, 0755) == 0)
		return 0;
	if(errno == EEXIST)
		return 0;
	return -1;
#endif
}

int fs_makedir_recursive(const char *path)
{
	char buffer[2048];
	int len;
	int i;
	str_copy(buffer, path, sizeof(buffer));
	len = str_length(buffer);
	// ignore a leading slash
	for(i = 1; i < len; i++)
	{
		char b = buffer[i];
		if(b == '/' || (b == '\\' && buffer[i - 1] != ':'))
		{
			buffer[i] = 0;
			if(fs_makedir(buffer) < 0)
			{
				return -1;
			}
			buffer[i] = b;
		}
	}
	return fs_makedir(path);
}

int fs_is_dir(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	WCHAR wPath[IO_MAX_PATH_LENGTH];
	DWORD attributes;
	MultiByteToWideChar(CP_UTF8, 0, path, -1, wPath, sizeof(wPath) / sizeof(WCHAR));
	attributes = GetFileAttributesW(wPath);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
	struct stat sb;
	if(stat(path, &sb) == -1)
		return 0;
	return S_ISDIR(sb.st_mode) ? 1 : 0;
#endif
}

int fs_chdir(const char *path)
{
	if(fs_is_dir(path))
	{
#if defined(CONF_FAMILY_WINDOWS)
		WCHAR wBuffer[IO_MAX_PATH_LENGTH];
		MultiByteToWideChar(CP_UTF8, 0, path, -1, wBuffer, sizeof(wBuffer) / sizeof(WCHAR));
		if(_wchdir(wBuffer))
			return 1;
		else
			return 0;
#else
		if(chdir(path))
			return 1;
		else
			return 0;
#endif
	}
	else
		return 1;
}

char *fs_getcwd(char *buffer, int buffer_size)
{
#if defined(CONF_FAMILY_WINDOWS)
	WCHAR wBuffer[IO_MAX_PATH_LENGTH];
#endif
	dbg_assert(buffer != 0, "buffer invalid");
	dbg_assert(buffer_size > 0, "buffer_size invalid");
#if defined(CONF_FAMILY_WINDOWS)
	if(_wgetcwd(wBuffer, buffer_size) == 0)
	{
		buffer[0] = '\0';
		return 0;
	}
	WideCharToMultiByte(CP_UTF8, 0, wBuffer, -1, buffer, buffer_size, NULL, NULL);
	return buffer;
#else
	if(getcwd(buffer, buffer_size) == 0)
	{
		buffer[0] = '\0';
		return 0;
	}
	return buffer;
#endif
}

int fs_parent_dir(char *path)
{
	char *parent = 0;
	for(; *path; ++path)
	{
		if(*path == '/' || *path == '\\')
			parent = path;
	}

	if(parent)
	{
		*parent = 0;
		return 0;
	}
	return 1;
}

int fs_remove(const char *filename)
{
#if defined(CONF_FAMILY_WINDOWS)
	WCHAR wFilename[IO_MAX_PATH_LENGTH];
	MultiByteToWideChar(CP_UTF8, 0, filename, -1, wFilename, sizeof(wFilename) / sizeof(WCHAR));
	if(DeleteFileW(wFilename) == 0)
		return 1;
	return 0;
#else
	if(remove(filename))
		return 1;
	return 0;
#endif
}

int fs_rename(const char *oldname, const char *newname)
{
#if defined(CONF_FAMILY_WINDOWS)
	WCHAR wOldname[IO_MAX_PATH_LENGTH];
	WCHAR wNewname[IO_MAX_PATH_LENGTH];
	MultiByteToWideChar(CP_UTF8, 0, oldname, -1, wOldname, sizeof(wOldname) / sizeof(WCHAR));
	MultiByteToWideChar(CP_UTF8, 0, newname, -1, wNewname, sizeof(wNewname) / sizeof(WCHAR));
	if(MoveFileExW(wOldname, wNewname, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) == 0)
		return 1;
	return 0;
#else
	if(rename(oldname, newname))
		return 1;
	return 0;
#endif
}

int fs_read(const char *name, void **result, unsigned *result_len)
{
	IOHANDLE file = io_open(name, IOFLAG_READ);
	*result = 0;
	*result_len = 0;
	if(!file)
	{
		return 1;
	}
	io_read_all(file, result, result_len);
	io_close(file);
	return 0;
}

char *fs_read_str(const char *name)
{
	IOHANDLE file = io_open(name, IOFLAG_READ | IOFLAG_SKIP_BOM);
	char *result;
	if(!file)
	{
		return 0;
	}
	result = io_read_all_str(file);
	io_close(file);
	return result;
}

int fs_file_time(const char *name, time_t *created, time_t *modified)
{
#if defined(CONF_FAMILY_WINDOWS)
	WIN32_FIND_DATAW finddata;
	HANDLE handle;
	WCHAR wBuffer[IO_MAX_PATH_LENGTH];

	MultiByteToWideChar(CP_UTF8, 0, name, -1, wBuffer, sizeof(wBuffer) / sizeof(WCHAR));
	handle = FindFirstFileW(wBuffer, &finddata);
	if(handle == INVALID_HANDLE_VALUE)
		return 1;

	*created = filetime_to_unixtime(&finddata.ftCreationTime);
	*modified = filetime_to_unixtime(&finddata.ftLastWriteTime);
	FindClose(handle);
#elif defined(CONF_FAMILY_UNIX)
	struct stat sb;
	if(stat(name, &sb))
		return 1;

	*created = sb.st_ctime;
	*modified = sb.st_mtime;
#else
#error not implemented
#endif

	return 0;
}
