/*
Generic FS functions for Minix
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
void copy_file(MinixFS *fs, Inode *file, const char *dst);
void print_verbose_structure_sizes(void);
void print_verbose_superblock(MinixFS *fs);
void print_verbose_inode(Inode *inode);

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
    } else if (r != (ssize_t)n) {
        fprintf(stderr, "Short read at offset %ld (got %ld bytes)\n",
                (long)off, (long)r);
        exit(EXIT_FAILURE);
    }
}

uint32_t get_base(MinixFS *fs, int pnum, int snum) {
    uint8_t mbr[512];

    check_read_check(fs->fd, mbr, sizeof(mbr), 0);
    int valid_mbr = (mbr[510] == 0x55 && mbr[511] == 0xAA);
    if (fs->verbose) {
        printf("MBR signature = 0x%02x 0x%02x\n", mbr[510], mbr[511]);
    }
    if (!valid_mbr || pnum < 0) {
        if (fs->verbose) {
            printf("No valid partition table, so not partitioned?"
                " Using base = 0\n");
        }
        return 0;
    }

    PartitionEntry *table = (PartitionEntry *)(mbr + PART_TABLE_OFFSET);
    PartitionEntry *p = &table[pnum];

    if (fs->verbose) {
        printf("Partition %d type = 0x%x\n", pnum, p->type);
        printf("Partition %d lFirst = %u\n", pnum, p->lFirst);
    }
    if (p->type != 0x81) {
        fprintf(stderr, "Not a Minix partition\n");
        exit(EXIT_FAILURE);
    }

    uint32_t base = p->lFirst * SECTOR_SIZE;

    if (snum < 0) {
        return base;
    }
    
    uint8_t submbr[512];
    check_read_check(fs->fd, submbr, sizeof(submbr), base);
    
    int valid_submbr = (submbr[510] == 0x55 && submbr[511] == 0xAA);
    if (!valid_submbr) {
        fprintf(stderr, "Invalid sub-MBR at partition base\n");
        exit(EXIT_FAILURE);
    }

    PartitionEntry *subtable = (PartitionEntry *)(submbr + PART_TABLE_OFFSET);
    PartitionEntry *sp = &subtable[snum];

    if (fs->verbose) {
        printf("Sub-partition %d type = 0x%x\n", snum, sp->type);
        printf("Sub-partition %d lFirst = %u\n", snum, sp->lFirst);
    }
    if (sp->type != 0x81) {
        fprintf(stderr, "Not a Minix sub-partition\n");
        exit(EXIT_FAILURE);
    }

    return sp->lFirst * SECTOR_SIZE;
}

void load_superblock(MinixFS *fs) {
    check_read_check(
        fs->fd, 
        &fs->sb, 
        sizeof(Superblock), 
        fs->base + SUPERBLOCK_OFFSET
    );

    if (fs->verbose) {
        print_verbose_superblock(fs);
    }
    
    if (fs->sb.magic != 0x4D5A && fs->sb.magic != 0x5A4D) {
        fprintf(stderr, "Invalid Minix superblock magic: 0x%x\n", fs->sb.magic);
        exit(EXIT_FAILURE);
    }
}

Inode read_inode(MinixFS *fs, uint32_t inum) {
    if (inum == 0 || inum > fs->sb.ninodes) {
        fprintf(stderr, "Invalid inode number: %u\n", inum);
        exit(EXIT_FAILURE);
    }

    uint32_t inode_table_block = 
        2 + fs->sb.i_blocks + fs->sb.z_blocks;
    uint32_t inode_table_start = 
        fs->base + inode_table_block * fs->sb.blocksize;
    uint64_t inode_offset = 
        inode_table_start + (inum - 1) * sizeof(Inode);

    Inode inode;
    check_read_check(fs->fd, &inode, sizeof(Inode), inode_offset);

    return inode;
}

void format_mode(uint16_t mode, char out[11]) {
    out[0] = (mode & 0040000) ? 'd' : '-';
    const uint16_t perms[9] = {
        0400, 0200, 0100,   // user
        0040, 0020, 0010,   // group
        0004, 0002, 0001    // other
    };
    const char chars[9] = {
        'r', 'w', 'x',
        'r', 'w', 'x',
        'r', 'w', 'x'
    };

    for (int i = 0; i < 9; i++) {
        out[i+1] = (mode & perms[i]) ? chars[i] : '-';
    }
    out[10] = '\0';
}

void list_directory(MinixFS *fs, Inode *dir) {
    uint32_t blocksize = fs->sb.blocksize;
    uint32_t entries_per_zone = blocksize / sizeof(DirectoryEntry);
    uint32_t total_entries = dir->size / sizeof(DirectoryEntry);
    uint32_t seen = 0;
    char perm[11];

    for (int z = 0; z < DIRECT_ZONES && dir->zone[z] != 0; z++) {
        uint32_t zone = dir->zone[z];
        uint64_t base = fs->base + ((uint64_t)zone * blocksize);

        for (
            uint32_t i = 0; 
            i < entries_per_zone && seen < total_entries; 
            i++
        ) {
            DirectoryEntry entry;
            check_read_check(
                fs->fd,
                &entry,
                sizeof(entry),
                base + i * sizeof(entry)
            );

            if (entry.inode == 0) {
                continue;
            }

            Inode child = read_inode(fs, entry.inode);
            format_mode(child.mode, perm);
            printf("%s %9u %s\n", perm, child.size, entry.name);
            seen++;
        }
    }
}

uint32_t lookup_in_directory(MinixFS *fs, Inode *dir, const char *name) {
    uint64_t dir_offset = fs->base + (dir->zone[0] * fs->sb.blocksize);
    DirectoryEntry entry;
    int entries = dir->size / sizeof(DirectoryEntry);

    for (int i = 0; i < entries; i++) {
        check_read_check(
            fs->fd, 
            &entry, 
            sizeof(entry), 
            dir_offset + i * sizeof(entry)
        );

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

void copy_file(MinixFS *fs, Inode *file, const char *dstpath)
{
    int outfd;

    if (dstpath) {
        outfd = open(dstpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outfd < 0)
            die("open dst");
    } else {
        outfd = STDOUT_FILENO;
    }

    uint32_t blocksize = fs->sb.blocksize;
    uint32_t remaining = file->size;

    char *buffer = malloc(blocksize);
    if (!buffer) {
        die("malloc");
    }

    uint32_t indirect[blocksize / 4];

    for (int i = 0; i < DIRECT_ZONES && remaining > 0; i++) {
        uint32_t zone = file->zone[i];
        if (zone == 0) {
            continue;
        }

        off_t offset = fs->base + (off_t)zone * blocksize;
        check_read_check(fs->fd, buffer, blocksize, offset);

        uint32_t to_write = (remaining < blocksize) ? remaining : blocksize;
        write(outfd, buffer, to_write);

        remaining -= to_write;
    }

    if (remaining > 0 && file->indirect != 0) {
        off_t indirect_offset = fs->base + (off_t)file->indirect * blocksize;
        check_read_check(fs->fd, indirect, blocksize, indirect_offset);

        for (
            uint32_t i = 0; 
            i < blocksize / sizeof(uint32_t) && remaining > 0; 
            i++
        ) {
            uint32_t zone = indirect[i];
            if (zone == 0) {
                continue;
            }

            off_t offset = fs->base + (off_t)zone * blocksize;
            check_read_check(fs->fd, buffer, blocksize, offset);

            uint32_t to_write = (remaining < blocksize) ? remaining : blocksize;
            write(outfd, buffer, to_write);

            remaining -= to_write;
        }
    }

    free(buffer);

    if (dstpath) {
        close(outfd);
    }
}

// Debug function to print the sizes of the structures
void print_verbose_structure_sizes(void) {
    printf("Structure Sizes:\n");
    printf("PartitionEntry %zu\n", sizeof(PartitionEntry));
    printf("Superblock %zu\n", sizeof(Superblock));
    printf("Inode %zu\n", sizeof(Inode));
    printf("DirectoryEntry %zu\n", sizeof(DirectoryEntry));
}

void print_verbose_superblock(MinixFS *fs) {
    Superblock *sb = &fs->sb;
    fprintf(stderr, "Superblock Contents:\n");
    fprintf(stderr, "ninodes %u\n", sb->ninodes);
    fprintf(stderr, "i_blocks %d\n", sb->i_blocks);
    fprintf(stderr, "z_blocks %d\n", sb->z_blocks);
    fprintf(stderr, "firstdata %u\n", sb->firstdata);
    fprintf(stderr, "log_zone_size %d\n", sb->log_zone_size);
    fprintf(stderr, "max_file %u\n", sb->max_file);
    fprintf(stderr, "zones %u\n", sb->zones);
    fprintf(stderr, "magic 0x%x\n", sb->magic);
    fprintf(stderr, "blocksize %u\n", sb->blocksize);
    fprintf(stderr, "subversion %u\n", sb->subversion);
}

void print_verbose_inode(Inode *inode) {
    char perms[11];
    format_mode(inode->mode, perms);
    fprintf(stderr, "File inode:\n");
    fprintf(stderr, "mode 0x%x (%s)\n", inode->mode, perms);
    fprintf(stderr, "links %u\n", inode->links);
    fprintf(stderr, "uid %u\n", inode->uid);
    fprintf(stderr, "gid %u\n", inode->gid);
    fprintf(stderr, "size %u\n", inode->size);
    for (int i = 0; i < DIRECT_ZONES; i++) {
        fprintf(stderr, "zone[%d] = %u\n", i, inode->zone[i]);
    }
    fprintf(stderr, "indirect  %u\n", inode->indirect);
    fprintf(stderr, "double    %u\n", inode->two_indirect);
}