#ifndef SFS_H
#define SFS_H


// types for sfs project
typedef unsigned char byte_t;
typedef short int inode_t;
typedef short int block_t;
typedef int dir_t;
typedef unsigned int bitmap_t;


#define FILESYSTEM_IMAGE_FILE	"fs.sfs"

#define BLOCK_SIZE			1024

// filesystem size in blocks
// 8MB or less
#define MAX_FS_SIZE			(1024*8)

// disk structures item sizes
#define DIR_ENTRY_SIZE		64
#define INODE_ENTRY_SIZE	256

#define INODES_PER_BLOCK	(BLOCK_SIZE / INODE_ENTRY_SIZE)
#define BLKPTR_PER_BLOCK	(BLOCK_SIZE / sizeof(block_t))

// 1% of disk size for file metadata
// max inodes = max files on disk
// MAX_FS_SIZE * INODES_PER_BLOCK / 100   ~327   81 blocks    81*4=324
#define MAX_INODES			324
// inodes table max size in blocks
#define MAX_INODE_BLOCKS	(MAX_INODES / INODES_PER_BLOCK)

// max block item in freemap block
#define MAX_FREEMAP_ID		(BLOCK_SIZE / sizeof(bitmap_t))

//#define MAX_FNAME_LENGTH	(DIR_ENTRY_SIZE - sizeof(inode_t))
#define MAX_FNAME_LENGTH	32
#define BLOCK_DIR_ENTRIES	(BLOCK_SIZE / DIR_ENTRY_SIZE)

// max file descriptors
#define MAX_FD 				16
// max blocks in file system
#define MAX_BLOCK			(MAX_FS_SIZE - 1 - 81 - 1)

// superblock params
#define SB_MAGIC			0xACBD0005

// markers for free elements
#define INODE_FREE			-1
#define BLOCK_FREE			-1




#pragma pack(push, 1)

typedef struct {
	int magic;
	int blksize;
	int fssize;
	int inodeBlks;
	int inodeRoot;
	int padding[251];
} SuperBlock;

// inodes table
typedef struct {
	int used;				// not 0 if inode inused
	int mode;				// to be "unix-like" - not using
	int linkcnt;			// to be "unix-like" - not using
	int uid;				// to be "unix-like" - not using
	int gid;				// to be "unix-like" - not using
	int size;				// file size
	block_t blocks[115]; 	// blocks of inode, relative to first_data_block var
	block_t next;			// block with next inode blocks
} INode;

// root directory items
typedef struct {
	char filename[MAX_FNAME_LENGTH+1];
	inode_t inode;
        char padding[DIR_ENTRY_SIZE - (MAX_FNAME_LENGTH + 1) - sizeof(inode_t)];
} DirEntry;

// open file table item
typedef struct {
	inode_t inode;	// equals INODE_FREE if entry is free
	int iopos;		// position in file
} FileDesc;

#pragma pack(pop)

// disk structures
extern SuperBlock sblock;
extern INode inodes[MAX_INODES];
extern DirEntry *root;
extern bitmap_t freemap[MAX_FREEMAP_ID];

// file search state
extern int last_search_index;

// misc tools
//extern inode_t last_inode_block;
extern block_t freemap_freeblocks;	// number of free blocks
extern block_t freemap_block; 		// free map is not greater than 1 block 1KB
extern block_t first_data_block;

// root directory
// get root item by fname
extern int dir_getfileid(const char* fname);
// get free root item
extern int dir_getfreeid();
// write root directory changes to disk
extern int dir_update(int fid);

// free blocks map - logical blocks
// allocates nblocks sfs data blocks
extern block_t* b_alloc(int nblocks);
// allocates 1 sfs data block
extern block_t b_alloc_one();
// clear block on disk - write zeros
extern int b_zero(block_t blk);
// marks block as unused - free it
extern int b_free(block_t block);
// write free map changes to disk
extern int fm_update();

// inode table
// allocates 1 sfs inode item
extern inode_t i_alloc();
// for given inode searches in inode record and inode pointers blocks
// for required file offset
// returns absolute block number by logical file block number(blkid)
extern block_t i_getblk(inode_t inode, int blkid);
// reads size bytes from disk to buf from offset for given inode
extern int i_read(inode_t inode, int offset, char* buf, int size);
// writes size bytes from buf to disk from offset for given inode
extern int i_write(inode_t inode, int offset, const char* buf, int size);
// appends new data blocks for inode for increase file size
extern int i_append_blocks(inode_t inode, block_t* new_blocks, int new_blocks_cnt);
// update inode structures on disk
extern int i_update(inode_t inode);



#endif

