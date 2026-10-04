/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#ifndef BASE_SYSTEM_FS_H
#define BASE_SYSTEM_FS_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Group: Filesystem */

/*
	Function: fs_listdir
		Lists the files in a directory

	Parameters:
		dir - Directory to list
		cb - Callback function to call for each entry
		type - Type of the directory
		user - Pointer to give to the callback
*/
typedef int (*FS_LISTDIR_CALLBACK)(const char *name, int is_dir, int dir_type, void *user);
void fs_listdir(const char *dir, FS_LISTDIR_CALLBACK cb, int type, void *user);

typedef struct
{
	const char *m_pName;
	time_t m_TimeCreated; // seconds since UNIX Epoch
	time_t m_TimeModified; // seconds since UNIX Epoch
} CFsFileInfo;

/*
	Function: fs_listdir_fileinfo
		Lists the files in a directory and gets additional file information

	Parameters:
		dir - Directory to list
		cb - Callback function to call for each entry
		type - Type of the directory
		user - Pointer to give to the callback
*/
typedef int (*FS_LISTDIR_CALLBACK_FILEINFO)(const CFsFileInfo *info, int is_dir, int dir_type, void *user);
void fs_listdir_fileinfo(const char *dir, FS_LISTDIR_CALLBACK_FILEINFO cb, int type, void *user);

/*
	Function: fs_makedir
		Creates a directory

	Parameters:
		path - Directory to create

	Returns:
		Returns 0 on success. Negative value on failure.

	Remarks:
		Does not create several directories if needed. "a/b/c" will result
		in a failure if b or a does not exist.
*/
int fs_makedir(const char *path);

/*
	Function: fs_makedir_recursive
		Recursively create directories

	Parameters:
		path - Path to create

	Returns:
		Returns 0 on success. Negative value on failure.
*/
int fs_makedir_recursive(const char *path);

/*
	Function: fs_storage_path
		Fetches per user configuration directory.

	Returns:
		Returns 0 on success. Negative value on failure.

	Remarks:
		- Returns ~/.appname on UNIX based systems
		- Returns ~/Library/Applications Support/appname on macOS
		- Returns %APPDATA%/Appname on Windows based systems
*/
int fs_storage_path(const char *appname, char *path, int max);

/*
	Function: fs_is_dir
		Checks if directory exists

	Returns:
		Returns 1 on success, 0 on failure.
*/
int fs_is_dir(const char *path);

/*
	Function: fs_chdir
		Changes current working directory

	Returns:
		Returns 0 on success, 1 on failure.
*/
int fs_chdir(const char *path);

/*
	Function: fs_getcwd
		Gets the current working directory.

	Parameters:
		buffer - Pointer to a buffer that will hold the result.
		buffer_size - The size of the buffer in bytes.

	Returns:
		Returns a pointer to the buffer on success, 0 on failure.
		On success, the buffer contains the result as a zero-terminated string.
		On failure, the buffer contains an empty zero-terminated string.

*/
char *fs_getcwd(char *buffer, int buffer_size);

/*
	Function: fs_parent_dir
		Get the parent directory of a directory

	Parameters:
		path - The directory string

	Returns:
		Returns 0 on success, 1 on failure.

	Remarks:
		- The string is treated as zero-terminated string.
*/
int fs_parent_dir(char *path);

/*
	Function: fs_remove
		Deletes the file with the specified name.

	Parameters:
		filename - The file to delete

	Returns:
		Returns 0 on success, 1 on failure.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int fs_remove(const char *filename);

/*
	Function: fs_rename
		Renames the file or directory. If the paths differ the file will be moved.

	Parameters:
		oldname - The actual name
		newname - The new name

	Returns:
		Returns 0 on success, 1 on failure.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int fs_rename(const char *oldname, const char *newname);

/*
	Function: fs_read
		Reads a whole file into memory and returns its contents.

	Parameters:
		name - The filename to read.
		result - Receives the file's contents.
		result_len - Receives the file's length.

	Returns:
		Returns 0 on success, 1 on failure.

	Remarks:
		- Does NOT guarantee that there are no internal null bytes.
		- Guarantees that result will contain zero-termination.
		- The result must be freed after it has been used.
*/
int fs_read(const char *name, void **result, unsigned *result_len);

/*
	Function: fs_read_str
		Reads a whole file into memory and returns its contents,
		guaranteeing a null-terminated string with no internal null
		bytes.

	Parameters:
		name - The filename to read.

	Returns:
		Returns the file's contents on success, null on failure.

	Remarks:
		- Guarantees that there are no internal null bytes.
		- Guarantees that result will contain zero-termination.
		- The result must be freed after it has been used.
*/
char *fs_read_str(const char *name);

/*
	Function: fs_file_time
		Gets the creation and the last modification date of a file.

	Parameters:
		name - The filename.
		created - Pointer to time_t
		modified - Pointer to time_t

	Returns:
		0 on success non-zero on failure

	Remarks:
		- Returned time is in seconds since UNIX Epoch
*/
int fs_file_time(const char *name, time_t *created, time_t *modified);

#ifdef __cplusplus
}
#endif

#endif
