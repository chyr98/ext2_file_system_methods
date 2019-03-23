#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "ext2.h"
#include "ext2_util.h"
#include <errno.h>

unsigned char *disk;

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s  <image file name> \n", argv[0]);
        exit(1);
    }

    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    int total_error_count = 0;

    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
    struct ext2_inode *inodes = (struct ext2_inode *)(disk + gd->bg_inode_table * EXT2_BLOCK_SIZE);
    
    // Checking & Fix order: (b) --> (c) --> (d) --> (e) --> (a)

    // =======Part (b)======: for every in_used inodes, check if its i_mode matches its corrsponding directory entry file_type; if not, trust its i_mode.
    struct ext2_inode *root_inode = (struct ext2_inode *)((unsigned char *)inodes + 128);
    struct ext2_dir_entry *root_de = (struct ext2_dir_entry *)malloc(sizeof(struct ext2_dir_entry));
    //printf("Get here : 33333\n");
    for (int i = 0; i < 12; i++)
    {
        //printf("i = %d\n", i);
        if (root_inode->i_block[i] > 0)
        {

            root_de = (struct ext2_dir_entry *)(disk + root_inode->i_block[i] * EXT2_BLOCK_SIZE);
            total_error_count += traverse_and_fix(root_de, inodes, disk, EXT2_ROOT_INO, &mode_ft_match);
        }
    }
    //================================================================================================================================================


    // =======Part (c)======: for every in_used inodes, check if it is marked in-used in inode bitmap; if not, fix the inode bitmap
    for (int i = 0; i < 12; i++)
    {
        //printf("i = %d\n", i);
        if (root_inode->i_block[i] > 0)
        {

            root_de = (struct ext2_dir_entry *)(disk + root_inode->i_block[i] * EXT2_BLOCK_SIZE);
            total_error_count += traverse_and_fix(root_de, inodes, disk, EXT2_ROOT_INO, &inode_marked_allocated);
        }
    }
    //================================================================================================================================================


    // =======Part (d)======: for every in_used inodes, check if its dtime is set to 0 in inode mitmap; if not, fix it

    for (int i = 0; i < 12; i++)
    {
        //printf("i = %d\n", i);
        if (root_inode->i_block[i] > 0)
        {

            root_de = (struct ext2_dir_entry *)(disk + root_inode->i_block[i] * EXT2_BLOCK_SIZE);
            total_error_count += traverse_and_fix(root_de, inodes, disk, EXT2_ROOT_INO, &inode_dtime_zero);
        }
    }

    //================================================================================================================================================



    // =======Part (e)======: for every in_used inodes, check if all its in_used data block are marked as allocated in block bitmap; if not, fix it
    for (int i = 0; i < 12; i++)
    {
        //printf("i = %d\n", i);
        if (root_inode->i_block[i] > 0)
        {

            root_de = (struct ext2_dir_entry *)(disk + root_inode->i_block[i] * EXT2_BLOCK_SIZE);
            total_error_count += traverse_and_fix(root_de, inodes, disk, EXT2_ROOT_INO, &block_bitmap_marked);
        }
    }
    //================================================================================================================================================
    

    // =======Part (a)======: check if free inodes/blocks counts in sb and bg match with their corresponding bitmaps; if not, trust the bitmaps.
    total_error_count += check_sb_gd_bitmap(disk);

    //================================================================================================================================================

    

    if (total_error_count > 0){
        printf("%d file system inconsistencies repaired!\n", total_error_count);
    }

    else{
        printf("No file system inconsistencies detected!\n");
    }

    return 0;
}