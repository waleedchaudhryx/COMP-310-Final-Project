
#include <stdlib.h>
#include <string.h>

#include "disk_emu.h"
#include "sfs.h"


#define max(a,b)        ((a > b) ? a : b)



// inode pointers block
inode_t last_inode = INODE_FREE;
inode_t inode_blocks_offset = 0;
inode_t last_inode_block = -1;
block_t blocks[BLKPTR_PER_BLOCK];

int i_update(inode_t inode)
{
	int inode_cnt = sblock.inodeBlks * INODES_PER_BLOCK;

	// check params
	if (inode <= INODE_FREE) return -1; // not opened file
	if (inode >= inode_cnt) return -1;
	
	int inodeblk = inode / INODES_PER_BLOCK;
	int ret = write_blocks(inodeblk+1, 1, &inodes[inodeblk * INODES_PER_BLOCK]);
	if ((ret < 0) || (ret != 1)) return -1; // error
	
	return 0;
}

int fm_update()
{
	int ret = write_blocks(freemap_block, 1, freemap);
	if ((ret < 0) || (ret != 1)) return -1; // error
	return 0;
}

int b_zero(block_t blk)
{
	byte_t zerodata[BLOCK_SIZE];
	memset(zerodata, 0, BLOCK_SIZE);
	
	int ret = write_blocks(blk + first_data_block, 1, zerodata);
	if ((ret < 0) || (ret != 1)) return -1; // error
	return 0;
}

int i_append_blocks(inode_t inode, block_t* new_blocks, int new_blocks_cnt)
{
	if (new_blocks_cnt <= 0) return -1;
	
	int i, newb = 0;
	int icnt = sizeof(inodes[inode].blocks) / sizeof(block_t);
	int fblks = (inodes[inode].size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	// append blocks to inode record first
	block_t *bp = inodes[inode].blocks;
	if (fblks > icnt) 
	{
		// load pointers block
		block_t blk = i_getblk(inode, fblks-1);
		if (blk < 0) return -1; // error

		bp = blocks;
		icnt = BLKPTR_PER_BLOCK - 1;
		fblks -= inode_blocks_offset;
	}

	while(new_blocks_cnt > 0) // append new pointers block
	{
		for(i=fblks;i < icnt;i++) {
			bp[i] = new_blocks[newb]; newb++;
			new_blocks_cnt--;
			if (new_blocks_cnt <= 0) break;
		}
		
		// needs to alloc new block ?
		if (new_blocks_cnt > 0) {
			bp[icnt] = b_alloc_one();
			if (!bp[icnt]) return -1; // error - disk full

			if (fm_update() < 0) return -1; // error
			if (b_zero(bp[icnt]) < 0) return -1; // error
		}
		
		if (bp == inodes[inode].blocks) { // save inode entry
			if (i_update(inode) < 0) return -1;
		}
		else { // save pointers block
			//int ret = write_blocks(bp[icnt] + first_data_block, 1, blocks);
			int ret = write_blocks(last_inode_block + first_data_block, 1, blocks);
			if ((ret < 0) || (ret != 1)) return -1; // error
		}
		
		// prepare next block
		if (new_blocks_cnt > 0) {
			last_inode_block = bp[icnt];
			inode_blocks_offset += icnt;
			memset(blocks, BLOCK_FREE, sizeof(blocks));
			bp = blocks;
			icnt = BLKPTR_PER_BLOCK - 1;
			fblks = 0; // fill from 0
		}
	}
	return 0;
}

int b_free(block_t block)
{
	// check params
	if (block < 0) return -1;
	if (block >= sblock.fssize) return -1;
	
	// mod 32
	int bmid = block >> 5;
	int bmbit = block & 0x1f;

	bitmap_t bm = freemap[bmid];
	bitmap_t mask = 1 << bmbit;
	
	if ((bm & mask) == 0) return -1; // error - is already free block
	
	// free block
	freemap[bmid] = bm & ~mask;
	freemap_freeblocks++;
	
	return 0;
}

// returns array of free blocks
block_t* b_alloc(int nblocks)
{
	if (nblocks <= 0) return 0; // error
	if (nblocks > freemap_freeblocks) return 0; // error - disk full	

	// save bitmap
	bitmap_t freemap_save[MAX_FREEMAP_ID];
	memcpy(freemap_save, freemap, sizeof(freemap_save));
	
	// alloc array
	block_t* free_blocks = malloc(nblocks * sizeof(block_t));

	int brest = nblocks;
	int bptr = 0;
	for(int i=0;i < sblock.fssize;i++)
	{
		bitmap_t bm = freemap[i];
		if (bm != 0xffffffff) { // is free blocks ?
			bitmap_t mask = 1;
			for(int j=0;j < 32;j++) 
			{
				if ((bm & mask) == 0) { // is free block
					bm |= mask; // alloc block
                                        // save block number
					free_blocks[bptr] = (i << 5) + j; bptr++;
					brest--;
					if (brest <= 0) break;
				}
				mask <<= 1;
			}
			freemap[i] = bm; // save bitmap
			if (brest <= 0) break;
		}
	}

	if (brest > 0) // disk full
	{
		// restore bitmap if error
		memcpy(freemap, freemap_save, sizeof(freemap));
		free(free_blocks);
		return 0; // disk is full
	}
	else { // all ok
		freemap_freeblocks -= nblocks;
		return free_blocks; // disk is OK
	}
}

// returns one free block
block_t b_alloc_one()
{
	block_t *fb = b_alloc(1);
	if (fb) {
		block_t b = *fb;
		free(fb);
		return b;
	}
	return 0;
}

// returns first free entry
inode_t i_alloc()
{
	int inode_cnt = sblock.inodeBlks * INODES_PER_BLOCK;
	
	for(int i=0;i < inode_cnt;i++) 
	{
		if (!inodes[i].used)  // is inode free ?
		{
			memset(&inodes[i], 0, sizeof(inodes[i]));
			memset(&inodes[i].blocks[0], BLOCK_FREE, sizeof(inodes[i].blocks));
			inodes[i].next = BLOCK_FREE;
			inodes[i].used = 1; // allocates inode
			return i;
		}
	}
	return -1; // inodes is full
}

block_t i_getblk(inode_t inode, int blkid)
{
	int inode_cnt = sblock.inodeBlks * INODES_PER_BLOCK;

	if (inode <= INODE_FREE) return -1; // not opened file
	if (inode >= inode_cnt) return -1;
	if (!inodes[inode].used) return -1; // invalid inode
	
	block_t *bp = inodes[inode].blocks;
	int icnt = sizeof(inodes[inode].blocks) / sizeof(block_t);
	int bptr = blkid;
	
	// checks prev blocks pointers block
	if ((inode == last_inode) && (blkid >= inode_blocks_offset)) {
		if (inode_blocks_offset >= icnt) {
			bp = blocks;
			icnt = BLKPTR_PER_BLOCK - 1;
			bptr -= inode_blocks_offset;
		}
	}
	else {
		// save current inode blocks block
		last_inode = inode;
		inode_blocks_offset = 0;
		last_inode_block = -1;
	}
	
        // read next pointers blocks if need
	while(bptr >= icnt) {
		bptr -= icnt;
		bp += icnt; // bp-> next block
		inode_blocks_offset += icnt;
		icnt = BLKPTR_PER_BLOCK - 1;
		// read next inode blocks array
		if (*bp == BLOCK_FREE) return -1; // error - block not found
		last_inode_block = *bp;
		int ret = read_blocks(*bp + first_data_block, 1, blocks);
		if ((ret < 0) || (ret != 1)) return -1; // error
		bp = blocks;
	}
	
        // return absolute block number for given file offset
	return first_data_block + bp[bptr];
	
}

int i_read(inode_t inode, int offset, char* buf, int size)
{
	int inode_cnt = sblock.inodeBlks * INODES_PER_BLOCK;

	if (inode <= INODE_FREE) return 0; // not opened file
	if (inode >= inode_cnt) return 0;
	if (!inodes[inode].used) return 0; // invalid inode
	if (offset < 0) return 0; // error
	if (size <= 0) return 0; // error
	if (!buf) return 0; // error
	
	// correct size to read if param size is greater then rest of file
	if ((offset + size) > inodes[inode].size) 
	{
		size -= ((offset + size) - inodes[inode].size);
	}
	if (size <= 0) return 0; // error
	
        // used space in first reading block
	int first_block_bytes = offset % BLOCK_SIZE;
        // first reading block number
	int first_block = offset / BLOCK_SIZE;
	
        // calc full number blocks to read
	int readblocks = size;
	if (first_block_bytes > 0) 
	{
		readblocks -= (BLOCK_SIZE - first_block_bytes);
	}
	readblocks = (readblocks + BLOCK_SIZE - 1) / BLOCK_SIZE;
	if (first_block_bytes > 0) readblocks++;

        // allocates buffer for data
	int blksize = readblocks * BLOCK_SIZE;
	byte_t *data = malloc(blksize);
	if (!data) return 0; // memory full

	// read file blocks
	int rest = readblocks;
	block_t blk = i_getblk(inode, first_block);
	if (blk < 0) {
		free(data);
		return 0; // error
	}
	int curblk = 0;
	while(rest > 0) {
		int ret = read_blocks(blk, 1, &data[curblk * BLOCK_SIZE]);
		if ((ret < 0) || (ret != 1)) {
			free(data);
			return 0; // error
		}
		curblk++;
		rest--;
		
                // next block ?
		if (rest > 0) {
			blk = i_getblk(inode, first_block+curblk);
			if (blk < 0) {
				free(data);
				return 0; // error
			}
		}
	}
	
	// copy data to user buffer
	memcpy(buf, &data[first_block_bytes], size);

	free(data);
	return size;
}


int i_write(inode_t inode, int offset, const char* buf, int size)
{
	int inode_cnt = sblock.inodeBlks * INODES_PER_BLOCK;

	if (inode <= INODE_FREE) return 0; // not opened file
	if (inode >= inode_cnt) return 0;
	if (!inodes[inode].used) return 0; // invalid inode
	if (offset < 0) return 0; // error
	if (size <= 0) return 0; // error
	if (!buf) return 0; // error
	
	// allocate new blocks for the inode according to size
	int new_fsize = offset + size;
	if (new_fsize > inodes[inode].size) 
	{
		int old_fblks = (inodes[inode].size + BLOCK_SIZE - 1) / BLOCK_SIZE;
		int new_fblks = (new_fsize + BLOCK_SIZE - 1) / BLOCK_SIZE;
		int new_fblks_cnt = new_fblks - old_fblks;

		// check disk full condition
		int total_new_blks_cnt = new_fblks_cnt;
		// + new inode ptr blocks
		int icnt = sizeof(inodes[inode].blocks) / sizeof(block_t);
		int obs = (max(0, old_fblks - icnt) + BLKPTR_PER_BLOCK - 2) / (BLKPTR_PER_BLOCK - 1);
		int nbs = (max(0, new_fblks - icnt) + BLKPTR_PER_BLOCK - 2) / (BLKPTR_PER_BLOCK - 1);
		total_new_blks_cnt += (nbs - obs);
		if (total_new_blks_cnt > freemap_freeblocks) return 0; // error - disk full
		
		// allocate blocks
		if (total_new_blks_cnt > 0)
		{
			block_t *new_blocks = b_alloc(new_fblks_cnt);
			if (!new_blocks) return 0; // error - disk full

			if (fm_update() < 0) {
				free(new_blocks);
				return 0; // error
			}
			if (i_append_blocks(inode, new_blocks, new_fblks_cnt) < 0) {
				free(new_blocks);
				return 0; // error
			}
			free(new_blocks);
		}
		
		// update size
		inodes[inode].size = new_fsize;
		if (i_update(inode) < 0) return 0;
	}

	// write data to inode blocks
	int first_block_bytes = offset % BLOCK_SIZE;
	int first_block = offset / BLOCK_SIZE;
	int first_write_bytes = BLOCK_SIZE - first_block_bytes;
	
	int writeblocks = size;
	if (first_block_bytes > 0) writeblocks -= first_write_bytes;
	writeblocks = (writeblocks + BLOCK_SIZE - 1) / BLOCK_SIZE;
	if (first_block_bytes > 0) writeblocks++;

	// write file blocks
	int rest = writeblocks;
	int rest_bytes = size;

	// read first block if needs
	int ret;
	block_t blk;
	char data[BLOCK_SIZE];
	char* bufptr = (char*)buf;
	int curblk = 0;
	
        // write first block
	if (first_block_bytes > 0) 
	{
		blk = i_getblk(inode, first_block);
		if (blk < 0) return 0; // error

		ret = read_blocks(blk, 1, data);
		if ((ret < 0) || (ret != 1)) return 0; // error

		// prepare block
		memcpy(&data[first_block_bytes], buf, first_write_bytes);
		
		// save block
		ret = write_blocks(blk, 1, data);
		if ((ret < 0) || (ret != 1)) return -1; // error
		
		rest--;
		rest_bytes -= first_write_bytes;
		bufptr += first_write_bytes;
		curblk++;
	}

        // write next blocks
	while(rest > 0) {
		blk = i_getblk(inode, first_block+curblk);
		if (blk < 0) return 0; // error
		
		int to_write = rest_bytes;
		if (to_write > BLOCK_SIZE) to_write = BLOCK_SIZE;

		// read & prepare last block if needs
		if (to_write < BLOCK_SIZE) 
		{
			ret = read_blocks(blk, 1, data);
			if ((ret < 0) || (ret != 1)) return 0; // error

			// prepare block
			memcpy(data, bufptr, to_write);
			bufptr = data;
		}

		ret = write_blocks(blk, 1, bufptr);
		if ((ret < 0) || (ret != 1)) return 0; // error
		
		rest_bytes -= to_write;
		bufptr += to_write;
		curblk++;
		rest--;
	}

	return size;
}
