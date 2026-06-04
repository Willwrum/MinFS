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
    const char *src;
    const char *dst;
} MingetArgs;

void parse_args(int argc, char **argv, MingetArgs *args)
{
    args->pnum = -1;
    args->snum = -1;
    args->verbose = 0;
    args->image = NULL;
    args->src = NULL;
    args->dst = NULL;

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
                    "usage: minget [-v] [-p part [-s subpart]]"
                    " image src [dst]\n");
                exit(EXIT_FAILURE);
        }
    }

    if (optind + 1 >= argc) {
        fprintf(stderr, "missing image or src path\n");
        exit(EXIT_FAILURE);
    }

    args->image = argv[optind];
    args->src = argv[optind + 1];

    if (optind + 2 < argc) {
        args->dst = argv[optind + 2];
    }
}

int main(int argc, char **argv) {
    MinixFS fs;
    MingetArgs args;

    parse_args(argc, argv, &args);

    fs.verbose = args.verbose;
    fs.fd = open(args.image, O_RDONLY);
    if (fs.fd < 0) {
        die("open");
    }
    fs.base = get_base(&fs, args.pnum, args.snum);
    load_superblock(&fs);

    uint32_t ino = lookup_path(&fs, args.src);
    Inode file = read_inode(&fs, ino);

    if (fs.verbose) {
        print_verbose_inode(&file);
    }

    if ((file.mode & 0100000) == 0) {
        fprintf(stderr, "not a regular file\n");
        exit(1);
    }

    copy_file(&fs, &file, args.dst);

    close(fs.fd);
}