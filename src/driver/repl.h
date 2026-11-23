#pragma once
#include <istream>
#include <ostream>

namespace mini {

// Interactive read-eval-print loop. Definitions persist between inputs;
// the value of a bare expression is echoed.
int runRepl(std::istream& in, std::ostream& out, std::ostream& err, bool optimize, bool color);

}  // namespace mini
