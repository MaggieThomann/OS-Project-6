
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

int IS_MOUNTED = 0;
int *BLOCK_BITMAP;
int *INODE_BITMAP;

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
/*
Creates a new filesystem on the disk, destroys any data already present.  Sets aside
10% of the blocks for inodes.  Clears the inode table.  Writes the superblock.  Returns
one on success and zero on failure.  When attempting to mount an already mounted disk,
it does nothing and returns failure.
*/
{
	if (IS_MOUNTED == 1){
		// The disk is already mounted so return failure
		return 0;
	}
	else{

		// Initialize the superblock
		struct fs_superblock new_superblock;
		new_superblock.magic = FS_MAGIC;
		new_superblock.nblocks = disk_size();

		// Sets aside 10% of the blocks for inodes
		new_superblock.ninodeblocks = round(new_superblock.nblocks * .10);
		new_superblock.ninodes = INODES_PER_BLOCK * new_superblock.ninodeblocks;

		// Clear the inode table

		struct fs_inode blank_inode;
		blank_inode.isvalid = 0;
		blank_inode.size = 0;
		int j;
		for (j = 0; j < POINTERS_PER_INODE; j++){blank_inode.direct[j] = 0;}
		blank_inode.indirect = 0;
		union fs_block blank_block;
		blank_block.inode[0] = blank_inode;
		int i;
		for (i = 1; i <= new_superblock.ninodeblocks; i++){
			disk_write(i, blank_block.data);
		}

		// Write the superblock
		union fs_block new_block;
		new_block.super = new_superblock;

		disk_write(0, new_block.data);

		return 1;

	}
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

/*
Examines the disk for a filesystem. If one is present, reads the superblock, builds a free
block bitmap and prepares the filesystem for use.  Returns one on success and zero on 
failure.
*/

int fs_mount()
{
	if (IS_MOUNTED == 1){
		printf("disk has already been mounted \n");
		return 0;
	}
	else{
		union fs_block block;
		union fs_block indirect_block;

		disk_read(0,block.data);
		int magic_number = block.super.magic;

		// Check if a file system is present
		if (magic_number == FS_MAGIC){

			// Read the superblock
			struct fs_superblock superblock;
			superblock = block.super;
			superblock.nblocks = disk_size();
			superblock.ninodeblocks = round(superblock.nblocks * .10);
			superblock.ninodes = INODES_PER_BLOCK * superblock.ninodeblocks;

			int i, j, k, m;

			// Initialize and fill the bitmaps with zeros for now
			BLOCK_BITMAP = (int *)malloc(sizeof(int)*superblock.nblocks); 
			for (i = 0; i < superblock.nblocks; i++){ BLOCK_BITMAP[i] = 0;}
			INODE_BITMAP = (int *)malloc(sizeof(int)*superblock.ninodes); 
			for (i = 0; i < superblock.ninodes; i++){INODE_BITMAP[i] = 0;}
			
			// Iterate through and update any unavailable positions with 1s
			for (j = 1; j < superblock.ninodeblocks; j++){
				disk_read(j, block.data);
				for (i = 0; i < INODES_PER_BLOCK; i++){
					if (block.inode[i].isvalid != 0){
						INODE_BITMAP[i] = 1;
						for (k = 0; k < POINTERS_PER_INODE; k++){
							if (block.inode[i].direct[k] != 0){
								BLOCK_BITMAP[block.inode[i].direct[k]] = 1;
							}
						}
						if (block.inode[i].indirect != 0){
							BLOCK_BITMAP[block.inode[i].indirect] = 1;

							disk_read(block.inode[i].indirect, indirect_block.data);
							for (m = 0; m < POINTERS_PER_BLOCK; m++){
								if (indirect_block.pointers[m] != 0){
									 BLOCK_BITMAP[indirect_block.pointers[m]] = 1;
								}
							}
						}

					}
				}
			}			

			IS_MOUNTED = 1;
			return 1;
		}
	}
	
	return 0;
}

int fs_create()
/*
Create a new inode of zero length. On success, return the (positive) inumber. On failure, return zero.
*/
{
	if (IS_MOUNTED == 0){
		printf("disk not yet mounted \n");
		return 0;
	}
	struct fs_inode new;
	new.size = 0;

	// Get the number of inodes
	union fs_block block;
	disk_read(0, block.data);
	
	int num_inodes = block.super.ninodes;

	int i;
	for (i = 0; i < num_inodes; i++){
		if ((INODE_BITMAP[i] == 0) && (i > 0)){
			INODE_BITMAP[i] = 1;
			return i;
		}
	}
	
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