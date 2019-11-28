/* -*- C++ -*-
 * (c) 2011 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <unistd.h>

#include "tscb/childproc-monitor.h"

#include <gtest/gtest.h>

namespace tscb {

class ChildprocMonitorTest : public ::testing::Test {
public:
	ChildprocMonitorTest() : called_count_(0) {}

protected:
	class sigchld_guard {
	public:
		sigchld_guard() noexcept
		{
			sigset_t sigset;
			sigemptyset(&sigset);
			sigaddset(&sigset, SIGCHLD);
			sigprocmask(SIG_BLOCK, &sigset, NULL);
		}
		~sigchld_guard() noexcept
		{
			sigset_t sigset;
			sigemptyset(&sigset);
			sigaddset(&sigset, SIGCHLD);
			sigprocmask(SIG_BLOCK, &sigset, NULL);
		}
		void wait()
		{
			sigset_t sigset;
			sigemptyset(&sigset);
			sigaddset(&sigset, SIGCHLD);
			sigwaitinfo(&sigset, NULL);
		}
	};

	void
	proc_handler() noexcept
	{
		called_count_.fetch_add(1, std::memory_order_relaxed);
	}

	void
	throwing_proc_handler()
	{
		called_count_.fetch_add(1, std::memory_order_relaxed);
		throw std::runtime_error("foo");
	}

	inline int
	called_count() const noexcept
	{
		return called_count_.load(std::memory_order_relaxed);
	}

	static pid_t
	launch_short_child_process() noexcept
	{
		pid_t pid = fork();
		if (pid == 0) {
			_exit(0);
		}
		return pid;
	}

	static pid_t
	launch_long_child_process() noexcept
	{
		pid_t pid = fork();
		if (pid == 0) {
			for (;;) {
				sleep(60);
			}
		}
		return pid;
	}

	std::atomic<int> called_count_;
	sigchld_guard guard_;
	childproc_monitor monitor_;
};

TEST_F(ChildprocMonitorTest, basic_operation)
{
	pid_t pid = launch_short_child_process();

	connection c = monitor_.watch_childproc([this](int, const rusage&){proc_handler();}, pid);

	EXPECT_EQ(0, called_count());

	/* wait until child process has terminated */
	guard_.wait();

	monitor_.dispatch();
	EXPECT_EQ(1, called_count());
}

TEST_F(ChildprocMonitorTest, cancel)
{
	pid_t pid = launch_long_child_process();

	connection c = monitor_.watch_childproc([this](int, const rusage&){proc_handler();}, pid);

	monitor_.dispatch();

	/* not exited yet */
	EXPECT_EQ(0, called_count());

	c.disconnect();

	kill(pid, SIGTERM);
	/* wait until child process has terminated */
	guard_.wait();

	/* callback cancelled, should not reap */
	monitor_.dispatch();
	EXPECT_EQ(0, called_count());

	int status;
	waitpid(pid, &status, 0);
}

TEST_F(ChildprocMonitorTest, ignore_unknown)
{
	pid_t pid = launch_short_child_process();

	/* wait until child process has terminated */
	guard_.wait();

	/* but dispatcher does not yet know about it, so won't reap */
	monitor_.dispatch();
	EXPECT_EQ(0, called_count());

	connection c = monitor_.watch_childproc([this](int, const rusage&){proc_handler();}, pid);

	/* now that dispatcher knows, it will reap */
	monitor_.dispatch();
	EXPECT_EQ(1, called_count());
}

TEST_F(ChildprocMonitorTest, throwing_handler)
{
	pid_t pid1 = launch_short_child_process();
	/* wait until child process has terminated */
	guard_.wait();

	pid_t pid2 = launch_short_child_process();
	/* wait until child process has terminated */
	guard_.wait();

	connection c1 = monitor_.watch_childproc([this](int, const rusage&){throwing_proc_handler();}, pid1);
	connection c2 = monitor_.watch_childproc([this](int, const rusage&){throwing_proc_handler();}, pid2);

	try {
		monitor_.dispatch();
		EXPECT_TRUE(false);
	}
	catch(std::runtime_error &) {
	}

	EXPECT_EQ(1, called_count());

	try {
		monitor_.dispatch();
		EXPECT_TRUE(false);
	}
	catch(std::runtime_error &) {
	}

	EXPECT_EQ(2, called_count());
}

}
