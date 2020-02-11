/* -*- C++ -*-
 * (c) 2010 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_REACTOR_SERVICE_H
#define TSCB_REACTOR_SERVICE_H

#include <tscb/timer.h>
#include <tscb/ioready.h>
#include <tscb/workqueue.h>

namespace tscb {

/**
	\brief Posix reactor service

	Combines the interfaces \ref tscb::workqueue_service "workqueue_service",
	\ref tscb::timer_service "timer_service" and \ref tscb::ioready_service "ioready_service"
*/
class reactor_service : public workqueue_service, public timer_service, public ioready_service {
public:
	~reactor_service() noexcept override;
};

};

#endif
