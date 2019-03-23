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

//modify the inode and free the inode and blocks in the file if link count becomes zero
void delete_file(struct ext2_dir_entry *file, unsigned char *ib, unsigned char *bb, struct ext2_inode *inode_table){
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
    
    struct ext2_inode *curr_inode = inode_table + file->inode - 1;
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
        bitmap_zero(ib, file->inode);
	gd->bg_free_inodes_count = gd->bg_free_inodes_count + 1;
	sb->s_free_inodes_count = sb->s_free_inodes_count + 1;
	

    }

}

/*
** Recursive deletion for all the files in <dir> and in subdirectories of <dir>
*/
void recursive_delete(struct ext2_dir_entry *dir, unsigned char *ib, unsigned char *bb, struct ext2_inode *inode_table)
{
    struct ext2_inode *parent_inode = inode_table + dir->inode - 1; 
    for (int i = 0; i < 12; i++){

        if (parent_inode->i_block[i] != 0){

	    int byte_count = 0;
	    unsigned char *block_beginning = disk + parent_inode->i_block[i] * EXT2_BLOCK_SIZE;

	    while(byte_count < EXT2_BLOCK_SIZE){
	    	struct ext2_dir_entry *curr_file = (struct ext2_dir_entry *)(block_beginning + byte_count);
		//get the name of current file with null terminator		
		char file_name[curr_file->name_len + 1];
		memcpy(file_name, curr_file->name, curr_file->name_len);
		file_name[curr_file->name_len] = '\0';
		//If the file is a directory and is not <dir> or <dir>'s parent, then delete all files in this sub directory
		if (strcmp(file_name, ".") && strcmp(file_name, "..")){
		    printf("file properties: name: %s, rec_len: %d, name_len: %d\n", file_name, curr_file->rec_len, curr_file->name_len);
		    if (curr_file->file_type == EXT2_FT_DIR)
		    	recursive_delete(curr_file, ib, bb, inode_table);
		    delete_file(curr_file, ib, bb, inode_table);	
		}

		byte_count += curr_file->rec_len;
	    }

	}

    }

}


int main(int argc, char **argv) {
    char *path;
    int rm_dir = 0;
    if(argc != 3) {
	if (argc != 4){
            fprintf(stderr, "Usage: %s <image file name> [-r] <path to remove>\n", argv[0]);
            exit(1);
	}
	else if (strcmp(argv[2],"-r")){
	    fprintf(stderr, "Usage: %s <image file name> [-r] <path to remove>\n", argv[0]);
            exit(1);
	}
	else{
	    path = argv[3];
	    rm_dir = 1;
	}
    }
    else{
	path = argv[2];
    }
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

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

    //find the previous dir entry and the dir entry of file to remove
    struct ext2_dir_entry *pre_dir_entry;
    if ((pre_dir_entry = find_previous_file_in_dir(disk, parent_inode, file_name)) == NULL){
        fprintf(stderr, "Error: cannot find the previous file\n");
        exit(1);
    }

    //remove the curr_file and files in curr_file if it is a dir and -r flag was called
    struct ext2_dir_entry *dir_to_rm = (struct ext2_dir_entry *)((char *)pre_dir_entry + pre_dir_entry->rec_len);
    if(dir_to_rm->file_type == EXT2_FT_DIR){
	if(rm_dir){
	    recursive_delete(dir_to_rm, ib, bb, inodes);
	}
	else{
            fprintf(stderr, "Error, trying to remove a directory\n");
            exit(1);
	}
    }
    delete_file(dir_to_rm, ib, bb, inodes);

    pre_dir_entry->rec_len += dir_to_rm->rec_len;


    return 0;
}
 
