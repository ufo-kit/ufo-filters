/*
 * util.h
 *
 * Utility definitions and functions used in different modules.
 *
 *  Created on: Nov 29, 2011
 *      Author: farago
 */

#ifndef UTIL_H_
#define UTIL_H_

#include <CL/cl.h>

#define RETURN_IF_CL_ERROR(err_num) { if((err_num) != CL_SUCCESS) { return (err_num); } }

static inline size_t div_up(size_t dividend, size_t divisor) {
    return (dividend % divisor == 0) ? (dividend / divisor) :
    											(dividend / divisor + 1);
}

#endif /* UTIL_H_ */
