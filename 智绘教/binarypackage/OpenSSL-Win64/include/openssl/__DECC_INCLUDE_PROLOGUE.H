﻿/*
 * Copyright 2016-2020 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * This file is only used by HP C/C++ on VMS, and is included automatically
 * after each header file from this directory
 */

/*
 * The C++ compiler doesn't understand these pragmas, even though it
 * understands the corresponding command line qualifier.
 */
#ifndef __cplusplus
/* save state */
# pragma names save
/* have the compiler shorten symbols larger than 31 chars to 23 chars
 * followed by a 8 hex char CRC
 */
# pragma names as_is,shortened
#endif
