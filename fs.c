
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
#define NELEMS(x)  (sizeof(x) / sizeof((x)[0]))

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
	int num_blocks = block.super.nblocks;
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

	int num_inode_blocks = block.super.ninodeblocks;
	int num_inodes = block.super.ninodes;
	int num_inodes_per_block = num_inodes / num_inode_blocks;

	int i, j, k, m;
	for (j = 0; j < num_inode_blocks; j++){
		disk_read(j, block.data);
		for (i = 1; i < num_inodes_per_block; i++){
			//printf("block.inode is valid = %d \n", block.inode[i].isvalid);
			if (block.inode[i].isvalid == 1){
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
						if ((indirect_block.pointers[m] != 0) && (indirect_block.pointers[m] < num_blocks) && (indirect_block.pointers[m] > 0)){

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
			BLOCK_BITMAP = malloc(sizeof(int)*superblock.nblocks); 
			for (i = 0; i < superblock.nblocks; i++){ BLOCK_BITMAP[i] = 0;}
			INODE_BITMAP = malloc(sizeof(int)*superblock.ninodes); 
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
			int p;
			// Reserve all inode blocks in the free block bitmap
			for (p = 1; p < superblock.ninodeblocks; p++){
				BLOCK_BITMAP[p] = 1;
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

	if (IS_MOUNTED == 0){
		printf("file system has not yet been mounted. \n");
		return 0;
	}

	// Convert the inumber 
	int block_number = inumber/127 + 1;
	int i_number = inumber % 127;
	int begin_block = offset / DISK_BLOCK_SIZE;
	int end_block = offset + length;


	// Read the block
	union fs_block block;
	disk_read(block_number, block.data);

	// Check if it's valid
	if (INODE_BITMAP[inumber] == 0){
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

int get_free_block(){
	// Get the super block
	union fs_block block;
	disk_read(0,block.data);
	struct fs_superblock super = block.super;
	int num_blocks = super.nblocks;

	int i;
	for (i = 1; i < num_blocks; i++){
		if (BLOCK_BITMAP[i] == 0 && i != 0){
			return i;    
		}
	}
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
/*
Write data to a valid inode. Copy "length" bytes from the pointer "data" into the inode 
starting at "offset" bytes. Allocate any necessary direct and indirect blocks in the process. 
Return the number of bytes actually written. The number of bytes actually written could be 
smaller than the number of bytes request, perhaps if the disk becomes full. If the given 
inumber is invalid, or any other error is encountered, return 0.
*/
{

	//fs_debug();

	// Check if it's been mounted
	if (IS_MOUNTED == 0){
		printf("file system has not yet been mounted. \n");
		return 0;
	}

	// Check if it's a valid inode
	if (INODE_BITMAP[inumber] == 0){
		printf("error in writing.  invalid number. \n");
		return 0;
	}



	// Load the inode into the block
	union fs_block block;
	int block_number = inumber/127 + 1;
	int i_number = inumber % 127;
	disk_read(block_number, block.data);
	struct fs_inode my_inode = block.inode[inumber];

	// Get the size of the inode
	int size_of_inode = my_inode.size;
	int size_of_last_block = size_of_inode % DISK_BLOCK_SIZE;
	int remainder_of_last_block = DISK_BLOCK_SIZE - size_of_last_block;
	int last_block_is_full = 1;						// Assume the last block is full
	if (remainder_of_last_block != 0){					// If the remainder calculated isn't 0
		last_block_is_full = 0;						// The last block isn't full
	}

	// Get the last block that is full
	int last_block = ceil((double)(offset + length) / (double)DISK_BLOCK_SIZE);

	

	// Constant for how much data has been written so far
	int data_written = 0;

	//-------------------------------------------Loop through the direct blocks----------------------------------------
	int i, direct_block_num, w_size, new_direct_block;
	union fs_block d_block;

	for (i = 0; i < POINTERS_PER_INODE; i++){
		direct_block_num = my_inode.direct[i];				// Get the direct block number that inode has
		//printf("Processing direct block num %d \n", direct_block_num);
		disk_read(direct_block_num, d_block.data);			// Read the direct block 

		if ((direct_block_num == last_block) && !(last_block_is_full)){ 	// If we're processing the last block and the last block isn't full
			//----------------------------------------Last Block Isn't Full----------------------------------------
			new_direct_block = direct_block_num;			// Set the place to write to the last direct block
			w_size = remainder_of_last_block;			// Set the size to write the rest of that block
			if (length < remainder_of_last_block){			// If the amount we have to write is less than the remainder
				w_size = length;				// Set the size to write equal to the length
			}

			//-----------------------------------------Write to the block---------------------------------------------
			memcpy(d_block.data+remainder_of_last_block, data+data_written+offset, w_size);	
			data_written = data_written + w_size;			// Update how much data has been written so far
			disk_write(new_direct_block, d_block.data);		// Write the block back to disk

			size_of_inode = size_of_inode + w_size;		// Update the size of the inode
			block.inode[inumber].size = size_of_inode;		// Reassign it to the inode data
			disk_write(block_number, block.data);			// Write that inode data back to disk

			if (data_written == length){
				return data_written;
			}

		}

		if (direct_block_num == 0 && BLOCK_BITMAP[direct_block_num] == 0){
			//-------------------------------------Allocate a new direct block------------------------------------
			new_direct_block = get_free_block();			// Get a new block
			//printf("writing to direct block: %d \n", new_direct_block);
			block.inode[inumber].direct[i] = new_direct_block;	// Add that new direct block to the array
			disk_write(block_number, block.data);			// Write it back to disk

			if (new_direct_block == 0){				// There are no more free blocks
				return data_written;				// Just return the amount of data written so far
			}
			BLOCK_BITMAP[new_direct_block] = 1;			// You're going to use that block, so set it equal to unavailable (1)
			my_inode.direct[i] = new_direct_block;			// Update the inode's direct array with the new block
			w_size = DISK_BLOCK_SIZE;				// Set the default for writing to be equal to the block size
			if (length - data_written < DISK_BLOCK_SIZE){ 		// If the amount of data to write is smaller than the block size
				w_size = length - data_written;			// Update the write size to that smaller amount of data
			}

			//-----------------------------------------Write to the block---------------------------------------------
			memcpy(d_block.data, data+data_written+offset, w_size);	// Copy the data over
			data_written = data_written + w_size;				// Update how much data has been written so far
			disk_write(new_direct_block, d_block.data);			// Write the block back to disk

			size_of_inode = size_of_inode + w_size;			// Update the size of the inode
			block.inode[inumber].size = size_of_inode;			// Reassign it to the inode data
			disk_write(block_number, block.data);				// Write that inode data back to disk

			if (data_written == length){
				return data_written;
			}
		}

	}
	//------------------------------------------Loop through the indirect blocks---------------------------------------

	// Reinitialize the block
	union fs_block og_block;
	block_number = inumber/127 + 1;
	disk_read(block_number, og_block.data);
	my_inode = og_block.inode[inumber];

	//printf("indirect from og: %d \n", my_inode.indirect);

	int indirect_present = my_inode.indirect;

	size_of_inode = my_inode.size;

	int j, new_indirect_block;
	union fs_block indirect_block;
	union fs_block pointer_block;
	if (data_written < length){						// All the data has not been written yet

		//---------------------------------------NO INDIRECT BLOCK PRESENT----------------------------------------------
		if (!indirect_present){						// There is no indirect block present
			int new_indirect_num = get_free_block();		// Get a new block number for that indirect block
			if (new_indirect_num == 0){				// There are no more free blocks
				return data_written;				// Return the amount of data written so far
			}
			//printf("Initializing indirect block %d \n", new_indirect_num);
			BLOCK_BITMAP[new_indirect_num] = 1;			// Set it to unavailable
			og_block.inode[i_number].indirect = new_indirect_num;	// Assign the indirect to that block

			disk_write(block_number, og_block.data);			// Write it back

			disk_read(new_indirect_num, indirect_block.data);	// Read that new indirect block

			/*int u;
			for (u = 0; u < POINTERS_PER_BLOCK; u++){
				printf("u = %d and data is %d \n", u, indirect_block.pointers[u]);
			}*/

			while(data_written < length){
				for (j = 0; j < POINTERS_PER_BLOCK; j++){
					//------------------------------Allocate a new indirect pointer-----------------------------------
					int new_i_block = get_free_block();					// Get a new block
					if (new_i_block == 0){							// There are no more free blocks
						return data_written;						// Return the amount of data written so far
					}
					BLOCK_BITMAP[new_i_block] = 1;					// Set the block to unavailable
					indirect_block.pointers[j] = new_i_block;				// Update inode's indirect ptrs w/ new block	
					
					disk_write(new_indirect_num, indirect_block.data);			// Write the indirect block back to disk
					

					disk_read(new_i_block, pointer_block.data);				// Read that pointer block in
					
					//-----------------------------------Write to the block----------------------------------------
					int w_size = DISK_BLOCK_SIZE;						// Set the default for writing to be 
														// equal to the block size
					if (length - data_written < DISK_BLOCK_SIZE){ 				// If the amount of data to write is smaller 
														// than the block size
						w_size = length - data_written;					// Update the write size
					}
					memcpy(pointer_block.data, data+data_written+offset, w_size);	// Copy the data over
					data_written = data_written + w_size;					// Update how much data has been 
														// written so far
					disk_write(new_i_block, pointer_block.data);				// Write the pointer block back to disk

					size_of_inode = size_of_inode + w_size;			// Update the size of the inode
					og_block.inode[inumber].size = size_of_inode;			// Reassign it to the inode data
					disk_write(block_number, og_block.data);			// Write that inode data back to disk

					//printf("Writing to pointer block %d \n", new_i_block);
					
					if (data_written == length){
						return data_written;
					}
				}
			}

		}
		//---------------------------------------INDIRECT BLOCK IS PRESENT----------------------------------------------
		else{									
			//printf("Indirect block is present and is: %d\n", indirect_present);
			disk_read(indirect_present, indirect_block.data);

			int last_indirect_ptr;

			/*int y;
			for (y = 0; y < POINTERS_PER_BLOCK; y++){
				printf("y = %d and data is %d \n", y, indirect_block.pointers[y]);
			}*/

			union fs_block super;
			disk_read(0, super.data);
			int total_blocks = super.super.nblocks;

			int o, index;
			int num_of_indirect_blocks = 0;
			for (o = 0; o < 1024; o++){
				if (indirect_block.pointers[o] < total_blocks){
					
					num_of_indirect_blocks++;
				}
				else{
					index = o - 1;
					last_indirect_ptr = indirect_block.pointers[index];
					break;
				}
			}
			//printf("num of indirect blocks is: %d \n", num_of_indirect_blocks);
			//printf("called debug before if statement for last block is full \n");
			//fs_debug();
			if (!last_block_is_full){						// The last indirect block is not completely full
				disk_read(last_indirect_ptr, indirect_block.data);
				//printf("Last blog isn't full \n");
				//----------------------------Last Block Isn't Full---------------------------------------------
				new_indirect_block = last_indirect_ptr;			// Set the place to write to the last direct block
				w_size = remainder_of_last_block;			// Set the size to write the rest of that block
				if (length < remainder_of_last_block){			// If the amount we have to write is less than the remainder
					w_size = length;				// Set the size to write equal to the length
				}
				//printf("Size to write is %d \n", w_size);
				//--------------------------------Write to the block------------------------------------------
				memcpy(indirect_block.data, data+data_written+offset, w_size);	
				data_written = data_written + w_size;			// Update how much data has been written so far
				disk_write(last_indirect_ptr, indirect_block.data);	// Write back to disk

				//printf("**AFTER WRITING TO THE LAST BLOW \n");
				//printf("Writing to indirect block %d\n", new_indirect_block);
				//fs_debug();

				//printf("Size of the inode before update: %d \n", size_of_inode);
				size_of_inode = size_of_inode + w_size;			// Update the size of the inode

				og_block.inode[inumber].size = size_of_inode;			// Reassign it to the inode data
				disk_write(block_number, og_block.data);			// Write that inode data back to disk
				
				
				//fs_debug();
				if (data_written == length){
					return data_written;
				}
			}
			while(data_written < length){
				for (j = num_of_indirect_blocks; j <= POINTERS_PER_BLOCK; j++){
					//------------------------------Allocate a new indirect pointer-----------------------------------
					int new_i_block = get_free_block();		// Get a new block
					if (new_i_block == 0){
						return data_written;
					}
					//printf("new pointer is: %d \n", new_i_block);
					//printf("j position for the new pointer in the pointer array is %d \n", j);
					BLOCK_BITMAP[new_i_block] = 1;		// You're going to use that block, so set it equal to unavailable (1)
					indirect_block.pointers[j] = new_i_block;	// Update the inode's indirect ptrs with the new block	
					//printf("before the write for new pointers \n");
					//fs_debug();
					//printf("indirect we're writing back to: %d \n", indirect_present);
					union fs_block indirect_block_again;
					disk_read(indirect_present, indirect_block_again.data);
					indirect_block_again.pointers[j] = new_i_block;
					disk_write(indirect_present, indirect_block_again.data);
					//disk_write(indirect_present, indirect_block.data);			// Write the indirect block back to disk
					//fs_debug();

					//-----------------------------------Write to the block----------------------------------------
					int w_size = DISK_BLOCK_SIZE;					// Set the default for writing to be equal to the block size
					if (length - data_written < DISK_BLOCK_SIZE){ 			// If the amount of data to write is smaller than the block size
						w_size = length - data_written;				// Update the write size to that smaller amount of data
					}
					memcpy(indirect_block.data, data+data_written+offset, w_size);	// Copy the data over
					data_written = data_written + w_size;					// Update how much data has been written so far
					disk_write(new_i_block, indirect_block.data);				// Write the block back to disk

					//printf("Writing to indirect block %d\n", new_i_block);
					//fs_debug();
					size_of_inode = size_of_inode + w_size;			// Update the size of the inode

					//printf("Size of the inode: %d \n", size_of_inode);
					og_block.inode[inumber].size = size_of_inode;			// Reassign it to the inode data
					disk_write(block_number, og_block.data);			// Write that inode data back to disk

					
					if (data_written == length){
						return data_written;
					}
				}
			}

		}
	}
	return 0;
}




