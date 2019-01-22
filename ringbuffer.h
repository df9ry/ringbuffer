/*
 *  Project: ringbuffer - File: ringbuffer.h
 *  Copyright (C) 2019 - Tania Hagn - tania@df9ry.de
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as
 *  published by the Free Software Foundation, either version 3 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file  ringbuffer.h
 * @brief Ringbuffer for exchanging streams between threads and decoupling
 *        synchonous from asynchronous io.
 */

#ifndef RINGBUFFER_H_
#define RINGBUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

struct _ringbuffer;
struct ringbuffer {
	struct _ringbuffer *_rb;
};

/**
 * @brief Initialize a ring buffer.
 * @param rb Ring buffer structure.
 * @param size Size of the ring buffer.
 * @return 0 on success. Error code otherwise.
 */
extern int rb_init(struct ringbuffer *rb, size_t size);

/**
 * @brief Destroy a ring buffer, release all ressources occupied by the ring
 *        buffer.
 * @param rb Ring buffer to destroy.
 * @return 0 on success. Error code otherwise.
 */
extern int rb_destroy(struct ringbuffer *rb);

/**
 * @brief Write synchronous to the ring buffer.
 * @param rb The ring buffer to write to.
 * @param po Pointer to data to write.
 * @param co Size of the data to write.
 * @return Number of bytes actually written or a negative error code.
 */
extern int rb_write_syn(struct ringbuffer *rb, uint8_t *po, size_t co);

/**
 * @brief Write asynchronous to the ring buffer.
 * @param rb The ring buffer to write to.
 * @param po Pointer to data to write.
 * @param co Size of the data to write.
 * @return Number of bytes actually written or a negative error code.
 */
extern int rb_write_asy(struct ringbuffer *rb, uint8_t *po, size_t co);

/**
 * @brief Read synchronous from the ring buffer.
 * @param rb The ring buffer to read from.
 * @param po Pointer to buffer to read into.
 * @param co Number of octets requested to read.
 * @return Number of bytes actually read or a negative error code.
 */
extern int rb_read_syn(struct ringbuffer *rb, uint8_t *po, size_t co);

/**
 * @brief Read synchronous from the ring buffer.
 * @param rb The ring buffer to read from.
 * @param po Pointer to buffer to read into.
 * @param co Number of octets requested to read.
 * @return Number of bytes actually read or a negative error code.
 */
extern int rb_read_asy(struct ringbuffer *rb, uint8_t *po, size_t co);

/**
 * @brief Clear the ring buffer.
 * @param rb The ring buffer to read from.
 */
extern void rb_clear(struct ringbuffer *rb);

/**
 * @brief Get size of the ring buffer.
 * @param rb The ring buffer to investigate.
 * @return size of the ring buffer.
 */
extern size_t rg_get_size(struct ringbuffer *rb);

/**
 * @brief Get the number of octets lost for writes without having enough room
 *        in the ring buffer.
 * @param rb The ring buffer to query.
 * @return Total number of octets lost up to now.
 */
extern size_t rb_get_lost(struct ringbuffer *rb);

/**
 * @brief Get the number of octets lost for writes without having enough room
 *        in the ring buffer and clear lost counter.
 * @return Total number of octets lost up to now.
 */
extern size_t rb_clear_lost(struct ringbuffer *rb);

/**
 * @brief Signal loosing of octets for the ring buffer and return the number
 *        of octets lost up to now.
 * @param loose Number of octets lost.
 * @return Total number of octets lost up to now.
 */
extern size_t rb_loose_data(struct ringbuffer *rb, size_t loose);

#ifdef __cplusplus
}
#endif

#endif /* RINGBUFFER_H_ */
