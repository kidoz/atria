#pragma once

#include "atria/request.hpp"
#include "atria/response.hpp"

#include <functional>

namespace atria {

using Handler = std::function<Response(Request&)>;
using Next = std::function<Response(Request&)>;
using Middleware = std::function<Response(Request&, const Next&)>;

namespace middleware {

[[nodiscard]] Middleware request_logger();
[[nodiscard]] Middleware cors(std::string allow_origin = "*");
[[nodiscard]] Middleware error_handler();

}  // namespace middleware
}  // namespace atria
