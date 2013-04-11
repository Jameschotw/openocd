/*
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *   Maintainer: frank.dols@synopsys.com
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "arc.h"



/* ----- Supporting functions ---------------------------------------------- */

static int arc_mem_read_block(struct arc_jtag *jtag_info, uint32_t addr,
	int size, int count, void *buf)
{
	int retval = ERROR_OK;
	int i;

	printf(" >> Entering: %s(%s @ln:%d)\n",__func__,__FILE__,__LINE__);
	LOG_DEBUG(">> Entering <<");

	assert(size <= 4);

	if (size <= 4) { /* means 4 * 2 bytes */
		for(i = 0; i < count; i++) {
			retval = arc_jtag_read_memory(jtag_info, addr + (i * 4),
				buf + (i * 4));
#ifdef DEBUG
			uint32_t buffer;
			retval = arc_jtag_read_memory(jtag_info, addr + (i * 4), &buffer);
			printf(" > value (size:%d): 0x%x @: 0x%x\n", size, buffer, addr + (i * 4));
#endif
		}
	} else
		retval = ERROR_FAIL;

	return retval;
}

static int arc_mem_write_block(struct arc_jtag *jtag_info, uint32_t addr,
	int size, int count, void *buf)
{
	int retval = ERROR_OK;
	int i;

	printf(" >> Entering: %s(%s @ln:%d)\n",__func__,__FILE__,__LINE__);
	LOG_DEBUG(">> Entering <<");

	assert(size <= 4);

	if (size == 4) {
		for(i = 0; i < count; i++) {
			LOG_DEBUG(" >> write: 0x%x @ 0x%x", *(uint32_t *)(buf + (i * size)),
				addr + (i * size));
			retval = arc_jtag_write_memory(jtag_info, (addr + (i * size)),
				(buf + (i * size)));
#ifdef DEBUG
			uint32_t buffer;
			retval = arc_jtag_read_memory(jtag_info, addr + (i * size), &buffer);
			printf(" >         0x%x @: 0x%x\n", buffer, addr + (i * 4));
#endif
		}
	} else {
		/*
		 * we have to handle the sw-breakpoint here.
		 * size 2  = half word
		 */
		uint32_t buffer;

		retval = arc_jtag_read_memory(jtag_info, addr, &buffer);
		printf(" > read:  0x%x @: 0x%x\n", buffer, addr);
		printf("   need to write(16): 0x%x\n", *(uint16_t *)buf);
		//printf("   need to write(32): 0x%x\n", *(uint32_t *)buf);

		memcpy(&buffer, buf, sizeof(uint16_t));
		printf(" >> have to write: 0x%x\n",buffer);
		retval = arc_jtag_write_memory(jtag_info, addr, &buffer);

		arc_jtag_read_memory(jtag_info, addr, &buffer);
		printf(" > read:  0x%x @: 0x%x\n", buffer, addr);
	}

	return retval;
}




/* ----- Exported functions ------------------------------------------------ */

int arc_mem_read(struct target *target, uint32_t address, uint32_t size,
	uint32_t count, uint8_t *buffer)
{
	int retval = ERROR_OK;
	struct arc32_common *arc32 = target_to_arc32(target);
//	struct arc_jtag *jtag_info = &arc32->jtag_info;

	printf(" >> Entering: %s(%s @ln:%d)\n",__func__,__FILE__,__LINE__);
	LOG_DEBUG(">> Entering <<");

#ifdef DEBUG
	printf("address: 0x%8.8" PRIx32 ", size: 0x%8.8" PRIx32 \
		", count: 0x%8.8" PRIx32 "\n", address, size, count);
#endif
	LOG_DEBUG("address: 0x%8.8" PRIx32 ", size: 0x%8.8" PRIx32 \
		", count: 0x%8.8" PRIx32 "", address, size, count);

	if (target->state != TARGET_HALTED) {
		LOG_WARNING("target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	/* sanitize arguments */

	if (((size != 4) && (size != 2) && (size != 1)) || (count == 0) || !(buffer))
		return ERROR_COMMAND_SYNTAX_ERROR;

	if (((size == 4) && (address & 0x3u)) || ((size == 2) && (address & 0x1u)))
		return ERROR_TARGET_UNALIGNED_ACCESS;

	/* since we don't know if buffer is aligned, we allocate new mem that
	 * is always aligned.
	 */
	void *tunnel = NULL;

	if (size > 1) {
		tunnel = malloc(count * size * sizeof(uint8_t));
		if (tunnel == NULL) {
			LOG_ERROR("Out of memory");
			return ERROR_FAIL;
		}
	} else
		tunnel = buffer;

	/* if noDMA off, use DMAACC mode for memory read */

//	if (jtag_info->impcode & JTAG_IMP_NODMA)
		retval = arc_mem_read_block(&arc32->jtag_info, address, size,
			count, tunnel);
//	else
//		retval = arc32_dmaacc_read_mem(jtag_info, address, size,
//			count, tunnel);

	/* arc32_..._read_mem with size 4/2 returns uint32_t/uint16_t in host */
	/* endianness, but byte array should represent target endianness       */
	if (ERROR_OK == retval) {
		switch (size) {
		case 4:
			target_buffer_set_u32_array(target, buffer, count, tunnel);
#ifdef DEBUG
			int i;
			for(i = 0; i < count; i++) {
				/* print byte position content of complete word */
				printf("    **> 0x%02x",buffer[3 + (4 * i)]);
				printf("%02x",buffer[2 + (4 * i)]);
				printf("%02x",buffer[1 + (4 * i)]);
				printf("%02x\n",buffer[0 + (4 * i)]);
			}
#endif
			break;
		case 2:
			target_buffer_set_u16_array(target, buffer, count, tunnel);
			break;
		}
	}

	if ((size > 1) && (tunnel != NULL))
		free(tunnel);

	return retval;
}

int arc_mem_write(struct target *target, uint32_t address, uint32_t size,
	uint32_t count, const uint8_t *buffer)
{
	int retval = ERROR_OK;
	struct arc32_common *arc32 = target_to_arc32(target);
	struct arc_jtag *jtag_info = &arc32->jtag_info;

	printf(" >> Entering: %s(%s @ln:%d)\n",__func__,__FILE__,__LINE__);
	LOG_DEBUG(">> Entering <<");

	printf("start writing @ address: 0x%8.8" PRIx32 " : %d times %d bytes\n",
			address, count, size);
	LOG_DEBUG("address: 0x%8.8" PRIx32 ", size: 0x%8.8" PRIx32 ", count: 0x%8.8" PRIx32 "",
			address, size, count);

	if (target->state != TARGET_HALTED) {
		LOG_WARNING("target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	/* sanitize arguments */

	if (((size != 4) && (size != 2) && (size != 1)) || (count == 0) || !(buffer))
		return ERROR_COMMAND_SYNTAX_ERROR;

	if (((size == 4) && (address & 0x3u)) || ((size == 2) && (address & 0x1u)))
		return ERROR_TARGET_UNALIGNED_ACCESS;

	/* correct endianess if we have word or hword access */
	void *tunnel = NULL;

	if (size > 1) {
		/*
		 * arc32_..._write_mem with size 4/2 requires uint32_t/uint16_t
		 * in host endianness, but byte array represents target endianness.
		 */
		tunnel = malloc(count * size * sizeof(uint8_t));
		if (tunnel == NULL) {
			LOG_ERROR("Out of memory");
			return ERROR_FAIL;
		}

		switch (size) {
		case 4:
			target_buffer_get_u32_array(target, buffer, count,
				(uint32_t *)tunnel);
			break;
		case 2:
			target_buffer_get_u16_array(target, buffer, count,
				(uint16_t *)tunnel);
			break;
		}
		buffer = tunnel;
	}

	/*
	 * if noDMA off, use DMAACC mode for memory write,
	 * else, do direct memory transfer
	 */
	//if (jtag_info->dma_transfer & JTAG_IMP_NODMA)
		retval = arc_mem_write_block(jtag_info, address, size, count,
			(void *)buffer);
	//else
		//retval = arc32_dmaacc_write_mem(jtag_info, address, size, count,
		//(void *)buffer);

	if (tunnel != NULL)
		free(tunnel);

	return retval;
}

int arc_mem_bulk_write(struct target *target, uint32_t address, uint32_t count,
	const uint8_t *buffer)
{
	int retval = ERROR_OK;
	struct arc32_common *arc32 = target_to_arc32(target);
	struct arc_jtag *jtag_info = &arc32->jtag_info;

	LOG_DEBUG(">> Entering <<");

	printf("write: 0x%8.8x words @: 0x%8.8x\n", count, address);
	LOG_DEBUG("address: 0x%8.8" PRIx32 ", count: 0x%8.8" PRIx32 "", address, count);

	if (target->state != TARGET_HALTED) {
		LOG_WARNING("target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	/* check alignment */
	if (address & 0x3u)
		return ERROR_TARGET_UNALIGNED_ACCESS;

	if (arc32->fast_data_area == NULL) {
		/*
		 * Get memory for block write handler
		 * we preserve this area between calls and gain a speed increase
		 * of about 3kb/sec when writing flash
		 * this will be released/nulled by the system when the target is
		 * resumed or reset.
		 */
		retval = target_alloc_working_area(target,
				ARC32_FASTDATA_HANDLER_SIZE,
				&arc32->fast_data_area);

		if (retval != ERROR_OK) {
			LOG_WARNING("No working area available, falling back to non-bulk write");
			return arc_mem_write(target, address, 4, count, buffer);
		} else {
			LOG_DEBUG("ARC32 fastdata handler uses 64Kb buffer.");
			retval = ERROR_OK;
		}

		/* reset fastadata state so the algo get reloaded */
		jtag_info->fast_access_save = -1;
	}

	/* arc32_pracc_fastdata_xfer requires uint32_t in host endianness, */
	/* but byte array represents target endianness                      */
	uint32_t *tunnel = NULL;
	tunnel = malloc(count * sizeof(uint32_t));
	if (tunnel == NULL) {
		LOG_ERROR("Out of memory");
		return ERROR_FAIL;
	}

	target_buffer_get_u32_array(target, buffer, count, tunnel);

#ifdef DEBUG
	/* transfer big data block into target !! needs performance upgrade !! */
	printf(" > going to store: 0x%08x\n", (uint32_t *)tunnel[0]);
	printf(" >               : 0x%08x\n", (uint32_t *)tunnel[1]);
	printf(" >               : 0x%08x\n", (uint32_t *)tunnel[2]);
	printf(" >               : 0x%08x\n", (uint32_t *)tunnel[3]);
	printf(" >       ----->  : 0x%08x\n", (uint32_t *)tunnel[4]);
	printf(" >               : 0x%08x\n", (uint32_t *)tunnel[5]);
	printf(" >               : 0x%08x\n", (uint32_t *)tunnel[6]);
#endif
	//retval = arc700_write_memory(target, address, 4, count, (uint8_t *)tunnel);
	retval = arc_jtag_write_block(jtag_info, address, 4, count, (uint32_t *)tunnel);

	if (tunnel != NULL)
		free(tunnel);

	if (retval != ERROR_OK) {
		/* FASTDATA access failed, try normal memory write */
		LOG_DEBUG("Fastdata access Failed, falling back to non-bulk write");
		retval = arc_mem_write(target, address, 4, count, buffer);
	}

	return retval;
}

int arc_mem_checksum(struct target *target, uint32_t address, uint32_t count,
	uint32_t *checksum)
{
	int retval = ERROR_OK;

	printf(" >> Entering: %s(%s @ln:%d)\n",__func__,__FILE__,__LINE__);
	LOG_DEBUG(">> Entering <<");

	printf(" !! @ software to do so :-) !!\n");

	return retval;
}

int arc_mem_blank_check(struct target *target, uint32_t address,
	uint32_t count, uint32_t *blank)
{
	int retval = ERROR_OK;

	printf(" >> Entering: %s(%s @ln:%d)\n",__func__,__FILE__,__LINE__);
	LOG_DEBUG(">> Entering <<");

	printf(" !! @ software to do so :-) !!\n");

	return retval;
}

/* ......................................................................... */

int arc_mem_run_algorithm(struct target *target,
	int num_mem_params, struct mem_param *mem_params,
	int num_reg_params, struct reg_param *reg_params,
	uint32_t entry_point, uint32_t exit_point,
	int timeout_ms, void *arch_info)
{
	int retval = ERROR_OK;

	printf(" >> Entering: %s(%s @ln:%d)\n",__func__,__FILE__,__LINE__);
	LOG_DEBUG(">> Entering <<");

	printf(" > NOT SUPPORTED IN THIS RELEASE.\n");

	return retval;
}

int arc_mem_start_algorithm(struct target *target,
	int num_mem_params, struct mem_param *mem_params,
	int num_reg_params, struct reg_param *reg_params,
	uint32_t entry_point, uint32_t exit_point,
	void *arch_info)
{
	int retval = ERROR_OK;

	printf(" >> Entering: %s(%s @ln:%d)\n",__func__,__FILE__,__LINE__);
	LOG_DEBUG(">> Entering <<");

	printf(" > NOT SUPPORTED IN THIS RELEASE.\n");

	return retval;
}

int arc_mem_wait_algorithm(struct target *target,
	int num_mem_params, struct mem_param *mem_params,
	int num_reg_params, struct reg_param *reg_params,
	uint32_t exit_point, int timeout_ms,
	void *arch_info)
{
	int retval = ERROR_OK;

	printf(" >> Entering: %s(%s @ln:%d)\n",__func__,__FILE__,__LINE__);
	LOG_DEBUG(">> Entering <<");

	printf(" > NOT SUPPORTED IN THIS RELEASE.\n");

	return retval;
}

/* ......................................................................... */

int arc_mem_virt2phys(struct target *target, uint32_t address,
	uint32_t *physical)
{
	int retval = ERROR_OK;

	printf(" >> Entering: %s(%s @ln:%d)\n",__func__,__FILE__,__LINE__);
	LOG_DEBUG(">> Entering <<");

	printf(" > NOT SUPPORTED IN THIS RELEASE.\n");

	return retval;
}

int arc_mem_read_phys_memory(struct target *target, uint32_t phys_address,
	uint32_t size, uint32_t count, uint8_t *buffer)
{
	int retval = ERROR_OK;

	printf(" >> Entering: %s(%s @ln:%d)\n",__func__,__FILE__,__LINE__);
	LOG_DEBUG(">> Entering <<");

	printf(" > NOT SUPPORTED IN THIS RELEASE.\n");

	return retval;
}

int arc_mem_write_phys_memory(struct target *target, uint32_t phys_address,
	uint32_t size, uint32_t count, const uint8_t *buffer)
{
	int retval = ERROR_OK;

	printf(" >> Entering: %s(%s @ln:%d)\n",__func__,__FILE__,__LINE__);
	LOG_DEBUG(">> Entering <<");

	printf(" > NOT SUPPORTED IN THIS RELEASE.\n");

	return retval;
}

int arc_mem_mmu(struct target *target, int *enabled)
{
	int retval = ERROR_OK;

	/* (gdb) load command runs through here */

	//printf(" >> Entering: %s(%s @ln:%d)\n",__func__,__FILE__,__LINE__);
	LOG_DEBUG(">> Entering <<");

	LOG_DEBUG(" > NOT SUPPORTED IN THIS RELEASE.\n");

	return retval;
}