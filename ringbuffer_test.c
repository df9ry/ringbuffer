/*
 *  Project: ringbuffer - File: reingbuffer_test.c
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

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "ringbuffer.h"

int main(int argc, char *argv[]) {
	struct ringbuffer rb;

	printf("Testing rb_init");
	assert(rb_init(&rb, 1024) == 0);
	printf("OK\n");

	printf("Testing rb_destroy");
	assert(rb_destroy(&rb) == 0);
	printf("OK\n");

	return EXIT_SUCCESS;
}
