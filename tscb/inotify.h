/* -*- C++ -*-
 * (c) 2022 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1.
 * Refer to the file "COPYING" for details.
 */

#ifndef TSCB_INOTIFY_H
#define TSCB_INOTIFY_H

#include <atomic>

#include <tscb/inotify-service.h>
#include <tscb/detail/deferred-locks.h>

namespace tscb {

class inotify_dispatcher final : public inotify_service {
public:
	~inotify_dispatcher() noexcept override;

	inotify_dispatcher();

	inotify_connection
	inode_watch(
		std::function<void(tscb::inotify_events, uint32_t, const char*)> function,
		const char* path,
		inotify_events event_mask
	) override;

	std::size_t
	dispatch(std::size_t limit);

	int
	fd() const noexcept;

private:
	class link_type;

	struct bucket_type {
		std::atomic<link_type *> active = nullptr;
		link_type* first = nullptr;
		link_type* last = nullptr;
		link_type* inactive_next = nullptr;
	};

	void
	insert(link_type* link) noexcept;

	bool
	remove(link_type* link) noexcept;

	void
	synchronize() noexcept;

	bool
	disconnect_all();

	void
	check_resize() noexcept;

	void
	rehash(std::size_t new_bucket_count);

	int fd_;

	detail::deferrable_rwlock lock_;
	std::unique_ptr<bucket_type[]> wd_hash_buckets_;
	std::size_t wd_hash_bucket_mask_ = 0;
	std::size_t wd_entry_count_ = 0;

	link_type* inactive_ = nullptr;
};

}

#endif
