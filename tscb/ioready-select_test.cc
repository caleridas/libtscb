/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include "ioready-testlib.h"

#include <tscb/ioready-select.h>

namespace tscb {

class IoreadySelectTests : public IoreadyTests {};

TEST_F(IoreadySelectTests, simple)
{
	auto dispatcher = std::make_unique<ioready_dispatcher_select>();
	run_simple(dispatcher.get());
}

TEST_F(IoreadySelectTests, threads)
{
	auto dispatcher = std::make_unique<ioready_dispatcher_select>();
	run_threads(dispatcher.get());
}

TEST_F(IoreadySelectTests, sync_disconnect)
{
	auto dispatcher = std::make_unique<ioready_dispatcher_select>();
	run_sync_disconnect(dispatcher.get());
}

}
