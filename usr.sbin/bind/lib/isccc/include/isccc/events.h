/*
 * Portions Copyright (C) 2004, 2005  Internet Systems Consortium, Inc. ("ISC")
 * Portions Copyright (C) 2001  Internet Software Consortium.
 * Portions Copyright (C) 2001  Nominum, Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC AND NOMINUM DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* $ISC: events.h,v 1.3.18.2 2005/04/29 00:17:13 marka Exp $ */

#ifndef ISCCC_EVENTS_H
#define ISCCC_EVENTS_H 1

/*! \file */

#include <isc/eventclass.h>

/*%
 * Registry of ISCCC event numbers.
 */

#define ISCCC_EVENT_CCMSG			(ISC_EVENTCLASS_ISCCC + 0)

#define ISCCC_EVENT_FIRSTEVENT			(ISC_EVENTCLASS_ISCCC + 0)
#define ISCCC_EVENT_LASTEVENT			(ISC_EVENTCLASS_ISCCC + 65535)

#endif /* ISCCC_EVENTS_H */
