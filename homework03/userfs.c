#include "userfs.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

long long
min(long long a, long long b)
{
	return a < b ? a : b;
}

long long
max(long long a, long long b)
{
	return a > b ? a : b;
}

enum
{
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char *memory;
	/** How many bytes are occupied. */
	int occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
	struct block *prev;

	/* PUT HERE OTHER MEMBERS */
};

struct file {
	/** Double-linked list of file blocks. */
	struct block *block_list;
	/**
	 * Last block in the list above for fast access to the end
	 * of file.
	 */
	struct block *last_block;
	/** How many file descriptors are opened on the file. */
	int refs;
	/** File name. */
	char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;

	size_t bytes_left;
	bool in_list;
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
	struct file *file;

	bool is_occupied;
	ssize_t offset;
	bool can_read;
	bool can_write;
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

// check permission for FD
bool
is_permitted(int flags, enum open_flags flag);

// search file in a list
struct file *
search_file(const char *filename);

// append file to files list
void
append_file(struct file *new_file);

// search FREE FD to use
int
get_fd();

// create empty block on file
struct block *
create_block(struct file *file);

bool
is_valid_fd(int fd);

// free file's blocks and memory
void
free_file_memory(struct file *file);

enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

int ufs_open(const char *filename, int flags)
{
	// search file or create if needed
	struct file *f = search_file(filename);
	if (f == NULL)
	{
		if (is_permitted(flags, UFS_CREATE))
		{
			f = (struct file *)malloc(sizeof(struct file));
			f->block_list = NULL;
			f->last_block = NULL;
			f->prev = NULL;
			f->next = NULL;
			f->name = (char *)malloc(strlen(filename));
			memcpy(f->name, filename, strlen(filename));
			f->refs = 0;
			f->bytes_left = MAX_FILE_SIZE;
			f->in_list = true;

			append_file(f);
		}
		else
		{
			ufs_error_code = UFS_ERR_NO_FILE;
			return -1;
		}
	}

	f->refs++;

	// search for free filedesc and create filedesc
	int fd = get_fd();
	struct filedesc *filedesc = (struct filedesc *)malloc(sizeof(struct filedesc));
	filedesc->file = f;
	filedesc->is_occupied = true;
	filedesc->offset = 0;
	filedesc->can_read = flags == 0 || is_permitted(flags, UFS_CREATE) || is_permitted(flags, UFS_READ_ONLY) || is_permitted(flags, UFS_READ_WRITE);
	filedesc->can_write = flags == 0 || is_permitted(flags, UFS_CREATE) || is_permitted(flags, UFS_WRITE_ONLY) || is_permitted(flags, UFS_READ_WRITE);
	file_descriptors[fd] = filedesc;
	return fd;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
	if (is_valid_fd(fd) == false)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	struct filedesc *filedesc = file_descriptors[fd];

	if (filedesc->can_write == false)
	{
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}

	struct file *file = filedesc->file;

	if (size > file->bytes_left)
	{
		ufs_error_code = UFS_ERR_NO_MEM;
		return -1;
	}

	ssize_t buf_offset = 0;
	ssize_t offset = filedesc->offset;
	struct block *block = file->block_list;

	while (size > 0)
	{
		if (block == NULL)
		{
			block = create_block(file);
			offset = 0;
		}

		ssize_t block_offset = min(offset, block->occupied);
		if (block_offset == BLOCK_SIZE)
		{
			offset -= BLOCK_SIZE;
			block = block->next;
			continue;
		}

		ssize_t write_bytes = min(size, BLOCK_SIZE - block_offset);
		memcpy(block->memory + block_offset, buf + buf_offset, write_bytes);
		buf_offset += write_bytes;
		size -= write_bytes;
		block->occupied = max(block_offset + write_bytes, block->occupied);
		block = block->next;
		offset = 0;
	}

	filedesc->offset += buf_offset;
	file->bytes_left -= buf_offset;

	return buf_offset;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
	if (is_valid_fd(fd) == false)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	struct filedesc *filedesc = file_descriptors[fd];

	if (filedesc->can_read == false)
	{
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}

	struct file *file = filedesc->file;

	ssize_t bytes_read = 0;
	struct block *file_block = file->block_list;

	ssize_t offset = filedesc->offset;

	while (file_block != NULL && size > 0)
	{
		int occupied = file_block->occupied;
		int block_offset = min(offset, BLOCK_SIZE);

		if (block_offset == BLOCK_SIZE)
		{
			offset -= BLOCK_SIZE;
		}
		else
		{
			ssize_t bytes_to_read = min(occupied - block_offset, size);
			if (bytes_to_read < 0)
				break;

			memcpy(buf + bytes_read, file_block->memory + block_offset, bytes_to_read);
			bytes_read += bytes_to_read;
			offset = 0;
			size -= bytes_to_read;
		}
		file_block = file_block->next;
	}

	filedesc->offset += bytes_read;
	return bytes_read;
}

int ufs_close(int fd)
{
	if (is_valid_fd(fd) == false)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	struct filedesc *filedesc = file_descriptors[fd];
	struct file *file = filedesc->file;
	file->refs--;

	if (file->refs == 0 && file->in_list == false)
	{
		free_file_memory(file);
		free(file);
	}

	free(filedesc);
	file_descriptors[fd] = NULL;
	return 0;
}

int ufs_delete(const char *filename)
{
	struct file *file = search_file(filename);
	if (file == NULL)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	struct file *prev_file = file->prev;
	struct file *next_file = file->next;
	if (prev_file != NULL && next_file != NULL)
	{
		prev_file->next = next_file;
		next_file->prev = prev_file;
	}
	else if (prev_file == NULL)
	{
		file_list = next_file;
		if (next_file != NULL)
		{
			next_file->prev = NULL;
		}
	}
	else if (next_file == NULL)
	{
		prev_file->next = NULL;
	}

	file->in_list = false;
	if (file->refs == 0)
	{
		free_file_memory(file);
		free(file);
	}
	return 0;
}

void ufs_destroy(void)
{
	struct file *file = file_list;
	while (file != NULL)
	{
		struct file *copy = file;
		file = file->next;
		free_file_memory(copy);
		free(copy);
	}

	for (int i = 0; i < file_descriptor_capacity; i++)
	{
		struct filedesc *fd = file_descriptors[i];
		if (fd == NULL)
			continue;

		struct file *file = fd->file;
		if (file != NULL && file->in_list == false)
		{
			free_file_memory(file);
			free(file);
		}

		free(fd);
		file_descriptors[i] = NULL;
	}

	free(file_list);
	free(file_descriptors);
}

int
ufs_resize(int fd, size_t new_size)
{
	if (is_valid_fd(fd) == false)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	struct filedesc *filedesc = file_descriptors[fd];

	if (filedesc->can_write == false)
	{
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}

	if (new_size > MAX_FILE_SIZE)
	{
		ufs_error_code = UFS_ERR_NO_MEM;
		return -1;
	}

	struct file *file = filedesc->file;
	struct block *block = file->block_list;

	while (new_size > 0)
	{
		if (block == NULL)
		{
			block = create_block(file);
			new_size -= BLOCK_SIZE;
		}
		else if (new_size >= BLOCK_SIZE)
		{
			new_size -= BLOCK_SIZE;
		}
		else if (new_size < (size_t)block->occupied)
		{
			char *new_memory = (char *)malloc(BLOCK_SIZE);
			memcpy(new_memory, block->memory, new_size);
			free(block->memory);
			block->memory = new_memory;
			block->occupied = new_size;
			new_size = 0;
		}

		block = block->next;
	}

	if (block != NULL && block->prev != NULL)
	{
		file->last_block = block->prev;
		block->prev->next = NULL;
	}

	while (block != NULL)
	{
		struct block *copy = block;
		block = block->next;
		free(copy->memory);
		free(copy);
	}

	size_t new_file_size = 0;
	block = file->block_list;
	while (block != NULL)
	{
		new_file_size += block->occupied;
		block = block->next;
	}

	file->bytes_left = MAX_FILE_SIZE - new_file_size;
	return 0;
}

bool
is_permitted(int flags, enum open_flags flag)
{
	return (flags & flag) == flag;
}

struct file *
search_file(const char *filename)
{
	struct file *f = file_list;
	while (f != NULL && strcmp(f->name, filename) != 0)
	{
		f = f->next;
	}

	return f;
}

void
append_file(struct file *new_file)
{
	struct file *f = file_list;
	if (f == NULL)
	{
		file_list = new_file;
	}
	else
	{
		while (f->next != NULL)
		{
			f = f->next;
		}
		f->next = new_file;
		new_file->prev = f;
	}
}

int
get_fd()
{
	int fd = -1;
	if (file_descriptors == NULL)
	{
		file_descriptor_capacity = 10;
		file_descriptors = (struct filedesc **)malloc(file_descriptor_capacity * sizeof(struct filedesc *));
		fd = 0;
	}
	else if (file_descriptor_count == file_descriptor_capacity)
	{
		file_descriptor_capacity *= 2;
		struct filedesc **new_fd_list = (struct filedesc **)malloc(file_descriptor_capacity * sizeof(struct filedesc *));
		memcpy(new_fd_list, file_descriptors, file_descriptor_capacity / 2 * sizeof(struct filedesc *));
		free(file_descriptors);
		file_descriptors = new_fd_list;
		fd = file_descriptor_count;
	}
	else
	{
		for (int i = 0; i < file_descriptor_capacity; i++)
		{
			if (file_descriptors[i] == NULL)
			{
				fd = i;
				break;
			}
		}
	}

	file_descriptor_count++;
	return fd;
}

struct block *
create_block(struct file *file)
{
	if (file == NULL)
	{
		return NULL;
	}

	struct block *new_block = (struct block *)malloc(sizeof(struct block));
	new_block->memory = (char *)malloc(BLOCK_SIZE);
	new_block->occupied = 0;
	new_block->next = NULL;

	if (file->last_block == NULL)
	{
		file->last_block = file->block_list = new_block;
	}
	else
	{
		file->last_block->next = new_block;
		new_block->prev = file->last_block;
		file->last_block = new_block;
	}

	return new_block;
}

bool
is_valid_fd(int fd)
{
	if (fd < 0 || fd >= file_descriptor_capacity)
	{
		return false;
	}

	struct filedesc *filedesc = file_descriptors[fd];
	if (filedesc == NULL || filedesc->file == NULL || filedesc->is_occupied == false)
	{
		return false;
	}

	return true;
}

void
free_file_memory(struct file *file)
{
	struct block *block = file->block_list;
	while (block != NULL)
	{
		struct block *copy = block;
		block = block->next;
		free(copy->memory);
		free(copy);
	}
	free(file->name);
	file->bytes_left = MAX_FILE_SIZE;
	file->block_list = file->last_block = NULL;
}
