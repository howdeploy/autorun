#include "ftpsrv_vfs.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FTP_LOCAL_PATH 1024
static char ftp_root[512];

int autorun_ftp_set_root( const char *root )
{
    struct stat st;
    size_t size = strlen( root );
    if (!size || size >= sizeof(ftp_root) || lstat( root, &st ) || !S_ISDIR(st.st_mode)) return 0;
    memcpy( ftp_root, root, size + 1 );
    while (size && ftp_root[size - 1] == '/') ftp_root[--size] = 0;
    return size != 0;
}

/* Resolve every path at the VFS boundary; no client path may select a device or symlink. */
static int local_path( const char *path, char out[FTP_LOCAL_PATH], int mutation )
{
    size_t length = strlen( ftp_root ), root_length = length;
    struct stat st;

    if (!length || !path || path[0] != '/') { errno = EACCES; return 0; }
    memcpy( out, ftp_root, length + 1 );
    while (*path)
    {
        size_t part;
        while (*path == '/') path++;
        if (!*path) break;
        part = strcspn( path, "/" );
        if (part == 1 && path[0] == '.') { path += part; continue; }
        if (part == 2 && !memcmp( path, "..", 2 ))
        { errno = EACCES; return 0; }
        for (size_t i = 0; i < part; i++)
            if ((unsigned char)path[i] < 32 || path[i] == 127 || path[i] == ':' || path[i] == '\\')
            { errno = EACCES; return 0; }
        if (length + part + 2 > FTP_LOCAL_PATH) { errno = ENAMETOOLONG; return 0; }
        out[length++] = '/';
        memcpy( out + length, path, part );
        length += part;
        out[length] = 0;
        if (!lstat( out, &st ))
        {
            if (S_ISLNK(st.st_mode)) { errno = EACCES; return 0; }
        }
        else if (errno != ENOENT) return 0;
        path += part;
    }
    assert( !memcmp( out, ftp_root, root_length ) );
    if (mutation && length == root_length) { errno = EACCES; return 0; }
    return 1;
}

int ftp_vfs_open( struct FtpVfsFile *file, const char *path, enum FtpVfsOpenMode mode )
{
    char local[FTP_LOCAL_PATH];
    int flags = mode == FtpVfsOpenMode_READ ? O_RDONLY :
                O_WRONLY | O_CREAT | (mode == FtpVfsOpenMode_APPEND ? O_APPEND : O_TRUNC);
    file->valid = 0;
    file->fd = -1;
    if (!local_path( path, local, mode != FtpVfsOpenMode_READ )) return -1;
    file->fd = open( local, flags, 0666 );
    file->valid = file->fd >= 0;
    return file->fd;
}

int ftp_vfs_read( struct FtpVfsFile *file, void *data, size_t size ) { return read( file->fd, data, size ); }
int ftp_vfs_write( struct FtpVfsFile *file, const void *data, size_t size )
{
    size_t done = 0;
    while (done < size)
    {
        ssize_t count = write( file->fd, (const char *)data + done, size - done );
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { if (!count) errno = EIO; return -1; }
        done += count;
    }
    return done;
}
int ftp_vfs_seek( struct FtpVfsFile *file, const void *data, size_t size, size_t offset )
{
    (void)data; (void)size;
    return lseek( file->fd, offset, SEEK_SET ) < 0 ? -1 : 0;
}
int ftp_vfs_isfile_open( struct FtpVfsFile *file ) { return file->valid && file->fd >= 0; }
int ftp_vfs_close( struct FtpVfsFile *file )
{
    int result = ftp_vfs_isfile_open( file ) ? close( file->fd ) : 0;
    file->valid = 0; file->fd = -1;
    return result;
}

int ftp_vfs_opendir( struct FtpVfsDir *dir, const char *path )
{
    char local[FTP_LOCAL_PATH];
    dir->fd = NULL;
    if (!local_path( path, local, 0 )) return -1;
    dir->fd = opendir( local );
    return dir->fd ? 0 : -1;
}
const char *ftp_vfs_readdir( struct FtpVfsDir *dir, struct FtpVfsDirEntry *entry )
{
    while ((entry->entry = readdir( dir->fd )))
        if (strcmp( entry->entry->d_name, "." ) && strcmp( entry->entry->d_name, ".." ))
            return entry->entry->d_name;
    return NULL;
}
int ftp_vfs_isdir_open( struct FtpVfsDir *dir ) { return dir->fd != NULL; }
int ftp_vfs_closedir( struct FtpVfsDir *dir )
{
    int result = dir->fd ? closedir( dir->fd ) : 0;
    dir->fd = NULL;
    return result;
}
int ftp_vfs_lstat( const char *path, struct stat *st )
{
    char local[FTP_LOCAL_PATH];
    return local_path( path, local, 0 ) ? lstat( local, st ) : -1;
}
int ftp_vfs_stat( const char *path, struct stat *st ) { return ftp_vfs_lstat( path, st ); }
int ftp_vfs_dirlstat( struct FtpVfsDir *dir, const struct FtpVfsDirEntry *entry, const char *path, struct stat *st )
{
    (void)dir; (void)entry;
    return ftp_vfs_lstat( path, st );
}
int ftp_vfs_mkdir( const char *path )
{
    char local[FTP_LOCAL_PATH];
    return local_path( path, local, 1 ) ? mkdir( local, 0777 ) : -1;
}
int ftp_vfs_unlink( const char *path )
{
    char local[FTP_LOCAL_PATH];
    return local_path( path, local, 1 ) ? unlink( local ) : -1;
}
int ftp_vfs_rmdir( const char *path )
{
    char local[FTP_LOCAL_PATH];
    return local_path( path, local, 1 ) ? rmdir( local ) : -1;
}
int ftp_vfs_rename( const char *source, const char *destination )
{
    char from[FTP_LOCAL_PATH], to[FTP_LOCAL_PATH];
    if (!local_path( source, from, 1 ) || !local_path( destination, to, 1 )) return -1;
    return rename( from, to );
}
int ftp_vfs_readlink( const char *path, char *out, size_t size )
{
    (void)path; (void)out; (void)size;
    errno = ENOTSUP;
    return -1;
}
const char *ftp_vfs_getpwuid( const struct stat *st ) { (void)st; return "switch"; }
const char *ftp_vfs_getgrgid( const struct stat *st ) { (void)st; return "switch"; }
