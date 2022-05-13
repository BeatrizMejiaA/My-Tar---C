#include "my_tarlib.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
// only print in verbose mode
#define V_PRINT(f, fmt, ...) if (verbosity) { fprintf(f, fmt "\n", ##__VA_ARGS__); }
// generic error
#define ERROR(fmt, ...) fprintf(stderr, "Error: " fmt "\n", ##__VA_ARGS__); return -1;
// capture errno when erroring
#define RC_ERROR(fmt, ...) const int rc = errno; ERROR(fmt, ##__VA_ARGS__); return -1;
#define WRITE_ERROR(fmt, ...) { ERROR(fmt, ##__VA_ARGS__); tar_free(*archive); *archive = NULL; return -1; }
#define EXIST_ERROR(fmt, ...) const int rc = errno; if (rc != EEXIST) { ERROR(fmt, ##__VA_ARGS__); return -1; }

// force read() to complete
static int read_size(int fd, char * buf, int size);

// force write() to complete
static int write_size(int fd, char * buf, int size);

// convert octal string to unsigned integer
static unsigned int oct2uint(char * oct, unsigned int size);

// check if a buffer is zeroed
static int iszeroed(char * buf, size_t size);

// make directory recursively
static int recursive_mkdir(const char * dir, const unsigned int mode);

int tar_read(const int fd, struct tar_t ** archive, const char verbosity){
    if (fd < 0){
        ERROR("Bad file descriptor");
    }

    if (!archive || *archive){
        ERROR("Bad archive");
    }

    unsigned int offset = 0;
    int count = 0;

    struct tar_t ** tar = archive;
    char update = 1;

    for(count = 0; ; count++){
        *tar = malloc(sizeof(struct tar_t));
        if (update && (read_size(fd, (*tar) -> block, 512) != 512)){
            V_PRINT(stderr, "Error: Bad read. Stopping");
            tar_free(*tar);
            *tar = NULL;
            break;
        }

        update = 1;
        // if current block is all zeros
        if (iszeroed((*tar) -> block, 512)){
            if (read_size(fd, (*tar) -> block, 512) != 512){
                V_PRINT(stderr, "Error: Bad read. Stopping");
                tar_free(*tar);
                *tar = NULL;
                break;
            }

            // check if next block is all zeros as well
            if (iszeroed((*tar) -> block, 512)){
                tar_free(*tar);
                *tar = NULL;

                // skip to end of record
                if (lseek(fd, RECORDSIZE - (offset % RECORDSIZE), SEEK_CUR) == (off_t) (-1)){
                    RC_ERROR("Unable to seek file: %s", strerror(rc));
                }

                break;
            }

            update = 0;
        }

        // set current entry's file offset
        (*tar) -> begin = offset;

        // skip over data and unfilled block
        unsigned int jump = oct2uint((*tar) -> size, 11);
        if (jump % 512){
            jump += 512 - (jump % 512);
        }

        // move file descriptor
        offset += 512 + jump;
        if (lseek(fd, jump, SEEK_CUR) == (off_t) (-1)){
            RC_ERROR("Unable to seek file: %s", strerror(rc));
        }

        // ready next value
        tar = &((*tar) -> next);
    }

    return count;
}

int tar_write(const int fd, struct tar_t ** archive, const int filecount, const char * files[], const char verbosity){
    if (fd < 0){
        ERROR("Bad file descriptor");
    }

    if (!archive){
        ERROR("Bad archive");
    }

    // where file descriptor offset is
    int offset = 0;

    // if there is old data
    struct tar_t ** tar = archive;
    if (*tar){
        // skip to last entry
        while (*tar && (*tar) -> next){
            tar = &((*tar) -> next);
        }

        // get offset past final entry
        unsigned int jump = 512 + oct2uint((*tar) -> size, 11);
        if (jump % 512){
            jump += 512 - (jump % 512);
        }

        // move file descriptor
        offset = (*tar) -> begin + jump;
        if (lseek(fd, offset, SEEK_SET) == (off_t) (-1)){
            RC_ERROR("Unable to seek file: %s", strerror(rc));
        }
        tar = &((*tar) -> next);
    }

    // write entries first
    if (write_entries(fd, tar, archive, filecount, files, &offset, verbosity) < 0){
        WRITE_ERROR("Failed to write entries");
    }

    // write ending data
    if (write_end_data(fd, offset, verbosity) < 0){
        ERROR("Failed to write end data");
    }

    // clear original names from data
    tar = archive;
    while (*tar){
        memset((*tar) -> name, 0, 100);
        tar = &((*tar) -> next);
    }
    return offset;
}

void tar_free(struct tar_t * archive){
    while (archive){
        struct tar_t * next = archive -> next;
        free(archive);
        archive = next;
    }
}

int tar_ls(FILE * f, struct tar_t * archive, const int filecount, const char * files[], const char verbosity){
    if (!verbosity){
        return 0;
    }

    if (filecount && !files){
        ERROR("Non-zero file count provided, but file list is NULL");
    }

    while (archive){
        if (ls_entry(f, archive, filecount, files, verbosity) < 0){
            return -1;
        }
        archive = archive -> next;
    }

    return 0;
}

int exist_duplicates_ahead(struct tar_t * archive) {
    while (archive && archive->next){
        if (!strncmp(archive -> block, archive->next->block, MAX(strlen(archive -> block), strlen(archive -> next -> block)) + 1)){
            return 1;
        }
        archive = archive -> next;
    }
    return 0;
}

int tar_extract(const int fd, struct tar_t * archive, const int filecount, const char * files[], const char verbosity){
    int ret = 0;

    // extract entries with given names
    if (filecount){
        if (!files){
            ERROR("Received non-zero file count but got NULL file list");
        }

        while (archive){
            for(int i = 0; i < filecount; i++){
                if (!strncmp(archive -> name, files[i], MAX(strlen(archive -> name), strlen(files[i])))){
                    if (lseek(fd, archive -> begin, SEEK_SET) == (off_t) (-1)){
                        RC_ERROR("Unable to seek file: %s", strerror(rc));
                    }

                    if (extract_entry(fd, archive, verbosity) < 0){
                        ret = -1;
                    }
                    break;
                }
            }
            archive = archive -> next;
        }
    }
    // extract all
    else{
        // move offset to beginning
        if (lseek(fd, 0, SEEK_SET) == (off_t) (-1)){
            RC_ERROR("Unable to seek file: %s", strerror(rc));
        }

        // extract each entry
        while (archive){
            //if (!exist_duplicates_ahead(archive)){
                if (extract_entry(fd, archive, verbosity) < 0){
                    ret = -1;
                }
            //}
            archive = archive -> next;
        }
    }

    return ret;
}

int tar_update(const int fd, struct tar_t ** archive, const int filecount, const char * files[], const char verbosity){
    if (!filecount){
        return 0;
    }

    if (filecount && !files){
        ERROR("Non-zero file count provided, but file list is NULL");
    }

    // buffer for subset of files that need to be updated
    char ** newer = (char **) malloc(sizeof(char *) * filecount);
    struct stat st;
    int currentNewer = 0;
    int all = 1;

    // check each source to see if it was updated
    struct tar_t *tar = *archive;
    for(int i = 0; i < filecount; i++){
        // make sure original file exists
        if (lstat(files[i], &st)){
            all = 0;
            RC_ERROR("Could not stat %s: %s", files[i], strerror(rc));
        }

        // find the file in the archive
        struct tar_t * old = exists(tar, files[i], 1);

        char* new_file = malloc(strlen(files[i])+1);
        memset(new_file,0,strlen(files[i])+1);

        // if there is an older version, check its timestamp
        if (old){

#if MY_TAR_ENV == 'L'
            double rounded = lround(st.st_mtim.tv_nsec/1.0e6);
            long long int milliseconds = st.st_mtim.tv_sec*1000 + rounded;
            milliseconds = milliseconds / 1000;
            if (milliseconds > oct2uint(old -> mtime, 11)){
                strncpy(new_file, files[i], strlen(files[i]));
                V_PRINT(stdout, "%s", files[i]);
                newer[currentNewer] = new_file;
                currentNewer++;
            }
#else
            if (st.st_mtimespec.tv_sec > oct2uint(old -> mtime, 11)){

                strncpy(new_file, files[i], strlen(files[i]));
                V_PRINT(stdout, "%s", files[i]);
                newer[currentNewer] = new_file;
                currentNewer++;
            }
#endif


        }
        // if there is no older version, just add it
        else{
            strncpy(new_file, files[i], strlen(files[i]));
            V_PRINT(stdout, "%s", files[i]);
            newer[currentNewer] = new_file;
            currentNewer++;
        }
    }



    // update listed files only
    if (tar_write(fd, archive, currentNewer, (const char **) newer, verbosity) < 0){
        ERROR("Unable to update archive");
    }

    // cleanupp
    for(int i = 0; i < currentNewer; i++){
        free(newer[i]);
    }
    free(newer);

    return all?0:-1;
}

struct tar_t * exists(struct tar_t * archive, const char * filename, const char ori){
    while (archive){
        if (!exist_duplicates_ahead(archive)){
            if (ori){
                if (!strncmp(archive -> block, filename, MAX(strlen(archive -> block), strlen(filename)) + 1)){
                    return archive;
                }
            }
            else{
                if (!strncmp(archive -> name, filename, MAX(strlen(archive -> name), strlen(filename)) + 1)){
                    return archive;
                }
            }
        }    
        archive = archive -> next;
    }
    return NULL;
}

int counting_slashs(const char* filename){
    int count = 0;
    while(*filename != '\0'){
        if (*filename == '/'){
            count++;
        }
        filename++;
    }
    return count;
}

int counting_files(const char** files){
    int count = 0;
    while(*files){
            count++;
            files++;
    }
    return count;
}

int format_tar_data(struct tar_t * entry, const char * filename, const char verbosity){
    if (!entry){
        ERROR("Bad destination entry");
    }

    struct stat st;
    if (lstat(filename, &st)){
        RC_ERROR("Cannot stat %s: %s", filename, strerror(rc));
    }

    // remove relative path
    int move = 0;
    if (!strncmp(filename, "/", 1) && counting_slashs(filename) == 1){
        move = 1;
    }
    else if (!strncmp(filename, "./", 2)){
        move = 2;
    }
    else if (!strncmp(filename, "../", 3)){
        move = 3;
    }

    // start putting in new data (all fields are NULL terminated ASCII strings)
    memset(entry, 0, sizeof(struct tar_t));
    strncpy(entry -> original_name, filename, 100);
    strncpy(entry -> name, filename + move, 100);
    snprintf(entry -> mode,  sizeof(entry -> mode),  "%07o", st.st_mode & 0777);
    snprintf(entry -> uid,   sizeof(entry -> uid),   "%07o", st.st_uid);
    snprintf(entry -> gid,   sizeof(entry -> gid),   "%07o", st.st_gid);
    snprintf(entry -> size,  sizeof(entry -> size),  "%011o", (int) st.st_size);
    snprintf(entry -> mtime, sizeof(entry -> mtime), "%011o", (int) st.st_mtime);
    strncpy(entry -> group, "None", 5);                     // default value
    memcpy(entry -> ustar, "ustar  \x00", 8);

    // figure out filename type and fill in type-specific fields
    switch (st.st_mode & S_IFMT) {
            case S_IFREG:
                entry -> type = NORMAL;
                break;
            case S_IFLNK:
                entry -> type = SYMLINK;

                // file size is 0, but will print link size
                memset(entry -> size, '0', sizeof(entry -> size) - 1);

                // get link name
                if (readlink(filename, entry -> link_name, 100) < 0){
                    RC_ERROR("Could not read link %s: %s", filename, strerror(rc));
                }
                break;
            case S_IFCHR:
                entry -> type = CHAR;
                // get character device major and minor values
                snprintf(entry -> major, sizeof(entry -> major), "%07o", major(st.st_rdev));
                snprintf(entry -> minor, sizeof(entry -> minor), "%07o", minor(st.st_rdev));
                break;
            case S_IFBLK:
                entry -> type = BLOCK;
                // get block device major and minor values
                snprintf(entry -> major, sizeof(entry -> major), "%07o", major(st.st_rdev));
                snprintf(entry -> minor, sizeof(entry -> minor), "%07o", minor(st.st_rdev));
                break;
            case S_IFDIR:
                memset(entry -> size, '0', 11);
                entry -> type = DIRECTORY;
                break;
            case S_IFIFO:
                entry -> type = FIFO;
                break;
            case S_IFSOCK:
                entry -> type = -1;
                ERROR("Error: Cannot tar socket");
            default:
                entry -> type = -1;
                ERROR("Error: Unknown filetype");
    }

    // get username
    struct passwd pwd;
    char buffer[4096];
    struct passwd * result = NULL;
    if (getpwuid_r(st.st_uid, &pwd, buffer, sizeof(buffer), &result)) {
        const int err = errno;
        V_PRINT(stderr, "Warning: Unable to get username of uid %u for entry '%s': %s", st.st_uid, filename, strerror(err));
    }

    strncpy(entry -> owner, buffer, sizeof(entry -> owner) - 1);

    // get group name
    struct group * grp = getgrgid(st.st_gid);
    if (grp){
        strncpy(entry -> group, grp -> gr_name, sizeof(entry -> group) - 1);
    }

    // get the checksum
    calculate_checksum(entry);

    return 0;
}

unsigned int calculate_checksum(struct tar_t * entry){
    // use spaces for the checksum bytes while calculating the checksum
    memset(entry -> check, ' ', 8);

    // sum of entire metadata
    unsigned int check = 0;
    for(int i = 0; i < 512; i++){
        check += (unsigned char) entry -> block[i];
    }

    snprintf(entry -> check, sizeof(entry -> check), "%06o0", check);

    entry -> check[6] = '\0';
    entry -> check[7] = ' ';
    return check;
}

int ls_entry(FILE * f, struct tar_t * entry, const int filecount, const char * files[], const char verbosity){
    if (!verbosity){
        return 0;
    }

    if (filecount && !files){
        V_PRINT(stderr, "Error: Non-zero file count given but no files given");
        return -1;
    }

    // figure out whether or not to print
    // if no files were specified, print everything
    char print = !filecount;
    // otherwise, search for matching names
    for(int i = 0; i < filecount; i++){
        if (strncmp(entry -> name, files[i], MAX(strlen(entry -> name), strlen(files[i])))){
            print = 1;
            break;
        }
    }

    if (print){
        if (verbosity > 1){
            const mode_t mode = oct2uint(entry -> mode, 7);
            const char mode_str[26] = { "-hlcbdp-"[entry -> type?entry -> type - '0':0],
                                        mode & S_IRUSR?'r':'-',
                                        mode & S_IWUSR?'w':'-',
                                        mode & S_IXUSR?'x':'-',
                                        mode & S_IRGRP?'r':'-',
                                        mode & S_IWGRP?'w':'-',
                                        mode & S_IXGRP?'x':'-',
                                        mode & S_IROTH?'r':'-',
                                        mode & S_IWOTH?'w':'-',
                                        mode & S_IXOTH?'x':'-',
                                        0};
            fprintf(f, "%s %s/%s ", mode_str, entry -> owner, entry -> group);
            char size_buf[22] = {0};
            int rc = -1;
            switch (entry -> type){
                case REGULAR: case NORMAL: case CONTIGUOUS:
                    rc = sprintf(size_buf, "%u", oct2uint(entry -> size, 11));
                    break;
                    case HARDLINK: case SYMLINK: case DIRECTORY: case FIFO:
                        rc = sprintf(size_buf, "%u", oct2uint(entry -> size, 11));
                        break;
                        case CHAR: case BLOCK:
                            rc = sprintf(size_buf, "%d,%d", oct2uint(entry -> major, 7), oct2uint(entry -> minor, 7));
                            break;
            }

            if (rc < 0){
                ERROR("Failed to write length");
            }

            fprintf(f, "%s", size_buf);

            time_t mtime = oct2uint(entry -> mtime, 11);
            struct tm * time = localtime(&mtime);
            fprintf(f, " %d-%02d-%02d %02d:%02d ", time -> tm_year + 1900, time -> tm_mon + 1, time -> tm_mday, time -> tm_hour, time -> tm_min);
        }

        fprintf(f, "%s", entry -> name);

        if (verbosity > 1){
            switch (entry -> type){
                case HARDLINK:
                    fprintf(f, " link to %s", entry -> link_name);
                    break;
                    case SYMLINK:
                        fprintf(f, " -> %s", entry -> link_name);
                        break;
                        break;
            }
        }

        fprintf(f, "\n");
    }

    return 0;
}

int extract_entry(const int fd, struct tar_t * entry, const char verbosity){
    V_PRINT(stdout, "%s", entry -> name);

    if ((entry -> type == REGULAR) || (entry -> type == NORMAL) || (entry -> type == CONTIGUOUS)){
        // create intermediate directories
        size_t len = strlen(entry -> name);
        if (!len)
        {
            ERROR("Attempted to extract entry with empty name");
        }

        char * path = calloc(len + 1, sizeof(char));
        strncpy(path, entry -> name, len);

        // remove file from path
        while (--len && (path[len] != '/'));
        path[len] = '\0';   // if nothing was found, path is terminated

        if (recursive_mkdir(path, DEFAULT_DIR_MODE) < 0){
            V_PRINT(stderr, "Could not make directory %s", path);
            free(path);
            return -1;
        }
        free(path);

        // create file
        const unsigned int size = oct2uint(entry -> size, 11);
        //int f = open(entry -> name, O_WRONLY | O_CREAT | O_TRUNC, oct2uint(entry -> mode, 7) & 0777),S_IRWXU;
        int f = open(entry -> name, O_WRONLY | O_CREAT | O_TRUNC,S_IRWXU);
        if (f < 0){
            RC_ERROR("Unable to open file %s: %s", entry -> name, strerror(rc));
        }

        // move archive pointer to data location
        if (lseek(fd, 512 + entry -> begin, SEEK_SET) == (off_t) (-1)){
            RC_ERROR("Bad index: %s", strerror(rc));
        }

        // copy data to file
        char buf[512];
        unsigned int got = 0;
        while (got < size){
            int r;
            if ((r = read_size(fd, buf, MIN(size - got, 512))) < 0){
                EXIST_ERROR("Unable to read from archive: %s", strerror(rc));
            }

            if (write(f, buf, r) != r){
                EXIST_ERROR("Unable to write to %s: %s", entry -> name, strerror(rc));
            }

            got += r;
        }

        close(f);
        //free(f);
    }
    else if ((entry -> type == CHAR) || (entry -> type == BLOCK)){
        if (mknod(entry -> name, oct2uint(entry -> mode, 7), (oct2uint(entry -> major, 7) << 20) | oct2uint(entry -> minor, 7)) < 0){
            EXIST_ERROR("Unable to make device %s: %s", entry -> name, strerror(rc));
        }
    }
    else if (entry -> type == HARDLINK){
        if (link(entry -> link_name, entry -> name) < 0){
                    EXIST_ERROR("Unable to create hardlink %s: %s", entry -> name, strerror(rc));
        }
    }
    else if (entry -> type == SYMLINK){
        if (symlink(entry -> link_name, entry -> name) < 0){
            EXIST_ERROR("Unable to make symlink %s: %s", entry -> name, strerror(rc));
        }
    }
    else if (entry -> type == CHAR){
        if (mknod(entry -> name, S_IFCHR | (oct2uint(entry -> mode, 7) & 0777), (oct2uint(entry -> major, 7) << 20) | oct2uint(entry -> minor, 7)) < 0){
            EXIST_ERROR("Unable to create directory %s: %s", entry -> name, strerror(rc));
        }
    }
    else if (entry -> type == BLOCK){
        if (mknod(entry -> name, S_IFBLK | (oct2uint(entry -> mode, 7) & 0777), (oct2uint(entry -> major, 7) << 20) | oct2uint(entry -> minor, 7)) < 0){
            EXIST_ERROR("Unable to create directory %s: %s", entry -> name, strerror(rc));
        }
    }
    else if (entry -> type == DIRECTORY){
        if (recursive_mkdir(entry -> name, oct2uint(entry -> mode, 7) & 0777) < 0){
            EXIST_ERROR("Unable to create directory %s: %s", entry -> name, strerror(rc));
        }
    }
    else if (entry -> type == FIFO){
        if (mkfifo(entry -> name, oct2uint(entry -> mode, 7) & 0777) < 0){
            EXIST_ERROR("Unable to make pipe %s: %s", entry -> name, strerror(rc));
        }
    }
    return 0;
}

int write_entries(const int fd, struct tar_t ** archive, struct tar_t ** head, const int filecount, const char * files[], int * offset, const char verbosity){
    if (fd < 0){
        ERROR("Bad file descriptor");
    }

    if (!archive || *archive){
        ERROR("Bad archive");
    }

    if (filecount && !files){
        ERROR("Non-zero file count provided, but file list is NULL");
    }

    // add new data
    struct tar_t ** tar = archive;  // current entry
    for(int i = 0; i < filecount; i++){
        *tar = malloc(sizeof(struct tar_t));

        // stat file
        if (format_tar_data(*tar, files[i], verbosity) < 0){
            WRITE_ERROR("Failed to stat %s", files[i]);
        }

        (*tar) -> begin = *offset;

        // directories need special handling
        if ((*tar) -> type == DIRECTORY){
            // save parent directory name (source will change)
            const size_t len = strlen((*tar) -> name);
            char * parent = calloc(len + 1, sizeof(char));
            strncpy(parent, (*tar) -> name, len);

            // add a '/' character to the end
            if ((len < 99) && ((*tar) -> name[len - 1] != '/')){
                (*tar) -> name[len] = '/';
                (*tar) -> name[len + 1] = '\0';
                calculate_checksum(*tar);
            }

            V_PRINT(stdout, "Writing %s", (*tar) -> name);

            // write metadata to (*tar) file
            if (write_size(fd, (*tar) -> block, 512) != 512){
                WRITE_ERROR("Failed to write metadata to archive");
            }

            // go through directory
            DIR * d = opendir(parent);
            if (!d){
                WRITE_ERROR("Cannot open directory %s", parent);
            }

            struct dirent * dir;
            while ((dir = readdir(d))){
                // if not special directories . and ..
                const size_t sublen = strlen(dir -> d_name);
                if (strncmp(dir -> d_name, ".", sublen) && strncmp(dir -> d_name, "..", sublen)){
                    char * path = calloc(len + sublen + 2, sizeof(char));
                    sprintf(path, "%s/%s", parent, dir -> d_name);

                    // recursively write each subdirectory
                    if (write_entries(fd, &((*tar) -> next), head, 1, (const char **) &path, offset, verbosity) < 0){
                        WRITE_ERROR("Recurse error");
                    }

                    // go to end of new data
                    while ((*tar) -> next){
                        tar = &((*tar) -> next);
                    }

                    free(path);
                }
            }
            closedir(d);

            free(parent);

            tar = &((*tar) -> next);
        }
        else{ // if (((*tar) -> type == REGULAR) || ((*tar) -> type == NORMAL) || ((*tar) -> type == CONTIGUOUS) || ((*tar) -> type == SYMLINK) || ((*tar) -> type == CHAR) || ((*tar) -> type == BLOCK) || ((*tar) -> type == FIFO)){
            V_PRINT(stdout, "Writing %s", (*tar) -> name);

            (*tar) -> type = REGULAR;

            // write metadata to (*tar) file
            if (write_size(fd, (*tar) -> block, 512) != 512){
                WRITE_ERROR("Failed to write metadata to archive");
            }

            if (((*tar) -> type == REGULAR) || ((*tar) -> type == NORMAL) || ((*tar) -> type == CONTIGUOUS)){

                int f = open((*tar) -> name, O_RDONLY);
                if (f < 0){
                    WRITE_ERROR("Could not open %s", files[i]);
                }

                int r = 0;
                char buf[512];
                while ((r = read_size(f, buf, 512)) > 0){
                    if (write_size(fd, buf, r) != r){
                        RC_ERROR("Could not write to archive: %s", strerror(rc));
                    }
                }

                close(f);
            }

            // pad data to fill block
            const unsigned int size = oct2uint((*tar) -> size, 11);
            const unsigned int pad = 512 - size % 512;
            if (pad != 512){
                for(unsigned int j = 0; j < pad; j++){
                    if (write_size(fd, "\0", 1) != 1){
                        WRITE_ERROR("Could not write padding data");
                    }
                }
                *offset += pad;
            }
            *offset += size;
            tar = &((*tar) -> next);
        }

        // add metadata size
        *offset += 512;
    }

    return 0;
}

int write_end_data(const int fd, int size, const char verbosity){
    if (fd < 0){
        return -1;
    }

    // complete current record
    const int pad = RECORDSIZE - (size % RECORDSIZE);
    for(int i = 0; i < pad; i++){
        if (write(fd, "\0", 1) != 1){
            V_PRINT(stderr, "Error: Unable to close tar file");
            return -1;
        }
    }

    // if the current record does not have 2 blocks of zeros, add a whole other record
    if (pad < (2 * BLOCKSIZE)){
        for(int i = 0; i < RECORDSIZE; i++){
            if (write(fd, "\0", 1) != 1){
                V_PRINT(stderr, "Error: Unable to close tar file");
                return -1;
            }
        }
        return pad + RECORDSIZE;
    }

    return pad;
}

int read_size(int fd, char * buf, int size){
    int got = 0, rc;
    while ((got < size) && ((rc = read(fd, buf + got, size - got)) > 0)){
        got += rc;
    }
    return got;
}

int write_size(int fd, char * buf, int size){
    int wrote = 0, rc;
    while ((wrote < size) && ((rc = write(fd, buf + wrote, size - wrote)) > 0)){
        wrote += rc;
    }
    return wrote;
}

unsigned int oct2uint(char * oct, unsigned int size){
    unsigned int out = 0;
    unsigned int i = 0;
    while ((i < size) && oct[i]){
        out = (out << 3) | (unsigned int) (oct[i++] - '0');
    }
    return out;
}

int iszeroed(char * buf, size_t size){
    for(size_t i = 0; i < size; buf++, i++){
        if (* (char *) buf){
            return 0;
        }
    }
    return 1;
}

int recursive_mkdir(const char * dir, const unsigned int mode){
    int rc = 0;
    const size_t len = strlen(dir);

    if (!len){
        return 0;
    }

    char * path = calloc(len + 1, sizeof(char));
    strncpy(path, dir, len);

    // remove last '/'
    if (path[len - 1] ==  '/'){
        path[len - 1] = 0;
    }

    // all subsequent directories do not exist
    for(char * p = path + 1; *p; p++){
        if (*p == '/'){
            *p = '\0';

            if ((rc = mkdir(path, mode?mode:DEFAULT_DIR_MODE))){
                EXIST_ERROR("Could not create directory %s: %s", path, strerror(rc));
            }

            *p = '/';
        }
    }

    if (mkdir(path, mode?mode:DEFAULT_DIR_MODE) < 0){
        EXIST_ERROR("Could not create directory %s: %s", path, strerror(rc));
    }

    free(path);
    return 0;
}