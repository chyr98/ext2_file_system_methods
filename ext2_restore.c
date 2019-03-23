#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <string.h>
#include "ext2.h"
#include "ext2_util.h"

unsigned char *disk;

int main(int argc, char **argv) {

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <path to link>\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);
    char *path = argv[2];

    disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
    struct ext2_inode *inodes = (struct ext2_inode *) (disk + gd->bg_inode_table*EXT2_BLOCK_SIZE);

    //bitmaps
    unsigned char *bb = disk + gd->bg_block_bitmap*EXT2_BLOCK_SIZE;
    unsigned char *ib = disk + gd->bg_inode_bitmap*EXT2_BLOCK_SIZE;

    char *buff = modify_path(path);
    char *file_name;

    //separate the file name and parent's path
    char *last_separator = strrchr(buff, (int)'/');
    *last_separator = '\0';
    file_name = last_separator + 1;

    unsigned int parent_inode = 0;
    struct linked_dir *linked_dir = get_linked_dirs(buff);
    parent_inode = findpath(linked_dir, disk);
    

    struct ext2_inode *parent_inode_pt = inodes + (parent_inode - 1);
    struct dir_entry_to_add *restore_file;

    //try to find directory entries in gaps that has the same name with given name
    if ((restore_file = find_possible_file_in_dir(disk, parent_inode_pt, file_name)) == NULL){
        printf("Cannot restore file: directory entry is reused\n");
        exit(0);
    }

    if (restore_file->new_file->file_type == EXT2_FT_DIR){
        fprintf(stderr, "Error: try to restore a directory\n");
        exit(1);
    }

    //check if the inodes block is occupied
    if (bitmap_find(ib, restore_file->new_file->inode)){
        printf("Cannot restore file: inode is reused\n");
        exit(0);
    }

    //check if any data block is occupied

    struct ext2_inode *curr_inode = inodes + (restore_file->new_file->inode - 1);

    int block_sector_count = 0;
    for (int p = 0; p < 13; p++){
        if (curr_inode->i_block[p] != 0){
             if (bitmap_find(bb, curr_inode->i_block[p])){
                printf("Cannot restore file: data block is reused\n");
                exit(0);
             }
             block_sector_count += 2;
        }

    }

    if (block_sector_count < curr_inode->i_blocks){
        int *single_indirect = (int *) (disk + (curr_inode->i_block[12]) * EXT2_BLOCK_SIZE);
        int index = 0;
        while(block_sector_count < curr_inode->i_blocks){
            if (single_indirect[index] != 0){
                 if (bitmap_find(bb, single_indirect[index])){
                     printf("Cannot restore file: data block is reused\n");
                     exit(0);
                 }
                 block_sector_count += 2;
            }
	    index++;

        }

    }



    //restore the inode and data blocks
    bitmap_one(ib, restore_file->new_file->inode);
    curr_inode->i_links_count += 1;
    curr_inode->i_dtime = 0;	
    gd->bg_free_inodes_count = gd->bg_free_inodes_count - 1;
    sb->s_free_inodes_count = sb->s_free_inodes_count - 1;

    block_sector_count = 0;
    for (int p = 0; p < 13; p++){
        if (curr_inode->i_block[p] != 0){
             bitmap_one(bb, curr_inode->i_block[p]);
	     gd->bg_free_blocks_count = gd->bg_free_blocks_count - 1;
	     sb->s_free_blocks_count = sb->s_free_blocks_count - 1;
             block_sector_count += 2;
        }

    }

    if (block_sector_count < curr_inode->i_blocks){
        int *single_indirect = (int *) (disk + (curr_inode->i_block[12]) * EXT2_BLOCK_SIZE);
        int index = 0;
        while(block_sector_count < curr_inode->i_blocks){
            if (single_indirect[index] != 0){
                 bitmap_one(bb, single_indirect[index]);
		 gd->bg_free_blocks_count = gd->bg_free_blocks_count - 1;
		 sb->s_free_blocks_count = sb->s_free_blocks_count - 1;
                 block_sector_count += 2;
            }
	    index++;
        }

    }

    //restore the directory entry
    (restore_file->new_file)->rec_len = (restore_file->contained_in)->rec_len - ((char *)(restore_file->new_file) - (char *)(restore_file->contained_in));
    (restore_file->contained_in)->rec_len = (char *)(restore_file->new_file) - (char *)(restore_file->contained_in);


    return 0;
}

