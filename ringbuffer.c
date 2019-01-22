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

#include "ringbuffer.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>

struct _ringbuffer {
	size_t             size;
	size_t             head;
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
	pthread_mutex_lock(lock);
	pthread_cond_wait(cond, lock);
	pthread_mutex_unlock(lock);
}

static inline void _cond_signal(pthread_cond_t *cond, pthread_mutex_t *lock)
{
	pthread_mutex_lock(lock);
	pthread_cond_signal(cond);
	pthread_mutex_unlock(lock);
}

static inline size_t _avail(struct _ringbuffer *_rb)
{
	if (_rb->tail <= _rb->head) {
		return _rb->size - (_rb->head - _rb->tail);
	} else {
		return _rb->tail - _rb->head;
	}
}

static inline size_t _length(struct _ringbuffer *_rb)
{
	if (_rb->tail <= _rb->head) {
		return _rb->head - _rb->tail;
	} else {
		return _rb->size - (_rb->tail - _rb->head);
	}
}

static inline void _put(struct _ringbuffer *_rb, uint8_t *po, size_t co)
{
	size_t new_head = _rb->head + co, i;
	if (new_head < _rb->size) {
		memcpy(&_rb->data[_rb->head], po, co);
	} else {
		i = _rb->size - _rb->head;
		memcpy(&_rb->data[_rb->head], po, i);
		new_head -= _rb->size;
		memcpy(_rb->data, &po[i], new_head);
	}
	_rb->head = new_head;
}

static inline size_t _get(struct _ringbuffer *_rb, uint8_t *po, size_t co)
{
	size_t avail;

	if (_rb->tail <= _rb->head) {
		avail = _rb->head - _rb->tail;
		if (co > avail)
			co = avail;
		memcpy(po, &_rb->data[_rb->tail], co);
		_rb->tail += co;
	} else {
		avail = _rb->head;
		if (co > avail)
			co = avail;
		memcpy(po, &_rb->data[0], co);
		_rb->tail = co;
	}
	return co;
}

int rb_init(struct ringbuffer *rb, size_t size)
{
	struct _ringbuffer *_rb;
	int                 erc;

	assert(rb);
	if (rb->_rb)
		return EFAULT;
	rb->_rb = _rb = malloc(sizeof(struct _ringbuffer) + size);
	if (!_rb)
		return ENOMEM;
	erc = pthread_spin_init(&_rb->spinlock, PTHREAD_PROCESS_PRIVATE);
	if (erc)
		goto undo_spinlock;
	erc = pthread_mutex_init(&_rb->rd_mutex, NULL);
	if (erc)
		goto undo_rd_mutex;
	erc = pthread_mutex_init(&_rb->rd_cond_lock, NULL);
	if (erc)
		goto undo_rd_cond_lock;
	erc = pthread_cond_init(&_rb->rd_cond, NULL);
	if (erc)
		goto undo_rd_cond;
	erc = pthread_mutex_init(&_rb->wr_mutex, NULL);
	if (erc)
		goto undo_wr_mutex;
	erc = pthread_mutex_init(&_rb->wr_cond_lock, NULL);
	if (erc)
		goto undo_wr_cond_lock;
	erc = pthread_cond_init(&_rb->wr_cond, NULL);
	if (erc)
		goto undo_wr_cond;
	_rb->head = _rb->tail = _rb->lost = 0;

	return 0;

undo_wr_cond:
	pthread_mutex_destroy(&_rb->wr_cond_lock);
undo_wr_cond_lock:
	pthread_mutex_destroy(&_rb->wr_mutex);
undo_wr_mutex:
	pthread_cond_destroy(&_rb->rd_cond);
undo_rd_cond:
	pthread_mutex_destroy(&_rb->rd_cond_lock);
undo_rd_cond_lock:
	pthread_mutex_destroy(&_rb->rd_mutex);
undo_rd_mutex:
	pthread_spin_destroy(&_rb->spinlock);
undo_spinlock:
	free(_rb);
	rb->_rb = NULL;
	return erc;
}

int rb_destroy(struct ringbuffer *rb)
{
	struct _ringbuffer *_rb;

	assert(rb);
	_rb = rb->_rb;
	if (rb->_rb)
		return EFAULT;
	pthread_cond_destroy(&_rb->wr_cond);
	pthread_mutex_destroy(&_rb->wr_cond_lock);
	pthread_mutex_destroy(&_rb->wr_mutex);
	pthread_cond_destroy(&_rb->rd_cond);
	pthread_mutex_destroy(&_rb->rd_cond_lock);
	pthread_mutex_destroy(&_rb->rd_mutex);
	pthread_spin_destroy(&_rb->spinlock);
	free(_rb);
	rb->_rb = NULL;
	return 0;
}

static inline int _rb_write(struct _ringbuffer *_rb, uint8_t *po, size_t co)
{
	size_t avail = _avail(_rb);
	if (co > avail) {
		co = avail;
	}
	if (co > 0) {
		_put(_rb, po, co);
	}
	return co;
}

static inline int _rb_read(struct _ringbuffer *_rb, uint8_t *po, size_t co)
{
	size_t length = _length(_rb), cr;
	if (co > length) {
		co = length;
	}
	while (co > 0) {
		cr = _get(_rb, po, co);
		po += cr;
		co -= cr;
	} /* end while */
	return length;
}

int rb_write_syn(struct ringbuffer *rb, uint8_t *po, size_t co)
{
	struct _ringbuffer *_rb;
	int res = 0, _co = co;
	uint8_t *_po = po;

	assert(rb);
	_rb = rb->_rb;
	if (!rb->_rb)
		return -EFAULT;
	pthread_mutex_lock(&_rb->wr_mutex);
	while ((res >= 0) && (_co > 0)) {
		pthread_spin_lock(&_rb->spinlock); /*----v*/
		res = _rb_write(_rb, _po, _co);
		pthread_spin_unlock(&_rb->spinlock); /*--^*/
		if (res > 0) {
			_po += res;
			_co -= res;
			_cond_signal(&_rb->wr_cond, &_rb->wr_cond_lock);
			if (_co > 0)
				_cond_wait(&_rb->rd_cond, &_rb->rd_cond_lock);
		}
	} /* end while */
	pthread_mutex_unlock(&_rb->wr_mutex);
	return (res >= 0) ? co : res;
}

int rb_write_asy(struct ringbuffer *rb, uint8_t *po, size_t co)
{
	struct _ringbuffer *_rb;
	int res;

	assert(rb);
	_rb = rb->_rb;
	if (!rb->_rb)
		return -EFAULT;
	pthread_spin_lock(&_rb->spinlock); /*----v*/
	res = _rb_write(_rb, po, co);
	pthread_spin_unlock(&_rb->spinlock); /*--^*/
	if (res > 0)
		_cond_signal(&_rb->wr_cond, &_rb->wr_cond_lock);
	return res;
}

int rb_read_syn(struct ringbuffer *rb, uint8_t *po, size_t co)
{
	struct _ringbuffer *_rb;
	int res = 0, _co = co;
	uint8_t *_po = po;

	assert(rb);
	_rb = rb->_rb;
	if (!rb->_rb)
		return -EFAULT;
	pthread_mutex_lock(&_rb->rd_mutex);
	while ((res >= 0) && (_co > 0)) {
		pthread_spin_lock(&_rb->spinlock); /*----v*/
		res = _rb_read(_rb, _po, _co);
		pthread_spin_unlock(&_rb->spinlock); /*--^*/
		if (res > 0) {
			_po += res;
			_co -= res;
			_cond_signal(&_rb->rd_cond, &_rb->rd_cond_lock);
			if (_co > 0)
				_cond_wait(&_rb->wr_cond, &_rb->wr_cond_lock);
		}
	} /* end while */
	pthread_mutex_unlock(&_rb->rd_mutex);
	return (res >= 0) ? co : res;
}

int rb_read_asy(struct ringbuffer *rb, uint8_t *po, size_t co)
{
	struct _ringbuffer *_rb;
	int res;

	assert(rb);
	_rb = rb->_rb;
	if (!rb->_rb)
		return -EFAULT;
	pthread_spin_lock(&_rb->spinlock); /*----v*/
	res = _rb_read(_rb, po, co);
	pthread_spin_unlock(&_rb->spinlock); /*--^*/
	if (res > 0)
		_cond_signal(&_rb->rd_cond, &_rb->rd_cond_lock);
	return res;
}

void rb_clear(struct ringbuffer *rb)
{
	struct _ringbuffer *_rb;

	assert(rb);
	_rb = rb->_rb;
	assert(_rb);
	assert(pthread_spin_lock(&_rb->spinlock) == 0); /*----v*/
	_rb->head = _rb->tail = _rb->lost = 0;
	assert(pthread_spin_unlock(&_rb->spinlock) == 0); /*--^*/
}

size_t rb_get_size(struct ringbuffer *rb)
{
	struct _ringbuffer *_rb;

	assert(rb);
	_rb = rb->_rb;
	assert(_rb);
	return _rb->size;
}

size_t rb_get_lost(struct ringbuffer *rb)
{
	struct _ringbuffer *_rb;
	size_t lost;

	assert(rb);
	_rb = rb->_rb;
	if (!_rb)
		return EFAULT;
	assert(pthread_spin_lock(&_rb->spinlock) == 0); /*----v*/
	lost = _rb->lost;
	assert(pthread_spin_unlock(&_rb->spinlock) == 0); /*--^*/
	return lost;
}

size_t rb_clear_lost(struct ringbuffer *rb)
{
	struct _ringbuffer *_rb;
	size_t lost;

	assert(rb);
	_rb = rb->_rb;
	if (!_rb)
		return EFAULT;
	assert(pthread_spin_lock(&_rb->spinlock) == 0); /*----v*/
	lost = _rb->lost;
	_rb->lost = 0;
	assert(pthread_spin_unlock(&_rb->spinlock) == 0); /*--^*/
	return lost;
}

size_t rb_loose_data(struct ringbuffer *rb, size_t loose)
{
	struct _ringbuffer *_rb;
	size_t lost;

	assert(rb);
	_rb = rb->_rb;
	if (!_rb)
		return EFAULT;
	assert(pthread_spin_lock(&_rb->spinlock) == 0); /*----v*/
	lost = _rb->lost + loose;
	assert(pthread_spin_unlock(&_rb->spinlock) == 0); /*--^*/
	return lost;
}
