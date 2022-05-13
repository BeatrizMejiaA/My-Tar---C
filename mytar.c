#include <stdio.h>

#include "my_tarlib.h"

#define MAX(x, y) (((x) > (y)) ? (x) : (y))

#ifndef DEFAULT_ARCHIVE
# define DEFAULT_ARCHIVE "tar.out"
#endif

int main(int argc, char * argv[]){

    if (((argc == 2) && (strncmp(argv[1], "help", MAX(strlen(argv[1]), 4)))) || (argc < 3)){
        //fprintf(stderr, "Usage: %s option(s) tarfile [sources]\n", argv[0]);
        //fprintf(stderr, "Usage: %s help\n", argv[0]);
       // return -1;
       fprintf(stdout, "Usage: %s options(s) tarfile [sources]\n"\
       "Usage: %s help\n"\
       "\n"\
       "    options (only one allowed at a time):\n"\
       "        -r - append files to archive\n"\
       "        -c - create a new archive\n"\
       "        -t - list archive contents to stdout\n"\
       "        -u - update just modified files\n"\
       "        -x - extract to disk from the archive\n"\
       "\n"\
       "    other options:\n"\
       "        v - make operation verbose\n"\
       "        f - get tar name from second argument instead default\n"\
       "\n"\
       "Ex: %s vl archive.tar\n"\
       , argv[0], argv[0], argv[0]);
       return 0;
    }

    if (argc == 2){
        fprintf(stdout, "Usage: %s options(s) tarfile [sources]\n"\
        "Usage: %s help\n"\
        "\n"\
                        "    options (only one allowed at a time):\n"\
                        "        -r - append files to archive\n"\
                        "        -c - create a new archive\n"\
                        "        -t - list archive contents to stdout\n"\
                        "        -u - update just modified files\n"\
                        "        -x - extract to disk from the archive\n"\
                        "\n"\
                        "    other options:\n"\
                        "        v - make operation verbose\n"\
                        "        f - get tar name from second argument instead default\n"\
                        "\n"\
                        "Ex: %s vl archive.tar\n"\
                        , argv[0], argv[0], argv[0]);
                        return 0;
    }

    argc -= 3;

    int rc,c,r,t,u,x = 0;
    c = 0,             // create
    r = 0,             // append
    t = 0,             // list
    u = 0,             // update
    x = 0;             // extract
    char verbosity = 0;     // 0: no print; 1: print file names; 2: print file properties
    char second_arg_is_arquive_name = 0;

    // parse options
    for(int i = 0; argv[1][i]; i++){
        switch (argv[1][i]){
            case 'c': c = 1; break;
            case 'r': r = 1; break;
            case 't': t = 1; break;
            case 'u': u = 1; break;
            case 'x': x = 1; break;
            case 'v': verbosity++; break;
            case 'f': second_arg_is_arquive_name++; break;
            case '-': break;
            default:
                fprintf(stderr, "Error: Bad option: %c\n", argv[1][i]);
                fprintf(stderr, "Do '%s help' for help\n", argv[0]);
                return 0;
                break;
        }
    }

    // make sure only one of these options was selected
    const char used = c + r + t + u + x;
    if (used > 1){
        fprintf(stderr, "Error: Cannot have so all of these flags at once\n");
        return -1;
    }
    else if (used < 1){
        fprintf(stderr, "Error: Need one of 'crtuxv' options set\n");
        return -1;
    }

    if (r || u){
        if (!second_arg_is_arquive_name){
            fprintf(stderr, "Error: Cannot append file(s) without f option ex: -rf tar.tar file1. it means tar file name followed by files to append.\n");
            return -1;
        }
    }

    const char * filename;
    const char ** files;
    if (second_arg_is_arquive_name) {
        filename = argv[2];
        files = (const char **) &argv[3];
    } else {
        filename = getenv ("TAPE");
        if (!filename)
            filename = DEFAULT_ARCHIVE;
        files = (const char **) &argv[2];
    }

    size_t filecount = counting_files(files);

    struct tar_t * archive = NULL;
    int fd = -1;
    if (c){             // create new file
        if ((fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR)) == -1){
            fprintf(stderr, "Error: Unable to open file %s\n", filename);
            return -1;
        }

        if (tar_write(fd, &archive, filecount, files, verbosity) < 0){
            rc = -1;
        }
    }
    else{
        // open existing file
        if ((fd = open(filename, O_RDWR)) < 0){
            fprintf(stderr, "Error: Unable to open file %s\n", filename);
            return -1;
        }

        // read in data
        if (tar_read(fd, &archive, verbosity) < 0){
            tar_free(archive);
            close(fd);
            return -1;
        }

        // perform operation
        if ((r && (tar_write(fd, &archive, filecount, files, verbosity) < 0))          ||  // append
        (t && (tar_ls(stdout, archive, filecount, files, verbosity + 1) < 0))      ||  // list entries
        (u && (tar_update(fd, &archive, filecount, files, verbosity) < 0))         ||  // update entries
        (x && (tar_extract(fd, archive, filecount, files, verbosity) < 0))             // extract entries
        ){
            fprintf(stderr, "Exiting with error due to previous error\n");
            rc = -1;
        }
    }

    tar_free(archive);
    close(fd);          // don't bother checking for fd < 0
    return rc;
}