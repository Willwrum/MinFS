/*
A Very Verbose Minix Filesystem Reader
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define SECTOR_SIZE        512
#define SUPERBLOCK_OFFSET  1024
#define PART_TABLE_OFFSET  0x1BE

#define MINIX_MAGIC        0x4D5A
#define DIRECT_ZONES       7

typedef struct __attribute__((packed)) {
    uint8_t  bootind;
    uint8_t  start_head;
    uint8_t  start_sec;
    uint8_t  start_cyl;
    uint8_t  type;
    uint8_t  end_head;
    uint8_t  end_sec;
    uint8_t  end_cyl;
    uint32_t lFirst;
    uint32_t size;
} PartitionEntry;

typedef struct __attribute__((packed)) {
    uint32_t ninodes;
    uint16_t pad1;
    int16_t  i_blocks;
    int16_t  z_blocks;
    uint16_t firstdata;
    int16_t  log_zone_size;
    int16_t  pad2;
    uint32_t max_file;
    uint32_t zones;
    int16_t  magic;
    int16_t  pad3;
    uint16_t blocksize;
    uint8_t  subversion;
} Superblock;

typedef struct __attribute__((packed)) {
    uint16_t mode;
    uint16_t links;
    uint16_t uid;
    uint16_t gid;
    uint32_t size;
    int32_t atime;
    int32_t mtime;
    int32_t ctime;
    uint32_t zone[DIRECT_ZONES];
    uint32_t indirect;
    uint32_t two_indirect;
    uint32_t unused;
} Inode;

typedef struct __attribute__((packed)) {
    uint32_t inode;
    char     name[60];
} DirectoryEntry;

typedef struct {
    int fd;
    uint32_t base;
    Superblock sb;
} MinixFS;

void die(const char *msg);
void check_read_check(int fd, void *buf, size_t n, off_t off);
uint32_t get_base(int fd, int pnum);
void load_superblock(MinixFS *fs);
Inode read_inode(MinixFS *fs, uint32_t inum);
void list_root_directory(MinixFS *fs);
uint32_t lookup_in_directory(MinixFS *fs, Inode *dir, const char *name);
uint32_t lookup_path(MinixFS *fs, const char *path);
void print_structure_sizes(void);

int main(int argc, char *argv[]) {
    int opt;
    int pnum = -1;
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                pnum = atoi(optarg);
                break;
            default:
                fprintf(stderr, "usage: %s [-p partition] image\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "missing image file\n");
        return EXIT_FAILURE;
    }

    int fd = open(argv[optind], O_RDONLY);
    if (fd < 0) {
        die("open");
    }

    MinixFS fs;
    fs.fd = fd;
    fs.base = get_base(fd, pnum);
    printf("final base = %u bytes\n", fs.base);

    load_superblock(&fs);

    Inode root = read_inode(&fs, 1);

    printf("Root Inode:\n");
    printf("mode  = 0%o\n", root.mode);
    printf("size  = %u\n", root.size);
    printf("links = %u\n", root.links);

    for (int i = 0; i < DIRECT_ZONES; i++) {
        printf("zone[%d] = %u\n", i, root.zone[i]);
    }

    list_root_directory(&fs);

    printf("Looking up 'src' in root using lookup_in_directory\n");
    uint32_t src_ino = lookup_in_directory(&fs, &root, "src");
    if (src_ino == 0) {
        printf("src not found\n");
    } else {
        Inode src = read_inode(&fs, src_ino);

        printf("src Inode:\n");
        printf("mode  = 0%o\n", src.mode);
        printf("size  = %u\n", src.size);
        printf("links = %u\n", src.links);
    }

    printf("Looking up '/src' using lookup_path\n");
    src_ino = lookup_path(&fs, "/src");
    if (src_ino == 0) {
        printf("/src not found\n");
    } else {
        Inode src = read_inode(&fs, src_ino);

        printf("src Inode:\n");
        printf("mode  = 0%o\n", src.mode);
        printf("size  = %u\n", src.size);
        printf("links = %u\n", src.links);
    }

    print_structure_sizes();
    close(fd);

    printf("Good job, you made it to the end of the program!\n");
    return EXIT_SUCCESS;
}

void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void check_read_check(int fd, void *buf, size_t n, off_t off) {
    if (lseek(fd, off, SEEK_SET) < 0) {
        die("lseek");
    }

    ssize_t r = read(fd, buf, n);

    if (r < 0) {
        die("read");
    }

    if (r != (ssize_t)n) {
        fprintf(stderr, "Short read at offset %ld (got %ld bytes)\n",
                (long)off, (long)r);
        exit(EXIT_FAILURE);
    }
}

uint32_t get_base(int fd, int pnum) {
    uint8_t mbr[512];

    check_read_check(fd, mbr, sizeof(mbr), 0);

    int valid_mbr = (mbr[510] == 0x55 && mbr[511] == 0xAA);

    printf("MBR signature = 0x%02x 0x%02x\n", mbr[510], mbr[511]);

    if (!valid_mbr || pnum < 0) {
        printf("No valid partition table, so not partitioned? Using base = 0\n");
        return 0;
    }

    PartitionEntry *table =
        (PartitionEntry *)(mbr + PART_TABLE_OFFSET);
    PartitionEntry *p = &table[pnum];

    printf("Partition %d type = 0x%x\n", pnum, p->type);
    printf("Partition %d lFirst = %u\n", pnum, p->lFirst);

    if (p->type != 0x81) {
        fprintf(stderr, "Not a Minix partition\n");
        exit(EXIT_FAILURE);
    }

    return p->lFirst * SECTOR_SIZE;
}

void load_superblock(MinixFS *fs) {
    check_read_check(fs->fd, &fs->sb, sizeof(Superblock), fs->base + SUPERBLOCK_OFFSET);

    printf("Superblock offset = %u\n", fs->base + SUPERBLOCK_OFFSET);
    printf("Magic = 0x%x\n", fs->sb.magic);
    printf("Blocksize = %u\n", fs->sb.blocksize);
    printf("Ninodes = %u\n", fs->sb.ninodes);
    printf("Zones = %u\n", fs->sb.zones);
    printf("i_blocks = %d\n", fs->sb.i_blocks);
    printf("z_blocks = %d\n", fs->sb.z_blocks);

    if (fs->sb.magic != 0x4D5A && fs->sb.magic != 0x5A4D) {
        fprintf(stderr, "Invalid Minix superblock magic: 0x%x\n", fs->sb.magic);
        exit(EXIT_FAILURE);
    }

    printf("Congrats! You found the superblock!\n");
}

Inode read_inode(MinixFS *fs, uint32_t inum) {
    if (inum == 0 || inum > fs->sb.ninodes) {
        fprintf(stderr, "Invalid inode number: %u\n", inum);
        exit(EXIT_FAILURE);
    }

    uint32_t inode_table_block = 2 + fs->sb.i_blocks + fs->sb.z_blocks;
    uint32_t inode_table_start = fs->base + inode_table_block * fs->sb.blocksize;
    uint64_t inode_offset = inode_table_start + (inum - 1) * sizeof(Inode);

    Inode inode;
    check_read_check(fs->fd, &inode, sizeof(Inode), inode_offset);

    printf("Read inode %u at offset %lu\n", inum, (unsigned long)inode_offset);
    printf("Inode mode = 0%o\n", inode.mode);
    printf("Inode size = %u bytes\n", inode.size);

    return inode;
}

void list_root_directory(MinixFS *fs) {
    Inode root = read_inode(fs, 1);

    uint64_t dir_offset = fs->base + (root.zone[0] * fs->sb.blocksize);

    DirectoryEntry entry;

    int entries = root.size / sizeof(DirectoryEntry);

    printf("Directory entries:\n");

    for (int i = 0; i < entries; i++) {
        check_read_check(fs->fd, &entry, sizeof(entry), dir_offset + i * sizeof(entry));

        if (entry.inode == 0) {
            continue;
        }

        char name[61];
        memcpy(name, entry.name, 60);
        name[60] = '\0';
        
        printf("inode=%u name=%s\n", entry.inode, name);
    }
}

uint32_t lookup_in_directory(MinixFS *fs, Inode *dir, const char *name) {
    uint64_t dir_offset = fs->base + (dir->zone[0] * fs->sb.blocksize);
    DirectoryEntry entry;
    int entries = dir->size / sizeof(DirectoryEntry);

    for (int i = 0; i < entries; i++) {
        check_read_check(fs->fd, &entry, sizeof(entry), dir_offset + i * sizeof(entry));

        if (entry.inode == 0) {
            continue;
        }

        char entry_name[61];
        memcpy(entry_name, entry.name, 60);
        entry_name[60] = '\0';

        if (strcmp(entry_name, name) == 0) {
            return entry.inode;
        }
    }

    return 0;
}

uint32_t lookup_path(MinixFS *fs, const char *path)
{
    if (path[0] != '/') {
        fprintf(stderr, "Path must be absolute\n");
        exit(EXIT_FAILURE);
    }

    char *copy = strdup(path);
    char *saveptr = NULL;
    Inode current = read_inode(fs, 1); // root
    uint32_t inum = 1;
    char *token = strtok_r(copy, "/", &saveptr);

    while (token != NULL) {
        inum = lookup_in_directory(fs, &current, token);
        if (inum == 0) {
            free(copy);
            return 0;   // not found
        }
        current = read_inode(fs, inum);
        token = strtok_r(NULL, "/", &saveptr);
    }
    free(copy);
    return inum;
}

// Debug function to print the sizes of the structures
void print_structure_sizes(void) {
    printf("Structure Sizes:\n");
    printf("PartitionEntry : %zu\n", sizeof(PartitionEntry));
    printf("Superblock     : %zu\n", sizeof(Superblock));
    printf("Inode          : %zu\n", sizeof(Inode));
    printf("DirectoryEntry : %zu\n", sizeof(DirectoryEntry));
}

