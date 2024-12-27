#include "SaolaConfiguration.h"
#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Toolbox.h>
#include <Logging.h>

SaolaConfiguration::SaolaConfiguration(/* args */)
{
  OrthancPlugins::OrthancConfiguration configuration;
  OrthancPlugins::OrthancConfiguration saola;
  configuration.GetSection(saola, "SaolaStorage");

  this->enable_ = saola.GetBooleanValue("Enable", false);

  this->mount_directory_ = saola.GetStringValue("MountDirectory", "fs1");

  std::string format = saola.GetStringValue("StoragePathFormat", "FULL");
  Orthanc::Toolbox::ToUpperCase(format);
  if (format == "FULL")
  {
    this->is_storage_path_format_full_ = true;
  }
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