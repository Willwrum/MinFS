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
    char name[60];
} DirectoryEntry;

typedef struct {
    int fd;
    uint32_t base;
    int verbose;
    Superblock sb;
} MinixFS;

void die(const char *msg);
void check_read_check(int fd, void *buf, size_t n, off_t off);
uint32_t get_base(MinixFS *fs, int pnum, int snum); 
void load_superblock(MinixFS *fs);
Inode read_inode(MinixFS *fs, uint32_t inum);
void format_mode(uint16_t mode, char out[11]);
void list_directory(MinixFS *fs, Inode *dir);
uint32_t lookup_in_directory(MinixFS *fs, Inode *dir, const char *name);
uint32_t lookup_path(MinixFS *fs, const char *path);
void copy_file(MinixFS *fs, Inode *file, const char *dstpath);
void print_verbose_structure_sizes(void);
void print_verbose_superblock(MinixFS *fs);
void print_verbose_inode(Inode *inode);


