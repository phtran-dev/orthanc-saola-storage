#include "SaolaConfiguration.h"
#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Toolbox.h>
#include <Logging.h>

static const char *DELAYED_DELETION = "DelayedDeletion";

SaolaConfiguration::SaolaConfiguration(/* args */)
{
  OrthancPlugins::OrthancConfiguration configuration;
  OrthancPlugins::OrthancConfiguration saola, delayDeletion;
  configuration.GetSection(saola, "SaolaStorage");
  saola.GetSection(delayDeletion, DELAYED_DELETION);

  this->enable_ = saola.GetBooleanValue("Enable", false);

  this->mount_directory_ = saola.GetStringValue("MountDirectory", "fs1");

  std::string format = saola.GetStringValue("StoragePathFormat", "FULL");
  Orthanc::Toolbox::ToUpperCase(format);
  if (format == "FULL")
  {
    this->is_storage_path_format_full_ = true;
  }

  this->delayedDeletionEnable_ = delayDeletion.GetBooleanValue("Enable", false);

  this->delayedDeletionThrottleDelayMs_ = delayDeletion.GetIntegerValue("ThrottleDelayMs", 0);

  this->delayedDeletionPath_ = delayDeletion.GetStringValue("Path", "");
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
  return this->is_storage_path_format_full_;
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