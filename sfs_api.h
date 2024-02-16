#ifndef SFS_API_H
#define SFS_API_H


#define MAXFILENAME		32

// create or mount sfs file system depends on parameter
// if param != 0 mksfs creates new sfs image
void mksfs(int fresh);

// returns next filename in sfs root directory
// returns 0 for success and -1 for error
int sfs_getnextfilename(char* fname);

// returns size of file for success and -1 for error
int sfs_getfilesize(const char* fname);

// opens file
// returns file descriptor for success and -1 for error
int sfs_fopen(char* fname);

// closes file
// returns 0 for success and -1 for error
int sfs_fclose(int fd);

// writes user data to file
// returns number of bytes writed for success and 0 for error
int sfs_fwrite(int fd, const char* buf, int size);

// reads user data from file
// returns number of bytes readed for success and 0 for error
int sfs_fread(int fd, char* buf, int size);

// set file position for file
// returns 0 for success and -1 for error
int sfs_fseek(int fd, int pos);

// removes file
// returns 0 for success and -1 for error
int sfs_remove(char* fname);

#endif
