/*
 *  Project: ringbuffer - File: ringbuffer.c
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

#include "ringbuffer/ringbuffer.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>

struct _ringbuffer {
	size_t             size;
	size_t             used;
	size_t             tail;
	size_t             lost;
	pthread_spinlock_t spinlock;
	pthread_mutex_t    rd_mutex;
	pthread_mutex_t    rd_cond_lock;
	pthread_cond_t     rd_cond;
	pthread_mutex_t    wr_mutex;
	pthread_mutex_t    wr_cond_lock;
	pthread_cond_t     wr_cond;
	uint8_t            data[0];
};

static inline void _cond_wait(pthread_cond_t *cond, pthread_mutex_t *lock)
{
	int erc;

	erc = pthread_mutex_lock(lock);
	assert(erc == 0);
	erc = pthread_cond_wait(cond, lock);
	assert(erc == 0);
	erc = pthread_mutex_unlock(lock);
	assert(erc == 0);
}

static inline void _cond_signal(pthread_cond_t *cond, pthread_mutex_t *lock)
{
	int erc;

	erc = pthread_mutex_lock(lock);
	assert(erc == 0);
	erc = pthread_cond_signal(cond);
	assert(erc == 0);
	erc = pthread_mutex_unlock(lock);
	assert(erc == 0);
}

static inline size_t _free(struct _ringbuffer *_rb)
{
	return _rb->size - _rb->used;
}

static inline size_t _used(struct _ringbuffer *_rb)
{
	return _rb->used;
}

static inline size_t _put(struct _ringbuffer *_rb, uint8_t *po, size_t co)
{
	size_t __free = _rb->size - _rb->used;
	size_t __head = (_rb->tail + _rb->used) % _rb->size;
	size_t new_head, i;

	if (co > __free)
		co = __free;
	new_head = (__head + co) % _rb->size;
	if (new_head <= _rb->size) {
		memcpy(&_rb->data[__head], po, co);
	} else {
		i = _rb->size - __head;
		memcpy(&_rb->data[__head], po, i);
		new_head -= _rb->size;
		memcpy(_rb->data, &po[i], new_head);
	}
	_rb->used += co;
	return co;
}

static inline size_t _get(struct _ringbuffer *_rb, uint8_t *po, size_t co)
{
	size_t new_tail;

	if (co > _rb->used)
		co = _rb->used;
	if (!co)
		return 0;
	new_tail = (_rb->tail + co) % _rb->size;
	if (new_tail > _rb->tail) {
		memcpy(po, &_rb->data[_rb->tail], co);
		_rb->tail = new_tail;
	} else {
		co = _rb->size - _rb->tail;
		memcpy(po, &_rb->data[_rb->tail], co);
		_rb->tail = 0;
	}
	_rb->used -= co;
	return co;
}

int rb_init(ringbuffer_t *rb, size_t size)
{
	int                 erc;

	assert(rb);
	rb->_rb = malloc(sizeof(struct _ringbuffer) + size);
	if (!rb->_rb)
		return ENOMEM;
	erc = pthread_spin_init(&rb->_rb->spinlock, PTHREAD_PROCESS_PRIVATE);
	if (erc)
		goto undo_spinlock;
	erc = pthread_mutex_init(&rb->_rb->rd_mutex, NULL);
	if (erc)
		goto undo_rd_mutex;
	erc = pthread_mutex_init(&rb->_rb->rd_cond_lock, NULL);
	if (erc)
		goto undo_rd_cond_lock;
	erc = pthread_cond_init(&rb->_rb->rd_cond, NULL);
	if (erc)
		goto undo_rd_cond;
	erc = pthread_mutex_init(&rb->_rb->wr_mutex, NULL);
	if (erc)
		goto undo_wr_mutex;
	erc = pthread_mutex_init(&rb->_rb->wr_cond_lock, NULL);
	if (erc)
		goto undo_wr_cond_lock;
	erc = pthread_cond_init(&rb->_rb->wr_cond, NULL);
	if (erc)
		goto undo_wr_cond;
	rb->_rb->used = rb->_rb->tail = rb->_rb->lost = 0;
	rb->_rb->size = size;
	return 0;

undo_wr_cond:
	pthread_mutex_destroy(&rb->_rb->wr_cond_lock);
undo_wr_cond_lock:
	pthread_mutex_destroy(&rb->_rb->wr_mutex);
undo_wr_mutex:
	pthread_cond_destroy(&rb->_rb->rd_cond);
undo_rd_cond:
	pthread_mutex_destroy(&rb->_rb->rd_cond_lock);
undo_rd_cond_lock:
	pthread_mutex_destroy(&rb->_rb->rd_mutex);
undo_rd_mutex:
	pthread_spin_destroy(&rb->_rb->spinlock);
undo_spinlock:
	return erc;
}

int rb_destroy(ringbuffer_t *rb)
{
	assert(rb);
	if (!rb->_rb)
		return EFAULT;
	pthread_cond_destroy(&rb->_rb->wr_cond);
	pthread_mutex_destroy(&rb->_rb->wr_cond_lock);
	pthread_mutex_destroy(&rb->_rb->wr_mutex);
	pthread_cond_destroy(&rb->_rb->rd_cond);
	pthread_mutex_destroy(&rb->_rb->rd_cond_lock);
	pthread_mutex_destroy(&rb->_rb->rd_mutex);
	pthread_spin_destroy(&rb->_rb->spinlock);
	free(rb->_rb);
	rb->_rb = NULL;
	return 0;
}

int rb_write_block(ringbuffer_t *rb, uint8_t *po, size_t co)
{
	struct _ringbuffer *_rb;
	int erc, res = 0, written = 0;

	assert(rb);
	_rb = rb->_rb;
	if (!rb->_rb)
		return -EFAULT;
	if (co == 0)
		return 0;
	erc = pthread_mutex_lock(&_rb->wr_mutex);
	assert(erc == 0);
	while (co > 0) {
		erc = pthread_spin_lock(&_rb->spinlock); /*----v*/
		assert(erc == 0);
		res = _put(_rb, po, co);
		erc = pthread_spin_unlock(&_rb->spinlock); /*--^*/
		assert(erc == 0);
		po += res;
		co -= res;
		written += res;
		if (res > 0)
			_cond_signal(&_rb->wr_cond, &_rb->wr_cond_lock);
		if (co > 0)
			_cond_wait(&_rb->rd_cond, &_rb->rd_cond_lock);
	} /* end while */
	erc = pthread_mutex_unlock(&_rb->wr_mutex);
	assert(erc == 0);
	return written;
}

int rb_write_nonblock(ringbuffer_t *rb, uint8_t *po, size_t co)
{
	struct _ringbuffer *_rb;
	int erc, res;
	size_t written = 0;

	assert(rb);
	_rb = rb->_rb;
	if (!rb->_rb)
		return -EFAULT;
	if (co > _rb->size)
		return -E2BIG;
	erc = pthread_spin_lock(&_rb->spinlock); /*----v*/
	assert(erc == 0);
	if (co <= _free(_rb)) {
		while (co > 0) {
			res = _put(_rb, po, co);
			po += res;
			co -= res;
			written += res;
			_cond_signal(&_rb->wr_cond, &_rb->wr_cond_lock);
		} /* end while */
		res = written;
	} else {
		res = -ENOMEM;
	}
	erc = pthread_spin_unlock(&_rb->spinlock); /*--^*/
	assert(erc == 0);
	return res;
}

int rb_read_block(ringbuffer_t *rb, uint8_t *po, size_t co)
{
	struct _ringbuffer *_rb;
	int erc, res, got = 0;

	assert(rb);
	_rb = rb->_rb;
	if (!rb->_rb)
		return -EFAULT;
	erc = pthread_mutex_lock(&_rb->rd_mutex);
	assert(erc == 0);
	while (co > 0) {
		erc = pthread_spin_lock(&_rb->spinlock); /*----v*/
		assert(erc == 0);
		res = _get(_rb, po, co);
		erc = pthread_spin_unlock(&_rb->spinlock); /*--^*/
		assert(erc == 0);
		if (res == 0) {
			if (got == 0) {
				_cond_wait(&_rb->wr_cond, &_rb->wr_cond_lock);
			} else {
				break;
			}
		} else {
			po += res;
			co -= res;
			got += res;
		} /* end while */
	} /* end while */
	erc = pthread_mutex_unlock(&_rb->rd_mutex);
	assert(erc == 0);
	_cond_signal(&_rb->rd_cond, &_rb->rd_cond_lock);
	return got;
}

int rb_read_nonblock(ringbuffer_t *rb, uint8_t *po, size_t co)
{
	struct _ringbuffer *_rb;
	int erc, res;
	size_t got = 0;

	assert(rb);
	_rb = rb->_rb;
	if (!rb->_rb)
		return -EFAULT;
	if (co > _rb->size)
		return -E2BIG;
	erc = pthread_spin_lock(&_rb->spinlock); /*----v*/
	assert(erc == 0);
	if (co <= _used(_rb)) {
		while (co > 0) {
			res = _get(_rb, po, co);
			po += res;
			co -= res;
			got += res;
			_cond_signal(&_rb->rd_cond, &_rb->rd_cond_lock);
		} /* end while */
		res = got;
	} else {
		res = -EAGAIN;
	}
	erc = pthread_spin_unlock(&_rb->spinlock); /*--^*/
	assert(erc == 0);
	return res;
}

void rb_clear(ringbuffer_t *rb)
{
	struct _ringbuffer *_rb;
	int erc;

	assert(rb);
	_rb = rb->_rb;
	assert(_rb);
	erc = pthread_spin_lock(&_rb->spinlock); /*----v*/
	assert(erc == 0);
	_rb->used = _rb->tail = _rb->lost = 0;
	erc = pthread_spin_unlock(&_rb->spinlock); /*--^*/
	assert(erc == 0);
}

size_t rb_get_size(ringbuffer_t *rb)
{
	struct _ringbuffer *_rb;

	assert(rb);
	_rb = rb->_rb;
	assert(_rb);
	return _rb->size;
}

size_t rb_get_used(ringbuffer_t *rb)
{
	struct _ringbuffer *_rb;
	size_t res;
	int erc;

	assert(rb);
	_rb = rb->_rb;
	assert(_rb);
	erc = pthread_spin_lock(&_rb->spinlock); /*----v*/
	assert(erc == 0);
	res = _used(_rb);
	erc = pthread_spin_unlock(&_rb->spinlock); /*--^*/
	assert(erc == 0);
	return res;
}

size_t rb_get_free(ringbuffer_t *rb)
{
	struct _ringbuffer *_rb;
	size_t res;
	int erc;

	assert(rb);
	_rb = rb->_rb;
	assert(_rb);
	erc = pthread_spin_lock(&_rb->spinlock); /*----v*/
	assert(erc == 0);
	res = _free(_rb);
	erc = pthread_spin_unlock(&_rb->spinlock); /*--^*/
	assert(erc == 0);
	return res;
}

size_t rb_get_lost(ringbuffer_t *rb)
{
	struct _ringbuffer *_rb;
	size_t lost;
	int erc;

	assert(rb);
	_rb = rb->_rb;
	if (!_rb)
		return EFAULT;
	erc = pthread_spin_lock(&_rb->spinlock); /*----v*/
	assert(erc == 0);
	lost = _rb->lost;
	erc = pthread_spin_unlock(&_rb->spinlock); /*--^*/
	assert(erc == 0);
	return lost;
}

size_t rb_clear_lost(ringbuffer_t *rb)
{
	struct _ringbuffer *_rb;
	size_t lost;
	int erc;

	assert(rb);
	_rb = rb->_rb;
	if (!_rb)
		return EFAULT;
	erc = pthread_spin_lock(&_rb->spinlock); /*----v*/
	assert(erc == 0);
	lost = _rb->lost;
	_rb->lost = 0;
	erc = pthread_spin_unlock(&_rb->spinlock); /*--^*/
	assert(erc == 0);
	return lost;
}

size_t rb_loose(ringbuffer_t *rb, size_t loose)
{
	struct _ringbuffer *_rb;
	size_t lost;
	int erc;

	assert(rb);
	_rb = rb->_rb;
	if (!_rb)
		return EFAULT;

	erc = pthread_spin_lock(&_rb->spinlock); /*----v*/
	assert(erc == 0);
	_rb->lost += loose;
	lost = _rb->lost;
	erc = pthread_spin_unlock(&_rb->spinlock); /*--^*/
	assert(erc == 0); /*--^*/
	return lost;
}
