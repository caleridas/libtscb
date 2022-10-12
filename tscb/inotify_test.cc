/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include "ioready-testlib.h"

#include <tscb/inotify.h>

#include <stdlib.h>

#include <filesystem>

namespace tscb {

class INotifyTests : public ::testing::Test {
public:
	void
	SetUp() override
	{
		char template_dir[] = "/tmp/tscb-inotify-test-XXXXXX";
		basedir_ = ::mkdtemp(template_dir);
	}

	void
	TearDown() override
	{
		std::filesystem::remove_all(basedir_);
	}

protected:
	std::filesystem::path basedir_;
};


TEST_F(INotifyTests, simple)
{
	inotify_dispatcher disp;

	struct recorded_event {
		uint32_t events;
		uint32_t cookie;
		std::string name;
	};

	std::vector<recorded_event> revents;

	auto c = disp.inode_watch(
		[&revents](tscb::inotify_events events, uint32_t cookie, const char* name) {
			recorded_event ev = {
				events, cookie, std::string(name)
			};
			revents.push_back(std::move(ev));
		},
		basedir_.c_str(),
		IN_CREATE
	);

	::mkdir((basedir_ / "test").c_str(), 0777);

	disp.dispatch(10);

	c.disconnect();

	::mkdir((basedir_ / "test2").c_str(), 0777);

	disp.dispatch(10);

	EXPECT_EQ(1, revents.size());
	EXPECT_EQ(IN_CREATE, revents[0].events);
	EXPECT_EQ("test", revents[0].name);
}

TEST_F(INotifyTests, multiple)
{
	inotify_dispatcher disp;

	struct recorded_event {
		uint32_t events;
		uint32_t cookie;
		std::string name;
	};

	std::vector<recorded_event> revents;

	auto c1 = disp.inode_watch(
		[&revents](tscb::inotify_events events, uint32_t cookie, const char* name) {
			recorded_event ev = {
				events, cookie, std::string(name)
			};
			revents.push_back(std::move(ev));
		},
		basedir_.c_str(),
		IN_CREATE
	);

	auto c2 = disp.inode_watch(
		[&revents](tscb::inotify_events events, uint32_t cookie, const char* name) {
			recorded_event ev = {
				events, cookie, std::string(name)
			};
			revents.push_back(std::move(ev));
		},
		basedir_.c_str(),
		IN_DELETE
	);

	::mkdir((basedir_ / "test").c_str(), 0777);
	::rmdir((basedir_ / "test").c_str());

	disp.dispatch(10);

	EXPECT_EQ(2, revents.size());
	EXPECT_EQ(IN_CREATE, revents[0].events);
	EXPECT_EQ("test", revents[0].name);
	EXPECT_EQ(IN_DELETE, revents[1].events);
	EXPECT_EQ("test", revents[1].name);

	revents.clear();
	c2.disconnect();

	::mkdir((basedir_ / "test2").c_str(), 0777);
	::rmdir((basedir_ / "test2").c_str());

	disp.dispatch(10);

	EXPECT_EQ(1, revents.size());
	EXPECT_EQ(IN_CREATE, revents[0].events);
	EXPECT_EQ("test2", revents[0].name);
}

TEST_F(INotifyTests, ordered_destruct)
{
	std::shared_ptr<inotify_dispatcher> disp = std::make_shared<inotify_dispatcher>();

	auto c = disp->inode_watch(
		[](tscb::inotify_events events, uint32_t cookie, const char* name) {},
		basedir_.c_str(),
		IN_CREATE
	);

	disp.reset();
	c.disconnect();
}

TEST_F(INotifyTests, scoped)
{
	std::shared_ptr<inotify_dispatcher> disp = std::make_shared<inotify_dispatcher>();

	scoped_inotify_connection c = disp->inode_watch(
		[](tscb::inotify_events events, uint32_t cookie, const char* name) {},
		basedir_.c_str(),
		IN_CREATE
	);
}

TEST_F(INotifyTests, not_existing)
{
	inotify_dispatcher disp;

	struct recorded_event {
		uint32_t events;
		uint32_t cookie;
		std::string name;
	};

	std::vector<recorded_event> revents;

	auto c1 = disp.inode_watch(
		[&revents](tscb::inotify_events events, uint32_t cookie, const char* name) {
			recorded_event ev = {
				events, cookie, std::string(name)
			};
			revents.push_back(std::move(ev));
		},
		(basedir_ / "foo").c_str(),
		IN_MODIFY
	);

	EXPECT_FALSE(c1.is_connected());
	c1.disconnect();
}

}
