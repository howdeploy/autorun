#ifndef AUTORUN_FTP_SERVER_VFS_H
#define AUTORUN_FTP_SERVER_VFS_H

#include <dirent.h>
struct FtpVfsFile { int fd, valid; };
struct FtpVfsDir { DIR *fd; };
struct FtpVfsDirEntry { struct dirent *entry; };
int autorun_ftp_set_root( const char *root );

#endif
