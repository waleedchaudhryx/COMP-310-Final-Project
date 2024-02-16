

#include <stdlib.h>
#include <string.h>


#include "disk_emu.h"
#include "sfs.h"


// ======================================================================================
// disk structures in memory & caches
// fs max size 8MB
SuperBlock sblock;
INode inodes[MAX_INODES]; 	// max size 81KB
DirEntry *root = 0;			// max size 20KB
bitmap_t freemap[MAX_FREEMAP_ID];
block_t freemap_freeblocks;	// number of free blocks

block_t freemap_block; 		// free map is not greater than 1 block 1KB
block_t first_data_block;

// file search state
int last_search_index = -1; // not started

// open files descriptor table
FileDesc ofdt[MAX_FD];

extern inode_t last_inode_block;



// ======================================================================================
// mounts sfs
void mksfs(int fresh)
{
	int ret, i;
	if (fresh) ret = init_fresh_disk(FILESYSTEM_IMAGE_FILE, BLOCK_SIZE, MAX_FS_SIZE);
	else ret = init_disk(FILESYSTEM_IMAGE_FILE, BLOCK_SIZE, MAX_FS_SIZE);
	
	if (ret < 0) return; // error
	
	if (fresh) {
		// fill fs with defaults
		block_t block = 0;
		
		// init superblock
		memset(&sblock, 0, sizeof(sblock));
		sblock.magic = SB_MAGIC;
		sblock.blksize = BLOCK_SIZE;
		sblock.fssize = MAX_BLOCK;
		sblock.inodeBlks = MAX_INODE_BLOCKS;
		sblock.inodeRoot = 0;
		ret = write_blocks(block, 1, &sblock); block++;
		if ((ret < 0) || (ret != 1)) return; // error
		
		// init inodes
		memset(inodes, 0, sizeof(inodes));
		int inode_cnt = sblock.inodeBlks * INODES_PER_BLOCK;
		for (i = 0; i < inode_cnt; i++)
		{
			memset(&inodes[i].blocks[0], BLOCK_FREE, sizeof(inodes[i].blocks));
			inodes[i].next = BLOCK_FREE;
		}
		inodes[sblock.inodeRoot].used = 1; // open root dir
		ret = write_blocks(block, sblock.inodeBlks, inodes); block += sblock.inodeBlks;
		if ((ret < 0) || (ret != sblock.inodeBlks)) return; // error

		// set where is freemap
		freemap_block = block;
		
		// init freemap
		memset(freemap, 0, sizeof(freemap));
		ret = write_blocks(block, 1, freemap); block++;
		if ((ret < 0) || (ret != 1)) return; // error
		freemap_freeblocks = sblock.fssize;
		
		// set first data block
		first_data_block = block;

		// root dir is empty
		root = 0;
	}
	else {
		// load fs data structures
		block_t block = 0;
		
		// read superblock
		ret = read_blocks(block, 1, &sblock); block++;
		if ((ret < 0) || (ret != 1)) return; // error
		
		// check superblock
		if (sblock.magic != SB_MAGIC) return; // error
		if (sblock.blksize != BLOCK_SIZE) return; // error
		if ((sblock.fssize <= 0) || (sblock.fssize > MAX_BLOCK)) return; // error
		if ((sblock.inodeBlks <= 0) || (sblock.inodeBlks > MAX_INODE_BLOCKS)) return; // error
		if ((sblock.inodeRoot < 0) || (sblock.inodeRoot >= MAX_INODES)) return; // error
		
		// read inodes
		memset(inodes, 0, sizeof(inodes));
		ret = read_blocks(block, sblock.inodeBlks, inodes); block += sblock.inodeBlks;
		if ((ret < 0) || (ret != sblock.inodeBlks)) return; // error

		// set where is freemap
		freemap_block = block;
		
		// read freemap
		memset(freemap, 0, sizeof(freemap));
		ret = read_blocks(block, 1, freemap); block++;
		if ((ret < 0) || (ret != 1)) return; // error
		
		// set first data block
		first_data_block = block;
		
		// update freemap_freeblocks
		freemap_freeblocks = sblock.fssize;
		int blocks_rest = sblock.fssize;
		for(i=0;i < sblock.fssize;i++)
		{
			bitmap_t bm = freemap[i];
			for(int j=0;j < 32;j++) 
			{
				if ((bm & 1) != 0) freemap_freeblocks--;
				bm >>= 1;
				blocks_rest--;
				if (blocks_rest <= 0) break;
			}
			if (blocks_rest <= 0) break;
		}
		

		// read root directory
		if (root) free(root);
		root = malloc(inodes[sblock.inodeRoot].size);
		ret = i_read(sblock.inodeRoot, 0, (char *)root, inodes[sblock.inodeRoot].size);
		if ((ret < 0) || (ret != inodes[sblock.inodeRoot].size)) return; // error
	}
	
	// init open files descriptor table
	for(i=0;i < MAX_FD;i++) ofdt[i].inode = INODE_FREE;
	
	last_search_index = -1;
	
	// all ok
}

// ======================================================================================
// Once all the files have been returned, this function returns 0.
int sfs_getnextfilename(char* fname)
{
	if (!root) return 0;
	if (!fname) return 0;
	
	if (last_search_index < 0) {
		// init search
		last_search_index = 0;
	}
	
	int dir_entry_cnt = inodes[sblock.inodeRoot].size / DIR_ENTRY_SIZE;
	
	// skip empty records
	while((last_search_index < dir_entry_cnt) && (root[last_search_index].inode == INODE_FREE)) {
		last_search_index++;
	}

	if (last_search_index >= dir_entry_cnt) {
		// close search
		last_search_index = -1;
		return 0;
	}
	else {
		strcpy(fname, root[last_search_index].filename);
		last_search_index++;
		return 1;
	}
}

// ======================================================================================
// returns  the size of a given file if success or -1 otherwise
int sfs_getfilesize(const char* fname)
{
	if (!root) return -1;
	if (!fname) return -1;

	int fid = dir_getfileid(fname);
	if (fid < 0) return -1; // error
	
	return inodes[root[fid].inode].size;
}

// ======================================================================================
// setup filepointer to the end of file
// returns the index the file descriptor table (ofdt)
int sfs_fopen(char* fname)
{
	if (!fname) return -1;
	if (strlen(fname) > MAX_FNAME_LENGTH) return -1; // fname too long

	// try to find the file
	dir_t fid = dir_getfileid(fname);
	if (fid < 0) 
	{
		// file not found - try create file
		fid = dir_getfreeid();
		if (fid < 0) return -1;

		inode_t n = i_alloc();
		if (fid < 0) return -1;
		
		// allocates dir entry
		root[fid].inode = n;
		strncpy(root[fid].filename, fname, sizeof(root[fid].filename)-1);

		// updates disk structures^ inodes & root directory
		if (i_update(n) < 0) return -1;
		if (dir_update(fid) < 0) return -1;
	}

	// check ofdt for open file
	int fd = 0;
	while (fd < MAX_FD)
	{
		// reopen opened file not allowed
		if (ofdt[fd].inode == root[fid].inode) return -1; // error
		fd++;
	}

	// try to open the file
	// finds free descriptor
	fd = 0;
	while(fd < MAX_FD) 
	{
		if (ofdt[fd].inode == INODE_FREE) break;
		fd++;
	}
	
	if (fd >= MAX_FD) return -1; // ofdt is full

	// allocate ofdt entry
	ofdt[fd].inode = root[fid].inode;
	// setup filepointer to the end of file
	ofdt[fd].iopos = inodes[ofdt[fd].inode].size;
	
	return fd;
	
}

// ======================================================================================
// removes the entry from the fdt
// returns 0 if success or a negative value otherwise
int sfs_fclose(int fd)
{
	// check params
	if (fd < 0) return -1;
	if (fd >= MAX_FD) return -1;
	
	// remove ofdt entry
	if (ofdt[fd].inode == INODE_FREE) return -1; // already closed file
	ofdt[fd].inode = INODE_FREE;
	
	return 0;
}

// ======================================================================================
// returns the number of bytes written if success or 0 otherwise
int sfs_fwrite(int fd, const char* buf, int size)
{
	int inode_cnt = sblock.inodeBlks * INODES_PER_BLOCK;

	// check params
	if (fd < 0) return 0;
	if (fd >= MAX_FD) return 0;
	if (ofdt[fd].inode <= INODE_FREE) return 0; // not opened file
	if (ofdt[fd].inode >= inode_cnt) return 0;

	int ret = i_write(ofdt[fd].inode, ofdt[fd].iopos, buf, size);

	// update file position
	if (ret > 0) ofdt[fd].iopos += ret;

	return ret;
}

// ======================================================================================
// returns the number of bytes readed if success or 0 otherwise
int sfs_fread(int fd, char* buf, int size)
{
	int inode_cnt = sblock.inodeBlks * INODES_PER_BLOCK;

	// check params
	if (fd < 0) return 0;
	if (fd >= MAX_FD) return 0;
	if (ofdt[fd].inode <= INODE_FREE) return 0; // not opened file
	if (ofdt[fd].inode >= inode_cnt) return 0;

	int ret = i_read(ofdt[fd].inode, ofdt[fd].iopos, buf, size);

	// update file position
	if (ret > 0) ofdt[fd].iopos += ret;

	return ret;
}

// ======================================================================================
// returns 0 if success or a negative value otherwise
int sfs_fseek(int fd, int pos)
{
	// check params
	if (fd < 0) return -1;
	if (fd >= MAX_FD) return -1;
	if (ofdt[fd].inode == INODE_FREE) return -1; // not opened file

	if (pos < 0) return -1;
	if (pos > inodes[ofdt[fd].inode].size) return -1; // wrong file position
	
	// update ofdt entry
	ofdt[fd].iopos = pos;
	
	return 0;
}

// ======================================================================================
// removes the file from the directory entry, releases the i-Node and 
// releases the data blocks used by the file
// (i.e., the data blocks are added to the free block list/map)
int sfs_remove(char* fname)
{
	if (!root) return -1;
	if (!fname) return -1;

	int fid = dir_getfileid(fname);
	if (fid < 0) return -1; // error
	
	inode_t inode = root[fid].inode;
	if (inode == INODE_FREE) return -1; // error
	
	// removing root directory not allowed
	if (inode == sblock.inodeRoot) return -1; // error
	
	// check ofdt for open file
	int fd = 0;
	while(fd < MAX_FD) 
	{
		// removing opened file not allowed
		if (ofdt[fd].inode == inode) return -1; // error
		fd++;
	}

	// remove file from root directory
	root[fid].inode = INODE_FREE;
	
	// free file inode blocks
	// free file data blocks
	int fblks = (inodes[inode].size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	block_t prev_inode_block = -1;
	for(int i=0;i < fblks;i++) 
	{
		block_t blk = i_getblk(inode, i);
		if (blk < 0) return -1; // error
		blk -= first_data_block; // gets logical block
		
		// free file data blocks
		if (b_free(blk) < 0) return -1; // error

		// free file inode blocks
		if (prev_inode_block != last_inode_block) 
		{
			prev_inode_block = last_inode_block;
			if (b_free(last_inode_block) < 0) return -1; // error
		}
	}

	// remove file inode
	inodes[inode].used = 0;

	// save changes
	if (dir_update(fid) < 0) return -1;
	if (i_update(inode) < 0) return -1;
	if (fm_update() < 0) return -1; // error
	
	return 0;


	
}

// ======================================================================================


