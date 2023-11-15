#include <userver/formats/json/validate.hpp>

#include <optional>

#include <rapidjson/reader.h>
#include <rapidjson/schema.h>

#include <formats/json/impl/accept.hpp>
#include <formats/json/impl/types_impl.hpp>
#include <userver/formats/json/impl/types.hpp>
#include <userver/formats/json/value.hpp>

USERVER_NAMESPACE_BEGIN

namespace formats::json {

namespace impl {

using SchemaDocument =
    rapidjson::GenericSchemaDocument<impl::Value, rapidjson::CrtAllocator>;

using SchemaValidator = rapidjson::GenericSchemaValidator<
    impl::SchemaDocument, rapidjson::BaseReaderHandler<impl::UTF8, void>,
    rapidjson::CrtAllocator>;

}  // namespace impl

struct Schema::Impl final {
  impl::SchemaDocument schemaDocument;
};

Schema::Schema(const formats::json::Value& doc)
    : pimpl_(Impl{impl::SchemaDocument{doc.GetNative()}}) {}

Schema::~Schema() = default;

std::optional<formats::json::Value> Validate(
    const formats::json::Value& doc, const formats::json::Schema& schema) {
  impl::SchemaValidator validator(schema.pimpl_->schemaDocument);
  if (!AcceptNoRecursion(doc.GetNative(), validator)) {
    impl::Document json;
    json.Swap(validator.GetError());
    return formats::json::Value(
        impl::VersionedValuePtr::Create(std::move(json)));
  }

  return {};
}

}  // namespace formats::json

USERVER_NAMESPACE_END
