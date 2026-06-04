#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "ffs.h"

typedef struct {
    int pnum;
    int snum;
    int verbose;
    const char *image;
    const char *path;
} MinlsArgs;

void parse_args(int argc, char **argv, MinlsArgs *args)
{
    args->pnum = -1;
    args->snum = -1;
    args->verbose = 0;
    args->image = NULL;
    args->path = "/";

    int opt;
    while ((opt = getopt(argc, argv, "vp:s:")) != -1) {
        switch (opt) {
            case 'v':
                args->verbose = 1;
                break;
            case 'p':
                args->pnum = atoi(optarg);
                break;
            case 's':
                args->snum = atoi(optarg);
                break;
            default:
                fprintf(stderr,
                    "usage: minls [-v] [-p part [-s subpart]] image [path]\n");
                exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "missing image file\n");
        exit(EXIT_FAILURE);
    }

    args->image = argv[optind];

    if (optind + 1 < argc) {
        args->path = argv[optind + 1];
    }
}

int main(int argc, char **argv) {
    MinixFS fs;
    MinlsArgs args;

    parse_args(argc, argv, &args);

    fs.verbose = args.verbose;
    fs.fd = open(args.image, O_RDONLY);
    if (fs.fd < 0) {
        die("open");
    }
    fs.base = get_base(&fs, args.pnum, args.snum);
    load_superblock(&fs);

    uint32_t ino = lookup_path(&fs, args.path);
    Inode dir = read_inode(&fs, ino);

    if (fs.verbose) {
        print_verbose_inode(&dir);
    }

    if ((dir.mode & 0040000) == 0) {
        fprintf(stderr, "not a directory\n");
        exit(1);
    }

    list_directory(&fs, &dir);

    close(fs.fd);
}