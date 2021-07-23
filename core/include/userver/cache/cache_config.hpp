#pragma once

#include <chrono>
#include <optional>
#include <stdexcept>
#include <unordered_map>

#include <userver/components/component_config.hpp>
#include <userver/dump/config.hpp>
#include <userver/taxi_config/snapshot.hpp>
#include <userver/taxi_config/value.hpp>

namespace cache {

class ConfigError : public std::logic_error {
 public:
  using std::logic_error::logic_error;
};

enum class AllowedUpdateTypes {
  kFullAndIncremental,
  kOnlyFull,
  kOnlyIncremental,
};

enum class FirstUpdateMode {
  kRequired,
  kBestEffort,
  kSkip,
};

enum class FirstUpdateType {
  kFull,
  kIncremental,
  kIncrementalThenAsyncFull,
};

struct ConfigPatch final {
  std::chrono::milliseconds update_interval;
  std::chrono::milliseconds update_jitter;
  std::chrono::milliseconds full_update_interval;
  bool updates_enabled;
};

ConfigPatch Parse(const formats::json::Value& value,
                  formats::parse::To<ConfigPatch>);

struct Config final {
  explicit Config(const components::ComponentConfig& config,
                  const std::optional<dump::Config>& dump_config);

  Config MergeWith(const ConfigPatch& patch) const;

  AllowedUpdateTypes allowed_update_types;
  bool allow_first_update_failure;
  std::optional<bool> force_periodic_update;
  bool config_updates_enabled;
  std::chrono::milliseconds cleanup_interval;

  FirstUpdateMode first_update_mode;
  FirstUpdateType first_update_type;

  std::chrono::milliseconds update_interval;
  std::chrono::milliseconds update_jitter;
  std::chrono::milliseconds full_update_interval;
  bool updates_enabled;
};

enum class BackgroundUpdateMode {
  kEnabled,
  kDisabled,
};

struct LruCacheConfig {
  explicit LruCacheConfig(const components::ComponentConfig& config);

  explicit LruCacheConfig(const formats::json::Value& value);

  size_t size;
  std::chrono::milliseconds lifetime;
  BackgroundUpdateMode background_update;
};

LruCacheConfig Parse(const formats::json::Value& value,
                     formats::parse::To<LruCacheConfig>);

struct LruCacheConfigStatic {
  explicit LruCacheConfigStatic(const components::ComponentConfig& config);

  LruCacheConfigStatic MergeWith(const LruCacheConfig& other) const;

  size_t GetWaySize() const;

  LruCacheConfig config;
  size_t ways;
};

class CacheConfigSet final {
 public:
  explicit CacheConfigSet(const taxi_config::DocsMap& docs_map);

  /// Get config for cache
  std::optional<ConfigPatch> GetConfig(const std::string& cache_name) const;

  /// Get config for LRU cache
  std::optional<LruCacheConfig> GetLruConfig(
      const std::string& cache_name) const;

 private:
  std::unordered_map<std::string, ConfigPatch> configs_;
  std::unordered_map<std::string, LruCacheConfig> lru_configs_;
};

}  // namespace cache