#include <tscb/detail/intrusive-list.h>

#include "gtest/gtest.h"

namespace tscb {
namespace detail {

class IntrusiveListTests : public ::testing::Test {
public:
	struct my_item {
		inline my_item() : p(nullptr) {}
		inline my_item(int * ptr) : p(ptr) {}
		inline ~my_item() { if (p) { *p = 0; } }

		int * p;
		intrusive_list_anchor<my_item> anchor;
		typedef intrusive_list_accessor<my_item, &my_item::anchor> accessor;
	};

	typedef intrusive_list<my_item, my_item::accessor> my_list;
};

TEST_F(IntrusiveListTests, test_simple_list)
{
	my_list l;

	EXPECT_TRUE(l.empty());
	my_item i1, i2, i3;

	l.push_back(&i2);
	EXPECT_TRUE(l.begin().ptr() == &i2);
	EXPECT_TRUE(std::next(l.begin()) == l.end());
	EXPECT_TRUE(std::prev(l.end()).ptr() == &i2);

	l.insert(l.begin(), &i1);
	EXPECT_TRUE(l.begin().ptr() == &i1);
	EXPECT_TRUE(std::next(l.begin()).ptr() == &i2);
	EXPECT_TRUE(std::next(l.begin(), 2) == l.end());
	EXPECT_TRUE(std::prev(l.end()).ptr() == &i2);

	l.insert(l.end(), &i3);
	EXPECT_TRUE(l.begin().ptr() == &i1);
	EXPECT_TRUE(std::next(l.begin()).ptr() == &i2);
	EXPECT_TRUE(std::next(l.begin(), 2).ptr() == &i3);
	EXPECT_TRUE(std::next(l.begin(), 3) == l.end());
	EXPECT_TRUE(std::prev(l.end()).ptr() == &i3);
	EXPECT_TRUE(std::prev(l.end(), 2).ptr() == &i2);
	EXPECT_TRUE(std::prev(l.end(), 3).ptr() == &i1);

	l.erase(&i2);
	EXPECT_TRUE(l.begin().ptr() == &i1);
	EXPECT_TRUE(std::next(l.begin()).ptr() == &i3);
	EXPECT_TRUE(std::next(l.begin(), 2) == l.end());
	EXPECT_TRUE(std::prev(l.end()).ptr() == &i3);
	EXPECT_TRUE(std::prev(l.end(), 2).ptr() == &i1);

	my_list l2;
	l2.splice(l2.begin(), l);
	EXPECT_TRUE(l.empty());
	EXPECT_TRUE(l2.size() == 2);
}

}
}
