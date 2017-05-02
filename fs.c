
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
one on success and zero on failure.  When attempting to format an already mounted disk,
it does nothing and returns failure.
*/
{
	if (IS_MOUNTED == 1){
		// The disk is already mounted so return failure
		printf("disk has already been mounted so format returns failure");
		return 0;
	}
	else{

		// Initialize a blank block
		union fs_block new_block;

		// Initialize the superblock
		struct fs_superblock new_superblock;
		new_superblock.magic = FS_MAGIC;
		new_superblock.nblocks = disk_size();

		// Sets aside 10% of the blocks for inodes
		new_superblock.ninodeblocks = new_superblock.nblocks * .10 + 1;
		new_superblock.ninodes = INODES_PER_BLOCK * new_superblock.ninodeblocks;

		// Clear the inode table

		struct fs_inode blank_inode;
		blank_inode.isvalid = 0;
		blank_inode.size = 0;
		int j;
		for (j = 0; j < POINTERS_PER_INODE; j++){blank_inode.direct[j] = 0;}
		blank_inode.indirect = 0;
		
		int i;
		for (i = 0; i < POINTERS_PER_INODE; i++){
			new_block.inode[i] = blank_inode;
		}

		// Write the superblock
		
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

	int i, j;
	for (i = 0; i < num_inodes; i++){
		if ((INODE_BITMAP[i] == 0) && (i > 0)){
			INODE_BITMAP[i] = 1; 

			// Get the block number
			int block_num = i / 127 + 1;
			union fs_block block_to_edit;
			disk_read(block_num, block_to_edit.data);

			struct fs_inode inode_to_write;
			inode_to_write.isvalid = 1;
			inode_to_write.size = 0;
			for (j = 0; j < POINTERS_PER_INODE; j++){
				inode_to_write.direct[j] = 0;
			}
			inode_to_write.indirect = 0;

			// Write the new inode
			block_to_edit.inode[i] = inode_to_write;
			disk_write(block_num, block_to_edit.data);

			return i;
		}
	}
	
	return 0;

}

int fs_delete( int inumber )
/* Delete the inode indicated by the inumber. Release all data and indirect blocks assigned to this 
inode and return them to the free block map. On success, return one. On failure, return 0.
*/
{
	if (IS_MOUNTED == 0){
		printf("disk not yet mounted \n");
		return 0;
	}

	// Convert the numbers
	int block_number = inumber/127 + 1;
	int i_number = inumber % 127;

	
	// Read the block
	union fs_block block;
	disk_read(block_number, block.data);
	int i;


	// Set everything to 0
	if (block.inode[i_number].isvalid == 1){
		// Set the isvalid to 0
		block.inode[i_number].isvalid = 0;
		INODE_BITMAP[i_number] = 0;
		
		// Set the size to 0
		block.inode[i_number].size = 0; 

		// Set the direct to 0
		for (i = 0; i < POINTERS_PER_INODE; i++){
			block.inode[i_number].direct[i] = 0;
			BLOCK_BITMAP[block.inode[i_number].direct[i]] = 0;
		}

		// Read the indirect block in
		union fs_block indirect_block;
		disk_read(block.inode[i_number].indirect , indirect_block.data);

		// Set all the indirect pointers to 0
		for (i = 0; i < POINTERS_PER_BLOCK; i++){
			indirect_block.pointers[i] = 0;
			BLOCK_BITMAP[indirect_block.pointers[i]] = 0;
		}

		// Set the indirect to 0
		block.inode[i_number].indirect = 0;     

		// Write the block back to the disk
		disk_write(block_number, block.data);

		return 1;
		
	}
	printf("%d is not a valid inode to delete \n", inumber);
	return 0;
	
}

int fs_getsize( int inumber )
/*
Return the logical size of the given inode, in bytes. Note that zero is a valid logical size 
for an inode! On failure, return -1.
*/
{
	// Convert the numbers
	int block_number = inumber/127 + 1;
	int i_number = inumber % 127;
	
	// Read the block
	union fs_block block;
	disk_read(block_number, block.data);

	if ( block.inode[i_number].isvalid == 1){
		return block.inode[i_number].size;
	}
	else{
		return -1;
	}
}

int fs_read( int inumber, char *data, int length, int offset )
/*
Read data from a valid inode. Copy "length" bytes from the inode into the "data" pointer, 
starting at "offset" in the inode. Return the total number of bytes read. The number of bytes 
actually read could be smaller than the number of bytes requested, perhaps if the end of the 
inode is reached. If the given inumber is invalid, or any other error is encountered, return 0.
*/
{

	// Convert the inumber 
	int block_number = inumber/127 + 1;
	int i_number = inumber % 127;
	int begin_block = offset / DISK_BLOCK_SIZE;
	int end_block = offset + length;


	// Read the block
	union fs_block block;
	disk_read(block_number, block.data);

	// Check if it's valid
	if ( block.inode[i_number].isvalid == 0){
		printf("error in reading.  invalid number.");
		return 0;
	}

	// If the length will put you off the block, set the 
	// end block to the size 
	if (length + offset > block.inode[i_number].size){
		end_block = block.inode[i_number].size;
	}

	// Calculate the last block to read
	int last_block = ceil((double)end_block / (double)DISK_BLOCK_SIZE);

	// Get remainder
	int partial_read = end_block % DISK_BLOCK_SIZE;

	// Calculate the number of direct blocks
	int num_direct_blocks;

	
	if (last_block > POINTERS_PER_INODE){
		num_direct_blocks = POINTERS_PER_INODE;
	}
	else{
		num_direct_blocks = last_block;
	}

	// Read from begin_block --> last_block
	// Loop through each of the blocks that the inode has
	int i, index;
	int data_read_so_far = 0;
	union fs_block each_block;
	for (i = begin_block; i < num_direct_blocks; i++){
		printf("Inside the direct blocks \n");
		if (i == last_block){
			index = partial_read;
		}
		else{
			index = DISK_BLOCK_SIZE;
		}
		
		disk_read(block.inode[i_number].direct[i], each_block.data);
		memcpy(data+data_read_so_far, each_block.data, index);
		data_read_so_far = data_read_so_far + index;
	}

	int start_for_indirect;
	if (begin_block >= 5){
		start_for_indirect = begin_block;
	}
	else {
		start_for_indirect = 0;
	}

	int num_indirect = last_block - num_direct_blocks;

	// Read the indirect block in
	union fs_block indirect_block;
	disk_read(block.inode[i_number].indirect , indirect_block.data);

	// Set all the indirect pointers to 0
	int pointer;
	union fs_block pointer_block;
	for (i = start_for_indirect; i < num_indirect; i++){
		if (i == last_block){
			index = partial_read;
		}
		else{
			index = DISK_BLOCK_SIZE;
		}
		// Get the info for the block that the pointer is indicating
		pointer = indirect_block.pointers[i];
		disk_read(pointer, pointer_block.data);

		// Copy over the data
		memcpy(data+data_read_so_far, pointer_block.data, index);
		data_read_so_far = data_read_so_far + index;
	}
	return data_read_so_far;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}