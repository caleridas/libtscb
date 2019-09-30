/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include "ioready-testlib.h"

#include <tscb/ioready-epoll.h>

namespace tscb {

class IoreadyEPollTests : public IoreadyTests {};

TEST_F(IoreadyEPollTests, simple)
{
	auto dispatcher = std::make_unique<ioready_dispatcher_epoll>();
	run_simple(dispatcher.get());
}

TEST_F(IoreadyEPollTests, threads)
{
	auto dispatcher = std::make_unique<ioready_dispatcher_epoll>();
	run_threads(dispatcher.get());
}

TEST_F(IoreadyEPollTests, sync_disconnect)
{
	auto dispatcher = std::make_unique<ioready_dispatcher_epoll>();
	run_sync_disconnect(dispatcher.get());
}

}
