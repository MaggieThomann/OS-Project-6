
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

int fs_format()
{
	return 0;
}

void fs_debug()
/*
Scans a mounted filesystem and reports on how the inodes and blocks are organized 
*/
{
	union fs_block block;
	union fs_block indirect_block;

	disk_read(0,block.data);

	printf("superblock:\n");
	int magic_number = block.super.magic; 
	if (magic_number == FS_MAGIC){
		printf("    magic number is valid\n");
	}
	else {
		printf("    magic number is invalid\n");
	}
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

	int num_inode_blocks = block.super.ninodeblocks;
	int num_inodes = block.super.ninodes;
	int num_inodes_per_block = num_inodes / num_inode_blocks;

	int i, j, k, m;
	for (j = 1; j <= num_inode_blocks; j++){
		disk_read(j, block.data);
		for (i = 0; i < num_inodes_per_block; i++){
			if (block.inode[i].isvalid != 0){
				printf("inode %d:\n", i);
				printf("    size %d bytes\n",block.inode[i].size);
				printf("    direct blocks:");
				for (k = 0; k < POINTERS_PER_INODE; k++){
					if (block.inode[i].direct[k] != 0){
						printf(" %d ", block.inode[i].direct[k]);
					}
				}
				printf("\n");
				if (block.inode[i].indirect != 0){
					printf("    indirect block: %d \n", block.inode[i].indirect);
					printf("    indirect data blocks:");
					disk_read(block.inode[i].indirect, indirect_block.data);
					for (m = 0; m < POINTERS_PER_BLOCK; m++){
						if (indirect_block.pointers[m] != 0){
							printf(" %d ", indirect_block.pointers[m]);
						}
					}
					printf("\n");

				}
			}
		}
		
	}
}

int fs_mount()
{
	return 0;
}

int fs_create()
{
	return 0;
}

int fs_delete( int inumber )
{
	return 0;
}

int fs_getsize( int inumber )
{
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}