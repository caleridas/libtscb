/* -*- C++ -*-
 * (c) 2006 Helge Bahmann <hcb@chaoticmind.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * Refer to the file "COPYING" for details.
 */

#include <tscb/timer.h>

namespace tscb {

template class basic_timer_connection<std::chrono::steady_clock::time_point>;
template class basic_scoped_timer_connection<std::chrono::steady_clock::time_point>;
template class basic_timer_service<std::chrono::steady_clock::time_point>;
template class basic_timer_dispatcher<std::chrono::steady_clock::time_point>;

}
