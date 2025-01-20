#include "SaolaConfiguration.h"
#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Toolbox.h>
#include <Logging.h>
#include <boost/filesystem.hpp>

static const char *ENABLE = "Enable";
static const char *ROOT = "Root";
static const char *DELAYED_DELETION = "DelayedDeletion";
static const char *SAOLA_STORAGE = "SaolaStorage";
static const char *MOUNT_DIRECTORY = "MountDirectory";
static const char *STORAGE_PATH_FORMAT = "StoragePathFormat";

SaolaConfiguration::SaolaConfiguration(/* args */)
{
  OrthancPlugins::OrthancConfiguration orthancConfig;
  OrthancPlugins::OrthancConfiguration saola, delayedDeletionConfig;
  orthancConfig.GetSection(saola, SAOLA_STORAGE);
  saola.GetSection(delayedDeletionConfig, DELAYED_DELETION);

  this->enable_ = saola.GetBooleanValue(ENABLE, false);

  this->root_ = saola.GetStringValue(ROOT, "/saola/");

  this->mount_directory_ = saola.GetStringValue(MOUNT_DIRECTORY, "fs1");

  this->storagePathFormat_ = saola.GetStringValue(STORAGE_PATH_FORMAT, "FULL");

  this->delayedDeletionEnable_ = delayedDeletionConfig.GetBooleanValue(ENABLE, false);
  this->delayedDeletionThrottleDelayMs_ = delayedDeletionConfig.GetIntegerValue("ThrottleDelayMs", 0);

  const char *databaseServerIdentifier_ = OrthancPluginGetDatabaseServerIdentifier(OrthancPlugins::GetGlobalContext());
  std::string pathStorage = orthancConfig.GetStringValue("StorageDirectory", "OrthancStorage");
  LOG(WARNING) << "DelayedDeletion - Path to the storage area: " << pathStorage;
  boost::filesystem::path defaultDbPath = boost::filesystem::path(pathStorage) / (std::string("pending-deletions.") + databaseServerIdentifier_ + ".db");
  this->delayedDeletionPath_ = delayedDeletionConfig.GetStringValue("Path", defaultDbPath.string());
  LOG(WARNING) << "DelayedDeletion - Path to the SQLite database: " << this->delayedDeletionPath_;
}

SaolaConfiguration &SaolaConfiguration::Instance()
{
  static SaolaConfiguration configuration_;
  return configuration_;
}

bool SaolaConfiguration::IsEnabled() const
{
  return this->enable_;
}

bool SaolaConfiguration::IsStoragePathFormatFull() const
{
  return this->storagePathFormat_ == "FULL";
}

const std::string &SaolaConfiguration::GetRoot() const
{
  return this->root_;
}

const std::string &SaolaConfiguration::GetMountDirectory() const
{
  return this->mount_directory_;
}

bool SaolaConfiguration::DelayedDeletionEnable() const
{
  return this->delayedDeletionEnable_;
}

int SaolaConfiguration::DelayedDeletionThrottleDelayMs() const
{
  return this->delayedDeletionThrottleDelayMs_;
}

const std::string &SaolaConfiguration::DelayedDeletionPath() const
{
  return this->delayedDeletionPath_;
}

void SaolaConfiguration::ApplyConfiguration(const Json::Value& config)
{
  if (config.isMember("MountDirectory"))
  {
    this->mount_directory_ = config["MountDirectory"].asString();
  }
  if (config.isMember("StoragePathFormat"))
  {
    this->storagePathFormat_ = config["StoragePathFormat"].asString();
  }
  if (config.isMember("MaxRetry"))
  {
    this->maxRetry_ = config["MaxRetry"].asInt();
  }
  // if (config.isMember("DelayedDeletion"))
  // {
  //   const Json::Value& delayDeletionConfig = config["DelayedDeletion"];
  //   if (delayDeletionConfig.isMember("Enable"))
  //   {
  //     this->delayedDeletionEnable_ = delayDeletionConfig["Enable"].asBool();

  //   }
  //   if (delayDeletionConfig.isMember("ThrottleDelayMs"))
  //   {
  //     this->delayedDeletionThrottleDelayMs_ = delayDeletionConfig["ThrottleDelayMs"].asInt();

  //   }
  //   if (delayDeletionConfig.isMember("Path"))
  //   {
  //     this->delayedDeletionPath_ = delayDeletionConfig["Path"].asString();
  //   }
  // }
}

void SaolaConfiguration::ToJson(Json::Value &json) const
{
  json["Enable"] = this->enable_;
  json["MountDirectory"] = this->mount_directory_;
  json["StoragePathFormat"] = this->storagePathFormat_;
  json["MaxRetry"] = 5;
  json["DelayedDeletion"] = Json::objectValue;
  json["DelayedDeletion"]["Enable"] = this->delayedDeletionEnable_;
  json["DelayedDeletion"]["ThrottleDelayMs"] = this->delayedDeletionThrottleDelayMs_;
  json["DelayedDeletion"]["Path"] = this->delayedDeletionPath_;
}

const std::string SaolaConfiguration::ToJsonString() const
{
  Json::Value json = Json::objectValue;
  this->ToJson(json);
  std::string s;
  Orthanc::Toolbox::WriteStyledJson(s, json);
  return s;
}