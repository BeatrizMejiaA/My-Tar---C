
#ifndef __TAR__
#define __TAR__

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#if !defined(__APPLE__)
#include <sys/sysmacros.h>
#endif
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>


#define DEFAULT_DIR_MODE S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH // 0755

#define BLOCKSIZE       512
#define BLOCKING_FACTOR 20
#define RECORDSIZE      1024

// file type values (1 octet)
#define REGULAR          0
#define NORMAL          '0'
#define HARDLINK        '1'
#define SYMLINK         '2'
#define CHAR            '3'
#define BLOCK           '4'
#define DIRECTORY       '5'
#define FIFO            '6'
#define CONTIGUOUS      '7'

#define MY_TAR_ENV 'L'

// tar entry metadata structure (singly-linked list)
struct tar_t {
    char original_name[100];                // original filenme; only availible when writing into a tar
    unsigned int begin;                     // location of data in file (including metadata)
    union {
        union {
            // Pre-POSIX.1-1988 format
            struct {
                char name[100];             // file name
                char mode[8];               // permissions
                char uid[8];                // user id (octal)
                char gid[8];                // group id (octal)
                char size[12];              // size (octal)
                char mtime[12];             // modification time (octal)
                char check[8];              // sum of unsigned characters in block, with spaces in the check field while calculation is done (octal)
                char link;                  // link indicator
                char link_name[100];        // name of linked file
            };

            // UStar format (POSIX IEEE P1003.1)
            struct {
                char old[156];              // first 156 octets of Pre-POSIX.1-1988 format
                char type;                  // file type
                char also_link_name[100];   // name of linked file
                char ustar[8];              // ustar\000
                char owner[32];             // user name (string)
                char group[32];             // group name (string)
                char major[8];              // device major number
                char minor[8];              // device minor number
                char prefix[155];
            };
        };

        char block[512];                    // raw memory (500 octets of actual data, padded to 1 block)
    };

    struct tar_t * next;
};


int counting_files(const char** files);
int exist_duplicates_ahead(struct tar_t * archive);


// read the tar file
int tar_read(const int fd, struct tar_t ** archive, const char verbosity);

// write to a tar file
int tar_write(const int fd, struct tar_t ** archive, const int filecount, const char * files[], const char verbosity);

// free recursivelly
void tar_free(struct tar_t * archive);
// /////////////////////////////////////////////////////////////////////////////

// print files archived
int tar_ls(FILE * f, struct tar_t * archive, const int filecount, const char * files[], const char verbosity);

// extracts files from an archive
int tar_extract(const int fd, struct tar_t * archive, const int filecount, const char * files[], const char verbosity);

// append files just whe newes than the already archived
int tar_update(const int fd, struct tar_t ** archive, const int filecount, const char * files[], const char verbosity);

// check if file with original name/modified name exists
struct tar_t * exists(struct tar_t * archive, const char * filename, const char ori);

// read file and construct metadata
int format_tar_data(struct tar_t * entry, const char * filename, const char verbosity);

// calculate checksum (6 ASCII octet digits + NULL + space)
unsigned int calculate_checksum(struct tar_t * entry);

// print single entry
int ls_entry(FILE * f, struct tar_t * archive, const int filecount, const char * files[], const char verbosity);

// extracts a single entry
int extract_entry(const int fd, struct tar_t * entry, const char verbosity);

// write entries to a tar file
int write_entries(const int fd, struct tar_t ** archive, struct tar_t ** head, const int filecount, const char * files[], int * offset, const char verbosity);

// add blocks with zeros as tar especification
int write_end_data(const int fd, int size, const char verbosity);


#endif