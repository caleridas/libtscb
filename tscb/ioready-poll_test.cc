/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include "ioready-testlib.h"

#include <tscb/ioready-poll.h>

namespace tscb {

class IoreadyPollTests : public IoreadyTests {};

TEST_F(IoreadyPollTests, simple)
{
	auto dispatcher = std::make_unique<ioready_dispatcher_poll>();
	run_simple(dispatcher.get());
}

TEST_F(IoreadyPollTests, threads)
{
	auto dispatcher = std::make_unique<ioready_dispatcher_poll>();
	run_threads(dispatcher.get());
}

TEST_F(IoreadyPollTests, sync_disconnect)
{
	auto dispatcher = std::make_unique<ioready_dispatcher_poll>();
	run_sync_disconnect(dispatcher.get());
}

}
