#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include "ext2.h"
#include "ext2_util.h"

unsigned char *disk;


int main(int argc, char **argv) {

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <path to remove>\n", argv[0]);
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

    //seperate the parent and file to remove
    char *buff = modify_path(path);
    char *file_name;

    char *last_separator = strrchr(buff, (int)'/');
    *last_separator = '\0';
    file_name = last_separator + 1;

    //find the file and its parent

    int p_inode_num = 0;
    struct linked_dir *parent_linked_dirs = get_linked_dirs(buff);
    p_inode_num = findpath(parent_linked_dirs, disk);

    struct ext2_inode *parent_inode = (struct ext2_inode *)((unsigned char *)inodes + (p_inode_num-1)*128);


    int inode_num = (find_file_in_dir(disk, parent_inode, file_name))->inode;
    struct ext2_inode *curr_inode = (struct ext2_inode *)((unsigned char *)inodes + (inode_num-1)*128);
    if(i_mode_type_checker(curr_inode->i_mode) == 'd'){
        fprintf(stderr, "Error, trying to remove a directory\n");
        exit(1);
    }

    //find the previous dir entry of file to remove
    struct ext2_dir_entry *pre_dir_entry;
    if ((pre_dir_entry = find_previous_file_in_dir(disk, parent_inode, file_name)) == NULL){
        fprintf(stderr, "Error: cannot find the previous file\n");
        exit(1);
    }

    struct ext2_dir_entry *dir_to_rm = (struct ext2_dir_entry *)((char *)pre_dir_entry + pre_dir_entry->rec_len);
    pre_dir_entry->rec_len += dir_to_rm->rec_len;

    //modify the inode and free the inode and blocks in the file if link count becomes zero
    curr_inode->i_links_count += -1;
    if (curr_inode->i_links_count == 0){
        int block_sector_count = 0;
        for (int p = 0; p < 13; p++){
            if (curr_inode->i_block[p] != 0){
                 bitmap_zero(bb, curr_inode->i_block[p]);
		 gd->bg_free_blocks_count = gd->bg_free_blocks_count + 1;
		 sb->s_free_blocks_count = sb->s_free_blocks_count + 1;
                 block_sector_count += 2;
            }

        }

        if (block_sector_count < curr_inode->i_blocks){
            int *single_indirect = (int *) (disk + (curr_inode->i_block[12]) * EXT2_BLOCK_SIZE);
            int index = 0;
            while(block_sector_count < curr_inode->i_blocks){
                if (single_indirect[index] != 0){
                     bitmap_zero(bb, single_indirect[index]);
		     gd->bg_free_blocks_count = gd->bg_free_blocks_count + 1;
		     sb->s_free_blocks_count = sb->s_free_blocks_count + 1;
                     block_sector_count += 2;
		     index += 1;
                }
            }

        }

	curr_inode->i_dtime = (unsigned int)(time(NULL));
        bitmap_zero(ib, inode_num);
	gd->bg_free_inodes_count = gd->bg_free_inodes_count + 1;
	sb->s_free_inodes_count = sb->s_free_inodes_count + 1;
	

    }

    return 0;
}

