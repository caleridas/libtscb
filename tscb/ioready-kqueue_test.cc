/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include "ioready-testlib.h"

#include <tscb/ioready-kqueue.h>

namespace tscb {

class IoreadyKqueueTests : public IoreadyTests {};

TEST_F(IoreadyKqueueTests, simple)
{
	auto dispatcher = std::make_unique<ioready_dispatcher_kqueue>();
	run_simple(dispatcher.get());
}

TEST_F(IoreadyKqueueTests, threads)
{
	auto dispatcher = std::make_unique<ioready_dispatcher_kqueue>();
	run_threads(dispatcher.get());
}

TEST_F(IoreadyKqueueTests, sync_disconnect)
{
	auto dispatcher = std::make_unique<ioready_dispatcher_kqueue>();
	run_sync_disconnect(dispatcher.get());
}

}
