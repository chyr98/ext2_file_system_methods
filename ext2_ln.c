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
#include <errno.h>

int is_symlink = 0;
unsigned char *disk;

int main(int argc, char **argv) {
    char *source_path;
    char *dest_path;

    if(argc != 4) {
        if (argc != 5){
            fprintf(stderr, "Usage: %s <image file name> [-s] <source path> <dest path>\n", argv[0]);
            exit(1);
        }
        else if (strcmp(argv[2], "-s")){
            fprintf(stderr, "Usage: %s <image file name> [-s] <source path> <dest path>\n", argv[0]);
            exit(1);
        }
        else{
            is_symlink = 1;
            source_path = argv[3];
            dest_path = argv[4];
        }
    }
    else{
        source_path = argv[2];
        dest_path = argv[3];
    }
    
    //format the path that got from user
    char *source_buff = modify_path(source_path);
    char *dest_buff = modify_path(dest_path);

    int fd = open(argv[1], O_RDWR);

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


    //Find the source file
    char *source_file_name;
    char *last_separator = strrchr(source_buff, (int)'/');
    *last_separator = '\0';
    source_file_name = last_separator + 1;

    unsigned int source_parent_inode_num;
    struct linked_dir *source_linked_dirs = get_linked_dirs(source_buff);
    source_parent_inode_num = findpath(source_linked_dirs, disk);


    struct ext2_inode *source_parent_inode = inodes + source_parent_inode_num - 1;
    struct ext2_dir_entry *source_file;
    if ((source_file = find_file_in_dir(disk, source_parent_inode, source_file_name)) == NULL){
	    return ENOENT;
    }

    //Find the destination of file to create
    char *dest_file_name;
    last_separator = strrchr(dest_buff, (int)'/');
    *last_separator = '\0';
    dest_file_name = last_separator + 1;

    unsigned int dest_parent_inode_num;
    struct linked_dir *dest_linked_dirs = get_linked_dirs(dest_buff);
    dest_parent_inode_num = findpath(dest_linked_dirs, disk);

    struct ext2_inode *dest_parent_inode = inodes + dest_parent_inode_num - 1;
    struct ext2_dir_entry *dest_file;
    struct ext2_dir_entry *previous_file;

    //check if the file's name is already exist
    if (find_file_in_dir(disk, dest_parent_inode, dest_file_name) != NULL){
    	return EEXIST;
    }

    if((previous_file = find_last_file_in_dir(disk, dest_parent_inode)) == NULL){
        fprintf(stderr, "Error: cannot find last file in dest dir.\n");
        exit(1);
    }

    //allocate the new directory entry, and modify other directory entries to maintain consistence 
    int new_inode_num;
    int new_block_num;
    if(previous_file->rec_len < dir_entry_len(previous_file->name_len) + dir_entry_len(strlen(dest_file_name))){
        if ((new_block_num = first_zero(bb, sb->s_blocks_count)) == -1){
            fprintf(stderr, "Error: No enough memory\n");
            exit(1);
        }
        int block_table_num = -1;
        for (int index = 0; index < 12; index++){
            if (dest_parent_inode->i_block[index] == 0){
                block_table_num = index;
                break;
            }
        }
        if (block_table_num == -1){
            fprintf(stderr, "Error: To many files in target directory\n");
            exit(1);
        }

        bitmap_one(bb, new_block_num);
        dest_parent_inode->i_block[block_table_num] = new_block_num;
        dest_parent_inode->i_blocks += 2;
        gd->bg_free_blocks_count += -1;
        sb->s_free_blocks_count += -1;

        dest_file = (struct ext2_dir_entry *)(disk + (new_block_num - 1) * EXT2_BLOCK_SIZE);
        dest_file->rec_len = EXT2_BLOCK_SIZE;
    }
    else{
        int actual_len = dir_entry_len(previous_file->name_len);
        dest_file = (struct ext2_dir_entry *)((char *)previous_file + actual_len);
        dest_file->rec_len = previous_file->rec_len - actual_len;
        previous_file->rec_len = actual_len;
    }

    //set up name information in dir entry
    dest_file->name_len = strlen(dest_file_name);
    memcpy(dest_file->name, dest_file_name, dest_file->name_len);

    //set up other information in dir entry
    if(is_symlink){
        if ((new_inode_num = first_zero(ib, sb->s_inodes_count)) == -1){
            fprintf(stderr, "Error: No enough memory\n");
            exit(1);
        }
        struct ext2_inode *new_inode = inodes + new_inode_num - 1;
        bitmap_one(ib, new_inode_num);
        gd->bg_free_inodes_count += -1;
        sb->s_free_inodes_count += -1;

        if ((new_block_num = first_zero(bb, sb->s_blocks_count)) == -1){
            fprintf(stderr, "Error: No enough memory\n");
            exit(1);
        }

        unsigned char *block_beginning = disk + new_block_num * EXT2_BLOCK_SIZE;
        bitmap_one(bb, new_block_num);
        gd->bg_free_blocks_count += -1;
        sb->s_free_blocks_count += -1;

        //initialize the inode
        setup_inode(new_inode);

	new_inode->i_mode = EXT2_S_IFLNK;
	new_inode->i_size = strlen(argv[3]);
        new_inode->i_blocks = 2;
        new_inode->i_links_count = 1;
        new_inode->i_block[0] = new_block_num;

        //store info in data block
        memcpy(block_beginning, argv[3], strlen(argv[3]));

        //update rest in dir entry
        dest_file->inode = new_inode_num;
        dest_file->file_type = (unsigned char)EXT2_FT_SYMLINK;
    }
    else{
	struct ext2_inode *source_inode = inodes + source_file->inode - 1;
	source_inode->i_links_count++;	

        //update rest in dir entry
        dest_file->inode = source_file->inode;
        dest_file->file_type = source_file->file_type;
    }
	
    return 0;
}
