/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include "main.h"

int main(void)
{
	printf("\r\nmain function will be started! %s\n", CONFIG_BOARD_TARGET);
	return 0;
}
