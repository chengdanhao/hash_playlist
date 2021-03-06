#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "hash.h"

#define HASH_INFO 1
#define HASH_DBUG 1
#define HASH_WARN 1
#define HASH_EROR 1

#if HASH_INFO
#define hash_info(fmt, ...) printf("\e[0;32m[HASH_INFO] [%s %d] : "fmt"\e[0m\n", __func__, __LINE__, ##__VA_ARGS__);
#else
#define hash_info(fmt, ...)
#endif

#if HASH_DBUG
#define hash_debug(fmt, ...) printf("\e[0m[HASH_DBUG] [%s %d] : "fmt"\e[0m\n", __func__, __LINE__, ##__VA_ARGS__);
#else
#define hash_debug(fmt, ...)
#endif

#if HASH_WARN
#define hash_warn(fmt, ...) printf("\e[0;33m[HASH_WARN] [%s %d] : "fmt"\e[0m\n", __func__, __LINE__, ##__VA_ARGS__);
#else
#define hash_warn(fmt, ...)
#endif

#if HASH_EROR
#define hash_error(fmt, ...) printf("\e[0;31m[HASH_EROR] [%s %d] : "fmt"\e[0m\n", __func__, __LINE__, ##__VA_ARGS__);
#else
#define hash_error(fmt, ...)
#endif

ssize_t happy_write(const char* func, const int line, int fd, void *buf, size_t count) {
	int ret = -1;
	ssize_t n_w = 0;

	if ((n_w = write(fd, buf, count)) < 0) {
		hash_error("(%s : %d calls) write error.", func, line);
		goto exit;
	}

	if (n_w != count) {
		hash_error("(%s : %d calls) write incomplete, n_w = %ld, count = %ld.", func, line, n_w, count);
		goto exit;
	}

	ret = 0;

exit:
	return ret;
}

ssize_t happy_read(const char* func, const int line, int fd, void *buf, size_t count) {
	int ret = -1;
	ssize_t n_r = 0;

	if ((n_r = read(fd, buf, count)) < 0) {
		hash_error("(%s : %d calls) read error.", func, line);
		goto exit;
	}

	if (n_r < count) {
		hash_error("(%s : %d calls) read incomplete, n_r = %ld, count = %ld.", func, line, n_r, count);
		goto exit;
	}

	ret = 0;

exit:
	return ret;
}

#define write(fd, buf, count)	happy_write(__func__, __LINE__, fd, buf, count)
#define read(fd, buf, count)	happy_read(__func__, __LINE__, fd, buf, count)

int get_slot_node_cnt(const char* path, uint32_t which_slot) {
	int ret = -1;
	int fd = 0;
	hash_header_t header;
	slot_info_t* slots = NULL;
	uint32_t slot_cnt = 0;
	uint32_t node_cnt = 0;

	memset(&header, 0, sizeof(hash_header_t));

	if ((fd = open(path, O_RDONLY)) < 0) {
		hash_error("open file %s fail : %s.", path, strerror(errno));
		goto exit;
	}

	// 先读取头部的哈希信息
	if (read(fd, &header, sizeof(hash_header_t)) < 0) {
		hash_error("read header error : %s.", strerror(errno));
		goto close_file;
	}

	slot_cnt = header.slot_cnt;

	// 为0表示获取第一个逻辑节点地址
	if (NULL == (slots = (void*)calloc(slot_cnt, sizeof(slot_info_t)))) {
		hash_error("calloc failed.");
		goto exit;
	}

	if (read(fd, slots, slot_cnt * sizeof(slot_info_t)) < 0) {
		hash_error("read slot_info error : %s.", strerror(errno));
		goto close_file;
	}

	header.slots = slots;

	which_slot %= slot_cnt;
	node_cnt = header.slots[which_slot].node_cnt;

	ret = 0;

close_file:
	close(fd);

exit:
	safe_free(slots);
	return (0 == ret ? node_cnt : ret);

}

bool is_slot_empty(const char* path, uint32_t which_slot) {
	return (0 == get_slot_node_cnt(path, which_slot));
}

#define DEBUG_GET_HEADER 1
// 外部调用时需填充header结构体，包括其中的header.data.value内容
int get_header_data(const char* path, hash_header_data_t* output_header_data) {
	int fd = 0;
	int ret = -1;
	hash_header_t header;
	uint32_t slot_cnt = 0;
	uint32_t header_data_value_size = 0;
	off_t header_data_value_offset = 0;

	memset(&header, 0, sizeof(hash_header_t));

	if ((fd = open(path, O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
		hash_error("open file %s fail : %s.", path, strerror(errno));
		goto exit;
	}

	// 先读取头部的哈希信息
	if (read(fd, &header, sizeof(hash_header_t)) < 0) {
		hash_error("read header error : %s.", strerror(errno));
		goto close_file;
	}

	slot_cnt = header.slot_cnt;
	header_data_value_size = header.header_data_value_size;
	header_data_value_offset = sizeof(hash_header_t) + slot_cnt * sizeof(slot_info_t);

	if (header_data_value_size > 0) {
		if (lseek(fd, header_data_value_offset, SEEK_SET) < 0) {
			hash_error("seek to %ld fail : %s.", header_data_value_offset, strerror(errno));
			goto close_file;
		}

		if (read(fd, output_header_data->value, header_data_value_size) < 0) {
			hash_error("read output_header->value error : %s.", strerror(errno));
			goto close_file;
		}
	}

close_file:
	close(fd);

exit:
	return ret;
}
#undef DEBUG_GET_HEADER

#define DEBUG_SET_HEADER 1
// 外部调用时需填充header结构体，包括其中的header.data.value内容
int set_header_data(const char* path, hash_header_data_t* input_header_data) {
	int fd = 0;
	int ret = -1;
	hash_header_t header;
	uint32_t slot_cnt = 0;
	uint32_t header_data_value_size = 0;
	off_t header_data_value_offset = 0;

	memset(&header, 0, sizeof(hash_header_t));

	if ((fd = open(path, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
		hash_error("open file %s fail : %s.", path, strerror(errno));
		goto exit;
	}

	// 先读取头部的哈希信息
	if (read(fd, &header, sizeof(hash_header_t)) < 0) {
		hash_error("read header error : %s.", strerror(errno));
		goto close_file;
	}

	slot_cnt = header.slot_cnt;
	header_data_value_size = header.header_data_value_size;
	header_data_value_offset = sizeof(hash_header_t) + slot_cnt * sizeof(slot_info_t);

	if (header_data_value_size > 0) {
		if (lseek(fd, header_data_value_offset, SEEK_SET) < 0) {
			hash_error("seek to %ld fail : %s.", header_data_value_offset, strerror(errno));
			goto close_file;
		}

		if (write(fd, input_header_data->value, header_data_value_size) < 0) {
			hash_error("write input_header_data->value error : %s.", strerror(errno));
			goto close_file;
		}
	}

close_file:
	close(fd);

exit:
	return ret;
}
#undef DEBUG_SET_HEADER

#define DEBUG_GET_NODE 0
int get_node(const char* path, uint32_t which_slot, off_t offset, hash_node_t* output_node) {
	int ret = -1;
	int fd = 0;
	hash_header_t header;
	slot_info_t* slots = NULL;
	void* node_data_value = NULL;
	uint32_t slot_cnt = 0;
	uint32_t node_data_value_size = 0;
	void *addr = NULL;	// 防止在memcpy中，文件中保存的上一次指针值覆盖了当前正在运行的指针

	memset(&header, 0, sizeof(hash_header_t));

	if ((fd = open(path, O_RDONLY)) < 0) {
		hash_error("open file %s fail : %s.", path, strerror(errno));
		goto exit;
	}

	// 先读取头部的哈希信息
	if (read(fd, &header, sizeof(hash_header_t)) < 0) {
		hash_error("read header error : %s.", strerror(errno));
		goto close_file;
	}

	slot_cnt = header.slot_cnt;
	node_data_value_size = header.node_data_value_size;

	// 为0表示获取第一个逻辑节点地址
	if (0 == offset) {
		if (NULL == (slots = (void*)calloc(slot_cnt, sizeof(slot_info_t)))) {
			hash_error("calloc failed.");
			goto exit;
		}

		if (read(fd, slots, slot_cnt * sizeof(slot_info_t)) < 0) {
			hash_error("read slot_info error : %s.", strerror(errno));
			goto close_file;
		}

		header.slots = slots;

		which_slot %= slot_cnt;
		offset = header.slots[which_slot].first_logic_node_offset;
	}

	if (lseek(fd, offset, SEEK_SET) < 0) {
		hash_error("seek to %ld fail : %s.", offset, strerror(errno));
		goto close_file;
	}

	addr = output_node->data.value;
	if (read(fd, output_node, sizeof(hash_node_t)) < 0) {
		hash_error("read node failed : %s.", strerror(errno));
		goto close_file;
	}

#if DEBUG_GET_NODE
	hash_debug("0x%lX <- 0x%lX -> 0x%lX.", output_node->offsets.logic_prev, offset, output_node->offsets.logic_next);
#endif

	output_node->data.value = addr;
	if (node_data_value_size > 0
			&& read(fd, output_node->data.value, node_data_value_size) < 0) {
		hash_error("read output_node->data.value failed : %s.", strerror(errno));
		goto close_file;
	}

	ret = 0;

close_file:
	close(fd);

exit:
	safe_free(slots);
	safe_free(node_data_value);
	return ret;
}
#undef DEBUG_GET_NODE

#define DEBUG_ADD_NODE 0
int insert_node(const char* path,
		hash_node_data_t* input_prev_node_data, hash_node_data_t* input_curr_node_data,
		bool (*cb)(hash_node_data_t*, hash_node_data_t*)) {
	int ret = -1;
	int fd = 0;
	bool find_prev_node = false;
	bool is_first_node = false;
	uint32_t which_slot = 0;
	off_t physic_offset = 0;
	off_t first_physic_node_offset = 0;
	off_t first_logic_node_offset = 0;
	off_t prev_logic_node_offset = 0;
	off_t tail_logic_node_offset = 0;
	off_t next_logic_node_offset = 0;
	off_t new_physic_node_offset = 0;
	hash_header_t header;
	slot_info_t* slots = NULL;
	hash_node_t first_physic_node;
	hash_node_t curr_physic_node;
	hash_node_t prev_logic_node;
	hash_node_t next_logic_node;
	void* node_data_value = NULL;
	uint32_t slot_cnt = 0;
	uint32_t header_data_value_size = 0;
	uint32_t node_data_value_size = 0;
	void *addr = NULL;	// 防止在memcpy中，文件中保存的上一次指针值覆盖了当前正在运行的指针

	memset(&header, 0, sizeof(hash_header_t));
	memset(&first_physic_node, 0, sizeof(hash_node_t));
	memset(&curr_physic_node, 0, sizeof(hash_node_t));
	memset(&prev_logic_node, 0, sizeof(hash_node_t));
	memset(&next_logic_node, 0, sizeof(hash_node_t));

	if ((fd = open(path, O_RDWR)) < 0) {
		hash_error("open file %s fail : %s.", path, strerror(errno));
		goto exit;
	}

	// 先读取头部的哈希信息
	if (read(fd, &header, sizeof(hash_header_t)) < 0) {
		hash_error("read header error : %s.", strerror(errno));
		goto close_file;
	}

	slot_cnt = header.slot_cnt;
	header_data_value_size = header.header_data_value_size;
	node_data_value_size = header.node_data_value_size;

	if (NULL == (slots = (void*)calloc(slot_cnt, sizeof(slot_info_t)))) {
		hash_error("calloc failed.");
		goto exit;
	}

	if (read(fd, slots, slot_cnt * sizeof(slot_info_t)) < 0) {
		hash_error("read slot_info error : %s.", strerror(errno));
		goto close_file;
	}

	header.slots = slots;

	which_slot = input_curr_node_data->key % slot_cnt;
	first_physic_node_offset = sizeof(hash_header_t) + slot_cnt * sizeof(slot_info_t) + header_data_value_size\
		 + which_slot * (sizeof(hash_node_t) + node_data_value_size);
	first_logic_node_offset = header.slots[which_slot].first_logic_node_offset;

	if (node_data_value_size > 0
			&& NULL == (node_data_value = (void*)calloc(1, node_data_value_size))) {
		hash_error("calloc failed.");
		goto exit;
	}

	// 读取第一个逻辑节点
	if (lseek(fd, first_logic_node_offset, SEEK_SET) < 0) {
		hash_error("seek to %ld fail : %s.", physic_offset, strerror(errno));
		goto close_file;
	}

	if (read(fd, &prev_logic_node, sizeof(hash_node_t)) < 0) {
		hash_error("read curr_physic_node failed : %s.", strerror(errno));
		goto close_file;
	}

	tail_logic_node_offset = prev_logic_node.offsets.logic_prev;

	physic_offset = first_physic_node_offset;
	do {
		// 先找到上一个节点的位置
		if (lseek(fd, physic_offset, SEEK_SET) < 0) {
			hash_error("seek to %ld fail : %s.", physic_offset, strerror(errno));
			goto close_file;
		}

		if (read(fd, &curr_physic_node, sizeof(hash_node_t)) < 0) {
			hash_error("read curr_physic_node failed : %s.", strerror(errno));
			goto close_file;
		}

		// 未使用的节点直接跳过
		if (0 == curr_physic_node.used) {
			//hash_debug("continue");
			goto next_loop;
		}

		if (node_data_value_size > 0
				&& read(fd, node_data_value, node_data_value_size) < 0) {
			hash_error("read node_data_value error : %s.", strerror(errno));
			goto close_file;
		}

		curr_physic_node.data.value = node_data_value;
		if (true == cb(&(curr_physic_node.data), input_prev_node_data)) {
			find_prev_node = true;
			prev_logic_node = curr_physic_node;
			prev_logic_node_offset = physic_offset;
#if DEBUG_ADD_NODE
			hash_debug("prev node at 0x%lX.", prev_logic_node_offset);
#endif
			break;
		}

next_loop:
		physic_offset = curr_physic_node.offsets.physic_next;
	} while (physic_offset != first_physic_node_offset);

	if (false == find_prev_node) {
		// 链表中有节点，但是没找到前驱节点，将curr插到尾部
		if (header.slots[which_slot].node_cnt > 0) {
			prev_logic_node_offset = tail_logic_node_offset;
			hash_warn("didn't find prev node, node cnt is %d, add curr to tail (0x%lX).", header.slots[which_slot].node_cnt, prev_logic_node_offset);
		}

		// 链表为空，当作第一个节点插入
		else {
			hash_warn("didn't find prev, no node in this slot, treat curr as first node.");
			is_first_node = true;
		}
	}

	do {
		/* START 拿一个节点数据，取完后文件指针不要挪动 */
		if (lseek(fd, physic_offset, SEEK_SET) < 0) {
			hash_error("seek to %ld fail : %s.", physic_offset, strerror(errno));
			goto close_file;
		}

		if (read(fd, &curr_physic_node, sizeof(hash_node_t)) < 0) {
			hash_error("read curr_physic_node failed : %s.", strerror(errno));
			goto close_file;
		}

		if (node_data_value_size > 0
				&& read(fd, node_data_value, node_data_value_size) < 0) {
			hash_error("read node_data_value failed : %s.", strerror(errno));
			goto close_file;
		}

		// 建立关联，方便后面使用。之后不要破坏这种关联（比如read调用）
		curr_physic_node.data.value = node_data_value;

		if (lseek(fd, physic_offset, SEEK_SET) < 0) {
			hash_error("seek back to %ld fail : %s.", physic_offset, strerror(errno));
			goto close_file;
		}
		/* END 拿一个节点数据，取完后文件指针不要挪动 */

		/*
		 * used  next_offset  desc
		 *  0         0        首次使用第一个节点
		 *  0         1        被清空过的节点
		 *  1         0        已被使用的最后一个节点
		 */

		if (0 == curr_physic_node.used || first_physic_node_offset == curr_physic_node.offsets.physic_next) {
			// 0 0, 首次使用第一个节点
			if (0 == curr_physic_node.used
					&& first_physic_node_offset == curr_physic_node.offsets.physic_next
					&& first_physic_node_offset == curr_physic_node.offsets.physic_prev) {
#if DEBUG_ADD_NODE
				hash_debug("(FIRST) <0x%lX> (0x%lX : %d) <0x%lX>",
						curr_physic_node.offsets.physic_prev, physic_offset, input_curr_node_data->key, curr_physic_node.offsets.physic_next);
#endif
				curr_physic_node.offsets.physic_prev = curr_physic_node.offsets.physic_next = first_physic_node_offset;
				new_physic_node_offset = physic_offset;
			}

			// 0 1, 被清空过的节点
			else if (0 == curr_physic_node.used) {
#if DEBUG_ADD_NODE
				hash_debug(" (USED) <0x%lX> (0x%lX : %d) <0x%lX>",
						curr_physic_node.offsets.physic_prev, physic_offset, input_curr_node_data->key, curr_physic_node.offsets.physic_next);
#endif
				new_physic_node_offset = physic_offset;
			}

			// 1 0, 正在使用的最后一个节点
			else if (1 == curr_physic_node.used && first_physic_node_offset == curr_physic_node.offsets.physic_next) {
				// 新节点在文件末尾插入，获取新节点偏移量
				if ((new_physic_node_offset = lseek(fd, 0, SEEK_END)) < 0) {
					hash_error("prepare new curr_physic_node, seek to %ld fail : %s.",
							physic_offset, strerror(errno));
					goto close_file;
				}

				/**** 1. START 修改 当前 节点的next_offset值，指向新节点 ****/
				lseek(fd, physic_offset, SEEK_SET);

				curr_physic_node.offsets.physic_next = new_physic_node_offset;

				if (write(fd, &curr_physic_node, sizeof(hash_node_t)) < 0) {
					hash_error("write curr_physic_node error : %s.", strerror(errno));
					goto close_file;
				}
				/**** 1. END 修改 当前 节点的next_offset值，指向新节点 ****/

				/**** 2. START 修改 头 节点的prev_offset值，指向新节点 ****/
				lseek(fd, first_physic_node_offset, SEEK_SET);


				if (read(fd, &first_physic_node, sizeof(hash_node_t)) < 0) {
					hash_error("read first_physic_node error : %s.", strerror(errno));
					goto close_file;
				}

				first_physic_node.offsets.physic_prev = new_physic_node_offset;

				// 再次回到节点开头写回
				lseek(fd, first_physic_node_offset, SEEK_SET);

				if (write(fd, &first_physic_node, sizeof(hash_node_t)) < 0) {
					hash_error("write first_physic_node error : %s.", strerror(errno));
					goto close_file;
				}
				/**** 2. END 修改 头 节点的prev_offset值，指向新节点 ****/

				/**** 3. START 修改 新 节点的prev和next指针 ****/
				lseek(fd, 0, SEEK_END);		// 新节点在尾部

				curr_physic_node.offsets.physic_prev = physic_offset;
				curr_physic_node.offsets.physic_next = first_physic_node_offset;
				/**** 3. END 修改 新 节点的prev和next指针 ****/

#if DEBUG_ADD_NODE
				hash_debug(" (TAIL) <0x%lX> (0x%lX : %d) <0x%lX>",
						curr_physic_node.offsets.physic_prev, new_physic_node_offset, input_curr_node_data->key, curr_physic_node.offsets.physic_next);
#endif
			}

			/**** 4. START 写入新节点的其他信息 ****/
			++header.slots[which_slot].node_cnt;

			curr_physic_node.used = 1;

			addr = curr_physic_node.data.value;
			memcpy(&(curr_physic_node.data), input_curr_node_data, sizeof(hash_node_data_t));

			curr_physic_node.data.value = addr;
			memcpy(curr_physic_node.data.value, input_curr_node_data->value, node_data_value_size);

			/* START 调整逻辑链表。上面已完成调整物理链表 */
			// 第一个节点。
			if (true == is_first_node) {
				header.slots[which_slot].first_logic_node_offset = physic_offset;
				curr_physic_node.offsets.logic_prev = curr_physic_node.offsets.logic_next = new_physic_node_offset;
#if DEBUG_ADD_NODE
				hash_debug("first node offset 0x%lX.", new_physic_node_offset);
#endif
			} else {
				/*
				 * 双向链表插入，curr为待插入节点
				 * nextNode->prev = curr;
				 * prevNode->next = curr;
				 * currNode->next = nextNode;
				 * currNode->prev = prevNode;
				 */

				/* START 4.1. 读取 next prev 节点操作 */
#if DEBUG_ADD_NODE
				hash_debug("new node 0x%lX inset behind 0x%lX, before add 0x%lX <- 0x%lX -> 0x%lX.",
						new_physic_node_offset, prev_logic_node_offset,
						prev_logic_node.offsets.logic_prev, prev_logic_node_offset, prev_logic_node.offsets.logic_next);
#endif

				// prev 节点
				if (lseek(fd, prev_logic_node_offset, SEEK_SET) < 0) {
					hash_error("seek to %ld fail : %s.", physic_offset, strerror(errno));
					goto close_file;
				}

				if (read(fd, &prev_logic_node, sizeof(hash_node_t)) < 0) {
					hash_error("read next_logic_node error : %s.", strerror(errno));
					goto close_file;
				}

				// next 节点。如果prev和next相等，说明当前只有一个节点，后面会有多个这种判断
				if (prev_logic_node_offset != (next_logic_node_offset = prev_logic_node.offsets.logic_next)) {
					if (lseek(fd, next_logic_node_offset, SEEK_SET) < 0) {
						hash_error("seek to %ld fail : %s.", next_logic_node_offset, strerror(errno));
						goto close_file;
					}

					if (read(fd, &next_logic_node, sizeof(hash_node_t)) < 0) {
						hash_error("read next_logic_node error : %s.", strerror(errno));
						goto close_file;
					}
				}
				/* END 4.1. 读取 next prev 节点操作 */

				/* START 4.2. 重新建立节点链接 */
				prev_logic_node.offsets.logic_next = new_physic_node_offset;

				if (prev_logic_node_offset != next_logic_node_offset) {
					next_logic_node.offsets.logic_prev = new_physic_node_offset;
				} else {		// 在只有一个节点的情况下插入
					prev_logic_node.offsets.logic_next = new_physic_node_offset;
					prev_logic_node.offsets.logic_prev = new_physic_node_offset;
				}

				curr_physic_node.offsets.logic_next = next_logic_node_offset;
				curr_physic_node.offsets.logic_prev = prev_logic_node_offset;
				/* END 4.2. 重新建立节点链接 */

#if DEBUG_ADD_NODE
				hash_debug("prevNode : 0x%lX <- (0x%lX) -> 0x%lX",
						prev_logic_node.offsets.logic_prev, prev_logic_node_offset, prev_logic_node.offsets.logic_next);

				hash_debug("currNode : 0x%lX <- (0x%lX) -> 0x%lX",
						curr_physic_node.offsets.logic_prev, new_physic_node_offset, curr_physic_node.offsets.logic_next);

				// 超过一个节点的情况下插入
				if (prev_logic_node_offset != next_logic_node_offset) {
					hash_debug("nextNode : 0x%lX <- (0x%lX) -> 0x%lX",
							next_logic_node.offsets.logic_prev, next_logic_node_offset, next_logic_node.offsets.logic_next);
				}
#endif
				/* START 4.3. 写回到文件 */
				if (lseek(fd, prev_logic_node_offset, SEEK_SET) < 0) {
					hash_error("seek to %ld fail : %s.", prev_logic_node_offset, strerror(errno));
					goto close_file;
				}

				if (write(fd, &prev_logic_node, sizeof(hash_node_t)) < 0) {
					hash_error("write prev_logic_node error : %s.", strerror(errno));
					goto close_file;
				}

				if (prev_logic_node_offset != next_logic_node_offset) {
					if (lseek(fd, next_logic_node_offset, SEEK_SET) < 0) {
						hash_error("seek to %ld fail : %s.", next_logic_node_offset, strerror(errno));
						goto close_file;
					}

					if (write(fd, &next_logic_node, sizeof(hash_node_t)) < 0) {
						hash_error("write next_logic_node error : %s.", strerror(errno));
						goto close_file;
					}
				}
				/* END 4.3. 写回到文件 */
			}

			/* END 完成调整逻辑链表 */

			if (lseek(fd, new_physic_node_offset, SEEK_SET) < 0) {
				hash_error("seek to %ld fail : %s.", new_physic_node_offset, strerror(errno));
				goto close_file;
			}

			if (write(fd, &curr_physic_node, sizeof(hash_node_t)) < 0) {
				hash_error("write curr_physic_node error : %s.", strerror(errno));
				goto close_file;
			}

			if (node_data_value_size > 0
					&& write(fd, curr_physic_node.data.value, node_data_value_size) < 0) {
				hash_error("write curr_physic_node.data.value error : %s.", strerror(errno));
				goto close_file;
			}
			/**** 4. END 写入新节点的其他信息 ****/
			break;
		} else {
			physic_offset = curr_physic_node.offsets.physic_next;
		}
	}  while (physic_offset != first_physic_node_offset);

	/* START 保存头部信息 */
	if (lseek(fd, sizeof(hash_header_t), SEEK_SET) < 0) {
		hash_error("seek to %ld fail : %s.", sizeof(hash_header_t), strerror(errno));
		goto close_file;
	}

	if (write(fd, header.slots, slot_cnt * sizeof(slot_info_t)) < 0) {
		hash_error("write header.slots error : %s.", strerror(errno));
		goto close_file;
	}
	/* END 保存头部信息 */

	ret = 0;

close_file:
	close(fd);

exit:
	safe_free(header.slots);
	safe_free(node_data_value);
	return ret;
}
#undef DEBUG_ADD_NODE

#define DEBUG_DEL_NODE 0
int _del_node_hepler(int fd, off_t curr_node_offset, uint32_t which_slot, hash_node_t *node, hash_header_t *header) {
	int ret = -1;
	uint32_t slot_cnt = 0;
	uint32_t node_data_value_size = 0;
	hash_node_t prev_logic_node;
	hash_node_t next_logic_node;
	off_t first_logic_node_offset = 0;
	off_t prev_logic_node_offset = 0;
	off_t next_logic_node_offset = 0;
	void *addr = NULL;	// 防止在memcpy中，文件中保存的上一次指针值覆盖了当前正在运行的指针

	slot_cnt = header->slot_cnt;
	node_data_value_size = header->node_data_value_size;
	first_logic_node_offset = header->slots[which_slot].first_logic_node_offset;

	node->used = 0;
	--header->slots[which_slot].node_cnt;

	/*
	 * 双向链表删除，curr为待插入节点
	 * nextNode->prev = prevNode;
	 * prevNode->next = nextNode;
	 */
	/* START 调整逻辑链表 */
	/* START 1. 读取 prev next 节点信息*/
	// prev 节点
	prev_logic_node_offset = node->offsets.logic_prev;
	if (lseek(fd, prev_logic_node_offset, SEEK_SET) < 0) {
		hash_error("seek to %ld fail : %s.", prev_logic_node_offset, strerror(errno));
		goto exit;
	}

	if (read(fd, &prev_logic_node, sizeof(hash_node_t)) < 0) {
		hash_error("read prev_logic_node error : %s.", strerror(errno));
		goto exit;
	}

	// next 节点。如果prev和next相等，说明当前只有一个节点，后面会有多个这种判断
	next_logic_node_offset = node->offsets.logic_next;
	if (lseek(fd, next_logic_node_offset, SEEK_SET) < 0) {
		hash_error("seek to %ld fail : %s.", next_logic_node_offset, strerror(errno));
		goto exit;
	}

	if (read(fd, &next_logic_node, sizeof(hash_node_t)) < 0) {
		hash_error("read next_logic_node error : %s.", strerror(errno));
		goto exit;
	}
	/* END 1. 读取 prev next 节点信息*/
	/* START 2. 修改节点链式关系 */

	// 仅剩 一个 节点
	if (curr_node_offset == prev_logic_node_offset && curr_node_offset == next_logic_node_offset) {
#if DEBUG_DEL_NODE
		hash_debug("only 1 node 0x%lX left.", curr_node_offset);
#endif
		header->slots[which_slot].first_logic_node_offset = node->offsets.logic_next;
		goto clear_node;
	} else {
		if (curr_node_offset == first_logic_node_offset) {	// 删除逻辑第一个节点
#if DEBUG_DEL_NODE
			hash_debug("delete first logic node 0x%lX, update first logic node to 0x%lX.",
					curr_node_offset, node->offsets.logic_next);
#endif
			header->slots[which_slot].first_logic_node_offset = node->offsets.logic_next;
		} else {
#if DEBUG_DEL_NODE
			hash_debug("delete normal node 0x%lX.", curr_node_offset);
#endif
		}

		// 剩两个节点
		if (prev_logic_node_offset == next_logic_node_offset) {
#if DEBUG_DEL_NODE
			hash_debug("2 nodes left.");
#endif
			prev_logic_node.offsets.logic_prev = prev_logic_node_offset;
			prev_logic_node.offsets.logic_next = prev_logic_node_offset;
		}
		
		// 更多节点
		else {
			next_logic_node.offsets.logic_prev = prev_logic_node_offset;
			prev_logic_node.offsets.logic_next = next_logic_node_offset;
		}
	}

	/* END 2. 修改节点链式关系 */
#if DEBUG_DEL_NODE
	// 节点等于2时，只需看prev即可
	hash_info("after del 0x%lX, prev = ( 0x%lX <- 0x%lX -> 0x%lX ), next = ( 0x%lX <- 0x%lX -> 0x%lX ).", curr_node_offset,
			prev_logic_node.offsets.logic_prev, prev_logic_node_offset, prev_logic_node.offsets.logic_next,
			next_logic_node.offsets.logic_prev, next_logic_node_offset, next_logic_node.offsets.logic_next);
#endif
	/* START 3. 写回到文件 */
	if (lseek(fd, prev_logic_node_offset, SEEK_SET) < 0) {
		hash_error("seek to %ld fail : %s.", prev_logic_node_offset, strerror(errno));
		goto exit;
	}

	if (write(fd, &prev_logic_node, sizeof(hash_node_t)) < 0) {
		hash_error("write prev_logic_node error : %s.", strerror(errno));
		goto exit;
	}

	// 剩余节点大于 2 时
	if (prev_logic_node_offset != next_logic_node_offset) {
		if (lseek(fd, next_logic_node_offset, SEEK_SET) < 0) {
			hash_error("seek to %ld fail : %s.", next_logic_node_offset, strerror(errno));
			goto exit;
		}

		if (write(fd, &next_logic_node, sizeof(hash_node_t)) < 0) {
			hash_error("write next_logic_node error : %s.", strerror(errno));
			goto exit;
		}
	}
	/* END 3. 写回到文件 */
	/* END 完成调整逻辑链表 */

clear_node:
	/* START 清空当前节点 */
	// 移到节点起始位置
	if (lseek(fd, curr_node_offset, SEEK_SET) < 0) {
		hash_error("seek back to %ld fail : %s.", curr_node_offset, strerror(errno));
		goto exit;
	}

	addr = node->data.value;
	node->used = 0;
	memset(&(node->data), 0, sizeof(hash_node_data_t));
	if (write(fd, node, sizeof(hash_node_t)) < 0) {
		hash_error("del node error : %s.", strerror(errno));
		goto exit;
	}

	node->data.value = addr;
	memset(node->data.value, 0, node_data_value_size);
	if (node_data_value_size > 0
			&& write(fd, node->data.value, node_data_value_size) < 0) {
		hash_error("del node.data.value error : %s.", strerror(errno));
		goto exit;
	}
	/* END 清空当前节点 */

	/* START 保存头部信息 */
	if (lseek(fd, sizeof(hash_header_t), SEEK_SET) < 0) {
		hash_error("seek to %ld fail : %s.", sizeof(hash_header_t), strerror(errno));
		goto exit;
	}

	if (write(fd, header->slots, slot_cnt * sizeof(slot_info_t)) < 0) {
		hash_error("write header.slots error : %s.", strerror(errno));
		goto exit;
	}
	/* END 保存头部信息 */

	ret = 0;

exit:
	return ret;
}
#undef DEBUG_DEL_NODE

int del_node(const char* path, hash_node_data_t* input_node_data,
		bool (*cb)(hash_node_data_t*, hash_node_data_t*)) {
	int ret = -1;
	int fd = 0;
	uint32_t which_slot = 0;
	off_t offset = 0;
	off_t first_logic_node_offset = 0;
	hash_header_t header;
	slot_info_t* slots = NULL;
	hash_node_t node;
	void* node_data_value = NULL;
	uint32_t slot_cnt = 0;
	uint32_t node_data_value_size = 0;

	memset(&header, 0, sizeof(hash_header_t));
	memset(&node, 0, sizeof(hash_node_t));

	if ((fd = open(path, O_RDWR)) < 0) {
		hash_error("open file %s fail : %s.", path, strerror(errno));
		goto exit;
	}

	// 先读取头部的哈希信息
	if (read(fd, &header, sizeof(hash_header_t)) < 0) {
		hash_error("read header error : %s.", strerror(errno));
		goto close_file;
	}

	slot_cnt = header.slot_cnt;
	node_data_value_size = header.node_data_value_size;

	if (NULL == (slots = (void*)calloc(slot_cnt, sizeof(slot_info_t)))) {
		hash_error("calloc failed.");
		goto exit;
	}

	if (read(fd, slots, slot_cnt * sizeof(slot_info_t)) < 0) {
		hash_error("read slot_info error : %s.", strerror(errno));
		goto close_file;
	}

	header.slots = slots;

	which_slot = input_node_data->key % slot_cnt;
	first_logic_node_offset = header.slots[which_slot].first_logic_node_offset;

	if (node_data_value_size > 0
			&& NULL == (node_data_value = (void*)calloc(1, node_data_value_size))) {
		hash_error("calloc failed.");
		goto exit;
	}

	offset = first_logic_node_offset;
	do {
		if (lseek(fd, offset, SEEK_SET) < 0) {
			hash_error("seek to %ld fail : %s.", offset, strerror(errno));
			goto close_file;
		}

		if (read(fd, &node, sizeof(hash_node_t)) < 0) {
			hash_error("read node failed : %s.", strerror(errno));
			goto close_file;
		}

		if (node_data_value_size > 0
				&& read(fd, node_data_value, node_data_value_size) < 0) {
			hash_error("read node_data_value failed : %s.", strerror(errno));
			goto close_file;
		}

		// 建立关联，方便后面使用。之后不要破坏这种关联（比如read调用）
		node.data.value = node_data_value;

		// 找到了节点
		if (true == cb(&(node.data), input_node_data)) {
			if ((ret = _del_node_hepler(fd, offset, which_slot, &node, &header)) < 0) {
				goto close_file;
			}
			break;
		}

		offset = node.offsets.logic_next;
	} while (offset != first_logic_node_offset);

close_file:
	close(fd);

exit:
	safe_free(slots);
	safe_free(node_data_value);
	return ret;
}

// which_slot小于slot_cnt则遍历指定哈希槽，如果大于slot_cnt则遍历所有哈希槽
uint8_t traverse_nodes(const char* list_path, traverse_by_what_t by_what,
		uint32_t which_slot, printable_t printable, void* input_arg,
		traverse_action_t (*cb)(hash_node_data_t* file_node_data, void* input_arg)) {
	traverse_action_t action = TRAVERSE_ACTION_DO_NOTHING;
	uint8_t i = 0;
	int fd = 0;
	off_t offset = 0;
	off_t first_node_offset = 0;
	off_t first_physic_node_offset = 0;
	off_t first_logic_node_offset = 0;
	off_t prev_offset = 0;
	off_t next_offset = 0;
	hash_header_t header;
	slot_info_t* slots = NULL;
	hash_node_t node;
	void* node_data_value = NULL;
	uint32_t slot_cnt = 0;
	uint32_t header_data_value_size = 0;
	uint32_t node_data_value_size = 0;
	uint8_t break_or_not = 0;
	static uint8_t s_first_node = 1;

	memset(&header, 0, sizeof(hash_header_t));
	memset(&node, 0, sizeof(hash_node_t));

	if ((fd = open(list_path, O_RDWR)) < 0) {
		hash_error("open %s fail : %s.", list_path, strerror(errno));
		goto exit;
	}

	// 先读取头部的哈希信息
	if (read(fd, &header, sizeof(hash_header_t)) < 0) {
		hash_error("read header error : %s.", strerror(errno));
		goto close_file;
	}

	slot_cnt = header.slot_cnt;
	header_data_value_size = header.header_data_value_size;
	node_data_value_size = header.node_data_value_size;

	if (NULL == (slots = (void*)calloc(slot_cnt, sizeof(slot_info_t)))) {
		hash_error("calloc failed.");
		goto exit;
	}

	if (read(fd, slots, slot_cnt * sizeof(slot_info_t)) < 0) {
		hash_error("read slot_info error : %s.", strerror(errno));
		goto close_file;
	}

	header.slots = slots;

	if (node_data_value_size > 0
			&& NULL == (node_data_value = (void*)calloc(1, node_data_value_size))) {
		hash_error("calloc failed.");
		goto exit;
	}

	for (i = 0; i < slot_cnt; i++) {
		s_first_node = 1;

		// TODO: hash_key和i的关系不一定可以直接比较，后续版本需要完善
		if (which_slot < slot_cnt && i != (which_slot % slot_cnt)) {
			continue;
		}

		first_physic_node_offset = sizeof(hash_header_t) + slot_cnt * sizeof(slot_info_t) + header_data_value_size\
			 + i * (sizeof(hash_node_t) + node_data_value_size);
		first_logic_node_offset = header.slots[i].first_logic_node_offset;

		first_node_offset = TRAVERSE_BY_LOGIC == by_what ? first_logic_node_offset : first_physic_node_offset;

		if (WITH_PRINT == printable) { printf("[%d] (%d) %s  ", i, header.slots[i].node_cnt,
				TRAVERSE_BY_LOGIC == by_what ? " \e[7;32mLOGIC\e[0m" : "\e[7;34mPHYSIC\e[0m"); }

		offset = first_node_offset;
		do {
			if (lseek(fd, offset, SEEK_SET) < 0) {
				hash_error("seek to %ld fail : %s.", offset, strerror(errno));
				goto close_file;
			}

			if (read(fd, &node, sizeof(hash_node_t)) < 0) {
				hash_error("read node failed : %s.", strerror(errno));
				goto close_file;
			}

			if (node_data_value_size > 0
					&& read(fd, node_data_value, node_data_value_size) < 0) {
				hash_error("read node_data_value failed : %s.", strerror(errno));
				goto close_file;
			}

			node.data.value = node_data_value;

			first_logic_node_offset = header.slots[i].first_logic_node_offset;

			// 遍历过程中的删除操作有可能会改变第 一个 逻辑节点的位置
			if (TRAVERSE_BY_LOGIC == by_what) { first_node_offset = first_logic_node_offset; }

			prev_offset = TRAVERSE_BY_LOGIC == by_what ? node.offsets.logic_prev : node.offsets.physic_prev;
			next_offset = TRAVERSE_BY_LOGIC == by_what ? node.offsets.logic_next : node.offsets.physic_next;

			if (s_first_node) {
				s_first_node = 0;
			} else {
				if (WITH_PRINT == printable) { printf(" --- "); }
			}

			if (WITH_PRINT == printable) { printf("<0x%lX> ( 0x%lX : ", prev_offset, offset); }

			if (0 == node.used) {
				if (WITH_PRINT == printable) { printf("* ) <0x%lX>", next_offset); }
				goto next_loop;
			}

			action = cb(&(node.data), input_arg);

			if (WITH_PRINT == printable) { printf(" ) <0x%lX>", next_offset); }

			if (TRAVERSE_ACTION_UPDATE & action) {
				// 跳回到节点头部
				if (lseek(fd, offset, SEEK_SET) < 0) {
					hash_error("seek to %ld fail : %s.", offset, strerror(errno));
					goto close_file;
				}

				if (write(fd, &node, sizeof(hash_node_t)) < 0) {
					hash_error("write node error : %s.", strerror(errno));
					goto close_file;
				}

				if (node_data_value_size > 0
						&& write(fd, node.data.value, node_data_value_size) < 0) {
					hash_error("write node.data.value error : %s.", strerror(errno));
					goto close_file;
				}
			}

			if (TRAVERSE_ACTION_DELETE & action) {
			// 跳回到节点头部
				if (lseek(fd, offset, SEEK_SET) < 0) {
					hash_error("seek to %ld fail : %s.", offset, strerror(errno));
					goto close_file;
				}
				_del_node_hepler(fd, offset, i, &node, &header);
			}

			if (TRAVERSE_ACTION_BREAK & action) {
				break_or_not = 1;
				goto close_file;
			}

next_loop:
			offset = next_offset;
		} while (offset != first_node_offset);

		if (WITH_PRINT == printable) { printf("\n"); }
	}

close_file:
	close(fd);

exit:
	safe_free(slots);
	safe_free(node_data_value);
	return break_or_not;
}

int init_hash_engine(const char* path, init_method_t rebuild,
		int slot_cnt, int node_data_value_size, int header_data_value_size) {
	int ret = -1;
	int fd = 0;
	uint32_t i = 0;
	uint8_t file_exist = 0;
	hash_header_t header;
	slot_info_t* slots = NULL;
	void* header_data_value = NULL;
	hash_node_t node;
	void* node_data_value = NULL;
	off_t offset = 0;

	hash_info("path = %s, rebuild = %d, "
			"slot_cnt = %d, node_data_value_size = %d, header_data_value_size = %d.",
			path, rebuild, slot_cnt, node_data_value_size, header_data_value_size);

	memset(&header, 0, sizeof(hash_header_t));
	memset(&node, 0, sizeof(hash_node_t));

	if (access(path, F_OK) < 0) {
		hash_debug("%s not exist.", path);
		file_exist = 0;
	} else {
		file_exist = 1;
	}

	if (1 == file_exist && 1 == rebuild) {
		if (unlink(path) < 0) {
			hash_error("delete '%s' error : %s.", path, strerror(errno));
			goto exit;
		} else {
			hash_info("delete old hash file '%s' success.", path);
			file_exist = 0;
		}
	}

	if (0 == file_exist) {
		if ((fd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
			hash_error("create file %s fail : %s.", path, strerror(errno));
			goto exit;
		}

		// 先写入头部信息
		if (NULL == (slots = (void*)calloc(slot_cnt, sizeof(slot_info_t)))) {
			hash_error("calloc failed.");
			goto exit;
		}

		if (header_data_value_size > 0
				&& NULL == (header_data_value = (void*)calloc(1, header_data_value_size))) {
			hash_error("calloc failed.");
			goto exit;
		}

		header.slot_cnt = slot_cnt;
		header.header_data_value_size = header_data_value_size;
		header.node_data_value_size = node_data_value_size;
		header.slots = slots;
		header.data.value = header_data_value;

		if (node_data_value_size > 0
				&& NULL == (node_data_value = (void*)calloc(1, node_data_value_size))) {
			hash_error("calloc failed.");
			goto close_file;
		}

		node.data.value = node_data_value;

		for (i = 0; i < slot_cnt; i++) {
			offset = sizeof(hash_header_t) + slot_cnt * sizeof(slot_info_t) + header_data_value_size\
				 + i * (sizeof(hash_node_t) + node_data_value_size);
			node.offsets.physic_prev = node.offsets.physic_next = offset;
			node.offsets.logic_prev = node.offsets.logic_next = offset;

			header.slots[i].first_logic_node_offset = offset;

			if (lseek(fd, offset, SEEK_SET) < 0) {
				hash_error("seek to %ld fail : %s.", offset, strerror(errno));
				goto close_file;
			}

			if (write(fd, &node, sizeof(hash_node_t)) < 0) {
				hash_error("init node error : %s.", strerror(errno));
				goto close_file;
			}

			if (node_data_value_size > 0
					&& write(fd, node.data.value, node_data_value_size) < 0) {
				hash_error("init node.data.value error : %s.", strerror(errno));
				goto close_file;
			}
		}

		if (lseek(fd, 0, SEEK_SET) < 0) {
			hash_error("seek to head fail : %s.", strerror(errno));
			goto close_file;
		}

		if (write(fd, &header, sizeof(hash_header_t)) < 0) {
			hash_error("write header error : %s.", strerror(errno));
			goto close_file;
		}

		if (write(fd, header.slots, slot_cnt * sizeof(slot_info_t)) < 0) {
			hash_error("write header.slots error : %s.", strerror(errno));
			goto close_file;
		}

		if (header_data_value_size > 0
				&& write(fd, header.data.value, header_data_value_size) < 0) {
			hash_error("write header.data.value error : %s.", strerror(errno));
			goto close_file;
		}
	}

	ret = 0;

close_file:
	close(fd);

exit:
	safe_free(slots);
	safe_free(header_data_value);
	safe_free(node_data_value);
	return ret;
}
