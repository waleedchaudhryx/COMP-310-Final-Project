
#include <stdlib.h>
#include <string.h>

#include "sfs.h"

int dir_update(int fid)
{
	// check params
	if (!root) return -1;

	int dir_entry_cnt = inodes[sblock.inodeRoot].size / DIR_ENTRY_SIZE;
	if (fid < 0) return -1; // wrong fid
	if (fid >= dir_entry_cnt) return -1;

	int dirblk = fid / BLOCK_DIR_ENTRIES;
	int ret = i_write(sblock.inodeRoot, dirblk * BLOCK_SIZE, (char *)&root[dirblk * BLOCK_DIR_ENTRIES], BLOCK_SIZE);
	if ((ret < 0) || (ret != BLOCK_SIZE)) return -1; // error
	
	return 0;
}

// returns entry for fname
int dir_getfileid(const char* fname)
{
	if (!root) return -1;
	if (!fname) return -1;
	if (strlen(fname) > MAX_FNAME_LENGTH) return -1; // fname too long
	
	int dir_entry_cnt = inodes[sblock.inodeRoot].size / DIR_ENTRY_SIZE;
	
	for(int i=0;i < dir_entry_cnt;i++) 
	{
                // return direcory index if names equals
		if ((root[i].inode != INODE_FREE) && !strcmp(root[i].filename, fname)) 
		{
			return i;
		}
	}
	return -1; // file not found
}

// returns first free entry
int dir_getfreeid()
{
	int i;
	int dir_entry_cnt = inodes[sblock.inodeRoot].size / DIR_ENTRY_SIZE;

	if (!root)  // create root if not exists
	{
		root = malloc(BLOCK_SIZE);
		memset(root, 0, BLOCK_SIZE);
		for (i = 0; i < BLOCK_DIR_ENTRIES; i++) root[i].inode = INODE_FREE;

		// save root entries
		int ret = i_write(sblock.inodeRoot, 0 * BLOCK_SIZE, (char *)&root[0 * BLOCK_DIR_ENTRIES], BLOCK_SIZE);
		if ((ret < 0) || (ret != BLOCK_SIZE)) return -1; // error

		dir_entry_cnt = BLOCK_DIR_ENTRIES;
	}
		
	for(i=0;i < dir_entry_cnt;i++) 
	{
                // return direcory index where free item found
		if (root[i].inode == INODE_FREE) return i;
	}
	
	// append new directory block if need
	int oldsz = inodes[sblock.inodeRoot].size;
	int oldcnt = oldsz / DIR_ENTRY_SIZE;
	root = realloc(root, oldsz + BLOCK_SIZE);
	memset(&root[oldcnt], 0, BLOCK_SIZE);
	for (i = oldcnt; i < (oldcnt+BLOCK_DIR_ENTRIES); i++) root[i].inode = INODE_FREE;

	// save root entries
	int ret = i_write(sblock.inodeRoot, oldsz, (char *)&root[oldcnt], BLOCK_SIZE);
	if ((ret < 0) || (ret != BLOCK_SIZE)) return -1; // error

	return oldcnt;
}

