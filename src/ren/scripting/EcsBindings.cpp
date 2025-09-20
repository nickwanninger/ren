#include <ren/scripting/EcsBindings.h>

#include <ren/scripting/Scheme.h>
#include <ren/core/Application.h>
#include <optional>
#include <string>

namespace ren {

  namespace {

    constexpr const char *kErrorSymbol = "ecs/error";

    flecs::world *g_world = nullptr;

    inline s7_pointer world_not_ready(s7_scheme *sc, const char *caller) {
      return s7_error(sc, s7_make_symbol(sc, kErrorSymbol),
                      s7_list(sc, 2, s7_make_string(sc, caller),
                              s7_make_string(sc, "Flecs world is not initialised")));
    }

    inline s7_pointer make_error(s7_scheme *sc, const char *caller, const std::string &message) {
      return s7_error(
          sc, s7_make_symbol(sc, kErrorSymbol),
          s7_list(sc, 2, s7_make_string(sc, caller), s7_make_string(sc, message.c_str())));
    }

    inline std::optional<flecs::entity_t> extract_entity_id(s7_scheme *sc, s7_pointer args,
                                                            const char *caller,
                                                            s7_pointer *error_out) {
      if (s7_is_null(sc, args) || !s7_is_null(sc, s7_cdr(args))) {
        if (error_out) { *error_out = s7_wrong_number_of_args_error(sc, caller, args); }
        return std::nullopt;
      }

      s7_pointer id_arg = s7_car(args);
      if (!s7_is_integer(id_arg)) {
        if (error_out) {
          *error_out = s7_wrong_type_arg_error(sc, caller, 1, id_arg, "integer entity id");
        }
        return std::nullopt;
      }

      return static_cast<flecs::entity_t>(s7_integer(id_arg));
    }

    inline s7_pointer make_bool(s7_scheme *sc, bool value) { return value ? s7_t(sc) : s7_f(sc); }

    inline std::optional<flecs::entity> resolve_existing_entity(s7_scheme *sc, s7_pointer value,
                                                                const char *caller, int arg_index,
                                                                s7_pointer *error_out) {
      if (s7_is_integer(value)) {
        flecs::entity candidate = g_world->entity(static_cast<flecs::entity_t>(s7_integer(value)));
        if (!candidate.is_alive()) {
          if (error_out) {
            *error_out = make_error(
                sc, caller, "entity id " + std::to_string(s7_integer(value)) + " is not alive");
          }
          return std::nullopt;
        }
        return candidate;
      }

      if (s7_is_string(value)) {
        const char *name = s7_string(value);
        flecs::entity candidate = g_world->lookup(name);
        if (!candidate.is_valid() || !candidate.is_alive()) {
          if (error_out) {
            *error_out = make_error(sc, caller, std::string("unknown entity '") + name + "'");
          }
          return std::nullopt;
        }
        return candidate;
      }

      if (error_out) {
        *error_out =
            s7_wrong_type_arg_error(sc, caller, arg_index, value, "integer id or string path");
      }
      return std::nullopt;
    }


    s7_pointer ecs_create(s7_scheme *sc, s7_pointer args) {
      if (!g_world) { return world_not_ready(sc, "ecs/create"); }

      flecs::entity entity = g_world->entity();

      if (!s7_is_null(sc, args)) {
        s7_pointer name_arg = s7_car(args);
        if (!s7_is_null(sc, s7_cdr(args))) {
          return s7_wrong_number_of_args_error(sc, "ecs/create", args);
        }
        if (!s7_is_string(name_arg)) {
          return s7_wrong_type_arg_error(sc, "ecs/create", 1, name_arg, "string");
        }
        entity.set_name(s7_string(name_arg));
      }

      return s7_make_integer(sc, static_cast<s7_int>(entity.id()));
    }

    s7_pointer ecs_destroy(s7_scheme *sc, s7_pointer args) {
      if (!g_world) { return world_not_ready(sc, "ecs/destroy!"); }

      s7_pointer error = nullptr;
      auto id = extract_entity_id(sc, args, "ecs/destroy!", &error);
      if (!id) { return error ? error : s7_f(sc); }

      flecs::entity entity = g_world->entity(*id);
      if (!entity.is_alive()) { return s7_f(sc); }
      entity.destruct();
      return s7_t(sc);
    }

    s7_pointer ecs_is_alive(s7_scheme *sc, s7_pointer args) {
      if (!g_world) { return world_not_ready(sc, "ecs/alive?"); }

      s7_pointer error = nullptr;
      auto id = extract_entity_id(sc, args, "ecs/alive?", &error);
      if (!id) { return error ? error : s7_f(sc); }

      flecs::entity entity = g_world->entity(*id);
      return make_bool(sc, entity.is_alive());
    }

    s7_pointer ecs_set_name(s7_scheme *sc, s7_pointer args) {
      if (!g_world) { return world_not_ready(sc, "ecs/set-name!"); }

      if (s7_is_null(sc, args) || s7_is_null(sc, s7_cdr(args)) || !s7_is_null(sc, s7_cddr(args))) {
        return s7_wrong_number_of_args_error(sc, "ecs/set-name!", args);
      }

      s7_pointer id_arg = s7_car(args);
      if (!s7_is_integer(id_arg)) {
        return s7_wrong_type_arg_error(sc, "ecs/set-name!", 1, id_arg, "integer entity id");
      }

      s7_pointer name_arg = s7_cadr(args);
      if (!s7_is_string(name_arg)) {
        return s7_wrong_type_arg_error(sc, "ecs/set-name!", 2, name_arg, "string");
      }

      flecs::entity entity = g_world->entity(static_cast<flecs::entity_t>(s7_integer(id_arg)));
      if (!entity.is_alive()) { return s7_f(sc); }
      entity.set_name(s7_string(name_arg));
      return s7_t(sc);
    }

    s7_pointer ecs_get_name(s7_scheme *sc, s7_pointer args) {
      if (!g_world) { return world_not_ready(sc, "ecs/name"); }

      s7_pointer error = nullptr;
      auto id = extract_entity_id(sc, args, "ecs/name", &error);
      if (!id) { return error ? error : s7_f(sc); }

      flecs::entity entity = g_world->entity(*id);
      if (!entity.is_alive()) { return s7_f(sc); }

      const char *name = ecs_get_name(g_world->c_ptr(), *id);
      if (!name || name[0] == '\0') { return s7_f(sc); }
      return s7_make_string(sc, name);
    }

    s7_pointer ecs_lookup_id(s7_scheme *sc, s7_pointer args) {
      if (!g_world) { return world_not_ready(sc, "ecs/lookup"); }

      if (s7_is_null(sc, args) || !s7_is_null(sc, s7_cdr(args))) {
        return s7_wrong_number_of_args_error(sc, "ecs/lookup", args);
      }

      s7_pointer name_arg = s7_car(args);
      if (!s7_is_string(name_arg)) {
        return s7_wrong_type_arg_error(sc, "ecs/lookup", 1, name_arg, "string path");
      }

      flecs::entity found = g_world->lookup(s7_string(name_arg));
      if (!found.is_valid()) { return s7_f(sc); }
      return s7_make_integer(sc, static_cast<s7_int>(found.id()));
    }

    s7_pointer ecs_add_tag(s7_scheme *sc, s7_pointer args) {
      if (!g_world) { return world_not_ready(sc, "ecs/add-tag!"); }

      if (s7_is_null(sc, args) || s7_is_null(sc, s7_cdr(args)) || !s7_is_null(sc, s7_cddr(args))) {
        return s7_wrong_number_of_args_error(sc, "ecs/add-tag!", args);
      }

      s7_pointer id_arg = s7_car(args);
      if (!s7_is_integer(id_arg)) {
        return s7_wrong_type_arg_error(sc, "ecs/add-tag!", 1, id_arg, "integer entity id");
      }

      s7_pointer tag_arg = s7_cadr(args);
      if (!s7_is_string(tag_arg)) {
        return s7_wrong_type_arg_error(sc, "ecs/add-tag!", 2, tag_arg, "string tag path");
      }

      flecs::entity target = g_world->entity(static_cast<flecs::entity_t>(s7_integer(id_arg)));
      if (!target.is_alive()) { return s7_f(sc); }

      flecs::entity tag = g_world->lookup(s7_string(tag_arg));
      if (!tag.is_valid()) { return s7_f(sc); }

      target.add(tag.id());
      return s7_t(sc);
    }

    s7_pointer ecs_remove_tag(s7_scheme *sc, s7_pointer args) {
      if (!g_world) { return world_not_ready(sc, "ecs/remove-tag!"); }

      if (s7_is_null(sc, args) || s7_is_null(sc, s7_cdr(args)) || !s7_is_null(sc, s7_cddr(args))) {
        return s7_wrong_number_of_args_error(sc, "ecs/remove-tag!", args);
      }

      s7_pointer id_arg = s7_car(args);
      if (!s7_is_integer(id_arg)) {
        return s7_wrong_type_arg_error(sc, "ecs/remove-tag!", 1, id_arg, "integer entity id");
      }

      s7_pointer tag_arg = s7_cadr(args);
      if (!s7_is_string(tag_arg)) {
        return s7_wrong_type_arg_error(sc, "ecs/remove-tag!", 2, tag_arg, "string tag path");
      }

      flecs::entity target = g_world->entity(static_cast<flecs::entity_t>(s7_integer(id_arg)));
      if (!target.is_alive()) { return s7_f(sc); }

      flecs::entity tag = g_world->lookup(s7_string(tag_arg));
      if (!tag.is_valid()) { return s7_f(sc); }

      target.remove(tag.id());
      return s7_t(sc);
    }

    // s7_pointer ecs_add_relation(s7_scheme *sc, s7_pointer args) {
    //   if (!g_world) { return world_not_ready(sc, "ecs/add-relation!"); }

    //   if (s7_list_length(sc, args) != 3) {
    //     return s7_wrong_number_of_args_error(sc, "ecs/add-relation!", args);
    //   }

    //   s7_pointer subject_arg = s7_car(args);
    //   if (!s7_is_integer(subject_arg)) {
    //     return s7_wrong_type_arg_error(sc, "ecs/add-relation!", 1, subject_arg, "integer entity
    //     id");
    //   }

    //   flecs::entity subject =
    //   g_world->entity(static_cast<flecs::entity_t>(s7_integer(subject_arg))); if
    //   (!subject.is_alive()) { return s7_f(sc); }

    //   s7_pointer relation_arg = s7_cadr(args);
    //   s7_pointer target_arg = s7_caddr(args);

    //   s7_pointer error = nullptr;
    //   auto relation = ensure_relation_entity(sc, relation_arg, "ecs/add-relation!", 2, &error);
    //   if (!relation) { return error ? error : s7_f(sc); }

    //   error = nullptr;
    //   auto target = resolve_existing_entity(sc, target_arg, "ecs/add-relation!", 3, &error);
    //   if (!target) { return error ? error : s7_f(sc); }

    //   subject.add(relation->id(), target->id());
    //   return s7_t(sc);
    // }

    s7_pointer ecs_add_pair_internal(s7_scheme *sc, s7_pointer args) {
      if (!g_world) { return world_not_ready(sc, "ecs/add-pair!"); }

      if (s7_list_length(sc, args) != 3) {
        return s7_wrong_number_of_args_error(sc, "ecs/add-pair!", args);
      }

      s7_pointer subject_arg = s7_car(args);
      s7_pointer relation_arg = s7_cadr(args);
      s7_pointer target_arg = s7_caddr(args);

      s7_pointer error = nullptr;
      auto relation = resolve_existing_entity(sc, relation_arg, "ecs/add-pair!", 2, &error);
      if (!relation) { return error ? error : s7_f(sc); }

      error = nullptr;
      auto target = resolve_existing_entity(sc, target_arg, "ecs/add-pair!", 3, &error);
      if (!target) { return error ? error : s7_f(sc); }

      ecs_add_pair(g_world->c_ptr(), static_cast<flecs::entity_t>(s7_integer(subject_arg)),
                   relation->id(), target->id());
      return s7_t(sc);
    }

  }  // namespace

  void registerEcsBindings(Scheme &vm, flecs::world &world) {
    g_world = &world;

    vm.bind("ecs/create", ecs_create, 0, 1, false,
            "(ecs/create [name]) -> entity-id\nCreate a new entity optionally named.");
    vm.bind("ecs/destroy!", ecs_destroy, 1, 0, false,
            "(ecs/destroy! entity-id) -> boolean\nDestroy the entity if it is alive.");
    vm.bind(
        "ecs/alive?", ecs_is_alive, 1, 0, false,
        "(ecs/alive? entity-id) -> boolean\nCheck whether an entity id refers to a live entity.");
    vm.bind("ecs/set-name!", ecs_set_name, 2, 0, false,
            "(ecs/set-name! entity-id name) -> boolean\nAssign a display name to an entity.");
    vm.bind("ecs/name", ecs_get_name, 1, 0, false,
            "(ecs/name entity-id) -> string|#f\nFetch the current name for an entity.");
    vm.bind("ecs/lookup", ecs_lookup_id, 1, 0, false,
            "(ecs/lookup path) -> entity-id|#f\nResolve an entity id from a hierarchical path.");
    vm.bind(
        "ecs/add-tag!", ecs_add_tag, 2, 0, false,
        "(ecs/add-tag! entity-id tag-path) -> boolean\nAdd an existing tag (entity) to another entity.");
    vm.bind(
        "ecs/remove-tag!", ecs_remove_tag, 2, 0, false,
        "(ecs/remove-tag! entity-id tag-path) -> boolean\nRemove a previously added tag from an entity.");
    // vm.bind("ecs/add-relation!", ecs_add_relation, 3, 0, false,
    //         "(ecs/add-relation! subject relation target) -> boolean\nCreate or fetch a relation
    //         and relate subject to target.");
    vm.bind(
        "ecs/add-pair!", ecs_add_pair_internal, 3, 0, false,
        "(ecs/add-pair! subject first second) -> boolean\nAdd an existing relation pair (first, second) to subject.");


    vm.bind(
        "ecs/run-script",
        +[](s7_scheme *sc, s7_pointer args) -> s7_pointer {
          if (s7_is_null(sc, args) || !s7_is_null(sc, s7_cdr(args))) {
            return s7_wrong_number_of_args_error(sc, "ecs/run-script", args);
          }

          s7_pointer script_arg = s7_car(args);
          if (!s7_is_string(script_arg)) {
            return s7_wrong_type_arg_error(sc, "ecs/run-script", 1, script_arg, "string");
          }

          const char *script = s7_string(script_arg);
          // fmt::println("Running Flecs script:\n{}\n", script);
          if (ren::world().script_run("anon", script)) {
            // failed!
            return s7_f(sc);
          }

          return s7_t(sc);
        },
        1, 0, false,
        "(ecs/run-script script) -> boolean\nRun a Flecs script to create entities and relationships.");


    vm.bind(
        "ecs/query-json",
        +[](s7_scheme *sc, s7_pointer args) -> s7_pointer {
          if (s7_is_null(sc, args) || !s7_is_null(sc, s7_cdr(args))) {
            return s7_wrong_number_of_args_error(sc, "ecs/run-script", args);
          }

          s7_pointer query_arg = s7_car(args);
          if (!s7_is_string(query_arg)) {
            return s7_wrong_type_arg_error(sc, "ecs/run-script", 1, query_arg, "string");
          }

          const char *query = s7_string(query_arg);


          flecs::query<> q = ren::world().query_builder().expr(query).build();


          ecs_iter_t it = ecs_query_iter(g_world->c_ptr(), q.c_ptr());

          char *json = ecs_iter_to_json(&it, NULL);
          auto s = s7_make_string(sc, json);

          ecs_os_free(json);


          return s;
        },
        1, 0, false,
        "(ecs/query-json query) -> string|#f\nRun a Flecs query and return results as JSON.");
  }

}  // namespace ren
