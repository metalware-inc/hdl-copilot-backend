#include "shared.hpp"

namespace metalware {
std::string to_string(ConstructType type) {
  switch (type) {
    case ConstructType::INSTANCE_NAME:
      return "INSTANCE_NAME";
    case ConstructType::HIERARCHY_INSTANTIATION:
      return "HIERARCHY_INSTANTIATION";
    case ConstructType::MODULE_DECLARATION:
      return "MODULE_DECLARATION";
    default:
      return "UNKNOWN";
  }
}
}  // namespace metalware
