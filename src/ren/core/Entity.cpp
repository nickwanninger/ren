#include <ren/core/Entity.h>
#include <ren/misc/json_serialize.h>

namespace ren {




#define SERIALIZE_TYPE(type)                                                          \
  {                                                                                   \
    if (auto comp = tryGet<comp::type>()) { j["components"][#type] = (json) * comp; } \
  }

  json Entity::serialize(void) {
    json j = {};
    j["uuid"] = fmt::format("{}", (u64)get<comp::ID>().uuid);
    j["name"] = get<comp::Name>();

    // SERIALIZE_TYPE(ID);
    // SERIALIZE_TYPE(Name);
    SERIALIZE_TYPE(Transform);

    return j;
  }
}  // namespace ren