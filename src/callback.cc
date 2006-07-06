/* -*- C++ -*-
 * (c) 2004 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/callback>

#include <stdio.h>

namespace tscb {
	
	callback_link::~callback_link(void) throw()
	{
	}
	
	void callback_link::cancelled(void) throw()
	{
	}	
	
}; /* namespace callback */
