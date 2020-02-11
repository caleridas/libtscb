/* -*- C++ -*-
 * (c) 2011 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_CHILDPROC_MONITOR_H
#define TSCB_CHILDPROC_MONITOR_H

#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <tscb/signal.h>

namespace tscb {

class childproc_monitor;

class childproc_monitor_service {
public:
	virtual ~childproc_monitor_service() noexcept;

	virtual connection
	watch_childproc(std::function<void(int, const rusage &)> function, pid_t pid) = 0;
};

class childproc_monitor final : public childproc_monitor_service {
public:
	~childproc_monitor() noexcept override;

	childproc_monitor();

	explicit childproc_monitor(bool reap_all_children);

	connection
	watch_childproc(std::function<void(int, const rusage &)> function, pid_t pid) override;

	void
	dispatch();

private:
	class link_type;

	void
	remove(link_type * cb) noexcept;

	void
	synchronize() noexcept;

	detail::deferrable_rwlock lock_;

	bool reap_all_children_;

	std::atomic<link_type *> active_;
	link_type * first_;
	link_type * last_;
	link_type * deferred_destroy_;

	friend class link_type;
};

};

#endif
